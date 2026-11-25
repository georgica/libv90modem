/* Test peer: a USR HST CALLING modem, in double, x86 test side.
 *
 * Written from docs/history/hst-facts.md only (each choice below names its
 * section and tag there), independently of the library's HST pump
 * (modem/hst_*.c), whose code it neither includes nor calls: it has its
 * own back-channel transmitter and receiver (pulse, preamble, scrambler,
 * timing, carrier), its own forward transmitter (training, trellis
 * encoder) and receiver (guard, AB acquisition, LMS equaliser, Viterbi),
 * so that it checks our answerer against the facts and not against
 * itself (the sim/v32call.h rule). Shared with the rest of the tree only
 * where sim/v32call already shares: sim/v32spec.h, the hand transcription
 * of V.32bis's signal sets and Tables 1 and 2. MNP is sim/mnppeer (the
 * originator, with USR's 0xC0).
 *
 * The call (hst-facts.md `Call start`, `Reversal`, `Rate changes`):
 *  - It listens for the answer tone, 2100 Hz: present for at least 400 ms,
 *    then absent 30 ms, is the tone's end; a shorter burst starts the count
 *    again [BM]. Then it starts its back transmitter (preamble at
 *    `start_bps`, then MNP's bits: the LR goes straight after the
 *    preamble) and its forward receiver together [FW].
 *  - Its forward receiver expects the opening rate (code 4, or 3 with
 *    `bm_training`) until MNP agrees; then the best agreed rate, from the
 *    answerer's next training on.
 *  - With 300 bit/s agreed it moves its back channel to 300 once its
 *    confirming LA has gone, plus 250 ms [BM].
 *  - Backlog: with `rev_backlog` > 0, when its unsent and unacknowledged
 *    data pass it, the answerer having held the forward channel 2 s, it
 *    asks for the forward channel (LN 0x1R, R the current forward code);
 *    on the LNA it stops its back transmitter, waits 200 ms, and trains
 *    forward at R, long the first time, short after (`Reversal`, [OURS]).
 *    With `la_after_rev`, its first frame on the new channel after each
 *    switch is an LA (`Reversal`, [OURS]: the LAs in flight at a switch
 *    are lost); by default not.
 *  - Holding the forward channel, it takes 0x1R (once its LNA has gone,
 *    plus 20 ms: forward off, back preamble, forward receiver listening),
 *    0x2R (a short training at R) and 0x3R (by marks: 15 ms of ones at the
 *    old rate, the new rate, 15 ms of ones; with `bm_training` a short
 *    training instead), each once its LNA has gone: the frame being sent
 *    finishes [PUB, P2:561], the LNA goes, then from the next symbol the
 *    marks ([OURS], `Rate changes`), or 20 ms later the training (as our
 *    reversal waits [OURS]: a receiver starting again on the training would
 *    lose the LNA's end, still in its decoder).
 *  - Forced: at `force_rate_at_ms` (and each `force_more`) it sends LN
 *    (`force_kind` | code: 0x10, 0x20 or 0x30); on its LNA, for 0x2R it
 *    expects a training at the code, for 0x3R the marks (or, with
 *    `bm_training`, a training), for 0x1R it reverses.
 *  - `ld_at_ms`: LD 255 then, and its carrier stops `ld_hold_ms` (1000
 *    [BM]) after the LD has gone; `drop_at_ms`: every transmitter stops
 *    then, at once.
 * Times are from hstcall_init, in ms.
 *
 * Simplifications [BM, OURS] that MNP recovers from: no HDLC reset at a
 * switch, and LTs not held back while a reversal or rate change of ours is
 * pending (the frame in flight is cut). Holding the forward channel it
 * hears the back channel at one speed only, the agreed one (BinModem
 * listens for both).
 *
 * No allocation and no global state: two in one process are independent.
 * hstcall_t is about 150 KB: keep it static or on the heap, never on a
 * stack (Windows' and musl's thread stacks are small). */
#ifndef V90D_SIM_HSTCALL_H
#define V90D_SIM_HSTCALL_H
#include <stddef.h>
#include <stdint.h>
#include "mnppeer.h"

typedef int (*hc_getbit_fn)(void *user);

/* ---------------- the back channel's transmitter ----------------
 * Differential 8-PSK (`Back channel`, [FW]): 375 Hz and 150 baud (450
 * bit/s) or 350 Hz and 100 baud (300); each tribit, its first bit the least
 * significant [BM], steps the phase by the nibble of 0x43705261 it indexes,
 * eighths of a turn; points at 22.5 + 45 k degrees; scrambler 1 + x^-14 +
 * x^-17 with no 64-ones rule; phase and scrambler from zero at each start
 * [BM]. Pulse: root-raised-cosine, beta 0.35, +-4 symbols with a Hann taper
 * [BM's shape]. Preamble: 60 symbols of scrambled ones [BM]. */
#define HC_BPRE   60
#define HC_BSPAN  4                       /* symbols each side */
#define HC_B450_T3 160                    /* a symbol in thirds of a sample at 8000 Hz */
#define HC_B300_T3 240
#define HC_BRING  12

typedef struct {
    double re, im;                        /* the symbol, unit modulus */
    long   t3;                            /* its pulse's centre, thirds of a sample */
    int    lane;                          /* 0: 450, 1: 300 */
} hc_bsym;

typedef struct {
    double   p450[2 * HC_BSPAN * HC_B450_T3 + 1], p300[2 * HC_BSPAN * HC_B300_T3 + 1];
    double   gain[2];                     /* symbol amplitude for the level */
    int      lane;                        /* of the next symbol */
    int      on;                          /* symbols being made */
    long     n;                           /* samples */
    long     next3;                       /* the next symbol's centre */
    int      phase;                       /* eighths of a turn */
    uint32_t scr;                         /* line bits, the newest in bit 0 */
    int      npre;                        /* preamble symbols left */
    hc_bsym  ring[HC_BRING];
    int      rpos, rn;
    long     nsym;
    hc_getbit_fn getbit; void *user;
} hc_btx;

/* level: RMS of the line signal, 16-bit units; silent until hc_btx_start */
void   hc_btx_init(hc_btx *t, int bps, double level, hc_getbit_fn getbit, void *user);
void   hc_btx_start(hc_btx *t);           /* preamble, then getbit's bits */
void   hc_btx_speed(hc_btx *t, int bps);  /* from the next symbol */
void   hc_btx_stop(hc_btx *t);            /* no more symbols; the pulses ring out */
double hc_btx_tick(hc_btx *t);

/* ---------------- the back channel's receiver ----------------
 * One speed. Mixed to baseband at 8000 Hz, a low-pass and every 5th sample
 * (1600 Hz), the carrier's offset estimated from the baseband's lag-one
 * correlation over the first 100 ms of carrier and then followed by the
 * decisions, the matched pulse at two strobes a symbol (Gardner timing),
 * differential decisions, the descrambler. Carrier on above -43 dBm0 for
 * 30 ms, off under -48 dBm0 for 60 ms. */
#define HC_BLP   161                      /* the low-pass's taps at 8000 Hz */
#define HC_BZN   256                      /* the 1600 Hz baseband ring */
#define HC_BMF_R 32                       /* the matched pulse's table: steps a 1600 Hz sample */
#define HC_BMFN  (2 * HC_BSPAN * 16 * HC_BMF_R + 1)   /* +-4 symbols at 300 (16 samples) */

typedef struct {
    int      bps;
    double   T;                           /* a symbol, 1600 Hz samples */
    double   w;                           /* the carrier, radians a sample at 8000 Hz */
    double   lp[HC_BLP];
    double   xr[2 * HC_BLP], xi[2 * HC_BLP];
    int      xp, dec;
    long     n;
    double   zr[HC_BZN], zi[HC_BZN];
    long     zn;                          /* 1600 Hz samples made */
    double   mf[HC_BMFN];                 /* the matched pulse, +-4 symbols */
    int      mfh;                         /* its half span, 1600 Hz samples */
    /* carrier detect */
    double   bp; int nb, on_n, off_n, cd;
    double   thr_on, thr_off;
    /* acquisition */
    int      acq;                         /* 0 none, 1 estimating, 2 deciding */
    double   ar, ai; int an;              /* lag-one correlation */
    double   fo, ph;                      /* the carrier's offset, radians a 1600 Hz sample; its phase */
    double   ts;                          /* the next strobe, in 1600 Hz samples */
    int      half;
    double   pr, pi_, mr, mi;             /* the last symbol strobe, the midpoint */
    double   pw;                          /* symbol power, smoothed */
    double   e2;                          /* the decisions' squared error, radians, smoothed */
    long     nsym;
    uint32_t hist;                        /* line bits, the newest in bit 0 */
    int      settle;                      /* symbols before bits go out */
    int      nbits;
    uint8_t  bits[3];
} hc_brx;

void hc_brx_init(hc_brx *r, int bps);
/* one sample; returns the bits decided (0 or 3), in r->bits[] */
int  hc_brx_tick(hc_brx *r, double x);
/* Es/N0 from the decisions, dB (0 without carrier) */
double hc_brx_snr(const hc_brx *r);

/* ---------------- the forward transmitter ----------------
 * 2400 baud on 1800 Hz (`Forward channel`, [FW]); root-raised-cosine,
 * beta 1/12 [OURS], +-15 symbols with a Kaiser taper; GPC scrambler
 * 1 + x^-18 + x^-23; V.32bis's sets and trellis code at codes 3..6, V.32's
 * 4800 at code 2 (v32spec.h). Trainings (`Training`): ours, provisional,
 * S1 180 AB, S2 72 CD, S3 TRN 3096 (long) or 200 (short) [OURS]; or
 * V.17's long train, 256 AB, 2976 conditioning, 64 bridge, 48 ones at the
 * rate (`V.17 long train`, [PUB]). hc_ftx_bm: BinModem's shape instead
 * (docs/hst/binmodem-facts.md "Pulse shape", [BM]): V.17's root-raised-
 * cosine, beta 0.25, and a 10th-order Butterworth high-pass at 560 Hz on
 * the output (bilinear, its corner prewarped, at 8000 Hz). */
#define HC_FSPAN 15
#define HC_FL    (2 * HC_FSPAN)           /* symbols in the pulse */

enum { HC_FS_OFF, HC_FS_S1, HC_FS_S2, HC_FS_S3, HC_FS_S4, HC_FS_MARK_OLD, HC_FS_MARK_NEW, HC_FS_DATA };

typedef struct {
    double   p[HC_FL * 10 + 1];           /* the pulse in thirds of a sample, a symbol 10 */
    double   pts[5][128][2];              /* the sets, v32spec's common scale */
    double   gain;
    double   ar[HC_FL + 1], ai[HC_FL + 1];   /* symbols, newest first */
    int      acc;                         /* thirds to the next symbol */
    long     n;
    int      seg, left;                   /* segment of the next symbol, symbols left of it */
    int      lens[4], v17;
    int      code, next_code;
    uint32_t scr;                         /* line bits, the newest in bit 0 */
    int      y1, y2, ta, tb, tc;          /* differential and trellis memory */
    int      last;                        /* the last state A..D sent (0..3) */
    int      bridge;                      /* bridge bits sent */
    int      on, ring;                    /* making symbols; symbols left ringing out */
    int      bm;                          /* BinModem's shape (hc_ftx_bm) */
    double   gb[5][3], ga[5][2], gz[5][2];   /* its high-pass: 5 sections, b, a, state */
    hc_getbit_fn getbit; void *user;
} hc_ftx;

void   hc_ftx_init(hc_ftx *t, double level, hc_getbit_fn getbit, void *user);
/* after hc_ftx_init: BinModem's pulse and high-pass, at the same level */
void   hc_ftx_bm(hc_ftx *t);
/* from the next symbol: a training (v17: V.17's long train), then data at code 2..6 */
void   hc_ftx_train(hc_ftx *t, int code, int long_form, int v17);
/* 0x3R: 36 symbols of ones at the code in force, then 36 at new_code, then data */
void   hc_ftx_marks(hc_ftx *t, int new_code);
void   hc_ftx_stop(hc_ftx *t);
int    hc_ftx_in_data(const hc_ftx *t);
double hc_ftx_tick(hc_ftx *t);

/* ---------------- the forward receiver ----------------
 * A linear-phase guard high-pass (Kaiser, 241 taps, cut at 525 Hz: some
 * 55 dB down at 470 Hz and below, flat from 580 Hz). Listening: the
 * training's AB lines at 600 and 3000 Hz, each at least 1/20 of the power,
 * the power over -43 dBm0, for 40 ms; the symbol instants from their phase
 * difference. Then to baseband, the matched pulse (RRC 1/12) at two strobes
 * a symbol, a 32-tap T/2 LMS equaliser: its gain from 16 symbols, then
 * decision-directed on the 4 points A..D (fast for 300 symbols), with a
 * decision-directed carrier loop. Data at code 3..6 once more than 3 of the
 * last 8 points are off the 4 points, but only after 48 symbols of the
 * random 4 points (TRN, V.17's conditioning: AB and CD alone do not train
 * an equaliser) and with the error from before them under -14 dB; at code
 * 2 once 8 symbols past the alternations. V.32bis's Viterbi (8 states, 24
 * deep) and Tables 1 and 2 inverted; the GPC descrambler. Timing: a second
 * order loop on the error's gradient against the strobes' time (the taps
 * shifted a strobe each way), and the taps' centre of energy held weakly at
 * the middle. In data, 16 alternating symbols on the 4 points are a new
 * training: trained again from the equaliser it has, at the code set by
 * hc_frx_set_code. Carrier lost (12 dB under its recent peak, which falls
 * 3 dB a second, or under -48 dBm0, for 100 ms; while the 5 ms power is 3
 * dB under the 50 ms power nothing adapts, and the equaliser is kept as
 * that begins; a real step down clears it in some 50 ms): listening
 * again, the equaliser kept (from data) for the next AB, which then starts
 * from it, the carrier's phase taken from 8 symbols' fourth power. hc_frx_arm: 0x3R,
 * both rates decoded (the equaliser frozen) until 16 symbols of ones come
 * at the new one, at most 2 s. The -14 dB gate on the data's start is this
 * peer's own choice (our hst_frx happens to use the same figure). */
#define HC_GN   241
#define HC_FMFH 54                        /* the matched pulse's half span, samples (16 symbols) */
#define HC_FMF_R 32                       /* its table's steps a sample */
#define HC_FBB  256                       /* the baseband ring */
#define HC_EQN  32
#define HC_VD   24

enum { HC_FRX_IDLE, HC_FRX_GAIN, HC_FRX_TRAIN, HC_FRX_DATA };

typedef struct {
    double pm[8];
    int    hist[32][8];                   /* label << 3 | previous state */
    int    pos; long n;
} hc_vit;

typedef struct {
    int      code;
    hc_vit   v;
    int      py1, py2;
    uint32_t dh;                          /* descrambler: line bits, the newest in bit 0 */
    int      ones;                        /* symbols in a row giving only ones */
} hc_dec;

typedef struct {
    double   g[HC_GN], gx[2 * HC_GN]; int gp;
    double   mf[2 * HC_FMFH * HC_FMF_R + 1];
    double   pts[5][128][2];
    int      vnext[8][4], vy0[8];
    long     n;
    double   bbr[HC_FBB], bbi[HC_FBB];
    /* power and AB */
    double   pw;                          /* guard output power, leaky */
    double   e50, epk, pkfall;            /* the power over 50 ms; its highest, falling 3 dB a second */
    int      dip;                         /* pw 3 dB under e50: nothing adapted */
    double   r6r, r6i, r30r, r30i;        /* 600 and 3000 Hz, leaky */
    long     abrun, low;
    double   thr_on, thr_off;
    int      state;
    /* strobes */
    double   ts, tstep;                   /* the next strobe; T/2 */
    double   tdrift;                      /* the clock offset's, a symbol */
    double   tadj;                        /* the timing loop's corrections since the AB, less the drift's */
    int      half;
    double   xr[HC_EQN], xi[HC_EQN];
    double   wr[HC_EQN], wi[HC_EQN];
    int      stored;
    double   sr[HC_EQN], si[HC_EQN], sfreq, sdrift, sadj;   /* the equaliser kept (stored) */
    double   theta, freq;
    double   dq2;                         /* the timing gradient's mean square */
    double   gacc; int gn;
    int      restoring;                   /* GAIN with the kept equaliser: the phase from z^4 */
    int      warm;                        /* this training started from the kept equaliser: slow steps */
    double   z4r, z4i;
    long     nsym, trsym;
    double   mse;                         /* |e|^2 against the decision, smoothed */
    double   mse0;                        /* training: the same over 16 symbols */
    double   msh[16]; int mshp;           /* ... over the last 16 symbols */
    double   sig;
    uint32_t sh;                          /* the last 16 4-point decisions, 2 bits each */
    int      seen_alt, rand_n;            /* AB or CD seen; symbols since an alternation */
    uint32_t offw;
    hc_dec   d, d2;                       /* the decoder; the new rate's while armed */
    int      armed; long armed_n;
    int      next_code;
    int      snb;                         /* the last symbol's bits */
    uint8_t  sbits[8];
    int      nbits;                       /* the last tick's bits, the first sent first */
    uint8_t  bits[16];
} hc_frx;

void hc_frx_init(hc_frx *r, int code);
/* the code of the next training (and, before the first, of the first) */
void hc_frx_set_code(hc_frx *r, int code);
void hc_frx_arm(hc_frx *r, int code);
/* keep the equaliser (taps, carrier frequency, clock offset) for the next
 * AB, if in data; done on a carrier loss, and by the caller when it stops
 * listening (a reversal) */
void hc_frx_keep(hc_frx *r);
int  hc_frx_tick(hc_frx *r, double x);
int  hc_frx_state(const hc_frx *r);
/* the equalised points' SNR, dB, from the decisions (0 before training) */
double hc_frx_snr(const hc_frx *r);

/* ---------------- the calling modem ---------------- */
typedef struct {
    int     back_bps;      /* the back speed it wants, 450 or 300 (0: 450); 300 sets 0xC0's 0x08 */
    int     start_bps;     /* the back speed it starts at, its LR's (0: 450, BinModem's) */
    int     hst96;         /* the 1986 HST as BinModem models it: rates 0x10, flags 0x09 [BM] */
    uint8_t rates, flags;  /* 0xC0 offered; both 0: no 0xC0 (plain MNP over HST) */
    int     send_opt;      /* the LR carries parameter 8, bit 2 */
    int     bm_training;   /* forward trainings V.17's long train; opening code 3; 0x3R retrains;
                              the forward channel in BinModem's shape (hc_ftx_bm) */
    int     no_lr;         /* never sends an LR */
    int     ld_at_ms;      /* > 0: LD 255 then */
    int     drop_at_ms;    /* > 0: every carrier off then */
    int     rev_backlog;   /* > 0: asks for the forward channel above this backlog, octets */
    int     force_rate_code, force_rate_at_ms, force_kind;   /* force_kind 0x20 or 0x30 */
    int     level_dbm0;    /* transmit level (0: -12) */
    /* for the later tasks */
    int     lr_delay_ms;   /* the LR no earlier than this after the back channel starts (0: straight after
                              the preamble) */
    struct { int code, at_ms, kind; } force_more[3];   /* further forced requests (kind 0: none),
                              each on its own, sent at its time once no LN of ours is outstanding */
    int     ignore_lna;    /* the first N LNAs to our own LNs are not acted on (as if lost):
                              no reversal, no rate change follows (Task 21's rev_lost) */
    int     ignore_ln;     /* the first N LNs from the answerer are not acted on (its LNA still goes) */
    int     ld_hold_ms;    /* carrier held this long after the LD has gone (0: 1000: BinModem's ATH holds
                              the line 1000 ms for its LD to get out, hst-facts `Cleardown` [BM]) */
    int     la_after_rev;  /* after each switch its first frame on the new channel is an LA (`Reversal`,
                              [OURS]; 0: not, as nothing says a Courier does) */
} hstcall_cfg;

/* back 450, start 450, rates 0x7C (codes 2..6), flags 0x01, level -12 */
void hstcall_defaults(hstcall_cfg *c);

enum { HC_TONE, HC_BACK, HC_GAP, HC_FWD, HC_TURN, HC_DROPPED };
enum { HC_ROLE_BACK, HC_ROLE_FWD };

typedef struct {
    hstcall_cfg cfg;
    int      phase;        /* HC_TONE .. HC_DROPPED */
    int      role;         /* HC_ROLE_*: which channel it sends */
    int      fwd_code;     /* the forward channel's rate code in force */
    int      back_bps;     /* the back channel's speed in force */
    int      reversals;
    long     n;            /* samples since init */
    long     ms;
    double   level;
    /* tone */
    double   tq1, tq2, te; int tk;
    int      t_on, t_off;  /* blocks present, absent */
    /* the pieces */
    hc_btx   btx;
    hc_brx   brx;
    hc_ftx   ftx;
    hc_frx   frx;
    mnppeer_t mnp;
    int      linked;       /* MNP connected once */
    long     conf_at;      /* ms the confirming LA had gone (-1 not yet) */
    int      ln_pend;      /* our LN's code waiting for its LNA, -1 none */
    int      ln_out;       /* ... and it has gone */
    int      ln_rx;        /* an LN taken from the answerer, to act on once our LNA has gone (-1 none) */
    int      lna_going;    /* our LNA frame is on the line */
    long     lna_done;     /* ms our last LNA had gone (-1 none) */
    long     t_switch;     /* ms of the last reversal (or the start) */
    long     t_wait;       /* ms the gap ends */
    int      wait_code;
    int      trained;      /* we have trained forward once */
    int      rate_rx;      /* an 0x2R or 0x3R from the answerer, done once our LNA has gone (-1 none) */
    int      hold;         /* the rest of the symbol after that LNA: ones, then the marks or training */
    int      conf_going;   /* the confirming LA is on the line */
    int      nforce;       /* forced requests sent: bit 0 request 0, bits 1..3 force_more[0..2] */
    int      lnas_ignored, lns_ignored;
    int      ld_sent;      /* LD asked; its carrier stop pending */
    long     ld_off;
    long     rx_bits;      /* bits given to MNP */
} hstcall_t;

void    hstcall_init(hstcall_t *c, const hstcall_cfg *cfg);
int16_t hstcall_tick(hstcall_t *c, int16_t rx);
size_t  hstcall_write(hstcall_t *c, const uint8_t *d, size_t n);
size_t  hstcall_read(hstcall_t *c, uint8_t *d, size_t max);
int     hstcall_state(const hstcall_t *c);
/* MNP's unsent and unacknowledged octets */
long    hstcall_backlog(const hstcall_t *c);

#endif
