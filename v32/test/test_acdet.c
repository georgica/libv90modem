/* The calling modem's detector for the answerer's AC (600 + 3000 Hz, 6.1) and
 * its phase reversals, alone: a receiver in call mode fed by an answering
 * transmitter's AC / CA, with our own AA / CC echo on top (the echo canceller
 * has not trained yet when it counts). How long after a reversed symbol's
 * pulse centre reaches the line it is counted (the 64 T turn-round of 6.1
 * depends on it), over all sampling phases; and what is not AC: our own echo
 * alone, other tones, S, data, noise, a signal too weak. */
#include <string.h>
#include "test.h"
#include "v32_priv.h"
#include "v32line.h"
#include "noise.h"

#define T (10.0 / 3)

static uint32_t dl = 1;
static int data_bit(void *u) { (void)u; dl = dl * 1103515245u + 12345u; return (dl >> 16) & 1; }

/* the far signal: an answering transmitter in `mode` (AC: then CA at symbol
 * 300 and AC again at symbol 400), or a tone of `hz` when hz > 0 */
typedef struct {
    v32tx_t far, own;
    double  far_gain, own_gain;
    int     hz, mode, delay;
    double  ph;
    int16_t dline[16];
    long    n, nsym;
    long    t_ca, t_ac;        /* tick at which the first CA / AC symbol left (pulse centre) */
    int     own_cc_at;         /* our AA turns into CC at this symbol (0: never) */
    rng_t   rng; double sigma;
} sig_t;

static void sig_init(sig_t *s, int mode, int hz, double far_dbm0, double own_dbm0, int delay)
{
    memset(s, 0, sizeof *s);
    v32tx_init(&s->far, 1, 3000, data_bit, NULL);
    v32tx_init(&s->own, 0, 3000, data_bit, NULL);
    s->far.rate = 4;
    v32tx_set_mode(&s->far, (v32_txmode)mode);
    v32tx_set_mode(&s->own, own_dbm0 > -100 ? V32_TX_AA : V32_TX_SILENCE);
    s->far_gain = v32line_rms(far_dbm0) / 3000;
    s->own_gain = own_dbm0 > -100 ? v32line_rms(own_dbm0) / 3000 : 0;
    s->hz = hz; s->mode = mode; s->delay = delay;
    rng_seed(&s->rng, 5);
}

static int16_t sig_tick(sig_t *s)
{
    double x, y;
    long q;
    s->n++;
    if (v32tx_symbol_due(&s->far)) {
        if (s->mode == V32_TX_AC && s->nsym == 300) { v32tx_set_mode(&s->far, V32_TX_CA); s->t_ca = s->n + V32_TX_DELAY + s->delay; }
        if (s->mode == V32_TX_AC && s->nsym == 400) { v32tx_set_mode(&s->far, V32_TX_AC); s->t_ac = s->n + V32_TX_DELAY + s->delay; }
        s->nsym++;
    }
    if (v32tx_symbol_due(&s->own) && s->own_cc_at && s->nsym == s->own_cc_at) v32tx_set_mode(&s->own, V32_TX_CC);
    if (s->hz > 0) { x = v32line_rms(-10) * 1.41421356 * sin(s->ph); s->ph += 2 * 3.14159265358979 * s->hz / 8000; }
    else x = v32tx_tick(&s->far) * s->far_gain;
    /* the far signal reaches us `delay` samples late */
    s->dline[s->n & 15] = (int16_t)lrint(x);
    y = s->dline[(s->n - s->delay) & 15] + v32tx_tick(&s->own) * s->own_gain + s->sigma * rng_gauss(&s->rng);
    q = lrint(y);
    return (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q);
}

/* runs `n` samples; returns the longest ac_run seen; *pure the longest ac_pure */
static int listen(sig_t *s, v32rx_t *r, long n, int *pure)
{
    int best = 0, bp = 0;
    while (n-- > 0) {
        v32rx_tick(r, sig_tick(s));
        if (r->ac_run > best) best = r->ac_run;
        if (r->ac_pure > bp) bp = r->ac_pure;
    }
    if (pure) *pure = bp;
    return best;
}

int main(void)
{
    static v32rx_t r;
    static sig_t s;
    int d, best, pure, lo = 1000, hi = -1000;
    double sum = 0;

    printf("AC, CA, AC (-16 dBm0) with our own AA echo (-15 dBm0): reversals and their latency\n");
    for (d = 0; d < 10; d++) {
        int l1, l2;
        sig_init(&s, V32_TX_AC, 0, -16, -15, d);
        v32rx_init(&r, 0, NULL, NULL);
        listen(&s, &r, 300 * 10 / 3, NULL);
        CHECK(r.ac_run >= 32);                          /* AC is there well before the reversal */
        CHECK_EQ_I(r.ac_rev, 0);
        listen(&s, &r, 100 * 10 / 3 - 20, NULL);
        CHECK_EQ_I(r.ac_rev, 1);
        l1 = (int)((long)r.ac_rev_t - s.t_ca);
        listen(&s, &r, 200 * 10 / 3, NULL);
        CHECK_EQ_I(r.ac_rev, 2);
        l2 = (int)((long)r.ac_rev_t - s.t_ac);
        CHECK(r.ac_run >= 32);
        lo = l1 < lo ? l1 : lo; lo = l2 < lo ? l2 : lo;
        hi = l1 > hi ? l1 : hi; hi = l2 > hi ? l2 : hi;
        sum += l1 + l2;
    }
    printf("  latency from the pulse centre at the input: %d to %d samples, mean %.1f (V32_AC_LAT %d)\n", lo, hi, sum / 20, V32_AC_LAT);
    CHECK(hi - lo <= 4);                                /* +- 2 samples: well inside the +- 2 T of 6.1 */
    CHECK_NEAR(sum / 20, V32_AC_LAT, 1.0);

    /* our own AA turning into CC, while AC goes on: the canceller is not
     * trained yet, so the caller's sequencer stops counting for a while */
    sig_init(&s, V32_TX_AC, 0, -16, -6, 3);
    s.own_cc_at = 200;
    v32rx_init(&r, 0, NULL, NULL);
    listen(&s, &r, 290 * 10 / 3, NULL);
    printf("  our own AA -> CC at -6 dBm0 under AC: %d reversal(s) counted (the sequencer blanks them)\n", r.ac_rev);

    /* weak, and too weak */
    sig_init(&s, V32_TX_AC, 0, -40, -15, 0);
    v32rx_init(&r, 0, NULL, NULL);
    best = listen(&s, &r, 280 * 10 / 3, NULL);
    printf("  AC at -40 dBm0: longest run %d half symbols\n", best);
    CHECK(best >= 256);
    sig_init(&s, V32_TX_AC, 0, -52, -15, 0);
    v32rx_init(&r, 0, NULL, NULL);
    best = listen(&s, &r, 280 * 10 / 3, NULL);
    printf("  AC at -52 dBm0: longest run %d\n", best);
    CHECK(best < 8);

    /* AC alone is pure; with our echo it is not */
    sig_init(&s, V32_TX_AC, 0, -16, -200, 0);
    v32rx_init(&r, 0, NULL, NULL);
    listen(&s, &r, 280 * 10 / 3, &pure);
    CHECK(pure >= 256);
    printf("  AC alone: pure for %d half symbols\n", pure);

    printf("not AC (with our own AA echo at -15 dBm0)\n");
    {
        static const int tones[] = { 1200, 1650, 1850, 2100, 2250, 2400, 3300 };
        int k;
        sig_init(&s, V32_TX_SILENCE, 0, -16, -15, 0);
        v32rx_init(&r, 0, NULL, NULL);
        best = listen(&s, &r, 8000, NULL);
        printf("  our AA alone: %d\n", best);
        CHECK(best < 8); CHECK_EQ_I(r.ac_rev, 0);
        for (k = 0; k < (int)(sizeof tones / sizeof tones[0]); k++) {
            sig_init(&s, V32_TX_SILENCE, tones[k], -16, -15, 0);
            v32rx_init(&r, 0, NULL, NULL);
            best = listen(&s, &r, 8000, NULL);
            printf("  %d Hz at -10 dBm0: %d\n", tones[k], best);
            CHECK(best < 32); CHECK_EQ_I(r.ac_rev, 0);     /* short of what a reversal needs */
        }
        sig_init(&s, V32_TX_DATA, 0, -16, -15, 0);
        v32rx_init(&r, 0, NULL, NULL);
        best = listen(&s, &r, 16000, &pure);
        printf("  14400 bit/s data: %d (pure %d)\n", best, pure);
        CHECK(best < 32); CHECK(pure < 32);
        sig_init(&s, V32_TX_SILENCE, 0, -16, -200, 0);
        s.sigma = v32line_rms(-20);
        v32rx_init(&r, 0, NULL, NULL);
        best = listen(&s, &r, 16000, NULL);
        printf("  noise at -20 dBm0: %d\n", best);
        CHECK(best < 32);
        /* S has 600 and 3000 Hz in it as well: never all there is */
        sig_init(&s, V32_TX_S, 0, -16, -200, 0);
        v32rx_init(&r, 0, NULL, NULL);
        listen(&s, &r, 8000, &pure);
        printf("  S alone: pure for %d\n", pure);
        CHECK(pure < 8);
    }
    T_REPORT("test_acdet");
}
