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
 *   8000 Hz -> hst_bbpf (250-500 Hz) at every 5th sample only -> 1600 Hz
 *   -> AGC -> 32-bit NCO to baseband -> matched filter (the transmit pulse
 *   of hst_tabs, +-4 symbols, evaluated only at the strobes, at the
 *   strobe's fractional time: no interpolator) -> Gardner timing at 2
 *   strobes a symbol -> differential decisions -> scr17 descrambler.
 *
 * hst_bbpf (243 taps, folded: 122 multiplies) runs at 1600 Hz, not 8000,
 * and once for both receivers (hst_brx_band): its >= 50 dB stop band at
 * 600 Hz and above is what folds into 250-500 Hz at 1600 Hz. hst_hbpf
 * (189 taps) is not the receiver's: it feeds the detector's power ratio
 * only (Task 13), which evaluates it at every 4th sample (the power of a
 * sub-sampled output is the output's power).
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
 * The estimate holds over about +-28 Hz at 450 and +-18 Hz at 300 (beyond,
 * a wrong turn can be chosen); telephone lines stay within +-7 Hz.
 *
 * Lock needs, over the last HST_BRX_WIN decisions: 15 within +-12 degrees
 * of the grid; at least 4 different steps, none in more than 8 (a tone, or
 * dial tone, or hum, steps the same way every symbol: a scrambled signal
 * uses all eight); an RMS error under HST_BRX_RMS degrees (noise that falls
 * on the grid by chance does not); and the alias check, on the last
 * HST_BRX_EST strobes, preferring the turn in use to r +- 45 by a clear
 * margin. When it prefers r +- 45 the carrier is estimated again from those
 * strobes: a lock 45 degrees off (an estimate made on noise, before the
 * signal or in a dropout, then pulled by the frequency loop) is never
 * declared. The check runs again every 32 symbols while locked, and three
 * in a row for the same turn 45 degrees off estimate the carrier again. A symbol
 * 9 dB under the mean gives no bits, and two in a row lose the lock.
 * Carrier detect: the band's power in 10 ms blocks (before the AGC), on
 * at -44 dBm0 for 3 blocks, off under -47 dBm0 for 3 blocks [OURS]. */
#define HST_BRX_XL  (HST_BBPF_N + 40)  /* the 8000 Hz input, linear, moved down when full */
#define HST_BRX_ZN  130                /* the matched filter's span at 1600 Hz: 8 symbols at 300, +2 */
#define HST_BRX_ZL  (HST_BRX_ZN + 32)  /* the 1600 Hz baseband, linear, moved down when full */
#define HST_BRX_EST 10                 /* symbols for the carrier's estimate */
#define HST_BRX_OM  8                  /* symbols for the timing's */
#define HST_BRX_WIN 16                 /* decisions the lock and grid_ok look over */
#define HST_BRX_RMS 7                  /* degrees: the most RMS error for lock and grid_ok */

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
    int32_t  wpos, nwin, qbad;        /* ring place; decisions in it; symbols of bad quality in a row */
    int32_t  nfade;                   /* locked symbols 9 dB under the mean in a row */
    /* decisions */
    uint32_t aprev;                   /* the last symbol's angle */
    int32_t  wturn;                   /* the carrier's turn a symbol left after the NCO, 2^32 a turn */
    uint32_t okhist;                  /* each decision within +-12 degrees of the grid, the newest in bit 0 */
    int32_t  nsym, locked, grid_ok;
    int64_t  e2;                      /* mean square of the decision error, turns Q16 squared */
    scr_t    scr;
    uint8_t  bits[3];                 /* the last symbol's bits, the first in bits[0] */
} hst_brx_t;

/* bps 450 or 300 */
void hst_brx_init(hst_brx_t *r, int bps);
/* one sample at 8000 Hz; returns the bits decided (0 or 3), in r->bits[];
 * bits only while locked */
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

#endif
