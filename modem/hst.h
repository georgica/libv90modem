/* USR HST, the answering side's pump (docs/history/hst-facts.md; the spec's
 * "HST pump"). Fixed-point, sample-driven (8000 Hz, 16-bit linear), no
 * allocation: all state lives in the caller's structures.
 *
 * The back channel: differential 8-PSK, 3 bits a symbol, at 375 Hz and 150
 * baud (450 bit/s) or 350 Hz and 100 baud (300 bit/s) [FW]. Each tribit,
 * its first bit the least significant, steps the phase by hst_bstep[t]
 * eighths of a turn; the points sit at 22.5 + 45 k degrees. Bits are
 * scrambled by 1 + x^-14 + x^-17 (scr17_scramble). */
#ifndef V90D_MODEM_HST_H
#define V90D_MODEM_HST_H
#include <stdint.h>
#include "scramble.h"
#include "hst_tabs.h"
#include "../v32/v32.h"

/* v32tx's history is as long as the forward pulse, and no longer */
typedef char hst_fl_is_v32_txl_max[(HST_FL == V32_TXL_MAX) ? 1 : -1];

/* ---------------- the back channel's receiver ----------------
 * One receiver a speed (both run while we hold the forward channel):
 *
 *   8000 Hz -> hst_bbpf at every 5th sample only -> 1600 Hz -> AGC ->
 *   32-bit NCO to baseband -> matched filter (the transmit pulse of
 *   hst_tabs, +-4 symbols, evaluated only at the strobes, at the strobe's
 *   fractional time: one row of hst_mf450 or hst_mf300 a strobe phase, no
 *   interpolator) -> Gardner timing at 2 strobes a symbol -> differential
 *   decisions -> scr17 descrambler.
 *
 * hst_bbpf is a decimation filter (Task 13; 39 taps, folded: 20
 * multiplies, at 1600 Hz only, once for both receivers through
 * hst_brx_band): it rejects, at least 60 dB, only the bands that fold into
 * the back channel's 274-476 Hz at 1600 Hz (1124-1326, 1874-2076,
 * 2724-2926, 3474-3676 Hz), and is flat over 250-500 Hz. The rest of the
 * selectivity is the matched filter's: the forward channel's lower edge
 * and its echo at 476-1124 Hz (hst_bbpf is -1.7 dB at 600 Hz, -29 at 1000)
 * and the hum below the band. Measured (test_hst_back `echo`): under our
 * own forward signal's echo at -16 dBm0 in HST's band, a back channel at
 * -33 dBm0 locks and runs error-free; V.32's 25 % pulse, which reaches
 * into the back band, is the limit at an echo 3 dB over the back channel.
 * The carrier detect and the AGC work on hst_bbpf's output, so an echo or
 * hum strong enough holds the carrier on with no back channel; `locked`
 * then ends through the fade rule (the matched filter's symbols fall 9 dB),
 * and the AGC's level includes them (the back channel is then smaller in
 * the baseband, which the decisions, being angles, do not mind).
 *
 * The 243-tap band-pass of Task 12 (50 dB down at 600 Hz and above) cost
 * 1.36 M ARMv5 instructions a second; the two receivers on one hst_bbpf now
 * cost about 1.9 M together (make cost-hst-back). hst_dbpf and hst_hbpf are
 * the detector's, not the receivers'.
 *
 * Timing, acquired in one step: four strobes a symbol over HST_BRX_OM
 * symbols, the phase of the symbol-rate line in their power (Oerder and
 * Meyr); then Gardner at two strobes a symbol.
 *
 * The carrier, acquired in one step: over HST_BRX_EST symbols, the eighth
 * power of the differential strobes gives the turn a symbol modulo 45
 * degrees, data removed. 45 degrees a symbol is baud/8 Hz, 12.5 Hz at
 * 300, where a 7 Hz offset turns a symbol 25 degrees; the midpoint strobes
 * settle which of r, r + 45 and r - 45 it is. A midpoint is the sum of the
 * symbols around it weighted by the pulse's autocorrelation, each turned
 * against it by the carrier; the right turn predicts it, one 45 degrees
 * off does not. The turn is taken off each differential angle (wturn), not
 * stepped onto the NCO, which reaches the strobes only through the matched
 * filter's 8 symbols. Then a frequency loop on the differential decisions'
 * error (no phase loop: the decisions are differential); once locked,
 * wturn drains slowly into the NCO.
 *
 * The whole acquisition again if no lock follows, or when lock is lost.
 * The estimate is right over about +-12 Hz at 300 and about +-28 Hz at
 * 450, but at 300 and +-12 Hz the lock is slow: about half the clean starts
 * give their first bit more than 450 ms after the lock it comes from would
 * have been due (the carrier turns 43 degrees a symbol inside the matched
 * filter until it drains into the NCO, and the alias checks see less); at
 * +-15 to +-17 Hz lock takes from 0.4 s to several seconds and comes and
 * goes at first. Telephone lines stay within +-7 Hz, where at 20 dB and
 * more the lock that gives the first bit comes within 450 ms.
 *
 * Lock needs, over the last HST_BRX_WIN decisions: 15 within +-12 degrees
 * of the grid; at least 4 different steps, none in more than 8 (a tone, or
 * dial tone, or hum, steps the same way every symbol: a scrambled signal
 * uses all eight); an RMS error under HST_BRX_RMS degrees (noise that falls
 * on the grid by chance does not); the symbols' power steady from one
 * symbol to the next: the mean squared change at most a quarter of the
 * squared mean power, over the window's symbols less those 9 dB under the
 * mean (a dropout), 12 at least (8-DPSK through the matched filter is of
 * constant modulus; two tones that beat can step the phase in a varied way
 * but their power swings from symbol to symbol, and stayed under 1 grid_ok
 * in 30 over 3978 pairs; a slow fade, 3 dB at 10 Hz, changes the power
 * little from one symbol to the next, and is let through: a spread over
 * the window, Task 13's first rule, was not); and the alias check, on the last
 * HST_BRX_EST strobes, preferring the turn in use to r +- 45 by 2:1. When
 * it prefers r +- 45 instead, the carrier is estimated again from those
 * strobes. A second check on two fresh windows of 10 symbols after the
 * lock, their mismatches summed, confirms the lock when the turn in use
 * predicts better than both r +- 45, or ends it (estimating again when it
 * prefers r +- 45 by 2:1). One fresh window alone is unclear about a right
 * lock now and then (dropping it made about 5 % of the clean starts at 300
 * late); the lock's own window is not pooled, since it passed its check by
 * selection, and an alias by chance. No bit leaves before the second
 * check: the bits are held back while locked, HST_BRX_DLY symbols. So a
 * lock 45 degrees off (an estimate made on noise, before the signal or in
 * a dropout, pulled there by the frequency loop) gives no bits. The check
 * runs again every 32 symbols, and three in a row for the same wrong turn
 * estimate the carrier again.
 *
 * Holding a lock takes the same conditions with an RMS ceiling of
 * HST_BRX_RMS_HOLD degrees (it holds to about 5 dB over 0-4 kHz, where it
 * is won down to about 8 dB). Noise, alone or with a tone, can raise
 * `locked` for up to 0.2 s now and then (6 runs in 1910 minutes of the
 * Task 12 review's set); the second check ends it, no bit given.
 *
 * A symbol 9 dB under the mean leaves the mean of the symbols' power as it
 * was (for 8 symbols), and a 10 ms block 9 dB under leaves the AGC's gain
 * (for 6 blocks), so that the symbols after a dropout are not measured
 * against a level the dropout pulled down. Locked, such a symbol is a
 * fade: about 75 ms of them in a row (11
 * symbols at 450, 8 at 300) lose the lock, unless they are a lasting
 * drop of the level (Task 23, the Task 13 review's N1): the last 8 (450)
 * or 5 (300) of them steady (the strongest at most twice the weakest's
 * power: the matched filter's output of a constant-modulus signal) and
 * on the grid, at most one off it, is a step (noise alone, of exponential
 * power, its decisions on the grid 24 times in 45, passes both 1 time in
 * 10^5 at 450, 1 in 550 at 300; it then holds the lock until the quality
 * rules end it, a few symbols' bits wrong): the mean taken again from
 * them, the lock kept. Measured (test_hst_back `lasting_drop`): 12 to 20
 * dB down for good, the lock held at both speeds. A shorter fade (a lost
 * 20 ms packet or two) keeps the lock, and its symbols' bits go out with
 * the rest, a few of them wrong: the stream has no gap. Faded symbols move
 * neither the loops nor the grid history.
 *
 * Losing a confirmed lock, to a fade, to its quality or to the carrier
 * detect, releases the bits still held back but for the faded symbols':
 * one symbol's bits at each 1600 Hz sample after the loss (so within 14
 * ms), while `locked` is already 0. A carrier stop is so given to its last
 * symbol (test_hst_back `stop_tail`: every bit sent, the last frame's FCS
 * and flag included), where discarding the ring lost the last 10 to 15
 * symbols the far end sent (Task 13's I-B; Task 21). A lock lost to the
 * second alias check, or to three alias checks in a row, may be 45 degrees
 * off: its held bits are dropped.
 *
 * Carrier detect: the band's power in 10 ms blocks (before the AGC), on
 * at -44 dBm0 for 3 blocks, off under -47 dBm0 for 6 blocks (a lost 50 ms
 * packet keeps it) [OURS]. Off comes 80 to 90 ms after a real stop
 * (residual noise under -47 dBm0), against the 100 ms the tests allow: a
 * margin of only 10 ms, so latency added in front of the receiver (a
 * longer band-pass, a jitter buffer in a test's channel) breaks the `cd`
 * test first. */
#define HST_BRX_XL  (HST_BBPF_N + 40)  /* the 8000 Hz input, linear, moved down when full */
#define HST_BRX_ZN  130                /* the matched filter's span at 1600 Hz: 8 symbols at 300, +2 */
#define HST_BRX_ZL  (HST_BRX_ZN + 128) /* the 1600 Hz baseband, linear, moved down when full */
#define HST_BRX_EST 10                 /* symbols for the carrier's estimate */
#define HST_BRX_OM  8                  /* symbols for the timing's */
#define HST_BRX_WIN 16                 /* decisions the lock and grid_ok look over */
#define HST_BRX_RMS 7                  /* degrees: the most RMS error for lock and grid_ok */
#define HST_BRX_RMS_HOLD 11            /* degrees: the most RMS error to hold a lock */
#define HST_BRX_DLY 21                 /* the most symbols the bits are held back while locked */
#define HST_BRX_FN  8                  /* faded symbols kept: a lasting drop's test (below) */

typedef struct {
    int16_t  xh[HST_BRX_XL];          /* the input at 8000 Hz */
    int16_t  zr[HST_BRX_ZL], zi[HST_BRX_ZL];   /* baseband at 1600 Hz */
    int32_t  xn, dec, zn;             /* the next places in xh and zr/zi; samples since the last band-pass */
    int32_t  y;                       /* the last band-passed sample (1600 Hz) */
    int32_t  lane;                    /* 0: 450, 1: 300 */
    /* carrier detect and AGC, on the band's power */
    int64_t  bpw, ps;                 /* this block's sum of squares; the smoothed block power */
    int32_t  nblk, cnt, cd;
    int32_t  nhold, nweak;            /* blocks, symbols 9 dB under the mean in a row (a dropout) */
    int32_t  gain;                    /* Q16 */
    /* NCO */
    uint32_t ph, dph, dph0;
    /* timing */
    int32_t  tacc;                    /* time since the last strobe, pulse phases Q16 */
    int32_t  half;                    /* the last strobe was a midpoint's (1) or a symbol's (0) */
    int32_t  acq;                     /* acquisition: timing, carrier, lock */
    int32_t  sidx, nom;               /* timing: the quarter-symbol strobe, symbols so far */
    int64_t  omr, omi;                /* timing: the symbol-rate line of the strobes' power */
    int32_t  ymr, ymi, ypr, ypi;      /* the last midpoint strobe, the last symbol strobe */
    int64_t  pref;                    /* mean symbol power */
    /* acquisition: the carrier's turn a symbol, from HST_BRX_EST symbol
     * and midpoint strobes */
    int32_t  rho1, rho3;              /* the pulse's autocorrelation at T/2 and 3T/2, Q14 of its peak */
    int32_t  esr[HST_BRX_EST], esi[HST_BRX_EST];   /* symbol strobes */
    int32_t  emr[HST_BRX_EST], emi[HST_BRX_EST];   /* the midpoint strobe before each */
    int32_t  nest, wait;              /* strobes kept; symbols left for a lock before acquiring again */
    int32_t  nchk;                    /* symbols to the next alias check while locked */
    int32_t  nalias;                  /* checks in a row for +45 (> 0) or -45 (< 0) while locked */
    /* the last HST_BRX_WIN decisions: their steps and squared errors */
    uint8_t  wstep[HST_BRX_WIN];
    int32_t  wsq[HST_BRX_WIN];        /* (error in turns Q16)^2 / 16 */
    uint16_t wpw[HST_BRX_WIN];        /* symbol power, Q8 of the mean, 1 .. 4095; 0: left out (9 dB under) */
    int32_t  wd2[HST_BRX_WIN];        /* its squared change from the decision before; -1: none */
    int32_t  wpos, nwin, qbad;        /* ring place; decisions in it; symbols of bad quality in a row */
    int32_t  qacq, qhold;             /* the window's quality to win a lock (and grid_ok), to hold one */
    int32_t  wn[8];                   /* the window's decisions of each step */
    int64_t  wsum, wp1, wds;          /* its sums: squared errors; powers, squared changes */
    int32_t  wpn, wdn;                /* the powers and the changes counted in them */
    int32_t  nfade;                   /* locked symbols 9 dB under the mean in a row */
    int64_t  fpw[HST_BRX_FN];         /* their powers, a ring at (nfade - 1) % HST_BRX_FN ... */
    uint32_t fok;                     /* ... and their decisions on the grid, the newest in bit 0 */
    uint8_t  dly[HST_BRX_DLY];        /* the last decisions' bits while locked, a ring (bit b: bits[b];
                                       * bit 3: the symbol 3 dB under the mean) */
    int32_t  ndly, pdly;              /* bits held back: symbols, the ring's place */
    uint8_t  rel[HST_BRX_DLY];        /* the held bits a lost lock releases, the oldest first */
    int32_t  nrel, prel;              /* symbols of them; the next one given */
    int32_t  conf;                    /* symbols to the second alias check after a lock (0: done) */
    int32_t  nconf;                   /* fresh windows checked after the lock (0 or 1) */
    int64_t  econf[3];                /* the first one's mismatch at r - 45, r, r + 45 */
    /* decisions */
    uint32_t aprev;                   /* the last symbol's angle */
    int32_t  wturn;                   /* the carrier's turn a symbol left after the NCO, 2^32 a turn */
    uint32_t okhist;                  /* each decision within +-12 degrees of the grid, the newest in bit 0 */
    int32_t  nsym, locked, grid_ok;
    uint32_t ndec;                    /* decisions made (on symbols, not the estimate's), modulo 2^32 */
    int64_t  e2;                      /* mean square of the decision error, turns Q16 squared */
    scr_t    scr;
    uint8_t  bits[3];                 /* the last symbol's bits, the first in bits[0] */
} hst_brx_t;

/* bps 450 or 300 */
void hst_brx_init(hst_brx_t *r, int bps);
/* one sample at 8000 Hz; returns the bits given (0 or 3), in r->bits[]:
 * while locked, held back HST_BRX_DLY symbols (see above), so the first
 * come some 140 ms (450) or 210 ms (300) after the lock; and just after a
 * lock is lost, those it releases */
int  hst_brx_tick(hst_brx_t *r, int16_t x);
/* one band-passed sample at 1600 Hz, as hst_brx_tick computes it at every
 * 5th sample (it returns the same). Both receivers run on one band-pass:
 * hst_brx_tick on the first, and whenever its `dec` is back to 0,
 * hst_brx_band on the second with the first's `y` */
int  hst_brx_band(hst_brx_t *r, int32_t y);
/* every bit held back (a confirmed lock's, and those a lost lock has yet
 * to release) into bits[], the oldest first, at most HST_BRX_FLUSH; their
 * number. They are given no more: the receiver is to be left, its far end
 * still sending (the holder's switch of a channel reversal) */
#define HST_BRX_FLUSH (6 * HST_BRX_DLY)
int  hst_brx_flush(hst_brx_t *r, uint8_t *bits);
/* carrier, with hysteresis */
int  hst_brx_cd(const hst_brx_t *r);
/* the differential decisions on the grid: 11 of the last 12, after the
 * acquisition; lost when 8 of the last 16 are off it, or with the carrier */
int  hst_brx_locked(const hst_brx_t *r);
/* the SNR (Es/N0, from the decisions' phase error) as num/den; 0 when not
 * locked */
int  hst_brx_snr(const hst_brx_t *r, int64_t *num, int64_t *den);
/* the last decision within +-12 degrees of the 45-degree grid, with the
 * lock's other conditions on the last HST_BRX_WIN decisions (steps of
 * variety, the RMS error); 0 until the carrier has been estimated, and
 * while acquiring again (the detector's input) */
int  hst_brx_grid_ok(const hst_brx_t *r);

/* ---------------- the back channel's transmitter ---------------- */
#define HST_BTX_PRE_MS 300            /* preamble: scrambled ones [OURS] */

typedef enum { HST_BTX_SILENT, HST_BTX_PREAMBLE, HST_BTX_DATA } hst_btx_mode;

/* One speed's pulse train: symbols in the pulse, newest first, and its
 * clock. Each speed has its own, so that after a change of speed the last
 * symbols of the old one ring out on their own pulse and carrier. */
typedef struct {
    int32_t  si[HST_BL], sq[HST_BL];  /* symbol x gain, line units */
    int32_t  acc;                     /* pulse phase, 0 .. HST_Bxxx_P */
    int32_t  left;                    /* symbols until no sent symbol is left in the pulse */
    uint32_t cph, dph;                /* carrier phase, and its step a sample */
    int32_t  gain;                    /* symbol amplitude for the line level */
} hst_blane_t;

typedef struct {
    hst_blane_t  lane[2];             /* 0: 450 bit/s, 1: 300 bit/s */
    int32_t      bps;                 /* speed of the symbols being sent */
    int32_t      want;                /* speed asked for: from the next symbol */
    int32_t      mode;                /* hst_btx_mode */
    int32_t      phase;               /* differential phase, eighths of a turn */
    int32_t      npre;                /* preamble symbols still to send */
    int32_t      newsym;              /* the sample just produced began a symbol sent at `bps`
                                       * (never while silent) */
    scr_t        scr;
    v32_getbit_fn getbit; void *user;
} hst_btx_t;

/* bps 450 or 300; level = RMS of the line signal (16-bit sample units;
 * a negative level is taken as 0).
 * Silent until hst_btx_preamble. */
void    hst_btx_init(hst_btx_t *t, int bps, int level, v32_getbit_fn getbit, void *user);
/* HST_BTX_PRE_MS of scrambled ones, phase and scrambler from 0, then the
 * bits of getbit; the first symbol comes with the next tick */
void    hst_btx_preamble(hst_btx_t *t);
/* 450 or 300 from the next symbol; the phase and the scrambler carry on */
void    hst_btx_set_speed(hst_btx_t *t, int bps);
int16_t hst_btx_tick(hst_btx_t *t);
/* no more symbols: the pulses in flight ring out, then silence */
void    hst_btx_stop(hst_btx_t *t);

/* ---------------- the detector of ANS_HST_WAIT ----------------
 * The spec's "Detecting the caller's back channel": it fires when both
 * stages hold.
 *
 * 1. Energy: over the last 100 ms (ten 10 ms blocks), the 250-500 Hz band
 *    over HST_DET_ON dBm0 and at least 10 dB above the 600-3400 Hz band.
 *    The band is hst_dbpf at 1600 Hz on the receivers' hst_bbpf output;
 *    the high band is hst_hbpf at 8000 Hz, evaluated at every 4th sample on
 *    average (3 and 5 in turn, so that a tone at a multiple of 1000 Hz is
 *    not sampled at one phase; the power of a sub-sampled output is the
 *    output's power). The spec's -43 dBm0 is the weakest back channel to be
 *    found; the threshold is -45 dBm0 [OURS, ruled in the Task 13 review]
 *    but never binds: the lock stage needs the receivers' carrier detect,
 *    on at -44 dBm0, so the floor is -44, 1 dB under the spec's -43
 *    (measured: -43 fires at 416 / 576 ms at 450 / 300, -44 at 516 and
 *    1166-1326 ms, -44.5 and under never).
 * 2. Lock: on the 450 (or the 300) receiver, both on one band-pass, 24 of
 *    the last 30 decisions grid_ok (hst_brx_grid_ok: on the grid, with the
 *    lock's window conditions, and only after the carrier's estimate). A
 *    window, never one decision: dial tone gives a lone grid_ok now and then.
 *
 * Measured (test_hst_det): it fires within 450 ms of the preamble's start
 * at 450 and 650 ms at 300 (the window alone needs 24 decisions: 160 ms at
 * 450, 240 ms at 300, after the lock's acquisition), never on dial tone,
 * hum, tones, noise, V.21, Bell 103 or V.22bis callers, or our ANSam's echo.
 * The receivers carry on after it fires (rx[], nb[]): bits, lock, carrier. */
#define HST_DET_450 1
#define HST_DET_300 2
#define HST_DET_ON  (-45)             /* dBm0: the band's threshold */
#define HST_DET_XL  (HST_HBPF_N + 40) /* the 8000 Hz input, linear, moved down when full */
#define HST_DET_YL  (HST_DBPF_N + 32) /* the 1600 Hz band-passed input, likewise */
#define HST_DET_BLKS 10               /* 10 ms blocks: 100 ms */

typedef struct {
    hst_brx_t rx[2];                  /* 450, 300; on one band-pass (this struct's) */
    int32_t  nb[2];                   /* the bits each gave at the last tick (0 or 3) */
    int16_t  xh[HST_DET_XL];          /* the input at 8000 Hz */
    int16_t  yh[HST_DET_YL];          /* hst_bbpf's output at 1600 Hz */
    int32_t  xn, yn, dec, hcnt, hstep;
    int32_t  nblk, nh;                /* 1600 Hz samples in the block; high-band samples in it */
    int64_t  pb, ph;                  /* this block's sums of squares: band, high band */
    int64_t  bb[HST_DET_BLKS], bh[HST_DET_BLKS];   /* the last blocks' means: band, high band */
    int32_t  pblk, nfull;             /* the ring's place; blocks in it */
    uint32_t ok[2], ndec[2];          /* each receiver's grid_ok history (newest in bit 0); decisions seen */
    int32_t  fired;                   /* 0, HST_DET_450 or HST_DET_300, once fired */
} hst_det_t;

void hst_det_init(hst_det_t *d);
/* one sample at 8000 Hz: 0, or HST_DET_450 / HST_DET_300 from the sample
 * it fires on (and at every one after) */
int  hst_det_tick(hst_det_t *d, int16_t x);
/* after it has fired (ANS_HST): the two receivers alone, on the one
 * band-pass, carrying on (their bits in nb[]); the energy stage and the
 * lock window are no longer kept */
void hst_det_rx(hst_det_t *d, int16_t x);
/* the energy stage alone, over the last 100 ms (stage 1 above): 1 while it
 * holds. ANS_HST_WAIT listens past HST_W_MS only while it does */
int  hst_det_energy(const hst_det_t *d);

/* ---------------- the forward channel's transmitter ----------------
 * 2400 baud on 1800 Hz, v32tx on hst_fpulse; GPC scrambler; data through
 * v32_encode at rate index code - 2 (code 2..6: 4800..14400). The training
 * is provisional (hst-facts.md "Training", [OURS]): V.32's S, S-bar and TRN
 * generators with the firmware's lengths 180 + 72 + (3096 or 200). */
#define HST_S1_SYMS   180    /* AB (V.32's S); provisional: Task 20 / R1 */
#define HST_S2_SYMS    72    /* CD (V.32's S-bar); provisional: Task 20 / R1 */
#define HST_S3_LONG  3096    /* TRN, the long form; provisional: Task 20 / R1 */
#define HST_S3_SHORT  200    /* TRN, the short form; provisional: Task 20 / R1 */

/* V.17's long train (hst-facts.md "V.17 long train", [PUB]): the BinModem
 * profile's long form */
#define HST_BM_AB      256   /* ABAB */
#define HST_BM_COND   2976   /* equaliser conditioning */
#define HST_BM_BRIDGE   64   /* the bridge, 16 bits 8 times */
#define HST_BM_ONES     48   /* scrambled ones at the rate */
#define HST_BM_SCR0 0x2ECDD5u /* the conditioning's scrambler state */
#define HST_BM_TOFF_ONES 32  /* V.17's turn-off: scrambled ones, then HST_BM_TOFF_QUIET symbols of no energy */
#define HST_BM_TOFF_QUIET 48 /* (hst-facts.md "V.17 long train", [PUB]): the BinModem profile's end of a burst */

#ifdef HST_PROFILE_BM
/* -DHST_PROFILE_BM: a test build for runs against BinModem only (SPEC D16);
 * release builds and golden files never use it */
#define HST_OPEN_CODE   3    /* 7200 (BinModem's); provisional: Task 20 / R1 */
#define HST_LONG_V17    1    /* every training, long or short, is V.17's long train */
#define HST_3R_TRAINS   1    /* a 0x3R rate request is carried out with a training */
#define HST_FMF_BM      1    /* the forward receiver starts from BinModem's pulse's matched
                              * filter (hst_fmf_bm), not hst_fpulse's */
#else
#define HST_OPEN_CODE   4    /* 9600 before MNP agreement; provisional: Task 20 / R1 */
#define HST_LONG_V17    0
#define HST_3R_TRAINS   0
#define HST_FMF_BM      0
#endif

/* samples from the tick that begins a symbol to the centre of its pulse on
 * the line: (V32_TXD + HST_FL / 2) symbols, 70 samples (V32_TX_DELAY, 40,
 * is V.32's pulse's) */
#define HST_FTX_DELAY ((V32_TXD + HST_FL / 2) * 10 / 3)
typedef char hst_ftx_delay_is_70[(HST_FTX_DELAY == 70) ? 1 : -1];

/* the segment of the symbol begun last */
enum {
    HST_SEG_OFF,                      /* silent: before the first training, after hst_ftx_stop */
    HST_SEG_S1, HST_SEG_S2, HST_SEG_S3,
    HST_SEG_S4,                       /* the V.17 long train's scrambled ones (HST_LONG_V17) */
    HST_SEG_DATA
};

typedef struct {
    v32tx_t  tx;                      /* answer = 0: GPC (hst_fwd.c) */
    int8_t   kind[4];                 /* this training's segments S1..S4 (hst_fwd.c) */
    int32_t  len[4];                  /* and their lengths, 0 = none */
    int32_t  seg;                     /* HST_SEG_* of the symbol begun last */
    int32_t  left;                    /* symbols of it still to begin */
    int32_t  code;                    /* rate code of the data, 2..6 */
    int32_t  d0;                      /* V.17: the state of the bridge's first symbol */
    int32_t  marks;                   /* a switch by marks: its symbols still to begin, 0 none */
    int32_t  mcode;                   /* ... and the code it switches to */
    int32_t  quiet;                   /* symbols begun since stopped (HST_SEG_OFF: each silent), to
                                       * HST_FTX_QUIET: from there v32tx's line is all zero and
                                       * hst_ftx_tick keeps its count alone (Task 23c) */
} hst_ftx_t;
#define HST_FTX_QUIET (V32_TXL_MAX + V32_TXD)

/* level = RMS of the line signal (16-bit sample units); getbit gives the
 * data. Silent until hst_ftx_train. */
void    hst_ftx_init(hst_ftx_t *t, int level, v32_getbit_fn getbit, void *user);
/* from the next symbol: the training, long or short (under HST_LONG_V17
 * always V.17's long train), then data at `code` (2..6; outside, the
 * nearest). The scrambler starts from zero; the pulses
 * already sent ring out under it. */
void    hst_ftx_train(hst_ftx_t *t, int code, int long_form);
/* the symbol begun last was data */
int     hst_ftx_in_data(const hst_ftx_t *t);
int16_t hst_ftx_tick(hst_ftx_t *t);
/* no more symbols: the pulses in flight ring out, then silence */
void    hst_ftx_stop(hst_ftx_t *t);
int     hst_ftx_seg(const hst_ftx_t *t);
/* 0x3R carried out as holder (spec "Rate changes", [PUB, US 4,890,316]):
 * from the next symbol HST_MARKS_SYMS / 2 symbols of ones at the code in
 * force, then as many at new_code, then data at new_code; getbit is not
 * called meanwhile (the caller has finished its frame first). The
 * scrambler, the differential encoder and the trellis encoder run on
 * through the change [OURS]; hst_ftx_in_data stays 1 throughout (no
 * retrain). Codes 3..6 both: outside data, or with code 2 on either side
 * (no trellis to carry on), the short training at new_code instead
 * (outside data: the code the data will be sent at, as hst_ftx_train's) */
#define HST_MARKS_SYMS 72
void    hst_ftx_marks_switch(hst_ftx_t *t, int new_code);
/* symbols of a switch by marks still to begin, 0 none */
int     hst_ftx_marking(const hst_ftx_t *t);

/* ---------------- the forward receiver's guard high-pass ----------------
 * The input, delayed, less a low-pass: hst_guard_f on samples
 * HST_GUARD_L apart (each sample; HST_GF_XN taps in its line), then hst_guard_i
 * on its sums (hst_tabs.h; tools/gen_hst_tabs.py, tab_guard). Each is
 * linear phase: pairs of samples the same distance from the centre added
 * first, (HST_GUARD_FN + 1) / 2 + (HST_GUARD_IN + 1) / 2 multiplies a
 * sample (53, against 154 for the single 307-tap filter before it).
 * Integer only: F's sums divided by 4 (truncated; under 2^30, the
 * generator checks, so two add in an int32), I's rounded to nearest (ties
 * up), the difference saturated: the same output on every target. Delay
 * HST_GF_DELAY samples (162, 20 ms). */
typedef char hst_guard_is_odd[(HST_GUARD_FN % 2 == 1 && HST_GUARD_IN % 2 == 1) ? 1 : -1];
#define HST_GF_XN    (HST_GUARD_L * (HST_GUARD_FN - 1) + 1)
#define HST_GF_DELAY (HST_GUARD_L * (HST_GUARD_FN - 1) / 2 + (HST_GUARD_IN - 1) / 2)
typedef struct {
    int16_t x[2 * HST_GF_XN];        /* the input, newest first from x[pos], twice */
    int32_t t[2 * HST_GUARD_IN];     /* F's sums / 4, newest first from t[tpos], twice */
    int     pos, tpos;
} hst_gf_t;
void    hst_gf_init(hst_gf_t *g);
int16_t hst_gf_tick(hst_gf_t *g, int16_t in);

/* ---------------- the forward receiver ----------------
 * The spec's "Forward channel: receiver" [OURS]: hst_gf_tick, then qamrx
 * (2400 baud, carrier 3/4 of it) with an acquisition that needs nothing of
 * the training but that it starts with AB (ours, V.32's S, V.17's segment
 * 1):
 *   IDLE   listening: both band-edge tones (600 and 3000 Hz) of AB, each
 *          at least 1/16 of the power and both 1/4, the power at least
 *          -43 dBm0
 *   AB     ... for HST_FRX_AB_MS; then qamrx_sync_eq with the symbol timing
 *          from the phase between the two tones, and the equaliser and
 *          carrier frequency kept (hst_frx_restore, or a carrier lost) or
 *          else the transmit pulse's matched filter and the tones' own turn
 *          (the BinModem profile, HST_FMF_BM: BinModem's pulse's, its 560
 *          Hz high-pass in it, hst_fmf_bm; from ours the receiver never
 *          trained on BinModem's training, the error after AB some -4 dB)
 *   TRAIN  decision-directed on the 4 points A..D, fast steps (until the
 *          error has stayed under -20 dB for HST_FRX_HOLD symbols, in
 *          training or data), then tracking. From the matched filter (the
 *          default receiver): over the first HST_FRX_MF_SYMS symbols that
 *          are not an alternation, the points of both starts in hst_fmf
 *          (ours, and ours through a 550 Hz high-pass) are measured, as
 *          they were loaded, by how far their power spreads (A..D have one
 *          power: |z|^4 against |z|^2 squared); unless the second's
 *          spread is 1.25 times the first's or more, the training starts
 *          again from it, AB's timing moved by HST_FMF_AB_SHIFT (the second
 *          start's high-pass reads AB early) (the band's lower edge: a
 *          steep high-pass at 550 to 600 Hz on the line left ours some 4 dB
 *          under its error, and it never trained; test_hstcall fwd_edge and
 *          its sweep)
 *   DATA   at code 3..6, once more than 3 of the last 8 points are off the
 *          4 points: the rate's set through v32_vit_t; at code 2, the 4
 *          points, once S and S-bar are over. Either only when the error
 *          (before those 8) was under -14 dB, or the hold was reached. At
 *          code 2, V.32's TRN (Table 4's dibits of scrambled ones, not
 *          differentially encoded) is recognised, and its bits given as
 *          ones: an idle line. In data mode a gain loop holds the points'
 *          power at the set's (a level step of the line)
 * and three states the receiver ends in by itself, each listening for AB
 * again as IDLE does (what to do about them is the scheme's):
 *   NOCARRIER  in training or data, the power low for HST_FRX_LOSS_MS:
 *          under -48 dBm0, or 10 dB under the signal's level (taken at the
 *          start and again where data mode begins, AB being louder than
 *          the data on some far ends; followed only while the receiver is well and within 3
 *          dB of it, and set again by a level step's gain: held, not
 *          decaying, so that line noise left after the carrier is not taken
 *          for it). Low for HST_FRX_CLAMP_MS, or 10 dB under at once: no
 *          bits, nothing adapted but the clock. Once counting, it counts
 *          until the power is back (over -45 dBm0 and within 9 dB of the
 *          level): back before HST_FRX_LOSS_MS, all goes on (a dropout).
 *          The equaliser and frequency are kept for the next training
 *   UNLOCKED   in data mode the error over 256 symbols above the rate's
 *          lock threshold (lock_mse in hst_fwd.c: 1 dB under the noise
 *          floor of its hard decisions; the thresholds are -5.8, -12.8,
 *          -15.8, -19.0, -21.9 dB at codes 2..6) for HST_FRX_LOCK_SYMS
 *          symbols, the gain loop's freezes included
 *   TRAINFAIL  no data mode HST_FRX_TRAIN_SYMS symbols after the start
 *          (S1's rest, S2 and the long S3 are 3220 or so)
 * The data bits come descrambled (GPC), in the order sent (HST sends each
 * octet's LSB first); at code 3..6 V32_VDEPTH symbols late. The power the
 * carrier detect measures is the guard's output (560 Hz up), not 600-3000
 * Hz alone. The point scale: HST_FRX_U per signal space unit. */
#include "qamrx.h"

enum {
    HST_FRX_IDLE, HST_FRX_AB, HST_FRX_TRAIN, HST_FRX_DATA,
    HST_FRX_NOCARRIER, HST_FRX_UNLOCKED, HST_FRX_TRAINFAIL
};

#define HST_FRX_U          1024  /* receiver units per signal space unit: 2 V32_UNIT */
#define HST_FRX_AB_MS        40  /* AB heard this long before the receiver starts */
#define HST_FRX_HOLD        100  /* symbols under -20 dB before tracking */
#define HST_FRX_CLAMP_MS     20  /* no carrier this long: no bits, nothing adapted */
#define HST_FRX_LOSS_MS     200  /* ... this long: NOCARRIER (a 100 ms dropout survives) */
#define HST_FRX_LOCK_SYMS  1200  /* data mode: symbols of poor error before UNLOCKED (0.5 s;
                                  * a 1 dB step at 14400 takes some 1000 to settle) */
#define HST_FRX_TRAIN_SYMS 3600  /* symbols of training at most */
#define HST_FRX_MF_SYMS     128  /* the first training: symbols measured before the start is chosen */
/* a switch by marks (hst_frx_arm_switch; spec "Rate changes", [OURS]) */
#define HST_SW_ONES          24  /* symbols of ones at the code in force (10 ms) ... */
#define HST_SW_WIN           48  /* ... then both codes decoded for at most this many symbols */
#define HST_SW_RUN           24  /* ... and the switch on this many symbols of ones at the new */
#define HST_SW_HOLD           8  /* symbols of ones decoded: nothing adapted from here ... */
#define HST_SW_IDLE          48  /* ... to here, where ones going on are an idle line */
#define HST_SW_SYMS        4800  /* armed this long (2 s) without a switch: acquisition */
#define HST_SW_ALT           16  /* symbols of S or S-bar, armed: a training instead */
#define HST_SW_RING          32  /* points kept, armed: at least V32_VDEPTH */
typedef char hst_sw_ring_fits[(HST_SW_RING >= V32_VDEPTH && (HST_SW_RING & (HST_SW_RING - 1)) == 0) ? 1 : -1];

/* the equaliser kept for the next training of the direction (spec,
 * "stored equaliser") */
typedef struct {
    int32_t cr[QRX_NTAP], ci[QRX_NTAP];
    int32_t freq;                     /* the carrier loop's frequency */
    int32_t valid;
} hst_eqstore_t;

typedef struct {
    hst_gf_t   gf;                    /* the guard high-pass */
    qamrx_t    q;
    v32_vit_t  vit;
    hst_eqstore_t eq;                 /* to start the next training from, when valid */
    int32_t    state;                 /* HST_FRX_* */
    int32_t    listen;                /* the listening state AB falls back to */
    int32_t    code;                  /* rate code in force, 2..6 */
    /* AB: 600 and 3000 Hz, a sample's phase from n40 (both repeat in 40 samples) */
    int32_t    n40;
    uint32_t   ph6;                   /* 600 Hz's phase */
    int32_t    lr, li, ur, ui;        /* the two tones, leaky (x 64) */
    int64_t    pw;                    /* the power, leaky (every state: the carrier detect) */
    int64_t    tr, ti;                /* 600 x conj(3000), summed: the symbol timing */
    int32_t    l0r, l0i, u0r, u0i;    /* the tones 40 samples before */
    int64_t    fr, fi;                /* each tone x conj(itself 40 samples before), summed:
                                       * the carrier's offset */
    int32_t    abrun;                 /* samples AB has been there */
    /* training and data */
    int32_t    off;                   /* samples the carrier has been off */
    int64_t    pref;                  /* the signal's level (see NOCARRIER) */
    int32_t    mlock;                 /* data mode: the error over 256 symbols */
    int32_t    bad;                   /* data symbols it has been poor (lock_mse) */
    int32_t    g;                     /* the data mode's gain on qamrx's input, Q14 */
    int64_t    pz, pz8, pzl;          /* the points' power, over 32, 8 and 128 symbols */
    int32_t    gstep;                 /* it is off (a level step): the decisions wrong,
                                       * nothing else adapted */
    int32_t    gcnt;                  /* symbols it has been so; negative: since the gain
                                       * was set, the equaliser flushing */
    int32_t    ga;                    /* the gain as last set (G_ONE at a training): the
                                       * loop leaks back to it and stays within 1 dB */
    int32_t    nsym;                  /* symbols since qamrx_sync */
    int32_t    held;                  /* symbols the error has been under -20 dB */
    int32_t    fast;                  /* still on the fast steps */
    uint32_t   offw;                  /* off the 4 points: the last 8 symbols, bit 0 the newest */
    int32_t    mse;                   /* |e|^2 / |d|^2 (65536 = 0 dB), smoothed over 16 symbols */
    int32_t    mse0;                  /* mse before the last 8 points, while none is off */
    int32_t    msh[8];                /* mse over the last 8 symbols */
    int32_t    hzr[8], hzi[8];        /* the last 8 points, for the decoder at the switch */
    uint32_t   sh;                    /* the last 8 decisions (A..D), 2 bits each, the newest lowest */
    int32_t    alt;                   /* symbols since the last 8 were an alternation (S, S-bar) */
    /* the first training's start (TRAIN, from hst_fmf) */
    int32_t    mfs;                   /* 1 while both starts are measured, else 0 */
    int32_t    mfn;                   /* ... symbols measured */
    int64_t    mfp[2], mfq[2];        /* ... each start's |z|^2 and |z|^4, summed (scaled) */
    int32_t    mft;                   /* ... n40 as AB's would be now (the symbol timing) */
    /* TRN, Table 4 read directly under each quarter turn (code 2) */
    v32_scr_t  dir[4];
    int32_t    ones[4];               /* ones in a row out of each */
    int32_t    trn;                   /* the quarter turn TRN is being heard under, -1 none */
    /* data */
    v32_scr_t  dscr;
    int32_t    yprev;                 /* the differential decoder's memory */
    int64_t    sig, err;              /* the decided points' power and the error's, leaky */
    int32_t    nbits;
    uint8_t    bits[8];               /* the bits of the last tick, the first sent first */
    /* a switch by marks (hst_frx_arm_switch) */
    int32_t    sw_code;               /* armed: the code switched to; 0 not armed */
    int32_t    sw_tonly;              /* ... for a training alone (hst_frx_arm_train): no marks */
    int32_t    sw_left;               /* symbols left of the 2 s */
    int32_t    sw_ones;               /* the decoder's symbols of ones in a row, at the code in
                                       * force, up to HST_SW_IDLE */
    int32_t    sw_win;                /* the parallel decode: its symbols left, 0 not running */
    int32_t    sw_run;                /* ... the new code's symbols of ones in a row */
    v32_scr_t  sw_dscr;               /* the new code's descrambler ... */
    int32_t    sw_y;                  /* ... and differential decoder's memory */
    int32_t    sw_alt;                /* symbols of S or S-bar (two points a quarter turn apart, in turn) in a row */
    int32_t    sw_hr[2], sw_hi[2];    /* ... the last two points, the newest first */
    int32_t    sw_pos;                /* the ring's next place */
    int32_t    sw_zr[HST_SW_RING], sw_zi[HST_SW_RING];   /* the last points, armed */
    v32_vit_t  vit2;                  /* the new code's decoder */
} hst_frx_t;

void hst_frx_init(hst_frx_t *r);
/* the rate code in force, 2..6 (outside, the nearest). In data mode a new
 * one takes the next symbol, and the decoder starts again: the
 * V32_VDEPTH symbols in it are lost. A switch armed (hst_frx_arm_switch)
 * is off */
void hst_frx_set_code(hst_frx_t *r, int code);
/* one line sample; returns the number of bits decided, in r->bits[] */
int  hst_frx_tick(hst_frx_t *r, int16_t x);
int  hst_frx_state(const hst_frx_t *r);
/* v32_vit_step's twin, the receiver's (Task 23c): the same decisions,
 * metrics and history (test_hst_fwd fast_paths), each subset's nearest
 * point found by rounding on the subset's grid (hst_vgrid_*), the
 * add-compare-select written out; rates 1..4 */
int  hst_vit_step(v32_vit_t *v, int32_t zr, int32_t zi, int *hard);
/* the forward carrier (the guard's output): in training and data, not low
 * (NOCARRIER's measure) for HST_FRX_CLAMP_MS; else over -45 dBm0 (the
 * pump's watchdogs, Task 23) */
int  hst_frx_carrier(const hst_frx_t *r);
/* the equaliser (taps, carrier frequency) as it is now */
void hst_frx_store(const hst_frx_t *r, hst_eqstore_t *s);
/* back to listening for AB (IDLE); the next training starts from s's
 * equaliser (when s->valid) instead of the matched filter. A switch armed
 * is off (Task 22b: a restore is a new direction or a retrain, the code
 * the pump sets after it, never the armed one) */
void hst_frx_restore(hst_frx_t *r, const hst_eqstore_t *s);
/* the signal and error power over the last 64 symbols or so (training or
 * data); 0 before the first training and after hst_frx_restore */
int  hst_frx_snr(const hst_frx_t *r, int64_t *num, int64_t *den);
/* data mode: the error over 256 symbols against the set's mean power
 * (65536 = 0 dB; what UNLOCKED and hst_good_mse are reckoned in); 4 x 65536
 * outside data mode */
int32_t hst_frx_mse(const hst_frx_t *r);
/* "Good" at a code (spec "Rate changes", [OURS]): hst_frx_mse under
 * hst_good_mse[code], our receiver's error on v32line 3 dB above the SNR at
 * which it gives a BER of 1e-5 at that code (make hst-thresh; the table in
 * docs/limits.md); codes 3..6, 0 at 0..2 */
extern const int32_t hst_good_mse[7];
/* 0x3R received (we asked and got the LNA; spec "Rate changes", [OURS]).
 * In data mode at code 3..6, new_code 3..6 and not the code in force:
 * armed. The decoder's ones at the code in force are watched for; after
 * HST_SW_ONES symbols of them in a row, where they end the last
 * V32_VDEPTH points are decoded again at new_code as well, and both codes
 * are decoded for at most HST_SW_WIN symbols, no bits given; HST_SW_RUN
 * symbols of ones in a row at new_code then switch the receiver to it, the
 * decoders (trellis, differential, descrambler) as new_code's left them:
 * nothing but marks is lost. Else the code in force goes on and the ones
 * are watched for again. Armed, S or S-bar for HST_SW_ALT symbols (a
 * training instead) or HST_SW_SYMS symbols without a switch, the equaliser
 * is stored and the receiver listens for AB (IDLE, or UNLOCKED at the
 * time-out): the generic acquisition takes over, at new_code. So does any
 * end the receiver comes to by itself while armed. With code 2 on either
 * side (no switch by marks: hst_ftx_marks_switch trains) armed for the
 * training alone. Outside data mode: hst_frx_set_code(new_code), the next
 * training's data at it */
void hst_frx_arm_switch(hst_frx_t *r, int new_code);
/* armed: the code a switch goes to; 0 none (switched, or acquisition took
 * over: then hst_frx_t.code is new_code) */
int  hst_frx_armed(const hst_frx_t *r);
/* 0x2R (Task 22b): a training at `code` is coming, at any code, the one in
 * force too. In data mode armed for it alone: S or S-bar for HST_SW_ALT
 * symbols hand over to the generic acquisition at `code` (the equaliser
 * stored, IDLE), as hst_frx_arm_switch's do, but no marks are watched for
 * and there is no time-out (the pump bounds the wait, and disarms with
 * hst_frx_set_code); data goes on at the code in force meanwhile. Outside
 * data mode: hst_frx_set_code(code) */
void hst_frx_arm_train(hst_frx_t *r, int code);

/* ---------------- the answerer's HST engine (modem/ans_hst.c) ----------------
 * answer_t.u.hst, from ANS_HST_WAIT on (spec "The answering sequence",
 * "HST pump"). The detector's two receivers (det.rx[0] at 450, det.rx[1]
 * at 300, on its one band-pass) are the back receivers of ANS_HST: they
 * carry on from the detection, locked, so the caller's LR, which follows
 * its preamble at once, is not lost to a second acquisition.
 *
 * W (decision 25, sized in Task 18: docs/limits.md): ANS_HST_WAIT listens
 * HST_W_MS after ANSam, and on to HST_W_MAX_MS only while the detector's
 * energy stage holds (250-500 Hz present, 10 dB over 600-3400 Hz), which
 * no V.22bis, V.21 or Bell caller gives (the Task 13 ruling). Both are the
 * least that leave every line of `make hst-wsize` (levels, dropouts, a
 * line's edge, the caller 0 to 500 ms slower to start) at least 100 ms
 * between its detection and the end of the wait (least 150 ms; W 600
 * leaves 16 lines short, a cap of 1900 two; Task 18 fix round 1). */
#include "../v42/hst_link.h"
#ifndef HST_W_MS
#define HST_W_MS     650
#endif
#define HST_W_MAX_MS 2000
#define HST_FAIL_MS  15000            /* ANS_HST: no MNP link this long: V90M_END_HST_FAILED */
/* The watchdogs (spec "Retrain, watchdogs, cleardown and end codes",
 * [OURS]; Task 23). Holding forward: no back carrier (neither back receiver
 * locked: our own echo holds their carrier detect on) for HST_LOST_MS ends
 * the call ANS_END_LOST. Holding back, no forward carrier (hst_frx_carrier)
 * for HST_SILENT_ASK_MS: our 0x2R at the rate in force; for
 * HST_SILENT_BACK_MS: the forward channel taken back (a retrain, as a
 * reversal's unheard training is), and the first rule from there. So no
 * call stays connected without a far signal for more than 10 s and the
 * detectors' delays (under 12 s) */
#define HST_LOST_MS  5000             /* holding forward: no back carrier this long: lost */
#define HST_SILENT_ASK_MS   1000      /* holding back: no forward carrier this long: our 0x2R */
#define HST_SILENT_BACK_MS  5000      /* ... this long: the forward channel taken back */
/* the far end's LD (link.far_ld): the call ends ANS_END_CLEARDOWN_FAR when
 * its carrier goes (the back receivers' lock, holding forward; the forward
 * carrier, holding back), or HST_FAR_LD_MS after, whichever is first */
#define HST_FAR_LD_MS       2000
/* our cleardown with MNP up (the host's v90m_ec_release has put LD 255 out
 * of the endpoint, tx_busy 0): the host's bits (the endpoint's flags) this
 * long more on the channel we send, so that the LD's closing flag goes out
 * whole (tx_busy is 0 from its first bit: at 450 bit/s the host's next
 * look may be 15 ms before its last) and the LD's last bits leave our pulse
 * (HST_FTX_DELAY) and the far decoder (a trellis decoder's depth, some 24
 * symbols; a back receiver's matched filter and hold), then both carriers
 * stop [OURS] */
#define HST_CLR_FWD_MS        30
#define HST_CLR_BACK_MS      200
#define HST_ONES_N   160              /* samples of scrambled ones after each training of ours
                                         (48 symbols, V.17's segment 4) before the first data
                                         bit: the far receiver enters data on them [OURS] */

#define HST_REV_DRAIN_MS   20         /* holder: our granting LNA drained, then this, then the switch [OURS] */
#define HST_REV_TAIL       20         /* ... and from the cut this many symbols of ones (8.3 ms; V.17's
                                         turn-off under the BinModem profile): the far decoder gives the
                                         last frame's end (the Task 21 review's M3) [OURS] */
#define HST_REV_GAP_MS    200         /* requester: its LNA, our back transmitter off, this, our training
                                         [OURS; BinModem 400, BM] */
#define HST_REV_BOUND_MS 3000         /* holder switched: no forward signal this long, the forward
                                         channel taken back [OURS] */

/* rate changes (spec "Rate changes", decision 15; Task 22b). The policy,
 * holding the back channel (we receive the forward one) [PUB, US
 * 4,890,316]: every HST_UP_MS, the next agreed rate up to max_up (0x3R)
 * when the forward receiver was "good" (hst_frx_mse under hst_good_mse of
 * that rate, read at the rate in force: docs/limits.md) HST_UP_PCT % of
 * the time, never within HST_UP_MS of a downshift; one agreed rate lower
 * (0x2R) on HST_DOWN_ERRS frame errors within HST_DOWN_MS of the first,
 * or HST_RC_FAILS failed retrains within HST_DOWN_MS of the first (a
 * failed one is tried again at the same rate). A retrain fails when the
 * receiver is not in data at its rate HST_RC_BOUND_MS after the request
 * left (or the LNA came), when its training ends without data, or when
 * the data mode it reached is lost within HST_RC_HOLD_MS [OURS]. Not
 * failed: an 0x3R followed (the holder's own, or the LNA to ours) that
 * was never carried out, the receiver in data at the rate in force
 * throughout HST_RC_BOUND_MS: dropped, the rate kept (BinModem's stale
 * 0x3R, binmodem-facts.md `Online fallback`) [OURS] */
#define HST_UP_MS        10000
#define HST_UP_PCT          90
#define HST_DOWN_ERRS       15
#define HST_DOWN_MS      10000
#define HST_RC_FAILS         3
#define HST_RC_BOUND_MS   4000
#define HST_RC_HOLD_MS    2000
/* Task 23 (the Task 22b review's M2): a retrain that holds data over
 * HST_RC_HOLD_MS is a success, so a line whose receiver unlocks every few
 * seconds retrained at one rate for ever. Each retrain the receiver's own
 * loss of data mode asks (UNLOCKED, TRAINFAIL, NOCARRIER with the carrier
 * back) counts in a window of HST_CY_MS opened by the first; the
 * HST_CY_N-th within it asks the next agreed rate lower instead, and at
 * the lowest ends the call ANS_END_RETRAIN_FAILED, as HST_RC_FAILS failed
 * retrains within HST_DOWN_MS there do [OURS] */
#define HST_CY_N             4
#define HST_CY_MS        30000
enum {
    HST_RC_NONE,
    HST_RC_WAIT,                      /* holding forward: a 0x2R/0x3R granted, our LNA gone: the
                                         endpoint's frame finishing */
    HST_RC_ONES,                      /* ... between frames: ones, HST_REV_DRAIN_MS, then the training */
    HST_RC_MARKS,                     /* ... the switch by marks going out */
    HST_RC_TRAIN,                     /* ... the short training at R going out (a retrain) */
    HST_RC_ASK,                       /* holding back: our 0x2R/0x3R asked, its LNA awaited */
    HST_RC_FOLLOW                     /* ... the holder's change: the forward receiver follows */
};

/* a channel reversal in progress (spec "Channel reversal"; Task 21) */
enum {
    HST_REV_NONE,
    HST_REV_GRANT,                    /* holding forward: our LNA granting a 0x1R has drained; 20 ms */
    HST_REV_LISTEN,                   /* switched to the back channel: the far end's training awaited */
    HST_REV_GAP,                      /* holding back: our 0x1R's LNA came, our back transmitter
                                         stopped; 200 ms */
    HST_REV_TRAIN                     /* switched to the forward channel: our training, its ones */
};

typedef struct {
    hst_link_t    link;               /* the mailbox: zeroed at ANS_HST_WAIT, never again in the call */
    hst_det_t     det;                /* ANS_HST_WAIT's detector; its receivers are the back channel's */
    hst_btx_t     btx;                /* holding the back channel (a reversal, Task 21) */
    hst_ftx_t     ftx;                /* the forward transmitter */
    hst_frx_t     frx;                /* holding the back channel: the forward receiver (Task 21) */
    hst_eqstore_t eq_fwd;             /* the forward receiver's equaliser across reversals (Task 21) */
    int32_t       code;               /* the forward rate code in force, 2..6 */
    int32_t       next;               /* retraining: the code being trained to */
    int32_t       lane;               /* the back receiver whose bits we hand on: 0 (450), 1 (300) */
    int32_t       quiet;              /* samples without a back carrier (connected) */
    int32_t       ones;               /* samples since the training's last symbol, to HST_ONES_N */
    int32_t       la_seen;            /* the caller's confirming LA has been acted on */
    uint32_t      reversals;          /* channel reversals this call (Task 21) */
    int32_t       rev;                /* HST_REV_* */
    int32_t       rt;                 /* samples in it */
    int32_t       cut;                /* HST_REV_GRANT, HST_RC_WAIT: the endpoint has been between
                                         frames since: ones until the switch; 1 + the symbols begun since */
    /* rate changes (Task 22b) */
    int32_t       rc;                 /* HST_RC_* */
    int32_t       rc_order;           /* its 0x2R or 0x3R */
    int32_t       rct;                /* samples in it (holding back: since the endpoint took the request) */
    int32_t       rc_left;            /* holding back: the receiver has left data mode in it (a training) */
    int32_t       rc_counted;         /* ... and that training is counted in retrains */
    int32_t       rc_st;              /* ... the receiver's state at the last sample (an end entered from
                                         training or data fails the change; listening does not) */
    int32_t       up_t, up_all, up_good;   /* the upshift's window: samples; ms in it, ms good */
    int32_t       since_down;         /* samples since our last downshift, to MS(HST_UP_MS) */
    int32_t       fe_t, fe_n;         /* frame errors: samples since the window's first (-1 none); count */
    int32_t       fl_t, fl_n;         /* failed retrains: likewise */
    int32_t       hold_t;             /* samples a retrain's data mode is still on probation */
    int32_t       rc_late;            /* our request done before its LNA came (the training seen, its
                                         LNA lost): that LNA, when it comes, is not a change; 0 none */
    uint32_t      rx_bad;             /* the endpoint's frame errors as last seen */
    /* Task 23 */
    int32_t       cy_t, cy_n;         /* holding back: unlock retrains, samples since the window's first
                                         (-1 none); count */
    int32_t       fq;                 /* holding back: samples without the forward carrier */
    int32_t       ld_t;               /* samples since the far end's LD came (link.far_ld) */
    int32_t       clr;                /* our cleardown: samples of ones still to send, 0 none */
} hst_t;

#endif
