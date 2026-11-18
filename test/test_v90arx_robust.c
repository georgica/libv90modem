/* modem/v90arx against hostile or broken line content: the far end is not
 * trusted, so nothing it sends may crash the receiver (a division by zero)
 * or hang it (an interpolator step at or below zero), and the clock it
 * follows must stay within what a codec's clock can be.
 *
 *   1. data mode, then nothing but Ucode 0: every decided level is 0 and
 *      the decision power decays to 0 (it divides the estimator's step);
 *   2. an "Sd" that is a tone away from 1333 Hz: its line turns by nearly a
 *      quarter period between the two ends of the clock measurement, the
 *      tangent is huge (it overflowed, and the step came out <= 0 or wild);
 *   3. a downstream whose clock runs 1500 ppm off: the timing loop followed
 *      it without bound.
 *
 * A watchdog (SIGALRM, where there is one: not on Windows) turns a hang
 * into a failure line and an exit. */
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <inttypes.h>
#include "test.h"
#include "v90arx.h"
#include "v90_tx.h"
#include "v90_pcm.h"
#include "dschan.h"
#include "noise.h"

#ifdef SIGALRM
static void on_alarm(int s)
{
    static const char msg[] = "  FAIL: hung (watchdog)\n";
    (void)s;
    if (write(1, msg, sizeof msg - 1) < 0) { /* nothing to do */ }
    _exit(2);
}
#endif

typedef struct {
    v90tx_t tx; rng_t rng;
    int32_t n, n_trn, n_data; int sel; int silence_after;
} src_t;

static int getbit(void *ctx) { src_t *s = (src_t *)ctx; return rng_uniform(&s->rng) < 0.5; }

static uint8_t next(void *ctx)
{
    src_t *s = (src_t *)ctx;
    int32_t n = s->n++;
    uint8_t o;
    if (n == 400) v90tx_select(&s->tx, V90TX_SD);
    o = v90tx_tick(&s->tx);
    if (v90tx_signal(&s->tx) == V90TX_TRN1D) {
        if (s->n_trn < 0) s->n_trn = n;
        if (!s->sel && (int32_t)v90tx_symbols(&s->tx) >= 2400 - 3) {
            v90tx_select(&s->tx, s->silence_after ? V90TX_SILENCE : V90TX_B1D);
            s->sel = 1;
        }
    }
    return o;
}

static src_t S;
static dschan_t ch;
static v90arx_t rx;

/* the clock offset in hundredths of a ppm, printed from integers (the C
 * libraries round decimals of doubles differently) */
static char ppmbuf[32];
static const char *ppm_of(void)
{
    int32_t q = v90arx_ppm_q8(&rx), a = q < 0 ? -q : q, h = (int32_t)(((int64_t)a * 100) / 256);
    snprintf(ppmbuf, sizeof ppmbuf, "%c%d.%02d", q < 0 ? '-' : '+', (int)(h / 100), (int)(h % 100));
    return ppmbuf;
}

/* a sane interpolator: step within 1000 ppm of one, step + slew > 0 */
static int sane(void)
{
    int64_t one = (int64_t)1 << 32, d = rx.step - one;
    if (d < 0) d = -d;
    return d <= one * 1000 / 1000000 && rx.step + rx.slew > 0;
}

/* 1: train on a real Sd / TRN1d (2 km, 0 ppm), then silence (Ucode 0) with
 * a constellation that holds Ucode 0 */
static void zeros(void)
{
    dschan_cfg c;
    v90arx_cfg_t rc;
    v90arx_cons_t cons;
    uint8_t mask[16];
    int32_t k, maxp = 0;
    int i, set = 0;
    printf("1. data mode, then Ucode 0 only, 3 s\n");
    memset(&S, 0, sizeof S);
    rng_seed(&S.rng, 3);
    S.n_trn = S.n_data = -1; S.silence_after = 1;
    v90tx_init(&S.tx, 0, 90);
    v90tx_set_getbit(&S.tx, getbit, &S);
    memset(&c, 0, sizeof c);
    c.loop_km = 2; c.level_db = -12; c.noise_dbm0 = -300; c.echo_db = -300; c.phase = 0.3; c.seed = 1;
    dschan_init(&ch, &c, next, &S);
    memset(&rc, 0, sizeof rc);
    rc.uinfo = 90; rc.ntrain = 2000;
    v90arx_init(&rx, &rc);
    memset(mask, 0, sizeof mask);
    for (i = 0; i < 128; i += 9) mask[i >> 3] |= (uint8_t)(1 << (i & 7));    /* Ucode 0, 9, 18 ... */
    for (i = 0; i < 6; i++) v90arx_cons_mask(&cons, 0, i, mask);
    for (k = 0; k < 8000 * 4; k++) {
        int32_t n0 = (int32_t)ch.n;
        (void)v90arx_tick(&rx, dschan_tick(&ch, 0), 0);
        if ((int32_t)ch.n - n0 > maxp) maxp = (int32_t)ch.n - n0;
        if (!set && S.n_trn >= 0) { v90arx_set_cons(&rx, &cons, 2400); set = 1; }
    }
    printf("  survived; state %d, decision power %" PRId64 ", ppm %s\n", rx.state, rx.pd, ppm_of());
    CHECK(rx.state == V90ARX_DD);
    CHECK(sane());
}

/* 2: a tone of f Hz at the A/D for 0.3 s, its phase reversed, then 1 s more */
static void tone(double f)
{
    v90arx_cfg_t rc;
    int32_t k;
    double ph = 0;
    memset(&rc, 0, sizeof rc);
    rc.uinfo = 90;
    v90arx_init(&rx, &rc);
    for (k = 0; k < 8000 * 13 / 10; k++) {
        double s = 4000 * cos(ph) * (k < 2400 ? 1 : -1);
        ph += 2 * 3.14159265358979 * f / 8000;
        (void)v90arx_tick(&rx, (int16_t)floor(s + 0.5), 0);
    }
}

static void tones(void)
{
    static const double F[] = { 1290, 1300, 1310, 1320, 1330, 1333.33, 1340, 1345, 1347, 1348, 1349, 1350, 1351, 1352, 1354, 1356, 1360, 1370 };
    int i;
    printf("2. a tone near 1333 Hz taken for Sd, then reversed\n");
    for (i = 0; i < (int)(sizeof F / sizeof F[0]); i++) {
        tone(F[i]);
        printf("  %5d.%02d Hz: state %d, clock %s ppm\n", (int)F[i], (int)(F[i] * 100 + 0.5) % 100, rx.state, ppm_of());
        CHECK(sane());
    }
}

/* 3: a real Sd / TRN1d / data at 1500 ppm */
static void fast_clock(void)
{
    dschan_cfg c;
    v90arx_cfg_t rc;
    int32_t k;
    int ok = 1;
    printf("3. a downstream 1500 ppm fast, 4 s\n");
    memset(&S, 0, sizeof S);
    rng_seed(&S.rng, 5);
    S.n_trn = S.n_data = -1;
    v90tx_init(&S.tx, 0, 90);
    v90tx_set_getbit(&S.tx, getbit, &S);
    {
        v90_map_t map;
        uint8_t mask[16];
        int i;
        memset(&map, 0, sizeof map); memset(mask, 0, sizeof mask);
        for (i = 6; i < 128; i += 20) mask[i >> 3] |= (uint8_t)(1 << (i & 7));
        map.K = 15;
        for (i = 0; i < 6; i++) v90_map_set_points(&map, i, mask);
        (void)v90tx_set_map(&S.tx, V90TX_MAP_DATA, &map);
    }
    memset(&c, 0, sizeof c);
    c.loop_km = 2; c.level_db = -12; c.noise_dbm0 = -75; c.echo_db = -300; c.phase = 0.3; c.seed = 2;
    c.ppm = -1500;
    dschan_init(&ch, &c, next, &S);
    memset(&rc, 0, sizeof rc);
    rc.uinfo = 90;
    v90arx_init(&rx, &rc);
    for (k = 0; k < 8000 * 4; k++) {
        (void)v90arx_tick(&rx, dschan_tick(&ch, 0), 0);
        if (!sane()) ok = 0;
    }
    printf("  clock followed to %s ppm, always within 1000 ppm: %d\n", ppm_of(), ok);
    CHECK(ok);
}

int main(int argc, char **argv)
{
    int only = argc > 1 ? argv[1][0] - '0' : 0;     /* run one part (for its failing-first evidence) */
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef SIGALRM
    signal(SIGALRM, on_alarm);
    alarm(120);
#endif
    if (!only || only == 1) zeros();
    if (!only || only == 2) tones();
    if (!only || only == 3) fast_clock();
    T_REPORT("v90arx_robust");
}
