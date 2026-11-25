/* Sizing W, the HST wait after ANSam (decision 25; Task 18 Step 4 and the
 * Task 13 ruling): a measurement, not a test (`make hst-wsize`; its figures
 * go into docs/limits.md).
 *
 * Each line: the library's answerer (HST offered and linked) against
 * sim/hstcall on sim/v32line, until ANSam is over and ANS_HST_WAIT begins;
 * from that sample on, the detector of ANS_HST_WAIT (a hst_det_t of our own,
 * started there, as the modem starts its own) on what the answerer hears,
 * the answerer silent, as it is in the wait, for 2 s. It reports when the
 * detector fires and, each ms, whether its energy stage holds. A W of w ms
 * finds the caller when the detector fires by w; with the extension (the
 * modem waits on past w while the energy stage holds, to HST_W_MAX_MS),
 * also when the energy stage held from w until it fired, by HST_W_MAX_MS.
 *
 * The lines (the caller's back channel starting and staying at the speed):
 *   levels    450 and 300 at -9, -26 and -43 dBm0 (Task 13's levels), the
 *             four lines of each (+-7 Hz, +-100 ppm), noise 20 dB under the
 *             signal, two seeds
 *   dropout   one 20 or 40 ms dropout of the caller's signal starting 100
 *             to 300 ms after its preamble began, in 10 ms steps, at +7 and
 *             -7 Hz, -26 dBm0, 450 and 300 (the Task 13 review's sweep)
 *   edge      the caller's signal through a 300 Hz 4th-order Butterworth
 *             high-pass (a line's lower edge; the Task 13 review's M1), at
 *             +-7 Hz, -26 dBm0, two seeds
 * Prints, per group and speed, the latest detection and, for W from 400 to
 * 700 ms in 50 ms steps, the lines missed without and with the extension. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "answer.h"
#include "../sim/hstcall.h"
#include "../sim/v32line.h"

#define PI 3.14159265358979323846
#define LISTEN_MS 2000

typedef struct {
    int bps; double dbm0, hz, ppm; uint32_t seed;
    int drop_at, drop_len;            /* ms after the preamble's start; 0: none */
    int hp;                           /* the 300 Hz high-pass */
} line_t;

typedef struct { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } biq_t;
static void hp_init(biq_t *f, double fc, double q)
{
    double w = 2 * PI * fc / 8000, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
    memset(f, 0, sizeof *f);
    f->b0 = (1 + c) / 2 / a0; f->b1 = -(1 + c) / a0; f->b2 = (1 + c) / 2 / a0;
    f->a1 = -2 * c / a0; f->a2 = (1 - al) / a0;
}
static double biq(biq_t *f, double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
    return y;
}

static hstcall_t HC;
static answer_t A;
static v32line_t L;
static hst_det_t D;
typedef struct { const line_t *ln; long n, back_at; biq_t hp[2]; } peer_t;
static peer_t P;

static int16_t sat16(double v) { long q = lrint(v); return (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q); }
static double peer(void *u, double rx)
{
    peer_t *p = u;
    double y = hstcall_tick(&HC, sat16(rx));
    if (p->back_at < 0 && HC.phase == HC_BACK) p->back_at = p->n;
    p->n++;
    if (p->ln->hp) y = biq(&p->hp[1], biq(&p->hp[0], y));
    if (p->ln->drop_len && p->back_at >= 0) {
        long t = p->n - p->back_at;
        if (t >= p->ln->drop_at * 8L && t < (p->ln->drop_at + p->ln->drop_len) * 8L) y = 0;
    }
    return y;
}
static int one(void *u) { (void)u; return 1; }
static void none(void *u, int b) { (void)u; (void)b; }

/* the detection time, ms from the wait's start (-1: none in LISTEN_MS);
 * e[ms]: the energy stage at each ms */
static int run_line(const line_t *ln, uint8_t *e)
{
    answer_cfg ac;
    hstcall_cfg hc;
    v32line_cfg lc;
    int16_t atx = 0, rx;
    long n, w0 = -1;
    int fired = -1;
    memset(&lc, 0, sizeof lc);
    lc.loss_db = 0; lc.delay = 200; lc.near_db = 300; lc.c_near_db = 300; lc.far_db = lc.c_far_db = 300;
    lc.noise_a_dbm0 = ln->dbm0 - 20; lc.noise_c_dbm0 = -60; lc.freq_offset = ln->hz; lc.ppm = ln->ppm; lc.seed = ln->seed;
    v32line_init(&L, &lc);
    hstcall_defaults(&hc);
    hc.back_bps = hc.start_bps = ln->bps; hc.level_dbm0 = (int)ln->dbm0;
    hstcall_init(&HC, &hc);
    memset(&P, 0, sizeof P); P.ln = ln; P.back_at = -1;
    hp_init(&P.hp[0], 300, 0.5412); hp_init(&P.hp[1], 300, 1.3066);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = V8_MOD_V22 | V8_MOD_V21 | ANS_MODE_HST | ANS_MODE_HST_LINKED;
    ac.getbit = one; ac.putbit = none;
    answer_init(&A, &ac);
    for (n = 0; n < 8000L * 12; n++) {
        rx = v32line_tick(&L, atx, peer, &P);
        if (w0 < 0) {
            atx = answer_tick(&A, rx);
            if (A.phase == ANS_HST_WAIT) { w0 = n; hst_det_init(&D); atx = 0; }
            continue;
        }
        atx = 0;
        if (hst_det_tick(&D, rx) && fired < 0) fired = (int)((n - w0) / 8);
        if ((n - w0) % 8 == 7) e[(n - w0) / 8] = (uint8_t)hst_det_energy(&D);
        if (fired >= 0 || n - w0 >= 8L * LISTEN_MS - 1) break;
    }
    return fired;
}

#define NW 7
typedef struct { int lines, latest, never, miss[NW], miss_ext[NW]; } tally_t;

static void count(tally_t *t, int fired, const uint8_t *e)
{
    int k;
    t->lines++;
    if (fired < 0) t->never++;
    if (fired > t->latest) t->latest = fired;
    for (k = 0; k < NW; k++) {
        int w = 400 + 50 * k, ok = fired >= 0 && fired <= w, i;
        if (!ok) t->miss[k]++;
        if (!ok && fired >= 0 && fired <= HST_W_MAX_MS) {
            ok = 1;
            for (i = w; i < fired; i++) if (!e[i]) { ok = 0; break; }
        }
        if (!ok) t->miss_ext[k]++;
    }
}

static void report(const char *what, int bps, const tally_t *t)
{
    int k;
    printf("%s %d: %d lines, latest %d ms, %d never within %d ms\n", what, bps, t->lines, t->latest, t->never, LISTEN_MS);
    printf("  W ms       ");
    for (k = 0; k < NW; k++) printf(" %4d", 400 + 50 * k);
    printf("\n  missed     ");
    for (k = 0; k < NW; k++) printf(" %4d", t->miss[k]);
    printf("\n  extended   ");
    for (k = 0; k < NW; k++) printf(" %4d", t->miss_ext[k]);
    printf("\n");
    fflush(stdout);
}

int main(void)
{
    static const struct { double hz, ppm; } offs[4] = { { 7, 100 }, { -7, -100 }, { 7, -100 }, { -7, 100 } };
    static const double lv[3] = { -9, -26, -43 };
    static uint8_t e[LISTEN_MS + 1];
    int b, i, j, s, len, at;
    for (b = 0; b < 2; b++) {
        int bps = b ? 300 : 450;
        tally_t t;
        memset(&t, 0, sizeof t);
        for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) for (s = 0; s < 2; s++) {
            line_t ln;
            memset(&ln, 0, sizeof ln);
            ln.bps = bps; ln.dbm0 = lv[i]; ln.hz = offs[j].hz; ln.ppm = offs[j].ppm; ln.seed = 1000u + (uint32_t)(i * 8 + j * 2 + s);
            memset(e, 0, sizeof e);
            count(&t, run_line(&ln, e), e);
        }
        report("levels", bps, &t);
    }
    for (b = 0; b < 2; b++) {
        int bps = b ? 300 : 450;
        tally_t t;
        memset(&t, 0, sizeof t);
        for (len = 20; len <= 40; len += 20) for (at = 100; at <= 300; at += 10) for (j = 0; j < 2; j++) {
            line_t ln;
            memset(&ln, 0, sizeof ln);
            ln.bps = bps; ln.dbm0 = -26; ln.hz = j ? -7 : 7; ln.seed = 2000u + (uint32_t)(at + len + j);
            ln.drop_at = at; ln.drop_len = len;
            memset(e, 0, sizeof e);
            count(&t, run_line(&ln, e), e);
        }
        report("dropout", bps, &t);
    }
    for (b = 0; b < 2; b++) {
        int bps = b ? 300 : 450;
        tally_t t;
        memset(&t, 0, sizeof t);
        for (j = 0; j < 2; j++) for (s = 0; s < 2; s++) {
            line_t ln;
            memset(&ln, 0, sizeof ln);
            ln.bps = bps; ln.dbm0 = -26; ln.hz = j ? -7 : 7; ln.seed = 3000u + (uint32_t)(j * 2 + s); ln.hp = 1;
            memset(e, 0, sizeof e);
            count(&t, run_line(&ln, e), e);
        }
        report("edge", bps, &t);
    }
    return 0;
}
