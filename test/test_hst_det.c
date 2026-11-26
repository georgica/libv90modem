/* The HST back channel's detector (hst_det, modem/hst_back.c; the spec's
 * "Detecting the caller's back channel"): the answerer's ANS_HST_WAIT
 * listens for the caller's back channel after ANSam. It fires when both
 * stages hold: the 250-500 Hz band over -45 dBm0 (the receivers' carrier
 * detect, on at -44, is the floor that binds) and 10 dB above 600-3400 Hz
 * over 100 ms, and 24 of 30 decisions in a row on the grid on that speed's
 * receiver.
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
 *   impaired_450, impaired_300  a 20 ms and a 40 ms dropout early in the
 *                preamble, and a +-3 dB fade at 5 and 10 Hz: detected within
 *                the same budgets
 *   late_drop_300  a 40 ms dropout 300 ms into the preamble and a 20 ms one
 *                at 200 ms (the Task 13 review's N3 rows): within 650 ms
 *   courier      a back channel as a Courier was recorded sending one
 *                ([REC]: 17 symbols of preamble, then flags) at -6, -9 and
 *                -12 dBm0, the detector started at many phases against it:
 *                450 within W from at least 95 % of them, within W - 100
 *                ms from 90 %
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
#include "../sim/hstcall.h"

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

/* one line: our transmitter at dbm0 through the line; optionally the
 * signal off for drop_len s from drop_at s after its start (a lost VoIP
 * packet), and a slow fade of +-am_db dB at am_hz. What fired (0 none),
 * and when, in samples from the preamble's arrival */
/* late_drop: whether the energy stage held at every sample from
 * held_from samples after the preamble's arrival until the detection (-1:
 * not watched) */
static long held_from = -1;
static int held;
static int detect_line(int bps, double dbm0, int c, uint32_t seed, double drop_at, double drop_len,
                       double am_hz, double am_db, long *at)
{
    static chan_t ch;
    static hst_det_t d;
    hst_btx_t tx;
    long n = 0, arrive = HILN / 2 + 1;
    int got = 0;
    double level = rms_of(dbm0);
    prbs = seed;
    hst_btx_init(&tx, bps, (int)lrint(level), prbs_bit, NULL);
    hst_btx_preamble(&tx);
    chan_init(&ch, offs[c].hz, offs[c].ppm, level / 10, seed * 7u + 1u);
    hst_det_init(&d);
    *at = -1;
    held = 1;
    while (n < 2 * FS && !got) {
        int16_t y[4];
        double t = n / (double)FS, v = hst_btx_tick(&tx);
        int k, m;
        if (drop_len > 0 && t >= drop_at && t < drop_at + drop_len) v = 0;
        if (am_db > 0) v *= pow(10, am_db / 20 * sin(2 * PI * am_hz * t));
        m = chan_push(&ch, v, y);
        for (k = 0; k < m && !got; k++, n++) {
            if ((got = hst_det_tick(&d, y[k])) != 0) *at = n - arrive;
            else if (held_from >= 0 && n - arrive >= held_from && !hst_det_energy(&d)) held = 0;
        }
    }
    return got;
}

static void positive(int bps)
{
    static const double lv[3] = { -9, -26, -43 };
    const int want = bps == 450 ? HST_DET_450 : HST_DET_300;
    const long lim = (bps == 450 ? 450L : 650L) * FS / 1000;
    long worst = 0, at;
    int l, c, bad = 0, wrong = 0, got;
    for (l = 0; l < 3; l++)
        for (c = 0; c < 4; c++) {
            got = detect_line(bps, lv[l], c, (uint32_t)(0xDE7 + 100 * bps + 10 * l + c), 0, 0, 0, 0, &at);
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

/* a lost VoIP packet or two (20 ms and 40 ms) early in the preamble (60 ms
 * at 450, 100 ms at 300 after its start), and a slow fade of +-3 dB at 5
 * and at 10 Hz: four lines each (+-7 Hz, +-100 ppm) at -26 dBm0, detected
 * with the right speed within the budget. A dropout later, in the
 * acquisition (140-180 ms at either speed; near the carrier's arrival at
 * 450) or while the detector's window fills after the lock (400 ms at
 * 300), costs a second acquisition or a refill of the window: past the
 * budget on some lines, with the power rule or without it (PROGRESS "hst
 * 13", fix round 1). */
static void impaired(int bps)
{
    const int want = bps == 450 ? HST_DET_450 : HST_DET_300;
    const long lim = (bps == 450 ? 450L : 650L) * FS / 1000;
    const double drop_at = bps == 450 ? 0.06 : 0.1;
    static const double am[2] = { 5, 10 };
    int i, c, bad_drop = 0, bad_am = 0, got;
    long at, worst_drop = 0, worst_am = 0;
    for (i = 0; i < 2; i++)
        for (c = 0; c < 4; c++) {
            got = detect_line(bps, -26, c, (uint32_t)(0xD80 + 100 * bps + 10 * i + c), drop_at, i ? 0.04 : 0.02, 0, 0, &at);
            if (got != want || at > lim) bad_drop++;
            if (at > worst_drop || !got) worst_drop = got ? at : 1L << 30;
            got = detect_line(bps, -26, c, (uint32_t)(0xA30 + 100 * bps + 10 * i + c), 0, 0, am[i], 3, &at);
            if (got != want || at > lim) bad_am++;
            if (at > worst_am || !got) worst_am = got ? at : 1L << 30;
        }
    printf("impaired_%d: 8 lines with a dropout early in the preamble (20 and 40 ms), 8 with a 3 dB fade (5 and 10 Hz)\n", bps);
    if (verbose)
        printf("  (latest detection: %.1f ms with a dropout, %.1f ms with a fade)\n",
               worst_drop * 1000.0 / FS, worst_am * 1000.0 / FS);
    bound(bps == 450 ? "a dropout: the right speed within 450 ms on every line"
                     : "a dropout: the right speed within 650 ms on every line", bad_drop == 0);
    bound(bps == 450 ? "a fade: the right speed within 450 ms on every line"
                     : "a fade: the right speed within 650 ms on every line", bad_am == 0);
}

/* the Task 13 review's rows at 300 (its N3): a dropout later in the
 * acquisition, 40 ms at 300 ms and 20 ms at 200 ms, on the four lines at
 * -26 dBm0. The detector alone is late on two of them (766 and 776 ms at
 * +7 Hz with the 40 ms one; 786-796 ms before the fix of 6794e93): each is
 * detected within the 650 ms budget, or after it with the energy stage
 * held from 650 ms until the detection and within HST_W_MAX_MS, which is
 * what ANS_HST_WAIT waits on past W for (modem/hst.h) */
static void late_drop(void)
{
    static const double at[2] = { 0.3, 0.2 }, len[2] = { 0.04, 0.02 };
    const long lim = 650L * FS / 1000;
    int i, c, bad = 0, got, late = 0;
    long t, worst = 0;
    held_from = lim;
    for (i = 0; i < 2; i++)
        for (c = 0; c < 4; c++) {
            got = detect_line(300, -26, c, (uint32_t)(0xE10 + 10 * i + c), at[i], len[i], 0, 0, &t);
            if (got == HST_DET_300 && t > lim) late++;
            if (got != HST_DET_300 || t > (long)HST_W_MAX_MS * FS / 1000 || (t > lim && !held)) bad++;
            if (verbose) printf("  (line %d, drop %d: %.1f ms)\n", c, i, got ? t * 1000.0 / FS : -1.0);
            if (t > worst || !got) worst = got ? t : 1L << 30;
        }
    held_from = -1;
    printf("late_drop_300: 8 lines, a 40 ms dropout at 300 ms and a 20 ms one at 200 ms: %d past 650 ms\n", late);
    if (verbose) printf("  (latest detection: %.1f ms)\n", worst * 1000.0 / FS);
    bound("the right speed within 650 ms, or the energy stage held from then on (W's extension)", bad == 0);
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

/* courier: a back channel as a Courier was recorded sending one
 * (hst-facts.md `Back channel`, [REC]; synthetic, from the measured
 * figures, not the recording): sim/hstcall's transmitter at 450 (375 Hz,
 * 150 baud, its RRC 0.35), a preamble of 17 symbols of scrambled ones (the
 * recording's 110-120 ms), then scrambled HDLC flags, at -6, -9 and -12
 * dBm0 (the recording: -12.1), on two lines (+-7 Hz, +-100 ppm, noise 20
 * dB under the signal). The detector started afresh at COUR_STARTS
 * phases, from 300 ms before the signal's start to 6 s into it (as the
 * wait may begin anywhere against a caller's start): at least 95 % fire
 * 450 within W, HST_W_MS, and 90 % with 100 ms to spare. 0.7.2's GAIN0
 * (-20 dBm0), its baseband from before the carrier at another level from
 * the rest (rx_block), failed its first acquisition more often: 78 % with
 * 100 ms to spare here, and on the recording itself the detector,
 * started afresh at 262 points through the call, fired within W from 37 %
 * of them (49 % now; the report's 63-point set: 12 of 63) */
#define COUR_LEN   (7 * FS)
#define COUR_ON    (FS / 2)
#define COUR_STEP  211
#define COUR_STARTS ((COUR_ON - 300 * 8 + 6 * FS) / COUR_STEP)
static int16_t cour_y[COUR_LEN];
static int cour_flag;
static int cour_bit(void *u) { (void)u; return (0x7E >> (cour_flag++ & 7)) & 1; }
static void courier(void)
{
    static const double lv[3] = { -6, -9, -12 };
    static const int line[2] = { 0, 3 };
    static chan_t ch;
    static hst_det_t d;
    static hc_btx tx;
    int l, c, i, in_w = 0, in_w100 = 0, total = 0, wrong = 0;
    long worst = 0;
    for (l = 0; l < 3; l++)
        for (c = 0; c < 2; c++) {
            long n = 0, t0;
            int lin = 0, lin100 = 0, ln = 0;
            hc_btx_init(&tx, 450, rms_of(lv[l]), cour_bit, NULL);
            tx.pre = 17;
            cour_flag = 0;
            chan_init(&ch, offs[line[c]].hz, offs[line[c]].ppm, rms_of(lv[l]) / 10, 0xC0u + (uint32_t)(10 * l + c));
            while (n < COUR_LEN) {
                int16_t y[4];
                int k, m;
                if (n == COUR_ON) hc_btx_start(&tx);
                m = chan_push(&ch, hc_btx_tick(&tx), y);
                for (k = 0; k < m && n < COUR_LEN; k++) cour_y[n++] = y[k];
            }
            for (i = 0; i < COUR_STARTS; i++) {
                long st, at;
                int got = 0;
                t0 = COUR_ON - 300 * 8 + (long)i * COUR_STEP;
                st = t0 > COUR_ON ? t0 : COUR_ON;
                hst_det_init(&d);
                for (n = t0; n < COUR_LEN && !(got = hst_det_tick(&d, cour_y[n])); n++) ;
                at = (n - st) / 8;
                ln++;
                if (got != HST_DET_450) { wrong += got != 0; continue; }
                if (at <= HST_W_MS) lin++;
                if (at <= HST_W_MS - 100) lin100++;
                if (at > worst) worst = at;
            }
            if (verbose)
                printf("  (%+.0f dBm0, %+.0f Hz %+.0f ppm: %d of %d within %d ms, %d within %d)\n", lv[l], offs[line[c]].hz,
                       offs[line[c]].ppm, lin, ln, HST_W_MS, lin100, HST_W_MS - 100);
            in_w += lin; in_w100 += lin100; total += ln;
        }
    printf("courier: a Courier's back channel (17 symbols of preamble, then flags) at -6, -9 and -12 dBm0, 2 lines, %d starts each\n",
           COUR_STARTS);
    if (verbose) printf("  (%d of %d within W, %d within W - 100 ms; the latest %ld ms)\n", in_w, total, in_w100, worst);
    bound("never the other speed", wrong == 0);
    bound("450 within W from at least 95 % of the starts", in_w * 100 >= 95 * total);
    bound("and within W - 100 ms (the slack W was sized with) from at least 90 %", in_w100 * 100 >= 90 * total);
}

static void pos_450(void) { positive(450); }
static void pos_300(void) { positive(300); }
static void imp_450(void) { impaired(450); }
static void imp_300(void) { impaired(300); }

int main(int argc, char **argv)
{
    int only = t_case_arg(argc, argv, 9);
    verbose = getenv("HST_VERBOSE") != NULL;
    if (T_CASE(only, 1)) pos_450();
    if (T_CASE(only, 2)) pos_300();
    if (T_CASE(only, 3)) quiet();
    if (T_CASE(only, 4)) mixtures();
    if (T_CASE(only, 5)) v22bis();
    if (T_CASE(only, 6)) imp_450();
    if (T_CASE(only, 7)) imp_300();
    if (T_CASE(only, 8)) late_drop();
    if (T_CASE(only, 9)) courier();
    T_REPORT("test_hst_det");
}
