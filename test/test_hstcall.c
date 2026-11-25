/* sim/hstcall, the simulated HST caller, against our HST pump's pieces
 * (modem/hst_back.c, modem/hst_fwd.c) on sim/v32line; and the two channels
 * at once (docs/history/hst-facts.md; the spec's "Test plan").
 *
 *   tone          the answer tone (2100 Hz with ANSam's reversals and AM)
 *                 for 1 s: hstcall's back channel starts 30 to 45 ms after
 *                 it ends; a 300 ms burst starts nothing; a 600 ms tone after
 *                 it does
 *   back_to_ours  hstcall's back transmitter at 450 and 300, random data,
 *                 v32line (mu-law, 6 dB loss, a mild loop, 25 ms), noise 20
 *                 dB under the signal over 0-4 kHz: our hst_brx locks, no
 *                 bit in error in 10^5 after the lock (BER < 1e-5), no loss
 *                 of lock; at +-7 Hz and +-100 ppm the same over 2 x 10^4
 *                 (TEST_SHORT: 10^4 and 2 x 10^3)
 *   back_from_ours our hst_btx at 450 and 300 the same way: hstcall's back
 *                 receiver, no bit in error
 *   fwd_from_ours our hst_ftx's long training at codes 2..6, and at 14400
 *                 with +-7 Hz and +-100 ppm (under make check-hst-bm, V.17's
 *                 long train), the line 30 dB: hstcall's forward receiver
 *                 reaches data, no bit in error in 10^5 (TEST_SHORT 10^4)
 *                 from the 300th
 *   fwd_to_ours   hstcall's forward transmitter: its long training at codes
 *                 2..6, its short one at 6 and V.17's long train at 3 and 6:
 *                 our hst_frx reaches data, no bit in error
 *   marks_self    hstcall's forward transmitter and receiver, 6 -> 5 and
 *                 4 -> 6 by marks (0x3R), the receiver armed 36 ms after the
 *                 marks began (the line, and an LNA's decoding): it
 *                 follows, no bit lost outside the 72 mark symbols'
 *                 neighbourhood
 *   coexist       our forward transmitter (14400, -10 dBm0) and hstcall's
 *                 back transmitter (450 and 300) at once, the back channel
 *                 at -33 dBm0 at our side, our echo 6 dB under our signal (6
 *                 dB ERL) and hstcall's own the same, -54 dBm0 of noise each
 *                 way: on a linear line each receiver's SNR within 1 dB of
 *                 its SNR with no echo; with G.711 at our side hstcall's
 *                 within 1 dB and ours within 2 dB (G.711 costs it 0.5 to
 *                 1.5 dB over 16 lines, task 17's report: our transmit's own
 *                 coding noise comes back in the echo, and the echo is coded
 *                 again with the back channel); no bit in error either way
 *   link          a test answerer made of our pieces and an mnppeer
 *                 responder: the tone, hstcall's LR at 450, our long training
 *                 at the opening code, MNP up, our short training at the
 *                 best agreed code; hstcall follows (code 6, back 450; with
 *                 back_bps 300, back 300); data both ways intact
 *   reversal      the same, hstcall's backlog over 64 octets: its 0x1R, our
 *                 LNA, our forward off and back on, hstcall's long training
 *                 at 6; data intact, reversals 1
 *   stored_eq     our short training into hstcall's receiver starting from
 *                 the equaliser it kept as our carrier faded: code 6, 24 dB,
 *                 and -7 Hz -100 ppm, no bit in error
 *   holder_rates  reversed, hstcall holding the forward channel takes our
 *                 0x35 (marks) and 0x24 (a short training), each after its
 *                 LNA has gone; our side arms or sets its code on that LNA
 *                 (with hstcall's receiver: our hst_frx follows marks only
 *                 from Task 22); data intact
 *   turn_back     reversed, our 0x16: hstcall back on the back channel, our
 *                 short training at 6 met by the equaliser hstcall kept when
 *                 it left that role; data intact, reversals 2
 *   forced_twice  three forced requests in one call: request 0 (0x24),
 *                 force_more[1] set alone (0x25), force_more[2] (0x26)
 *   rev_lost      ignore_lna: hstcall's 0x1R acknowledged, and it never
 *                 trains (its forward transmitter never on)
 *   ignore_ln     reversed, our 0x35 acknowledged and ignored: no marks,
 *                 both ends stay at 6, data intact
 *   knobs         lr_delay_ms (no frame before it); ld_hold_ms, 1000 by
 *                 default and 3000 (the carrier held that long after the LD)
 *
 * Under make check-hst-bm (HST_PROFILE_BM) only fwd_from_ours runs.
 *
 * The output (the golden file) holds exact integers and one pass or fail
 * line per bound; the measured values, from floating-point arithmetic, only
 * with HST_VERBOSE set. Each case seeds its own data. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "../modem/hst.h"
#include "../sim/v32line.h"
#include "../sim/mnppeer.h"
#include "../sim/hstcall.h"

#define FS 8000
#define PI 3.14159265358979323846

static int verbose;
static void bound(const char *what, int ok)
{
    printf("  %s: %s\n", what, ok ? "pass" : "FAIL");
    CHECK(ok);
}

static double dbm0_of(double rms) { return 20 * log10(rms / v32line_rms(0)); }
static int16_t sat16(double v)
{
    v = floor(v + 0.5);
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* ---- random data, logged ---- */
/* bits compared: none in error in 10^5 is BER < 1e-5; the offset lines
 * 2 x 10^4 (TEST_SHORT: 10^4 and 2 x 10^3) */
#ifdef TEST_SHORT
#define NB_BACK 10000L
#define NB_OFF  2000L
#define NB_FWD  10000L
#else
#define NB_BACK 100000L
#define NB_OFF  20000L
#define NB_FWD  100000L
#endif
#define NLOG 200000L               /* the logs: the longest run (coexist, 9 s at 14400) */
typedef struct {
    uint32_t r;
    long     n;
} src_t;
static uint8_t txb[NLOG], rxb[NLOG];
static long nrx;
static int src_bit(void *user)
{
    src_t *s = (src_t *)user;
    int b;
    s->r = s->r * 1664525u + 1013904223u;
    b = (int)(s->r >> 27) & 1;
    if (s->n < NLOG) txb[s->n] = (uint8_t)b;
    s->n++;
    return b;
}
static void rx_put(int b) { if (nrx < NLOG) rxb[nrx] = (uint8_t)b; nrx++; }

/* the sent data among the received bits: the 64 received before `upto`
 * found among those sent; then errors over the received bits [from, upto)
 * that are sent bits from the skip-th on; off: the received index of sent
 * bit 0 */
typedef struct { int found; long cmp, errs, off; } cmp_t;
static cmp_t compare_in(const uint8_t *tx, long nsent, const uint8_t *rx, long nrecv, long skip, long from, long upto)
{
    cmp_t o = { 0, 0, 0, 0 };
    long i0, k, j;
    if (nsent > NLOG) nsent = NLOG;
    if (nrecv > NLOG) nrecv = NLOG;
    if (upto > nrecv) upto = nrecv;
    i0 = upto - 64;
    if (i0 < 0) return o;
    for (k = 0; k + 64 <= nsent; k++) {
        for (j = 0; j < 64 && rx[i0 + j] == tx[k + j]; j++) ;
        if (j == 64) break;
    }
    if (k + 64 > nsent) return o;
    o.found = 1;
    o.off = i0 - k;
    for (j = from < 0 ? 0 : from; j < upto; j++) {
        long t = j - o.off;
        if (t < skip || t >= nsent) continue;
        o.cmp++;
        if (rx[j] != tx[t]) o.errs++;
    }
    return o;
}
static cmp_t compare(long nsent, long skip) { return compare_in(txb, nsent, rxb, nrx, skip, 0, nrx); }

/* ---- the line ---- */
static const double loop4[] = { 0.3, 1.0, -0.5, 0.3 };
static double loop_gain(double f)
{
    double re = 0, im = 0;
    int k;
    for (k = 0; k < 4; k++) { re += loop4[k] * cos(2 * PI * f * k / FS); im -= loop4[k] * sin(2 * PI * f * k / FS); }
    return sqrt(re * re + im * im);
}
static double band_gain(double lo, double hi)
{
    double g = 0;
    int n = 0;
    double f;
    for (f = lo; f <= hi; f += 10, n++) g += loop_gain(f) * loop_gain(f);
    return sqrt(g / n);
}
static v32line_t L;
static int mulaw = 1;              /* G.711 at our interface (coexist runs without it too) */
static void line(double loss, double near, double cnear, double na, double nc, double hz, double ppm, uint32_t seed)
{
    v32line_cfg c;
    memset(&c, 0, sizeof c);
    c.mulaw = mulaw; c.loss_db = loss; c.delay = 200; c.taps = loop4; c.ntaps = 4;
    c.near_db = near; c.c_near_db = cnear; c.far_db = c.c_far_db = 300;
    c.noise_a_dbm0 = na; c.noise_c_dbm0 = nc;
    c.freq_offset = hz; c.ppm = ppm; c.seed = seed;
    v32line_init(&L, &c);
}

/* ====================== tone ====================== */
static hstcall_t HC;

/* samples of ANSam-like tone (on) or silence, through hstcall alone; the
 * first sample it sends, -1 none */
static long tone_feed(long n0, long n, int on, long *first)
{
    long i;
    for (i = 0; i < n; i++) {
        long t = n0 + i;
        double x = 0;
        if (on) {
            double am = 1 + 0.2 * sin(2 * PI * 15 * t / FS);
            int rev = (int)((t / (FS * 450 / 1000)) & 1);
            x = v32line_rms(-12) * sqrt(2.0) / 1.2 * am * sin(2 * PI * 2100 * t / FS + (rev ? PI : 0));
        }
        if (hstcall_tick(&HC, sat16(x)) != 0 && *first < 0) *first = t;
    }
    return n0 + n;
}

static void tone(void)
{
    hstcall_cfg c;
    long t = 0, first = -1, end;
    hstcall_defaults(&c);
    hstcall_init(&HC, &c);
    t = tone_feed(t, FS, 1, &first);
    end = t;
    t = tone_feed(t, FS / 2, 0, &first);
    printf("tone: 1000 ms, then silence\n");
    if (verbose) printf("  (the back channel %.1f ms after the tone's end)\n", (first - end) / 8.0);
    bound("starts 30 to 45 ms after the tone", first >= end + 240 && first <= end + 360);
    bound("then in the back phase at 450", hstcall_state(&HC) == HC_BACK && HC.back_bps == 450);

    hstcall_init(&HC, &c);
    first = -1; t = 0;
    t = tone_feed(t, FS / 2, 0, &first);
    t = tone_feed(t, FS * 3 / 10, 1, &first);
    t = tone_feed(t, FS, 0, &first);
    printf("tone: a 300 ms burst, then silence\n");
    bound("starts nothing", first < 0 && hstcall_state(&HC) == HC_TONE);
    t = tone_feed(t, FS * 6 / 10, 1, &first);
    end = t;
    t = tone_feed(t, FS / 2, 0, &first);
    printf("tone: then 600 ms, then silence\n");
    bound("starts 30 to 45 ms after it", first >= end + 240 && first <= end + 360);
}

/* ====================== the back channel ====================== */
typedef struct { hc_btx t; src_t s; } hcb_peer_t;
static double hcb_tx(void *u, double rx) { (void)rx; return hc_btx_tick(&((hcb_peer_t *)u)->t); }

static void back_to_ours_one(int bps, double hz, double ppm, long nb_want, uint32_t seed)
{
    static hcb_peer_t P;
    static hst_brx_t R;
    double lvl = v32line_rms(-16), arrive = lvl * pow(10, -6 / 20.0) * band_gain(bps == 450 ? 274 : 283, bps == 450 ? 476 : 417);
    long n, lock_at = -1, nmax, lost = 0;
    cmp_t o;
    memset(&P, 0, sizeof P);
    P.s.r = seed;
    hc_btx_init(&P.t, bps, lvl, src_bit, &P.s);
    hst_brx_init(&R, bps);
    line(6, 300, 300, dbm0_of(arrive) - 20, -300, hz, ppm, seed);
    nrx = 0;
    nmax = (long)((nb_want + 3000) / bps + 3) * FS;
    for (n = 0; n < nmax && nrx < nb_want + 600; n++) {
        int16_t x;
        int k, nb;
        if (n == FS / 5) hc_btx_start(&P.t);
        x = v32line_tick(&L, 0, hcb_tx, &P);
        nb = hst_brx_tick(&R, x);
        if (hst_brx_locked(&R) && lock_at < 0) lock_at = n;
        if (lock_at >= 0 && !hst_brx_locked(&R)) lost++;
        for (k = 0; k < nb; k++) rx_put(R.bits[k]);
    }
    o = compare(P.s.n, 300);
    printf("back_to_ours %d, %+d Hz %+d ppm: %ld bits wanted, %ld errors\n", bps, (int)hz, (int)ppm, nb_want, o.errs);
    if (verbose) {
        int64_t num = 0, den = 0;
        int ok = hst_brx_snr(&R, &num, &den);
        printf("  (%ld compared; locked %.1f ms after the preamble began; SNR %.1f dB)\n", o.cmp, (lock_at - FS / 5) / 8.0,
               ok && den > 0 ? 10 * log10((double)num / den) : 0.0);
    }
    bound("our hst_brx locks", lock_at >= 0);
    bound("no bit in error", o.found && o.cmp >= nb_want && o.errs == 0);
    bound("no loss of lock", lost == 0);
}

static void back_to_ours(int bps)
{
    if (bps == 450) { back_to_ours_one(450, 0, 0, NB_BACK, 11u); back_to_ours_one(450, 7, 100, NB_OFF, 13u); }
    else { back_to_ours_one(300, 0, 0, NB_BACK, 12u); back_to_ours_one(300, -7, -100, NB_OFF, 14u); }
}

typedef struct { hst_btx_t t; src_t s; } ob_peer_t;
static hc_brx HB;
static double hb_rx(void *u, double rx)
{
    int k, nb = hc_brx_tick(&HB, rx);
    (void)u;
    for (k = 0; k < nb; k++) rx_put(HB.bits[k]);
    return 0;
}

static void back_from_ours_one(int bps, double hz, double ppm, long nb_want, uint32_t seed)
{
    static ob_peer_t P;
    double lvl = v32line_rms(-16), arrive = lvl * pow(10, -6 / 20.0) * band_gain(bps == 450 ? 274 : 283, bps == 450 ? 476 : 417);
    long n, nmax;
    cmp_t o;
    memset(&P, 0, sizeof P);
    P.s.r = seed;
    hst_btx_init(&P.t, bps, (int)lvl, src_bit, &P.s);
    hc_brx_init(&HB, bps);
    line(6, 300, 300, -300, dbm0_of(arrive) - 24, hz, ppm, seed);
    nrx = 0;
    nmax = (long)((nb_want + 3000) / bps + 3) * FS;
    for (n = 0; n < nmax && nrx < nb_want + 600; n++) {
        if (n == FS / 5) hst_btx_preamble(&P.t);
        (void)v32line_tick(&L, hst_btx_tick(&P.t), hb_rx, NULL);
    }
    o = compare(P.s.n, 300);
    printf("back_from_ours %d, %+d Hz %+d ppm: %ld bits wanted, %ld errors\n", bps, (int)hz, (int)ppm, nb_want, o.errs);
    if (verbose) printf("  (%ld compared; SNR %.1f dB)\n", o.cmp, hc_brx_snr(&HB));
    bound("no bit in error", o.found && o.cmp >= nb_want && o.errs == 0);
}

static void back_from_ours(int bps)
{
    if (bps == 450) { back_from_ours_one(450, 0, 0, NB_BACK, 21u); back_from_ours_one(450, -7, 100, NB_OFF, 23u); }
    else { back_from_ours_one(300, 0, 0, NB_BACK, 22u); back_from_ours_one(300, 7, -100, NB_OFF, 24u); }
}

/* ====================== the forward channel ====================== */
#define FLEVEL 3000.0
static hc_frx FR;
static long fdata_at;
static double fr_rx(void *u, double rx)
{
    int k, was = hc_frx_state(&FR), nb = hc_frx_tick(&FR, rx);
    long *n = (long *)u;
    if (was != HC_FRX_DATA && hc_frx_state(&FR) == HC_FRX_DATA && fdata_at < 0) fdata_at = *n;
    for (k = 0; k < nb; k++) rx_put(FR.bits[k]);
    return 0;
}

static void fwd_from_ours_one(int code, double hz, double ppm, uint32_t seed)
{
    static hst_ftx_t T;
    static src_t S;
    double arrive = FLEVEL * pow(10, -6 / 20.0) * band_gain(600, 3000);
    long n, nmax = (NB_FWD + 4000) / (2400 * code) * FS + 4 * FS;
    cmp_t o;
    memset(&S, 0, sizeof S);
    S.r = seed;
    hst_ftx_init(&T, (int)FLEVEL, src_bit, &S);
    hc_frx_init(&FR, code);
    line(6, 300, 300, -300, dbm0_of(arrive) - 30, hz, ppm, seed);
    nrx = 0; fdata_at = -1;
    for (n = 0; n < nmax && S.n < NB_FWD + 2000; n++) {
        if (n == FS / 5) hst_ftx_train(&T, code, 1);
        (void)v32line_tick(&L, hst_ftx_tick(&T), fr_rx, &n);
    }
    o = compare(S.n, 300);
    printf("fwd_from_ours, code %d, %+d Hz %+d ppm: data mode %s; %ld errors\n", code, (int)hz, (int)ppm,
           fdata_at >= 0 ? "reached" : "NOT reached", o.errs);
    if (verbose)
        printf("  (%ld compared; data mode %.1f ms after the training began; SNR %.1f dB)\n", o.cmp, (fdata_at - FS / 5) / 8.0,
               hc_frx_snr(&FR));
    bound("hstcall's receiver reaches data", fdata_at >= 0);
    bound("no bit in error", o.found && o.cmp >= NB_FWD && o.errs == 0);
}

static void fwd_from_ours(void)
{
    int code;
    for (code = 2; code <= 6; code++) fwd_from_ours_one(code, 0, 0, 30u + (uint32_t)code);
    fwd_from_ours_one(6, 7, 100, 37u);
    fwd_from_ours_one(6, -7, -100, 38u);
}

typedef struct { hc_ftx t; src_t s; } hcf_peer_t;
static double hcf_tx(void *u, double rx) { (void)rx; return hc_ftx_tick(&((hcf_peer_t *)u)->t); }

static void fwd_to_ours_one(const char *what, int code, int long_form, int v17, uint32_t seed)
{
    static hcf_peer_t P;
    static hst_frx_t R;
    double arrive = FLEVEL * pow(10, -6 / 20.0) * band_gain(600, 3000);
    long n, nmax = (NB_FWD + 4000) / (2400 * code) * FS + 4 * FS, data_at = -1;
    cmp_t o;
    memset(&P, 0, sizeof P);
    P.s.r = seed;
    hc_ftx_init(&P.t, FLEVEL, src_bit, &P.s);
    hst_frx_init(&R);
    hst_frx_set_code(&R, code);
    line(6, 300, 300, dbm0_of(arrive) - 30, -300, 0, 0, seed);
    nrx = 0;
    for (n = 0; n < nmax && P.s.n < NB_FWD + 2000; n++) {
        int k, nb, was = hst_frx_state(&R);
        int16_t x;
        if (n == FS / 5) hc_ftx_train(&P.t, code, long_form, v17);
        x = v32line_tick(&L, 0, hcf_tx, &P);
        nb = hst_frx_tick(&R, x);
        if (was != HST_FRX_DATA && hst_frx_state(&R) == HST_FRX_DATA && data_at < 0) data_at = n;
        for (k = 0; k < nb; k++) rx_put(R.bits[k]);
    }
    o = compare(P.s.n, 300);
    printf("fwd_to_ours, %s, code %d: data mode %s; %ld errors\n", what, code, data_at >= 0 ? "reached" : "NOT reached", o.errs);
    if (verbose) {
        int64_t num = 0, den = 0;
        int ok = hst_frx_snr(&R, &num, &den);
        printf("  (%ld compared; data mode %.1f ms after the training began; SNR %.1f dB)\n", o.cmp, (data_at - FS / 5) / 8.0,
               ok && den > 0 ? 10 * log10((double)num / den) : 0.0);
    }
    bound("our hst_frx reaches data", data_at >= 0);
    bound("no bit in error", o.found && o.cmp >= NB_FWD && o.errs == 0);
}

static void fwd_to_ours(void)
{
    int code;
    for (code = 2; code <= 6; code++) fwd_to_ours_one("long", code, 1, 0, 40u + (uint32_t)code);
    fwd_to_ours_one("short", 6, 0, 0, 47u);
    fwd_to_ours_one("V.17", 3, 1, 1, 48u);
    fwd_to_ours_one("V.17", 6, 1, 1, 49u);
}

/* 0x3R by marks, hstcall's transmitter to its own receiver: the old
 * rate's data intact to the switch, the new rate's from 2000 bits after
 * it; the received stream longer than the data by the marks' ones, 36
 * symbols at each rate, give or take the new rate's marks read at the old
 * rate before the receiver switches (at most 36 symbols' difference) */
static void marks_one(int from, int to, uint32_t seed)
{
    static hcf_peer_t P;
    double arrive = FLEVEL * pow(10, -6 / 20.0) * band_gain(600, 3000);
    long n, nmax = 12L * FS, sw_rx = -1, sent_at_sw = 0, d, arm_at = -1;
    cmp_t a, b;
    memset(&P, 0, sizeof P);
    P.s.r = seed;
    hc_ftx_init(&P.t, FLEVEL, src_bit, &P.s);
    hc_frx_init(&FR, from);
    line(6, 300, 300, dbm0_of(arrive) - 30, -300, 0, 0, seed);
    nrx = 0;
    for (n = 0; n < nmax; n++) {
        int k, nb;
        int16_t x;
        if (n == FS / 5) hc_ftx_train(&P.t, from, 1, 0);
        if (sw_rx < 0 && P.s.n >= 30000) {
            hc_ftx_marks(&P.t, to);
            sent_at_sw = P.s.n; sw_rx = nrx; arm_at = n + 288;
        }
        /* armed as an LNA just before the marks would arm it: the line's 25
         * ms and some 11 ms of decoding later, inside the old rate's 15 ms */
        if (n == arm_at) hc_frx_arm(&FR, to);
        if (sw_rx >= 0 && P.s.n >= sent_at_sw + 30000) break;
        x = v32line_tick(&L, 0, hcf_tx, &P);
        nb = hc_frx_tick(&FR, x);
        for (k = 0; k < nb; k++) rx_put(FR.bits[k]);
    }
    a = compare_in(txb, P.s.n, rxb, nrx, 300, 0, sw_rx - 200);
    b = compare_in(txb, P.s.n, rxb, nrx, 0, sw_rx + 2000, nrx);
    d = b.off - a.off - 36L * (from + to);
    printf("marks %d -> %d: %ld errors before the switch, %ld after\n", from, to, a.errs, b.errs);
    if (verbose)
        printf("  (%ld and %ld compared; the stream %ld bits over the data and the marks; SNR %.1f dB)\n", a.cmp, b.cmp, d,
               hc_frx_snr(&FR));
    bound("the old rate's data intact", a.found && a.cmp >= 20000 && a.errs == 0);
    bound("the receiver follows: the new rate's data intact", b.found && b.cmp >= 20000 && b.errs == 0);
    bound("no data bit lost: only the marks between", d >= -36L * (from > to ? from - to : to - from) && d <= 36L * (from > to ? from - to : to - from));
    bound("still in data", hc_frx_state(&FR) == HC_FRX_DATA);
}

static void marks_self(void)
{
    marks_one(6, 5, 61u);
    marks_one(4, 6, 62u);
}

/* ====================== coexistence ======================
 * Both channels at once; the back data logged in txb/rxb (hstcall sends,
 * we receive), the forward in ftb/frb (we send, hstcall receives). */
typedef struct { hc_btx b; hc_frx f; src_t s; } co_peer_t;
static co_peer_t CP;
static uint8_t ftb[NLOG], frb[NLOG];
static long nftb, nfrb;
static int fwd_bit(void *u)
{
    uint32_t *r = (uint32_t *)u;
    int b;
    *r = *r * 1664525u + 1013904223u;
    b = (int)(*r >> 27) & 1;
    if (nftb < NLOG) ftb[nftb] = (uint8_t)b;
    nftb++;
    return b;
}
static double co_peer(void *u, double rx)
{
    int k, nb = hc_frx_tick(&CP.f, rx);
    (void)u;
    for (k = 0; k < nb; k++) { if (nfrb < NLOG) frb[nfrb] = CP.f.bits[k]; nfrb++; }
    return hc_btx_tick(&CP.b);
}

typedef struct { double snr_back, snr_fwd; cmp_t back, fwd; } co_res_t;

/* 9 s; the SNRs averaged (in dB) over the last 4 */
static co_res_t coexist_run(int bps, int echo, uint32_t seed)
{
    static hst_ftx_t T;
    static hst_brx_t R;
    static uint32_t fr;
    co_res_t o;
    double ours = v32line_rms(-10), loss = 9;
    double g = band_gain(bps == 450 ? 274 : 283, bps == 450 ? 476 : 417);
    double back = v32line_rms(-33) / (pow(10, -loss / 20.0) * g);
    double sb = 0, sf = 0;
    long n, nmax = 9L * FS, ns = 0;
    int64_t num, den;
    memset(&o, 0, sizeof o);
    memset(&CP, 0, sizeof CP);
    fr = seed; CP.s.r = seed * 7u + 1u;
    hst_ftx_init(&T, (int)ours, fwd_bit, &fr);
    hst_brx_init(&R, bps);
    hc_btx_init(&CP.b, bps, back, src_bit, &CP.s);
    hc_frx_init(&CP.f, 6);
    line(loss, echo ? 6 : 300, echo ? 6 : 300, -54, -54, 0, 0, seed);
    nrx = 0; nftb = nfrb = 0;
    for (n = 0; n < nmax; n++) {
        int k, nb;
        int16_t x;
        if (n == FS / 10) { hc_btx_start(&CP.b); hst_ftx_train(&T, 6, 1); }
        x = v32line_tick(&L, hst_ftx_tick(&T), co_peer, NULL);
        nb = hst_brx_tick(&R, x);
        for (k = 0; k < nb; k++) rx_put(R.bits[k]);
        if (n >= 5L * FS && n % 80 == 0) {
            num = den = 0;
            if (hst_brx_snr(&R, &num, &den) && den > 0) sb += 10 * log10((double)num / den); else sb -= 100;
            sf += hc_frx_snr(&CP.f);
            ns++;
        }
    }
    o.snr_back = sb / ns; o.snr_fwd = sf / ns;
    o.back = compare(CP.s.n, 300);
    o.fwd = compare_in(ftb, nftb, frb, nfrb, 300, 0, nfrb);
    return o;
}

/* each receiver's SNR, its echo-free SNR, and the loss (dB) */
static void coexist_one(int bps, int g711, uint32_t seed)
{
    co_res_t e, f;
    double lb, lf;
    mulaw = g711;
    e = coexist_run(bps, 1, seed);
    f = coexist_run(bps, 0, seed);
    mulaw = 1;
    lb = f.snr_back - e.snr_back; lf = f.snr_fwd - e.snr_fwd;
    printf("coexist 14400 / %d, 6 dB ERL, %s: %ld errors back, %ld forward\n", bps, g711 ? "G.711" : "linear",
           e.back.errs, e.fwd.errs);
    if (verbose)
        printf("  (%ld and %ld compared; back SNR %.2f dB with the echoes, %.2f without: %.2f dB lost; forward %.2f and %.2f: %.2f dB lost)\n",
               e.back.cmp, e.fwd.cmp, e.snr_back, f.snr_back, lb, e.snr_fwd, f.snr_fwd, lf);
    bound("hstcall's forward receiver within 1 dB of its echo-free SNR", lf <= 1 && f.snr_fwd > 25);
    if (g711)
        bound("our back receiver within 2 dB of its echo-free SNR (G.711's noise on the echo)", lb <= 2 && f.snr_back > 20);
    else
        bound("our back receiver within 1 dB of its echo-free SNR", lb <= 1 && f.snr_back > 20);
    bound("no bit in error either way", e.back.found && e.back.cmp > 1000 && e.back.errs == 0
                                        && e.fwd.found && e.fwd.cmp > 100000 && e.fwd.errs == 0);
}

static void coexist(void)
{
    coexist_one(450, 0, 71u);
    coexist_one(300, 0, 72u);
    coexist_one(450, 1, 73u);
    coexist_one(300, 1, 74u);
}

/* ====================== the call: a test answerer of our pieces ======================
 * Holding the forward channel: hst_ftx, both hst_brx (450 preferred while
 * locked), an mnppeer responder with 0xC0. It retrains at the best agreed
 * code on MNP's link-up; carries out 0x1R (its LNA gone + 20 ms: forward
 * off, back on) and 0x2R (its LNA gone + 20 ms: a short training).
 * Holding the back channel: hst_btx and hst_frx (or, with `hcrx`, hstcall's
 * own receiver, which follows marks: our hst_frx learns that in Task 22).
 * It sends the LN `send_code` at `send_at` ms; on its LNA, 0x3R arms the
 * receiver, 0x2R sets its code, 0x1R stops the back transmitter and, 200
 * ms on, trains forward (short). */
typedef struct {
    hst_ftx_t ftx;
    hst_brx_t b450, b300;
    hst_btx_t btx;
    hst_frx_t frx;
    hc_frx    hfrx;
    mnppeer_t m;
    int  hcrx;           /* holding back: hstcall's receiver for ours */
    int  role;           /* 0: we hold forward, 1: back */
    int  started, linked, retrained, best;
    int  ln;             /* 0x1R taken, our LNA to go */
    int  ln2;            /* 0x2R taken, our LNA to go */
    int  lna_going; long lna_done;
    int  send_code; long send_at;   /* our own LN, and when */
    int  my_ln, my_out;
    long turn_at; int turn_code;
    int  lns_taken;
    long ms;
} ans_t;
static ans_t A;
static int best_code(int rates)
{
    int k;
    for (k = 6; k >= 2; k--) if (rates & (1 << k)) return k;
    return HST_OPEN_CODE;
}
/* the answerer's bit source, watching its LNA go */
static int ans_bit(void *u)
{
    ans_t *a = (ans_t *)u;
    int due = a->m.lna_due, b = mnppeer_tx_bit(&a->m);
    if (due && !a->m.lna_due) { a->lna_going = 1; a->lna_done = -1; }
    if (a->lna_going && a->m.line_framing == 0) { a->lna_going = 0; a->lna_done = a->ms; }
    return b;
}

static void ans_every_ms(ans_t *a)
{
    int code;
    a->ms++;
    mnppeer_tick(&a->m, 1);
    if (mnppeer_state(&a->m) == MNPP_CONNECTED && !a->linked) {
        a->linked = 1;
        if (a->m.hst) { a->best = best_code(a->m.hst_rates & 0x7C); hst_ftx_train(&a->ftx, a->best, 0); a->retrained = 1; }
        else a->best = HST_OPEN_CODE;
    }
    while ((code = mnppeer_get_ln(&a->m)) >= 0) {
        a->lns_taken++;
        if (a->role != 0) continue;
        if ((code & 0xF0) == 0x10) a->ln = code;
        else if ((code & 0xF0) == 0x20) a->ln2 = code;
        else continue;
        if (a->m.lna_due || a->lna_going) a->lna_done = -1;
    }
    if (a->ln2 && a->lna_done >= 0 && a->ms >= a->lna_done + 20) {
        hst_ftx_train(&a->ftx, a->ln2 & 0x0F, 0);
        a->ln2 = 0;
    }
    if (a->ln && a->lna_done >= 0 && a->ms >= a->lna_done + 20) {
        /* the holder's switch: forward off, back on, the forward receiver listening */
        hst_ftx_stop(&a->ftx);
        hst_btx_init(&a->btx, a->m.back300 ? 300 : 450, (int)v32line_rms(-16), ans_bit, a);
        hst_btx_preamble(&a->btx);
        hst_frx_init(&a->frx);
        hst_frx_set_code(&a->frx, a->ln & 0x0F);
        hc_frx_init(&a->hfrx, a->ln & 0x0F);
        a->role = 1; a->ln = 0;
    }
    /* our own LN, holding back */
    if (a->send_code && a->ms >= a->send_at && a->role == 1 && a->my_ln < 0) {
        mnppeer_send_ln(&a->m, a->send_code);
        a->my_ln = a->send_code; a->my_out = 0; a->send_code = 0;
    }
    if (a->my_ln >= 0) {
        if (a->m.ln_code == a->my_ln) a->my_out = 1;
        else if (a->my_out && a->m.ln_code < 0) {
            int r = a->my_ln & 0x0F;
            switch (a->my_ln & 0xF0) {
            case 0x30: hc_frx_arm(&a->hfrx, r); hst_frx_set_code(&a->frx, r); break;
            case 0x20: hc_frx_set_code(&a->hfrx, r); hst_frx_set_code(&a->frx, r); break;
            case 0x10: hst_btx_stop(&a->btx); a->turn_at = a->ms + 200; a->turn_code = r; break;
            default: break;
            }
            a->my_ln = -1;
        }
    }
    if (a->turn_at && a->ms >= a->turn_at) {
        a->turn_at = 0;
        a->role = 0;
        hst_brx_init(&a->b450, 450);
        hst_brx_init(&a->b300, 300);
        hst_ftx_train(&a->ftx, a->turn_code, 0);
    }
}

static int16_t ans_tick(ans_t *a, int16_t rx, long n)
{
    int16_t tx = 0;
    int k, nb;
    if (n < FS) tx = sat16(v32line_rms(-12) * sqrt(2.0) * sin(2 * PI * 2100 * n / FS));
    if (a->role == 0) {
        int n4 = hst_brx_tick(&a->b450, rx), n3 = hst_brx_tick(&a->b300, rx);
        if (!a->started && (hst_brx_locked(&a->b450) || hst_brx_locked(&a->b300))) {
            a->started = 1;
            hst_ftx_train(&a->ftx, HST_OPEN_CODE, 1);
        }
        if (a->started) {
            if (hst_brx_locked(&a->b450)) for (k = 0; k < n4; k++) mnppeer_rx_bit(&a->m, a->b450.bits[k]);
            else for (k = 0; k < n3; k++) mnppeer_rx_bit(&a->m, a->b300.bits[k]);
        }
        if (n >= FS) tx = hst_ftx_tick(&a->ftx);
    } else {
        if (a->hcrx) {
            nb = hc_frx_tick(&a->hfrx, rx);
            for (k = 0; k < nb; k++) mnppeer_rx_bit(&a->m, a->hfrx.bits[k]);
            (void)hst_frx_tick(&a->frx, rx);
        } else {
            nb = hst_frx_tick(&a->frx, rx);
            for (k = 0; k < nb; k++) mnppeer_rx_bit(&a->m, a->frx.bits[k]);
        }
        tx = (int16_t)(hst_btx_tick(&a->btx) + hst_ftx_tick(&a->ftx));
    }
    if (n % 8 == 0 && a->started) ans_every_ms(a);
    return tx;
}

static void ans_init(ans_t *a)
{
    mnp_peer_cfg m;
    memset(a, 0, sizeof *a);
    hst_ftx_init(&a->ftx, (int)v32line_rms(-12), ans_bit, a);
    hst_brx_init(&a->b450, 450);
    hst_brx_init(&a->b300, 300);
    memset(&m, 0, sizeof m);
    m.originator = 0; m.framing = 3; m.c0 = 1; m.c0_flags = 1; m.c0_rates = 0x7C;
    m.layout_rx_any = 1; m.t401_ms = 3000; m.n400 = 15; m.seed = 99u;
    mnppeer_init(&a->m, &m);
    a->lna_done = -1; a->my_ln = -1;
}

static uint8_t pat(uint32_t seed, long i) { return (uint8_t)((seed * 2654435761u + (uint32_t)i * 40503u) >> 13); }

/* a call in steps: data written at both ends once both are linked, up to
 * the totals asked so far; run_to runs until `upto` samples, or until all
 * of it has arrived (with `done`) */
typedef struct {
    long n, w_hc, w_ans;           /* samples; octets written */
    long to_hc, to_ans;            /* octets to write */
    long got_hc, got_ans; int bad; /* octets read, wrong */
    int16_t ax;
    long hc_first_tx, hc_last_tx;  /* hstcall's first and last sample not zero */
} xfer_t;
static xfer_t X;

static void call_start(hstcall_t *c, const hstcall_cfg *cfg, uint32_t seed)
{
    hstcall_init(c, cfg);
    ans_init(&A);
    line(6, 300, 300, -60, -60, 0, 0, seed);
    memset(&X, 0, sizeof X);
    X.hc_first_tx = X.hc_last_tx = -1;
}
static double hc_peer_watch(void *u, double rx)
{
    int16_t y = hstcall_tick((hstcall_t *)u, sat16(rx));
    if (y != 0) { if (X.hc_first_tx < 0) X.hc_first_tx = X.n; X.hc_last_tx = X.n; }
    return y;
}
static void run_to(hstcall_t *c, long upto, int done)
{
    static uint8_t buf[256];
    for (; X.n < upto; X.n++) {
        int16_t rx = v32line_tick(&L, X.ax, hc_peer_watch, c);
        size_t k, got;
        X.ax = ans_tick(&A, rx, X.n);
        if (X.n % 80) continue;
        if (A.linked && c->linked) {
            while (X.w_ans < X.to_hc) {
                uint8_t b = pat(1u, X.w_ans);
                if (mnppeer_write(&A.m, &b, 1) != 1) break;
                X.w_ans++;
            }
            while (X.w_hc < X.to_ans) {
                uint8_t b = pat(2u, X.w_hc);
                if (hstcall_write(c, &b, 1) != 1) break;
                X.w_hc++;
            }
        }
        got = hstcall_read(c, buf, sizeof buf);
        for (k = 0; k < got; k++, X.got_hc++) if (buf[k] != pat(1u, X.got_hc)) X.bad++;
        got = mnppeer_read(&A.m, buf, sizeof buf);
        for (k = 0; k < got; k++, X.got_ans++) if (buf[k] != pat(2u, X.got_ans)) X.bad++;
        if (done && X.got_hc >= X.to_hc && X.got_ans >= X.to_ans) { X.n++; break; }
    }
}
static int intact(void) { return X.got_hc == X.to_hc && X.got_ans == X.to_ans && X.bad == 0; }
static void data_line(const char *what)
{
    printf("%s: %ld of %ld octets to hstcall, %ld of %ld to us, %d wrong\n", what, X.got_hc, X.to_hc, X.got_ans, X.to_ans, X.bad);
}

static void link_one(int back, uint32_t seed)
{
    hstcall_cfg cfg;
    char what[64];
    hstcall_defaults(&cfg);
    cfg.back_bps = back;
    call_start(&HC, &cfg, seed);
    X.to_hc = 4096; X.to_ans = 300;
    run_to(&HC, 40L * FS, 1);
    snprintf(what, sizeof what, "link, back %d: hstcall code %d back %d", back, HC.fwd_code, HC.back_bps);
    data_line(what);
    if (verbose)
        printf("  (hstcall: receiver state %d code %d, frames %d in, %d bad, %d LT sent, %d again; us: frames %d in, %d bad, %d LT sent, %d again)\n",
               hc_frx_state(&HC.frx), HC.frx.d.code, HC.mnp.rx_frames, HC.mnp.rx_bad, HC.mnp.tx_lt, HC.mnp.tx_retx,
               A.m.rx_frames, A.m.rx_bad, A.m.tx_lt, A.m.tx_retx);
    bound("MNP up at both ends", HC.linked && A.linked);
    bound("HST agreed, our retrain at the best code", A.retrained && A.best == 6);
    bound("hstcall at code 6 and the back speed it asked for", HC.fwd_code == 6 && HC.back_bps == back);
    bound("the data both ways, intact", intact());
}

static void link_case(void)
{
    link_one(450, 81u);
    link_one(300, 82u);
}

/* a call reversed by hstcall's backlog: hstcall then holds the forward
 * channel (code 6), we the back */
static int reversed_call(hstcall_t *c, hstcall_cfg *cfg, int hcrx, uint32_t seed)
{
    cfg->rev_backlog = 64;
    call_start(c, cfg, seed);
    A.hcrx = hcrx;
    X.to_hc = 1024; X.to_ans = 3000;
    run_to(c, 40L * FS, 1);
    return c->role == HC_ROLE_FWD && c->reversals == 1 && A.role == 1;
}

static void reversal(void)
{
    hstcall_cfg cfg;
    int ok;
    hstcall_defaults(&cfg);
    ok = reversed_call(&HC, &cfg, 0, 91u);
    printf("reversal: hstcall role %d code %d, reversals %d\n", HC.role, HC.fwd_code, HC.reversals);
    data_line("reversal");
    bound("hstcall holds the forward channel, once reversed", ok);
    bound("our forward receiver in data", hst_frx_state(&A.frx) == HST_FRX_DATA);
    bound("the data both ways, intact", intact());
}

/* hstcall holding the forward channel takes our 0x35 (marks) and then our
 * 0x24 (a short training), each from the frame boundary after its LNA: our
 * side arms (sets its code) on that LNA, and the data goes on intact */
static void holder_rates(void)
{
    hstcall_cfg cfg;
    int ok;
    hstcall_defaults(&cfg);
    ok = reversed_call(&HC, &cfg, 1, 92u);
    A.send_code = 0x35; A.send_at = A.ms + 500;
    X.to_hc += 1000; X.to_ans += 6000;
    run_to(&HC, X.n + 8L * FS, 0);
    printf("holder_rates: after 0x35 hstcall code %d, ours %d\n", HC.fwd_code, A.hfrx.d.code);
    bound("reversed first", ok);
    bound("0x35: hstcall at code 5 by marks, our receiver followed, in data",
          HC.fwd_code == 5 && A.hfrx.d.code == 5 && hc_frx_state(&A.hfrx) == HC_FRX_DATA && !A.hfrx.armed);
    run_to(&HC, X.n + 30L * FS, 1);
    bound("the data both ways, intact", intact());
    A.send_code = 0x24; A.send_at = A.ms + 500;
    X.to_hc += 1000; X.to_ans += 6000;
    run_to(&HC, X.n + 8L * FS, 0);
    printf("holder_rates: after 0x24 hstcall code %d, ours %d\n", HC.fwd_code, A.hfrx.d.code);
    bound("0x24: hstcall trained at code 4, our receiver in data at 4",
          HC.fwd_code == 4 && A.hfrx.d.code == 4 && hc_frx_state(&A.hfrx) == HC_FRX_DATA);
    run_to(&HC, X.n + 30L * FS, 1);
    data_line("holder_rates");
    bound("the data both ways, intact", intact());
}

/* hstcall holding the forward channel takes our 0x16: its LNA, 20 ms, its
 * back channel; our short training at 6 meets the equaliser it kept when it
 * left the back role */
static void turn_back(void)
{
    hstcall_cfg cfg;
    int ok;
    hstcall_defaults(&cfg);
    ok = reversed_call(&HC, &cfg, 0, 93u);
    A.send_code = 0x16; A.send_at = A.ms + 500;
    X.to_hc += 6000; X.to_ans += 300;
    run_to(&HC, X.n + 30L * FS, 1);
    printf("turn_back: hstcall role %d code %d, reversals %d, receiver code %d\n", HC.role, HC.fwd_code, HC.reversals,
           HC.frx.d.code);
    data_line("turn_back");
    bound("reversed first", ok);
    bound("hstcall back on the back channel, reversals 2, its receiver in data at 6",
          HC.role == HC_ROLE_BACK && HC.reversals == 2 && hc_frx_state(&HC.frx) == HC_FRX_DATA && HC.frx.d.code == 6);
    bound("the data both ways, intact", intact());
}

/* hstcall's forced requests in one call: request 0 (0x24), force_more[1]
 * alone (0x25; [0] unset) and force_more[2] (0x26); we hold the forward
 * channel and retrain at each */
static void forced_twice(void)
{
    hstcall_cfg cfg;
    hstcall_defaults(&cfg);
    cfg.force_rate_code = 4; cfg.force_kind = 0x20; cfg.force_rate_at_ms = 6000;
    cfg.force_more[1].code = 5; cfg.force_more[1].kind = 0x20; cfg.force_more[1].at_ms = 9000;
    cfg.force_more[2].code = 6; cfg.force_more[2].kind = 0x20; cfg.force_more[2].at_ms = 12000;
    call_start(&HC, &cfg, 94u);
    X.to_hc = 8000; X.to_ans = 300;
    run_to(&HC, 10L * FS, 0);
    printf("forced: by 10 s %d LNs taken by us, hstcall code %d\n", A.lns_taken, HC.fwd_code);
    bound("request 0 and force_more[1] taken, hstcall at code 5", A.lns_taken == 2 && HC.fwd_code == 5);
    run_to(&HC, 16L * FS, 0);
    run_to(&HC, 40L * FS, 1);
    printf("forced: %d LNs taken by us, hstcall code %d, its receiver at %d\n", A.lns_taken, HC.fwd_code, HC.frx.d.code);
    data_line("forced");
    bound("force_more[2] taken too, hstcall and its receiver at code 6",
          A.lns_taken == 3 && HC.fwd_code == 6 && HC.frx.d.code == 6 && hc_frx_state(&HC.frx) == HC_FRX_DATA);
    bound("the data both ways, intact", intact());
}

/* ignore_ln: hstcall holding the forward channel acknowledges our 0x35 but
 * does nothing: no marks, it stays at 6; our receiver, armed on the LNA,
 * gives the switch up and goes on at 6; data intact */
static void ignore_ln(void)
{
    hstcall_cfg cfg;
    int ok;
    hstcall_defaults(&cfg);
    cfg.ignore_ln = 1;
    ok = reversed_call(&HC, &cfg, 1, 97u);
    A.send_code = 0x35; A.send_at = A.ms + 500;
    X.to_hc += 1000; X.to_ans += 6000;
    run_to(&HC, X.n + 5L * FS, 0);
    run_to(&HC, X.n + 30L * FS, 1);
    printf("ignore_ln: hstcall code %d, ours %d, our LN acknowledged %d\n", HC.fwd_code, A.hfrx.d.code, A.my_ln < 0);
    data_line("ignore_ln");
    bound("reversed first", ok);
    bound("our 0x35 acknowledged, ignored: hstcall and our receiver still at 6",
          A.my_ln < 0 && HC.lns_ignored == 1 && HC.fwd_code == 6 && A.hfrx.d.code == 6 && hc_frx_state(&A.hfrx) == HC_FRX_DATA);
    bound("the data both ways, intact", intact());
}

/* ignore_lna: hstcall's 0x1R is acknowledged, our forward stops, and
 * hstcall never trains (Task 21's rev_lost) */
static void rev_lost(void)
{
    hstcall_cfg cfg;
    hstcall_defaults(&cfg);
    cfg.ignore_lna = 1;
    cfg.rev_backlog = 64;
    call_start(&HC, &cfg, 95u);
    X.to_hc = 300; X.to_ans = 3000;
    run_to(&HC, 20L * FS, 0);
    printf("rev_lost: hstcall role %d phase %d reversals %d; us role %d, our receiver state %d\n", HC.role, HC.phase,
           HC.reversals, A.role, hst_frx_state(&A.frx));
    bound("we turned on the LN; hstcall stayed on the back channel and never trained",
          A.role == 1 && HC.role == HC_ROLE_BACK && HC.reversals == 0 && HC.lnas_ignored == 1 && !HC.trained
          && !HC.ftx.on && hst_frx_state(&A.frx) != HST_FRX_DATA && hst_frx_state(&A.frx) != HST_FRX_TRAIN);
}

/* lr_delay_ms: no frame before it; ld_hold_ms: the carrier held that long
 * after the LD (1000 by default) */
static void knobs(void)
{
    hstcall_cfg cfg;
    long first = -1, t = 0, end, n;
    int i;
    hstcall_defaults(&cfg);
    cfg.lr_delay_ms = 2000;
    hstcall_init(&HC, &cfg);
    t = tone_feed(t, FS, 1, &first);
    end = t;
    for (n = 0; n < 3L * FS; n++, t++) {
        (void)hstcall_tick(&HC, 0);
        if (n == 2L * FS - 200) bound("lr_delay_ms 2000: no frame 1.95 s after the back channel's start", HC.mnp.tx_frames == 0);
    }
    printf("knobs: lr_delay_ms 2000, %d frames by 3 s\n", HC.mnp.tx_frames > 0 ? 1 : 0);
    bound("lr_delay_ms 2000: the LR by 3 s", HC.mnp.tx_frames > 0);
    (void)end;
    for (i = 0; i < 2; i++) {
        long ld_gone = -1, hold = i ? 3000 : 1000;
        hstcall_defaults(&cfg);
        cfg.ld_at_ms = 9000;
        if (i) cfg.ld_hold_ms = 3000;
        call_start(&HC, &cfg, 96u + (uint32_t)i);
        while (X.n < 16L * FS) {
            run_to(&HC, X.n + 8, 0);
            if (ld_gone < 0 && HC.ld_sent && HC.ld_off >= 0) ld_gone = X.n;
        }
        printf("knobs: ld_hold_ms %ld, the LD out, then the carrier\n", hold);
        if (verbose) printf("  (the carrier stopped %.1f ms after the LD had gone)\n", (X.hc_last_tx - ld_gone) / 8.0);
        bound("the LD received", A.m.rx_ld_reason == 255);
        /* the last pulses ring out 4 symbols (27 ms at 450) after the stop */
        bound("the carrier held for ld_hold_ms after the LD, then off within 60 ms",
              ld_gone > 0 && X.hc_last_tx >= ld_gone + hold * 8 && X.hc_last_tx <= ld_gone + hold * 8 + 480
              && hstcall_state(&HC) == HC_DROPPED);
    }
}

/* our short training into hstcall's receiver with its equaliser kept from
 * a long one (kept as the carrier fades, 1 s of silence): code 6, 24 dB,
 * and at -7 Hz -100 ppm, no error in 2 x 10^4. 24 dB: 14400's 1e-5 point
 * (hst-facts `Rate changes`, BinModem's working 27 dB less 3); at 20 dB
 * even the long training gives some 3e-3 */
static void stored_one(double hz, double ppm, uint32_t seed)
{
    static hst_ftx_t T;
    static src_t S;
    double arrive = FLEVEL * pow(10, -6 / 20.0) * band_gain(600, 3000);
    long n, nmax;
    int was_data;
    cmp_t o;
    memset(&S, 0, sizeof S);
    S.r = seed;
    hst_ftx_init(&T, (int)FLEVEL, src_bit, &S);
    hc_frx_init(&FR, 6);
    line(6, 300, 300, -300, dbm0_of(arrive) - 24, hz, ppm, seed);
    nrx = 0; fdata_at = -1;
    n = 0;
    for (; n < 6L * FS; n++) {
        if (n == FS / 5) hst_ftx_train(&T, 6, 1);
        (void)v32line_tick(&L, hst_ftx_tick(&T), fr_rx, &n);
    }
    was_data = hc_frx_state(&FR) == HC_FRX_DATA;
    hc_frx_keep(&FR);
    hst_ftx_stop(&T);
    for (nmax = n + FS; n < nmax; n++) (void)v32line_tick(&L, hst_ftx_tick(&T), fr_rx, &n);
    nrx = 0; S.n = 0; fdata_at = -1;
    hst_ftx_train(&T, 6, 0);
    for (nmax = n + (NB_OFF + 4000) / 14400 * FS + 2 * FS; n < nmax && S.n < NB_OFF + 2000; n++)
        (void)v32line_tick(&L, hst_ftx_tick(&T), fr_rx, &n);
    o = compare(S.n, 300);
    printf("stored_eq, code 6, 24 dB, %+d Hz %+d ppm: short training data mode %s; %ld errors\n", (int)hz, (int)ppm,
           fdata_at >= 0 ? "reached" : "NOT reached", o.errs);
    if (verbose) printf("  (%ld compared; SNR %.1f dB)\n", o.cmp, hc_frx_snr(&FR));
    bound("in data after the long training, the equaliser kept", was_data && FR.stored);
    bound("the short training reaches data, no bit in error", fdata_at >= 0 && o.found && o.cmp >= NB_OFF && o.errs == 0);
}

static void stored_eq(void)
{
    stored_one(0, 0, 51u);
    stored_one(-7, -100, 52u);
}

int main(int argc, char **argv)
{
#ifdef HST_PROFILE_BM
    int only = t_case_arg(argc, argv, 1);
    verbose = getenv("HST_VERBOSE") != NULL;
    if (T_CASE(only, 1)) fwd_from_ours();
    /* the rest do not depend on the profile: run in the default build only */
    (void)tone; (void)back_to_ours; (void)back_from_ours; (void)fwd_to_ours;
    (void)marks_self; (void)coexist; (void)link_case; (void)reversal; (void)stored_eq;
    (void)holder_rates; (void)turn_back; (void)forced_twice; (void)rev_lost; (void)knobs; (void)ignore_ln;
#else
    int only = t_case_arg(argc, argv, 18);
    verbose = getenv("HST_VERBOSE") != NULL;
    if (T_CASE(only, 1)) tone();
    if (T_CASE(only, 2)) back_to_ours(450);
    if (T_CASE(only, 3)) back_to_ours(300);
    if (T_CASE(only, 4)) back_from_ours(450);
    if (T_CASE(only, 5)) back_from_ours(300);
    if (T_CASE(only, 6)) fwd_from_ours();
    if (T_CASE(only, 7)) fwd_to_ours();
    if (T_CASE(only, 8)) marks_self();
    if (T_CASE(only, 9)) coexist();
    if (T_CASE(only, 10)) link_case();
    if (T_CASE(only, 11)) reversal();
    if (T_CASE(only, 12)) stored_eq();
    if (T_CASE(only, 13)) holder_rates();
    if (T_CASE(only, 14)) turn_back();
    if (T_CASE(only, 15)) forced_twice();
    if (T_CASE(only, 16)) rev_lost();
    if (T_CASE(only, 17)) knobs();
    if (T_CASE(only, 18)) ignore_ln();
#endif
    T_REPORT("test_hstcall");
}
