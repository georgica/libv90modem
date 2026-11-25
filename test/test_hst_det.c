/* The HST back channel's detector (hst_det, modem/hst_back.c; the spec's
 * "Detecting the caller's back channel"): the answerer's ANS_HST_WAIT
 * listens for the caller's back channel after ANSam. It fires when both
 * stages hold: the 250-500 Hz band over -43 dBm0 and 10 dB above 600-3400
 * Hz for 100 ms, and 24 of 30 decisions in a row on the grid on that
 * speed's receiver.
 *
 *   pos_450, pos_300  our transmitter (preamble, then random data) at -9,
 *                -26 and -43 dBm0, through a line of sim/resamp (+-100
 *                ppm), a +-7 Hz shift and white noise 20 dB below the
 *                signal over 0-4 kHz; four lines a level: fires, with the
 *                right speed, within 450 ms of the preamble's start at 450
 *                and 650 ms at 300 (the Task 12 review: the 24-of-30 window
 *                alone is first met at 379-392 and 559-599 ms)
 *   quiet        3 s each, never fires: silence; white noise at -50 dBm0;
 *                US dial tone (350 + 440 Hz at -13 dBm0 each); 60 Hz and
 *                50 Hz hum with harmonics at -20 dBm0; a V.21 caller and a
 *                Bell 103 caller (random data at -12 dBm0); the echo of our
 *                ANSam at -10 dBm0; a 375 Hz tone (the 450 carrier) and
 *                300 + 450 Hz at -20 dBm0; 250-500 Hz noise at -20 dBm0
 *   mixtures     3 s each, never fires: dial tone, hum and the 375 Hz tone
 *                each with white noise 0, 3 and 6 dB below it; the busy
 *                tone (480 + 620 Hz) with noise
 *   v22bis       a V.22bis caller (sim/callpeer, no V.8) against the
 *                library's answerer: 9 s of what the answerer hears (the
 *                echo of its ANSam, then the caller's start-up and data,
 *                connected at some 6 s), never fires
 *
 * The output (the golden file) holds exact integers and one pass or fail
 * line per bound; the detection times only with HST_VERBOSE set. Each case
 * seeds its own data. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "../modem/hst.h"
#include "../modem/v21.h"
#include "../modem/ansam.h"
#include "../modem/answer.h"
#include "../sim/noise.h"
#include "../sim/resamp.h"
#include "../sim/channel.h"
#include "../sim/callpeer.h"
#include "../sim/g711.h"

#define FS 8000
#define PI 3.14159265358979323846
#define DBM0 15770.0                   /* the RMS of 0 dBm0 */

static int verbose;
static void bound(const char *what, int ok)
{
    printf("  %s: %s\n", what, ok ? "pass" : "FAIL");
    CHECK(ok);
}
static double rms_of(double dbm0) { return DBM0 * pow(10, dbm0 / 20); }
static int16_t q16(double v) { return (int16_t)lrint(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static uint32_t prbs;
static int prbs_bit(void *u)
{
    (void)u;
    prbs = prbs * 1664525u + 1013904223u;
    return (int)(prbs >> 27) & 1;
}

/* the caller's line: sim/resamp (its clock), a frequency shift (a Hilbert
 * transformer, 63 taps, Hamming; the carrier by a recursive rotation, set
 * again every 1024 samples) and white noise, as test_hst_back's */
#define HILN 63
typedef struct {
    resamp_t rs;
    rng_t    g;
    double   nrms, w, c, s;
    double   hil[HILN], h[HILN];
    int      hp;
    long     n;
} chan_t;
static void chan_init(chan_t *ch, double hz, double ppm, double nrms, uint32_t seed)
{
    int i;
    memset(ch, 0, sizeof *ch);
    resamp_init(&ch->rs, 1.0 / ppm_ratio(ppm));
    rng_seed(&ch->g, seed);
    ch->nrms = nrms;
    ch->w = 2 * PI * hz / FS;
    ch->c = 1;
    for (i = 0; i < HILN; i++) {
        int k = i - HILN / 2;
        ch->hil[i] = (k & 1) ? 2.0 / (PI * k) * (0.54 + 0.46 * cos(2 * PI * k / (HILN - 1))) : 0.0;
    }
}
static int chan_push(chan_t *ch, double x, int16_t *out)
{
    double r[4], im, re, v;
    int n = resamp_push(&ch->rs, x, r, 4), i, j;
    for (i = 0; i < n; i++) {
        ch->hp = (ch->hp + 1) % HILN;
        ch->h[ch->hp] = r[i];
        re = ch->h[(ch->hp - HILN / 2 + HILN) % HILN];
        for (im = 0, j = 0; j < HILN; j += 2) im += ch->hil[j] * ch->h[(ch->hp - j + HILN) % HILN];
        if ((ch->n & 1023) == 0) { ch->c = cos(ch->w * ch->n); ch->s = sin(ch->w * ch->n); }
        v = re * ch->c - im * ch->s + rng_gauss(&ch->g) * ch->nrms;
        { double c = ch->c * cos(ch->w) - ch->s * sin(ch->w); ch->s = ch->s * cos(ch->w) + ch->c * sin(ch->w); ch->c = c; }
        ch->n++;
        out[i] = q16(v);
    }
    return n;
}

/* ---- positives ---- */
static const struct { double hz, ppm; } offs[4] = { { 7, 100 }, { 7, -100 }, { -7, 100 }, { -7, -100 } };

static void positive(int bps)
{
    static const double lv[3] = { -9, -26, -43 };
    static chan_t ch;
    static hst_det_t d;
    const int want = bps == 450 ? HST_DET_450 : HST_DET_300;
    const long lim = (bps == 450 ? 450L : 650L) * FS / 1000;
    long worst = 0;
    int l, c, bad = 0, wrong = 0;
    for (l = 0; l < 3; l++)
        for (c = 0; c < 4; c++) {
            hst_btx_t tx;
            long n = 0, at = -1, arrive = HILN / 2 + 1;
            int got = 0;
            double level = rms_of(lv[l]);
            prbs = (uint32_t)(0xDE7 + 100 * bps + 10 * l + c);
            hst_btx_init(&tx, bps, (int)lrint(level), prbs_bit, NULL);
            hst_btx_preamble(&tx);
            chan_init(&ch, offs[c].hz, offs[c].ppm, level / 10, (uint32_t)(0xDE70 + 100 * bps + 10 * l + c));
            hst_det_init(&d);
            while (n < 2 * FS && !got) {
                int16_t y[4];
                int k, m = chan_push(&ch, hst_btx_tick(&tx), y);
                for (k = 0; k < m && !got; k++, n++)
                    if ((got = hst_det_tick(&d, y[k])) != 0) at = n - arrive;
            }
            if (got != want) wrong += got != 0;
            if (got != want || at > lim) bad++;
            if (got && at > worst) worst = at;
            if (!got) worst = 1L << 30;
            if (verbose)
                printf("  (%+.0f dBm0, %+.0f Hz %+.0f ppm: %s after %.1f ms)\n", lv[l], offs[c].hz, offs[c].ppm,
                       got == HST_DET_450 ? "450" : got == HST_DET_300 ? "300" : "nothing", at * 1000.0 / FS);
        }
    printf("pos_%d: 12 lines, -9, -26 and -43 dBm0, 20 dB SNR, +-7 Hz, +-100 ppm\n", bps);
    if (verbose) printf("  (latest detection %.1f ms after the preamble's start)\n", worst * 1000.0 / FS);
    bound("never the other speed", wrong == 0);
    bound(bps == 450 ? "the right speed within 450 ms on every line" : "the right speed within 650 ms on every line",
          bad == 0);
}

/* ---- negatives ---- */
enum { Q_SILENCE, Q_NOISE, Q_DIAL, Q_HUM60, Q_HUM50, Q_V21, Q_B103, Q_ANSAM, Q_375, Q_300_450, Q_BAND, Q_BUSY };

/* n samples of input kind k at its level, plus white noise at nz dBm0 over
 * 0-4 kHz (nz > 0: none), into a fresh detector: what it returned last
 * (0 throughout, or the first detection) */
static int quiet_run(int k, double dbm0, double nz, double secs, uint32_t seed)
{
    static hst_det_t d;
    v21mod_t fsk;
    ansam_t an;
    rng_t g, gb;
    double rms = rms_of(dbm0), nr = nz > 0 ? 0 : rms_of(nz), bp[64] = { 0 }, bh[64];
    long n, nmax = (long)(secs * FS);
    int bit = 0, done = 0, r = 0, i;
    rng_seed(&g, seed);
    rng_seed(&gb, seed ^ 0x5A5A5A5Au);
    prbs = seed;
    /* the band noise: a windowed-sinc band-pass, 250-500 Hz, 63 taps */
    for (i = 0; i < 63; i++) {
        int t = i - 31;
        double v = t == 0 ? 2 * 250.0 / FS : (sin(2 * PI * 500 * t / FS) - sin(2 * PI * 250 * t / FS)) / (PI * t);
        bh[i] = v * (0.54 + 0.46 * cos(2 * PI * t / 62)) * sqrt(FS / 2 / 250.0);
    }
    if (k == Q_V21) v21mod_init(&fsk, 980, 1180, FS, 300, (int16_t)lrint(rms * sqrt(2)));
    if (k == Q_B103) v21mod_init(&fsk, 1270, 1070, FS, 300, (int16_t)lrint(rms * sqrt(2)));
    if (k == Q_ANSAM) ansam_init(&an, FS, (int16_t)lrint(rms * sqrt(2)), 1);
    hst_det_init(&d);
    for (n = 0; n < nmax && !r; n++) {
        double t = n / (double)FS, v = 0;
        int16_t s;
        switch (k) {
        case Q_SILENCE: break;
        case Q_NOISE: v = rng_gauss(&gb) * rms; break;
        case Q_DIAL: v = rms * sqrt(2) * (sin(2 * PI * 350 * t) + sin(2 * PI * 440 * t)); break;
        case Q_HUM60: case Q_HUM50: {
            double f = k == Q_HUM60 ? 60 : 50;
            v = rms * sqrt(2) * (sin(2 * PI * f * t) + 0.5 * sin(2 * PI * 3 * f * t + 0.3)
                                 + 0.3 * sin(2 * PI * 5 * f * t + 1.1) + 0.2 * sin(2 * PI * 7 * f * t + 2.0)
                                 + 0.1 * sin(2 * PI * 2 * f * t + 0.7));
            break;
        }
        case Q_V21: case Q_B103:
            v = v21mod_tick(&fsk, bit, &done);
            if (done) bit = prbs_bit(NULL);
            break;
        case Q_ANSAM: ansam_run(&an, &s, 1); v = s; break;
        case Q_375: v = rms * sqrt(2) * sin(2 * PI * 375 * t); break;
        case Q_300_450: v = rms * sqrt(2) * (sin(2 * PI * 300 * t) + sin(2 * PI * 450 * t)); break;
        case Q_BAND:
            memmove(bp + 1, bp, 62 * sizeof bp[0]);
            bp[0] = rng_gauss(&gb) * rms;
            for (i = 0; i < 63; i++) v += bh[i] * bp[i];
            break;
        default: v = rms * sqrt(2) * (sin(2 * PI * 480 * t) + sin(2 * PI * 620 * t)); break;   /* busy */
        }
        if (nr > 0) v += rng_gauss(&g) * nr;
        r = hst_det_tick(&d, q16(v));
    }
    if (verbose && r) printf("  (input %d at %.0f dBm0: fired %d at %.3f s)\n", k, dbm0, r, n / (double)FS);
    return r;
}

static void quiet(void)
{
    static const struct { int k; double dbm0; } in[] = {
        { Q_SILENCE, 0 }, { Q_NOISE, -50 }, { Q_DIAL, -13 }, { Q_HUM60, -20 }, { Q_HUM50, -20 },
        { Q_V21, -12 }, { Q_B103, -12 }, { Q_ANSAM, -10 }, { Q_375, -20 }, { Q_300_450, -20 }, { Q_BAND, -20 },
    };
    int i, fired = 0;
    for (i = 0; i < (int)(sizeof in / sizeof in[0]); i++)
        fired += quiet_run(in[i].k, in[i].dbm0, 1, 3.0, (uint32_t)(0x900D + i)) != 0;
    printf("quiet: 11 inputs, 3 s each\n");
    bound("never fires", fired == 0);
}

static void mixtures(void)
{
    static const int kind[4] = { Q_DIAL, Q_HUM60, Q_375, Q_BUSY };
    static const double below[3] = { 0, 3, 6 };
    int i, j, fired = 0;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 3; j++) {
            double lv = kind[i] == Q_DIAL ? -13 : -20;
            fired += quiet_run(kind[i], lv, lv - below[j], 3.0, (uint32_t)(0x313 + 10 * i + j)) != 0;
        }
    printf("mixtures: 12 inputs (dial tone, hum, 375 Hz, busy tone; noise 0, 3 and 6 dB below), 3 s each\n");
    bound("never fires", fired == 0);
}

/* a V.22bis caller against the library's answerer, as test_answer's */
static void v22bis(void)
{
    static const double one[1] = { 1.0 };
    static answer_t a;
    static callpeer_t c;
    static v14_t dte;
    static hst_det_t d;
    channel_cfg cfg;
    channel_t ch;
    answer_cfg ac;
    int16_t atx = 0, ctx = 0;
    int n, fired = 0, conn = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.ds_noise_dbfs = -55.0; cfg.us_noise_dbfs = -55.0;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.echo_gain = 0.2; cfg.echo_delay = 21; cfg.full_scale = 32124.0; cfg.seed = 23;
    channel_init(&ch, &cfg);
    v14_init(&dte);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = V8_MOD_V22 | V8_MOD_V21; ac.getbit = v14_getbit; ac.putbit = v14_putbit; ac.user = &dte;
    answer_init(&a, &ac);
    callpeer_init(&c, 0, 0, 1, 3000);
    hst_det_init(&d);
    for (n = 0; n < 9 * FS; n++) {
        double client_rx;
        uint8_t srv;
        int16_t heard, t;
        channel_tick(&ch, linear_to_ulaw(atx), (double)ctx, &client_rx, &srv);
        heard = (int16_t)ulaw_to_linear(srv);
        t = answer_tick(&a, heard);
        ctx = callpeer_tick(&c, (int16_t)lround(client_rx));
        atx = t;
        if (!fired && hst_det_tick(&d, heard)) fired = 1;
        conn |= a.connected && c.connected;
    }
    printf("v22bis: 9 s of a V.22bis call, as the answerer hears it, %s\n", conn ? "connected" : "not connected");
    bound("never fires", !fired);
}

static void pos_450(void) { positive(450); }
static void pos_300(void) { positive(300); }

int main(int argc, char **argv)
{
    int only = t_case_arg(argc, argv, 5);
    verbose = getenv("HST_VERBOSE") != NULL;
    if (T_CASE(only, 1)) pos_450();
    if (T_CASE(only, 2)) pos_300();
    if (T_CASE(only, 3)) quiet();
    if (T_CASE(only, 4)) mixtures();
    if (T_CASE(only, 5)) v22bis();
    T_REPORT("test_hst_det");
}
