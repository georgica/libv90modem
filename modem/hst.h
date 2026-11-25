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
 * on the grid by chance does not); the symbols' power steady, its spread
 * (variance over the squared mean) at most 1/6 (8-DPSK through the matched
 * filter is of constant modulus: its spread stays under 0.13 down to 6 dB;
 * two tones that beat, which can step the phase in a varied way, spread
 * 0.24 and more); and the alias check, on the last
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
 * A symbol 9 dB under the mean is a fade: about 75 ms of them in a row (11
 * symbols at 450, 8 at 300) lose the lock. A shorter fade (a lost 20 ms
 * packet or two) keeps the lock, and its symbols' bits go out with the
 * rest, a few of them wrong: the stream has no gap. Faded symbols move
 * neither the loops nor the grid history.
 *
 * Losing the lock, to a fade or to the carrier detect, discards every bit
 * still held back: the whole ring, HST_BRX_DLY symbols, not just the faded
 * ones. On a carrier stop that is the last 10 to 15 symbols the far end
 * really sent before it (measured, a stop at 40 dB: the last bit given is
 * 42 line bits short of the last sent at 450, 31 at 300), so a frame sent
 * just before the far end's back transmitter stops loses its FCS and
 * closing flag. Task 21 (reversals) releases the held bits older
 * than the fade's start (plus the matched filter's 4 symbols) on a loss
 * instead, or sends 20 symbols of flags or more before a stop.
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

typedef struct {
    int16_t  xh[HST_BRX_XL];          /* the input at 8000 Hz */
    int16_t  zr[HST_BRX_ZL], zi[HST_BRX_ZL];   /* baseband at 1600 Hz */
    int32_t  xn, dec, zn;             /* the next places in xh and zr/zi; samples since the last band-pass */
    int32_t  y;                       /* the last band-passed sample (1600 Hz) */
    int32_t  lane;                    /* 0: 450, 1: 300 */
    /* carrier detect and AGC, on the band's power */
    int64_t  bpw, ps;                 /* this block's sum of squares; the smoothed block power */
    int32_t  nblk, cnt, cd;
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
    uint16_t wpw[HST_BRX_WIN];        /* symbol power, Q8 of the mean (at most 255.99) */
    int32_t  wpos, nwin, qbad;        /* ring place; decisions in it; symbols of bad quality in a row */
    int32_t  qacq, qhold;             /* the window's quality to win a lock (and grid_ok), to hold one */
    int32_t  wn[8];                   /* the window's decisions of each step */
    int64_t  wsum, wp1, wp2;          /* its sums: squared errors, powers, squared powers */
    int32_t  nfade;                   /* locked symbols 9 dB under the mean in a row */
    uint8_t  dly[HST_BRX_DLY];        /* the last decisions' bits while locked, a ring (bit b: bits[b]) */
    int32_t  ndly, pdly;              /* bits held back: symbols, the ring's place */
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
 * only while locked, and held back HST_BRX_DLY symbols (see above), so the
 * first come some 140 ms (450) or 210 ms (300) after the lock */
int  hst_brx_tick(hst_brx_t *r, int16_t x);
/* one band-passed sample at 1600 Hz, as hst_brx_tick computes it at every
 * 5th sample (it returns the same). Both receivers run on one band-pass:
 * hst_brx_tick on the first, and whenever its `dec` is back to 0,
 * hst_brx_band on the second with the first's `y` */
int  hst_brx_band(hst_brx_t *r, int32_t y);
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
 *    found: the threshold sits 2 dB under it [OURS].
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

#endif
