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

#ifdef HST_PROFILE_BM
/* -DHST_PROFILE_BM: a test build for runs against BinModem only (SPEC D16);
 * release builds and golden files never use it */
#define HST_OPEN_CODE   3    /* 7200 (BinModem's); provisional: Task 20 / R1 */
#define HST_LONG_V17    1    /* every training, long or short, is V.17's long train */
#define HST_3R_TRAINS   1    /* a 0x3R rate request is carried out with a training */
#else
#define HST_OPEN_CODE   4    /* 9600 before MNP agreement; provisional: Task 20 / R1 */
#define HST_LONG_V17    0
#define HST_3R_TRAINS   0
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
} hst_ftx_t;

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
 *   TRAIN  decision-directed on the 4 points A..D, fast steps (until the
 *          error has stayed under -20 dB for HST_FRX_HOLD symbols, in
 *          training or data), then tracking
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
 *   NOCARRIER  the power under -48 dBm0 for HST_FRX_LOSS_MS in training or
 *          data. From HST_FRX_CLAMP_MS on, or as soon as the power is 10
 *          dB under its slow average, no bits come and nothing is adapted
 *          (the gain loop neither); should the power come back (over -45
 *          dBm0, and within 10 dB) before the loss, all goes on: a
 *          dropout. The equaliser and frequency are kept for the next
 *          training
 *   UNLOCKED   in data mode the error (over 256 symbols), for
 *          HST_FRX_LOCK_SYMS symbols, near what noise alone gives after
 *          the rate's decisions (-5.8, -12.8, -15.8, -19, -21.9 dB at codes
 *          2..6)
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
#define HST_FRX_LOSS_MS     300  /* ... this long: NOCARRIER (a 100 ms dropout survives) */
#define HST_FRX_LOCK_SYMS  1200  /* data mode: symbols of poor error before UNLOCKED (0.5 s;
                                  * a 1 dB step at 14400 takes some 1000 to settle) */
#define HST_FRX_TRAIN_SYMS 3600  /* symbols of training at most */

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
    int64_t    pslow;                 /* the power, slowly (2^-12 a sample) */
    int32_t    mlock;                 /* data mode: the error over 256 symbols */
    int32_t    bad;                   /* data symbols it has been poor (lock_mse) */
    int32_t    g;                     /* the data mode's gain on qamrx's input, Q14 */
    int64_t    pz, pz8, pzl;          /* the points' power, over 32, 8 and 128 symbols */
    int32_t    gstep;                 /* it is off (a level step): the decisions wrong,
                                       * nothing else adapted */
    int32_t    gcnt;                  /* symbols it has been so; negative: since the gain
                                       * was set, the equaliser flushing */
    int32_t    nsym;                  /* symbols since qamrx_sync */
    int32_t    held;                  /* symbols the error has been under -20 dB */
    int32_t    fast;                  /* still on the fast steps */
    uint32_t   offw;                  /* off the 4 points: the last 8 symbols, bit 0 the newest */
    int32_t    mse;                   /* |e|^2 / |d|^2 (65536 = 0 dB), smoothed over 16 symbols */
    int32_t    mse0;                  /* mse before the last 8 points */
    int32_t    msh[8];                /* mse over the last 8 symbols */
    int32_t    hzr[8], hzi[8];        /* the last 8 points, for the decoder at the switch */
    uint32_t   sh;                    /* the last 8 decisions (A..D), 2 bits each, the newest lowest */
    int32_t    alt;                   /* symbols since the last 8 were an alternation (S, S-bar) */
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
} hst_frx_t;

void hst_frx_init(hst_frx_t *r);
/* the rate code in force, 2..6 (outside, the nearest). In data mode a new
 * one takes the next symbol, and the decoder starts again: the
 * V32_VDEPTH symbols in it are lost (the switch's ones cover them) */
void hst_frx_set_code(hst_frx_t *r, int code);
/* one line sample; returns the number of bits decided, in r->bits[] */
int  hst_frx_tick(hst_frx_t *r, int16_t x);
int  hst_frx_state(const hst_frx_t *r);
/* the equaliser (taps, carrier frequency) as it is now */
void hst_frx_store(const hst_frx_t *r, hst_eqstore_t *s);
/* back to listening for AB (IDLE); the next training starts from s's
 * equaliser (when s->valid) instead of the matched filter */
void hst_frx_restore(hst_frx_t *r, const hst_eqstore_t *s);
/* the signal and error power over the last 64 symbols or so (training or
 * data); 0 before the first training and after hst_frx_restore */
int  hst_frx_snr(const hst_frx_t *r, int64_t *num, int64_t *den);

#endif
