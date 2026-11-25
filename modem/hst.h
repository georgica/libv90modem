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

/* ---------------- the back channel's receivers (Task 12) ----------------
 * hst_bbpf (hst_tabs.h, 243 taps) is meant to be evaluated only at the
 * receiver's 1600 Hz instants (every 5th sample), shared by both
 * receivers: at 8000 Hz it would cost some 2 M multiply-adds a second. Its
 * >= 50 dB stop band at 600 Hz and above is what folds into 250-500 Hz at
 * 1600 Hz. hst_hbpf (189 taps) serves the detector's power ratio only, so
 * it can be evaluated at a sub-sampled rate. */

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

/* ---------------- the forward receiver's guard high-pass ----------------
 * hst_guard (HST_GUARD_N taps, linear phase) on 16-bit samples, a doubled
 * delay line: (HST_GUARD_N + 1) / 2 multiplies a sample, pairs of samples
 * the same distance from the centre added first. Integer only, rounded to
 * nearest (ties up), saturated: the same output on every target. Delay
 * (HST_GUARD_N - 1) / 2 samples. */
typedef char hst_guard_is_odd[(HST_GUARD_N % 2 == 1) ? 1 : -1];
typedef struct {
    int16_t x[2 * HST_GUARD_N];      /* newest first from x[pos], twice */
    int     pos;
} hst_gf_t;
void    hst_gf_init(hst_gf_t *g);
int16_t hst_gf_tick(hst_gf_t *g, int16_t in);

#endif
