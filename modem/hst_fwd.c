/* HST's forward channel (hst.h; hst-facts.md, "Forward channel" and
 * "Training"): the transmitter and its provisional training, and the
 * receiver's guard high-pass.
 *
 * The transmitter is V.32's (v32tx) on HST's pulse (hst_fpulse), set up
 * with answer = 0. In v32tx `answer` chooses only the scrambler's
 * polynomial, and 0 is V.32's calling-modem one, GPC (1 + x^-18 + x^-23),
 * which HST's forward channel uses whichever end sends (BF §2, [FW]); so
 * v32tx scrambles for us and its scrambler runs on from the training into
 * the data, with no second copy of it (plan ruling F7). */
#include <stddef.h>
#include "hst.h"
#include "sintab.h"
#include "fixmath.h"
/* for v32_label_abcd, V.32's Table 4 (dibit -> synchronising state), which
 * S3 sends as V.32's TRN does: v32's own table, not exported by v32.h */
#include "../v32/v32_priv.h"

/* what a training segment sends (hst_ftx_t.kind) */
enum {
    K_S,          /* A B A B ... (V.32's S) */
    K_SBAR,       /* C D C D ... (V.32's S-bar) */
    K_TRN,        /* ones through the scrambler, a dibit a symbol, as V.32's
                   * Table 4 (A 00, B 01, C 11, D 10), on the 4 points from
                   * the first symbol: our receiver, once it has found AB,
                   * trains decision-directed on all 4 (V.32's TRN begins
                   * with 256 on A and C for V.32's receiver) */
    K_COND,       /* V.17's segment 2: the same, scrambler from HST_BM_SCR0,
                   * 00 C, 01 D, 11 A, 10 B */
    K_BRIDGE,     /* V.17's segment 3: 0000 0001 0001 0001 (B0 first),
                   * scrambled, differentially encoded as V.32's 4800 */
    K_ONES        /* V.17's segment 4: scrambled ones at the rate, the
                   * differential encoder from the bridge's first symbol */
};

/* the bridge as v32tx's rate sequence (B0 the most significant bit) */
#define BRIDGE_WORD 0x0111u

void hst_ftx_init(hst_ftx_t *t, int level, v32_getbit_fn getbit, void *user)
{
    int i;
    v32tx_init(&t->tx, 0, level, getbit, user);
    v32tx_set_pulse(&t->tx, &hst_fpulse[0][0][0], HST_FL);    /* after every v32tx_init */
    for (i = 0; i < 4; i++) { t->kind[i] = K_S; t->len[i] = 0; }
    t->seg = HST_SEG_OFF;
    t->left = 0;
    t->code = HST_OPEN_CODE;
    t->d0 = 0;
    t->marks = 0;
    t->mcode = t->code;
}

/* Under HST_PROFILE_BM every training is V.17's long train, the opening
 * and the short ones alike (a retrain, a reversal, 0x2R, recovery):
 * BinModem's receiver trains on nothing else (BinModem itself always sends
 * the long form). Coordinator's ruling, review of Task 15 (I-1). */
void hst_ftx_train(hst_ftx_t *t, int code, int long_form)
{
    static const int8_t k_hst[4] = { K_S, K_SBAR, K_TRN, K_ONES };
    static const int8_t k_v17[4] = { K_S, K_COND, K_BRIDGE, K_ONES };
    int i, v17 = HST_LONG_V17;
    t->code = code < 2 ? 2 : code > 6 ? 6 : code;
    t->marks = 0;
    for (i = 0; i < 4; i++) t->kind[i] = v17 ? k_v17[i] : k_hst[i];
    if (v17) {
        t->len[0] = HST_BM_AB; t->len[1] = HST_BM_COND;
        t->len[2] = HST_BM_BRIDGE; t->len[3] = HST_BM_ONES;
    } else {
        t->len[0] = HST_S1_SYMS; t->len[1] = HST_S2_SYMS;
        t->len[2] = long_form ? HST_S3_LONG : HST_S3_SHORT; t->len[3] = 0;
    }
    t->tx.scr.reg = 0;
    /* through silence, so that the first segment's mode starts afresh (its
     * count from 0) even when it is the mode in force; no silent symbol is
     * sent: the next symbol's mode is set before it begins */
    v32tx_set_mode(&t->tx, V32_TX_SILENCE);
    t->seg = HST_SEG_S1;
    t->left = t->len[0];
}

int hst_ftx_in_data(const hst_ftx_t *t) { return t->seg == HST_SEG_DATA; }
int hst_ftx_seg(const hst_ftx_t *t) { return t->seg; }

void hst_ftx_stop(hst_ftx_t *t)
{
    v32tx_set_mode(&t->tx, V32_TX_SILENCE);
    t->seg = HST_SEG_OFF;
    t->left = 0;
    t->marks = 0;
}

static int scr1(hst_ftx_t *t) { return v32_scramble(&t->tx.scr, t->tx.answer, 1); }

/* V.17 5.1.4: segment 4's differential encoder starts from the first
 * symbol of segment 3, i.e. from that point's Y1 Y2 in V.17's labelling,
 * the same at every rate: A 11, B 01, C 10, D 00 (Figures 2-5/V.17; our
 * 7200 set, v32_c7200, labels the four alike; our 9600 to 14400 sets are
 * V.32bis's, on another scale, without them). v32tx takes the start as a
 * state and uses its 4800 label (A 00, B 01, C 11, D 10), so the state
 * whose 4800 label is that Y1 Y2: A -> C, B -> B, C -> D, D -> A. At 4800
 * (no V.17 rate) the 4800 labelling is the rate's own: the state itself. */
static int ones_start(int rate, int s)
{
    static const uint8_t v17_y12_state[4] = { 2, 1, 3, 0 };
    return rate == 0 ? s : v17_y12_state[s & 3];
}

/* the mode, and what it needs, of the symbol about to begin */
static void next_symbol(hst_ftx_t *t)
{
    static const uint8_t v17_abcd[4] = { 2, 3, 1, 0 };   /* dibit 00 C, 01 D, 10 B, 11 A */
    int i, pos, q;
    if (t->seg == HST_SEG_OFF) return;
    while (t->seg != HST_SEG_DATA && t->left == 0) {
        t->seg++;
        if (t->seg != HST_SEG_DATA) t->left = t->len[t->seg - HST_SEG_S1];
    }
    if (t->seg == HST_SEG_DATA) {
        if (t->marks > 0) {
            /* ones, the rate changed half-way; set as the mode directly:
             * v32tx_set_mode from data would start its count, and with it
             * the encoders, again (from ones back to data it does not) */
            if (t->marks-- == HST_MARKS_SYMS / 2) t->code = t->mcode;
            t->tx.rate = t->code - 2;
            t->tx.mode = V32_TX_ONES;
            return;
        }
        t->tx.rate = t->code - 2;
        v32tx_set_mode(&t->tx, V32_TX_DATA);
        return;
    }
    i = t->seg - HST_SEG_S1;
    pos = t->len[i] - t->left--;
    switch (t->kind[i]) {
    case K_S:    v32tx_set_mode(&t->tx, V32_TX_S); break;
    case K_SBAR: v32tx_set_mode(&t->tx, V32_TX_SBAR); break;
    case K_TRN:
    case K_COND:
        if (t->kind[i] == K_COND && pos == 0) t->tx.scr.reg = HST_BM_SCR0;
        q = scr1(t) << 1;
        q |= scr1(t);
        t->tx.abcd = t->kind[i] == K_TRN ? v32_label_abcd[q] : v17_abcd[q];
        v32tx_set_mode(&t->tx, V32_TX_STATE);
        break;
    case K_BRIDGE:
        if (pos == 1) t->d0 = t->tx.abcd;     /* the state of the bridge's first symbol */
        t->tx.rword = BRIDGE_WORD;
        v32tx_set_mode(&t->tx, V32_TX_RATE);
        break;
    default:     /* K_ONES */
        if (pos == 0) t->tx.abcd = ones_start(t->code - 2, t->d0);
        t->tx.rate = t->code - 2;
        v32tx_set_mode(&t->tx, V32_TX_ONES);
        break;
    }
}

int16_t hst_ftx_tick(hst_ftx_t *t)
{
    if (v32tx_symbol_due(&t->tx)) next_symbol(t);
    return v32tx_tick(&t->tx);
}

void hst_gf_init(hst_gf_t *g)
{
    int i;
    for (i = 0; i < 2 * HST_GF_XN; i++) g->x[i] = 0;
    for (i = 0; i < 2 * HST_GUARD_IN; i++) g->t[i] = 0;
    g->pos = g->tpos = 0;
}

int16_t hst_gf_tick(hst_gf_t *g, int16_t in)
{
    enum { L = HST_GUARD_L, FN = HST_GUARD_FN, IN = HST_GUARD_IN, XN = HST_GF_XN };
    const int16_t *x;
    const int32_t *t;
    int64_t acc;
    int32_t y;
    int i;
    if (--g->pos < 0) g->pos = XN - 1;
    g->x[g->pos] = g->x[g->pos + XN] = in;
    x = &g->x[g->pos];                 /* x[i]: i samples ago, i < XN */
    /* F on every L-th sample, from the outside in, four pairs a pass (the
     * sum is exact: the order does not matter) */
    acc = (int64_t)hst_guard_f[FN / 2] * x[L * (FN / 2)];
    for (i = 0; i + 4 <= FN / 2; i += 4) {
        acc += (int64_t)hst_guard_f[i] * ((int32_t)x[L * i] + x[XN - 1 - L * i]);
        acc += (int64_t)hst_guard_f[i + 1] * ((int32_t)x[L * (i + 1)] + x[XN - 1 - L * (i + 1)]);
        acc += (int64_t)hst_guard_f[i + 2] * ((int32_t)x[L * (i + 2)] + x[XN - 1 - L * (i + 2)]);
        acc += (int64_t)hst_guard_f[i + 3] * ((int32_t)x[L * (i + 3)] + x[XN - 1 - L * (i + 3)]);
    }
    for (; i < FN / 2; i++)
        acc += (int64_t)hst_guard_f[i] * ((int32_t)x[L * i] + x[XN - 1 - L * i]);
    if (--g->tpos < 0) g->tpos = IN - 1;
    g->t[g->tpos] = g->t[g->tpos + IN] = (int32_t)(acc / 4);
    t = &g->t[g->tpos];
    /* I on F's sums, likewise */
    acc = (int64_t)hst_guard_i[IN / 2] * t[IN / 2];
    for (i = 0; i + 4 <= IN / 2; i += 4) {
        acc += (int64_t)hst_guard_i[i] * (t[i] + t[IN - 1 - i]);
        acc += (int64_t)hst_guard_i[i + 1] * (t[i + 1] + t[IN - 2 - i]);
        acc += (int64_t)hst_guard_i[i + 2] * (t[i + 2] + t[IN - 3 - i]);
        acc += (int64_t)hst_guard_i[i + 3] * (t[i + 3] + t[IN - 4 - i]);
    }
    for (; i < IN / 2; i++)
        acc += (int64_t)hst_guard_i[i] * (t[i] + t[IN - 1 - i]);
    /* the low-pass, Q28 to a sample, rounded; the delayed input less it */
    y = x[HST_GF_DELAY] - (int32_t)((acc + (1 << 27)) >> 28);
    return (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
}

/* ---------------- the forward receiver (hst.h) ---------------- */

#define U          HST_FRX_U
/* AB: each tone at least 1/16 of the power: (64 A / 2)^2 >= 128 x A^2 / 2 x 16 / 8;
 * both together at least 1/4 (AB on a flat line: 1/2; data, a few
 * percent: 14400 back after a cut once passed for AB on 1/16 each) */
#define AB_K       6               /* the tones' and the power's smoothing, 2^-6 a sample */
#define AB_SHARE   128             /* |tone x 64|^2 against the power: 1/16 of it */
#define AB_PW_MIN  ((int64_t)112 * 112)   /* -43 dBm0 (RMS 112) */
#define AB_RUN     (HST_FRX_AB_MS * 8)
#define DPH600     322122547u      /* 600 / 8000 of 2^32, set to 0 every 40 samples */
/* the carrier in training and data: low under -48 dBm0 (RMS 62) or 10 dB
 * under the signal's level (pref); back over -45 dBm0 (RMS 88) and within
 * 9 dB of it */
#define FCD_OFF     ((int64_t)62 * 62)
#define FCD_ON      ((int64_t)88 * 88)
#define FCD_CLAMP   (HST_FRX_CLAMP_MS * 8)
#define FCD_LOSS    (HST_FRX_LOSS_MS * 8)
/* The lock thresholds: data mode's error over 256 symbols (mlock) above
 * lock_mse[code] for HST_FRX_LOCK_SYMS is UNLOCKED. Each is 1 dB under the
 * noise floor of the rate's hard decisions (noise alone, decided to the
 * nearest point, gives a uniform error over the set's cell: s^2 / 6 against
 * the set's power, -4.8, -11.8, -14.8, -18.0, -20.9 dB at codes 2..6), so
 * the thresholds are -5.8, -12.8, -15.8, -19.0, -21.9 dB (a flat -10 dB
 * never fires at 14400). A receiver getting over a level step is well
 * under them; the error over 16 symbols, nearer, put some of them out */
static const int32_t lock_mse[7] = { 0, 0, 17238, 3439, 1724, 825, 423 };
/* decisions on the 4 points */
#define MU_FAST    9               /* qamrx_update's steps: training (8 diverges; at 10
                                    * a short S3 leaves too much error for 14400) */
#define MU_SLOW    12              /* ... tracking, once the error has held */
#define MU_DFAST   10              /* data mode before the hold (no QRX_TFAST) */
#define MU_DATA    13              /* ... and after it */
#define MSE_OK     655             /* the error (r->mse, 65536 = 0 dB) held HST_FRX_HOLD
                                    * symbols under this, -20 dB, ends the fast steps */
#define MSE_GATE   2609            /* -14 dB: before the hold, data only under this */
/* Off the 4 points: farther from the nearest than 4 times the error's RMS
 * (16 x the mse in units^2: mse x 40 x 16 / 65536 of U^2), and at least
 * 1.2 units, at most 2.5. Then half or more of every data rate's points are
 * off (7200 0.75, 9600 0.50, 12000 0.75, 14400 0.62 of them), and a
 * training point is off with odds of e^-16, or e^-3.6 at -20 dB */
#define OFF2_MIN   ((int64_t)U * U * 144 / 100)
#define OFF2_MAX   ((int64_t)U * U * 625 / 100)
#define OFF2_MSE   ((int64_t)U * U * 640 / 65536)
/* training: the spec's flags, QRX_TFAST while the steps are fast */
#define TRAIN_FLAGS(f) (QRX_EQ | QRX_CARRIER | QRX_TIMING | ((f) ? QRX_TFAST : 0))
#define TRN_RUN    8               /* symbols of ones out of a direct reading: TRN */
#define ALT_CLEAR  24              /* code 2: no S or S-bar for this many symbols before data */
/* the matched filter's gain, chosen so that A..D come out near HST_FRX_U
 * per unit after qamrx's AGC (measured: test_hst_fwd, acq_*) */
#define MF_GAIN    29
#define E_MAX      ((int64_t)1 << 22)
/* The data mode's gain loop, on qamrx's input (the equaliser, the carrier
 * and the timing then see the scale they were trained on), from the
 * points' power against the set's:
 * - over 32 symbols (its own spread some 7 % for 14400's set) a quarter or
 *   more (1 dB) off, or over 128 symbols (3.7 %) a sixth or more off, for
 *   G_WAIT symbols (27 ms; past the equaliser's 12 symbols), and within
 *   10 dB (a dropout is not; the clamp has it by then), is a level step:
 *   the gain is set at once by the root of the powers' ratio, and nothing
 *   measured for G_FLUSH symbols, while the equaliser's line still holds
 *   samples at the old gain (a loop overshot; and a 1 dB step at 14400,
 *   left to it, took longer than HST_FRX_LOCK_SYMS);
 * - while it is a quarter off, or the power over 8 symbols is 30 % or more
 *   off (the quicker sign), or flushing, nothing else is adapted: the
 *   decisions are wrong; fed them for the 30-odd symbols, the carrier loop
 *   was kicked a Hz off and stayed there;
 * - else the gain moves by half the difference (over 32), 2^-8 of it a
 *   symbol while it is a 16th or more, else 2^-10;
 * - and it leaks back to ga, the gain as last set (G_ONE from a training,
 *   else gain_set's), 1/32 of the way every 64 symbols (a time constant of
 *   2048 symbols, 0.85 s), and stays within 1 dB of it.
 * The leak and the bound: the gain and the equaliser's taps both set the
 * points' scale, and the two loops want different scales. The equaliser
 * (least squares on the decisions) leaves the points' power at the set's
 * less the error's (E|z|^2 = E|d|^2 - E|e|^2: its error is orthogonal to
 * its output); the loop pulls it to the set's. With nothing to fix the
 * scale, the gain climbed and the taps shrank by the same amount without
 * end, faster the noisier the line: at 7200 on a 19 dB line the gain went
 * +4.9 dB in 60 s, the input clipped and the lock went in about 2 min
 * (Task 22c, test_hst_fwd long_drift). Now the slow scale is the
 * equaliser's alone; the loop still takes a step at once (a 1 dB step at
 * 14400 that gain_set does not see is the loop's for the first 0.1 s or
 * so, while the equaliser follows) */
#define G_WAIT     64
#define G_FLUSH    16
#define G_LEAKN    64              /* every 64 symbols, 1/32 of the way back to ga */
#define G_LEAK     32
#define G_LO       7298            /* 2^13 x 10^(-1/20): the loop within 1 dB of ga */
#define G_HI       9192            /* 2^13 x 10^(1/20) */
#define G_ONE      16384
#define G_MIN      2048            /* -18 dB */
#define G_MAX      131072          /* +18 dB */

/* the sets' mean power in units^2 (v32_constel), by code */
static const uint8_t set_pw[7] = { 0, 0, 40, 40, 40, 42, 41 };

/* nearest of A B C D (V.32's decision regions, quadrants turned by 2 + j:
 * independent of scale) */
static int slice4(int32_t zr, int32_t zi)
{
    int64_t a = 2 * (int64_t)zr - zi, b = 2 * (int64_t)zi + zr;
    if (a >= 0) return b >= 0 ? 2 : 1;
    return b >= 0 ? 3 : 0;
}

/* The transmit pulse's matched filter, demodulating: tap k, (36 - k) T/3
 * before the on-time sample, is g(u) e^(-j w u) at u = (36 - k) T/3, which
 * hst_fpulse holds (up to a fixed turn) at (150 - 10 (36 - k) / 3) thirds
 * of a sample from its start (g is even); between the table's thirds,
 * linear */
static void mf_taps(int32_t *cr, int32_t *ci)
{
    int k;
    for (k = 0; k < QRX_NTAP; k++) {
        int n9 = 450 - (QRX_CENTER - k) * 10, n = n9 / 3, f = n9 % 3;
        const int16_t *a = hst_fpulse[n % 10][n / 10], *b = hst_fpulse[(n + 1) % 10][(n + 1) / 10];
        cr[k] = (a[0] * (3 - f) + b[0] * f) * MF_GAIN;
        ci[k] = (a[1] * (3 - f) + b[1] * f) * MF_GAIN;
    }
}

/* back to listening for AB, in state `why` */
static void listen(hst_frx_t *r, int why)
{
    if (r->sw_code) {                 /* armed: the acquisition at the new code */
        r->code = r->sw_code;
        r->sw_code = 0;
        r->sw_win = 0;
    }
    qamrx_stop(&r->q);                /* its ring and its power go on; no points */
    r->state = r->listen = why;
    r->abrun = 0;
    r->tr = r->ti = r->fr = r->fi = 0;
}

/* what a training starts from */
static void train_reset(hst_frx_t *r)
{
    int i;
    r->off = r->bad = 0;
    r->g = G_ONE; r->pz = r->pz8 = r->pzl = 0; r->gstep = r->gcnt = 0; r->ga = G_ONE;
    r->nsym = r->held = 0; r->fast = 1;
    r->offw = 0; r->sh = 0; r->alt = 0; r->mse = r->mse0 = 65536;
    for (i = 0; i < 8; i++) { r->hzr[i] = r->hzi[i] = 0; r->msh[i] = 65536; }
    for (i = 0; i < 4; i++) { r->dir[i].reg = 0; r->ones[i] = 0; }
    r->trn = -1;
    r->sig = r->err = 0;
}

void hst_frx_init(hst_frx_t *r)
{
    int i;
    hst_gf_init(&r->gf);
    qamrx_init(&r->q, 2400, 3, 4);
    v32_vit_init(&r->vit, 1);
    r->eq.valid = 0;
    r->code = HST_OPEN_CODE;
    r->sw_code = 0; r->sw_left = 0; r->sw_ones = 0; r->sw_win = 0; r->sw_run = 0;
    r->sw_dscr.reg = 0; r->sw_y = 0; r->sw_alt = 0; r->sw_sh = 0; r->sw_pos = 0;
    for (i = 0; i < HST_SW_RING; i++) r->sw_zr[i] = r->sw_zi[i] = 0;
    v32_vit_init(&r->vit2, 1);
    r->n40 = 0; r->ph6 = 0;
    r->lr = r->li = r->ur = r->ui = 0;
    r->pw = r->pref = 0;
    r->l0r = r->l0i = r->u0r = r->u0i = 0;
    listen(r, HST_FRX_IDLE);
    train_reset(r);
    r->dscr.reg = 0; r->yprev = 0;
    r->nbits = 0;
    for (i = 0; i < 8; i++) r->bits[i] = 0;
}

void hst_frx_set_code(hst_frx_t *r, int code)
{
    code = code < 2 ? 2 : code > 6 ? 6 : code;
    r->sw_code = 0;                   /* a switch armed is off */
    r->sw_win = 0;
    if (r->state == HST_FRX_DATA && code != r->code && code > 2) v32_vit_init(&r->vit, code - 2);
    r->code = code;
}

int hst_frx_state(const hst_frx_t *r) { return r->state; }

void hst_frx_store(const hst_frx_t *r, hst_eqstore_t *s)
{
    qamrx_get_eq(&r->q, s->cr, s->ci, &s->freq);
    s->valid = 1;
}

void hst_frx_restore(hst_frx_t *r, const hst_eqstore_t *s)
{
    r->eq = *s;
    r->eq.valid = s->valid != 0;
    listen(r, HST_FRX_IDLE);
    r->sig = r->err = 0;
}

int hst_frx_snr(const hst_frx_t *r, int64_t *num, int64_t *den)
{
    if (r->sig <= 0) return 0;
    *num = r->sig;
    *den = r->err;
    return 1;
}

/* the turn of (xr, xi), in 1024ths: the largest projection on sintab's */
static int turn1024(int64_t xr, int64_t xi)
{
    int64_t best = 0;
    int i, arg = 0;
    while (xr > ((int64_t)1 << 30) || xr < -((int64_t)1 << 30) || xi > ((int64_t)1 << 30) || xi < -((int64_t)1 << 30)) {
        xr /= 2; xi /= 2;
    }
    for (i = 0; i < SIN_N; i++) {
        int64_t p = xr * sintab[(i + SIN_N / 4) & SIN_MASK] + xi * sintab[i];
        if (i == 0 || p > best) { best = p; arg = i; }
    }
    return arg;
}

/* The line's carrier offset from AB: both tones turn by it, 40 samples
 * (a 200th of a second) at a time; a clock offset turns them oppositely,
 * and cancels in the sum. As qamrx's frequency: a turn of i/1024 in 40
 * samples is i x 200 / 1024 Hz, i x 2^32 / 12288 a symbol; +-100 Hz */
static int32_t ab_freq(const hst_frx_t *r)
{
    int i = turn1024(r->fr, r->fi);
    if (i >= SIN_N / 2) i -= SIN_N;
    return (int32_t)((int64_t)i * ((int64_t)1 << 32) / 12288);
}

/* the receiver starts: symbol timing from AB's tones, the equaliser */
static void start(hst_frx_t *r)
{
    int32_t cr[QRX_NTAP], ci[QRX_NTAP], delay;
    /* the turn of 600 x conj(3000) is 2400 Hz times the time of a symbol's
     * centre */
    int arg = turn1024(r->tr, r->ti);
    /* in 1/1536 sample: a symbol 5120, a centre at arg x 5 from a sample
     * whose n40 is 0; from the next sample, n40 (already moved on) */
    delay = (arg * 5 - r->n40 * 1536) % 5120;
    if (delay < 0) delay += 5120;
    if (r->eq.valid) qamrx_sync_eq(&r->q, delay / 6, r->eq.cr, r->eq.ci, r->eq.freq);
    else {
        mf_taps(cr, ci);
        qamrx_sync_eq(&r->q, delay / 6, cr, ci, ab_freq(r));
    }
    r->state = HST_FRX_TRAIN;
    train_reset(r);
    r->pref = r->pw;
}

/* AB: both band-edge tones, a sample (the power is the tick's); 1 when it
 * has been there AB_RUN samples */
static int ab_hear(hst_frx_t *r, int16_t y)
{
    int i3 = (r->n40 * 384) & SIN_MASK, i6 = (int)(r->ph6 >> 22);
    int32_t c6 = sintab[(i6 + SIN_N / 4) & SIN_MASK], s6 = sintab[i6];
    int32_t c3 = sintab[(i3 + SIN_N / 4) & SIN_MASK], s3 = sintab[i3];
    int64_t l2, u2;
    r->lr += ((y * c6) / 512 - r->lr) / (1 << AB_K);
    r->li += (-(y * s6) / 512 - r->li) / (1 << AB_K);
    r->ur += ((y * c3) / 512 - r->ur) / (1 << AB_K);
    r->ui += (-(y * s3) / 512 - r->ui) / (1 << AB_K);
    if (++r->n40 == 40) { r->n40 = 0; r->ph6 = 0; } else r->ph6 += DPH600;
    l2 = (int64_t)r->lr * r->lr + (int64_t)r->li * r->li;
    u2 = (int64_t)r->ur * r->ur + (int64_t)r->ui * r->ui;
    if (r->pw < AB_PW_MIN || l2 < AB_SHARE * r->pw || u2 < AB_SHARE * r->pw || l2 + u2 < 4 * AB_SHARE * r->pw) {
        r->abrun = 0;
        r->tr = r->ti = r->fr = r->fi = 0;
        r->state = r->listen;
        return 0;
    }
    r->tr += ((int64_t)r->lr * r->ur + (int64_t)r->li * r->ui) / 4096;
    r->ti += ((int64_t)r->li * r->ur - (int64_t)r->lr * r->ui) / 4096;
    if (r->n40 == 0) {
        if (r->abrun >= 40) {
            r->fr += ((int64_t)r->lr * r->l0r + (int64_t)r->li * r->l0i + (int64_t)r->ur * r->u0r + (int64_t)r->ui * r->u0i) / 4096;
            r->fi += ((int64_t)r->li * r->l0r - (int64_t)r->lr * r->l0i + (int64_t)r->ui * r->u0r - (int64_t)r->ur * r->u0i) / 4096;
        }
        r->l0r = r->lr; r->l0i = r->li; r->u0r = r->ur; r->u0i = r->ui;
    }
    r->state = HST_FRX_AB;
    return ++r->abrun >= AB_RUN;
}

/* TRN (Table 4's dibits of scrambled ones) read directly under each
 * quarter turn; trn the turn it is heard under. Only training finds it; it
 * ends at the first symbol that is not TRN */
static void trn_watch(hst_frx_t *r, int st)
{
    int k;
    for (k = 0; k < 4; k++) {
        int lab = v32_abcd_label[(st + k) & 3], b1, b0;
        b1 = v32_descramble(&r->dir[k], 0, lab >> 1);
        b0 = v32_descramble(&r->dir[k], 0, lab & 1);
        if (b1 && b0) { if (r->ones[k] < TRN_RUN) r->ones[k]++; }
        else r->ones[k] = 0;
    }
    if (r->trn >= 0 && r->ones[r->trn] == 0) r->trn = -1;
    if (r->trn < 0 && r->state == HST_FRX_TRAIN)
        for (k = 0; k < 4 && r->trn < 0; k++) if (r->ones[k] >= TRN_RUN) r->trn = k;
}

static void put_bit(hst_frx_t *r, int b)
{
    if (r->nbits < 8) r->bits[r->nbits++] = (uint8_t)b;
}

/* v32_decode_bits on r's memory */
static int decode(hst_frx_t *r, int rate, int label)
{
    int y = (int)r->yprev, q = v32_decode_bits(rate, label, &y);
    r->yprev = y;
    return q;
}

/* a label decided at code 3..6 */
static void data_bits(hst_frx_t *r, int label)
{
    int n = r->code, q = decode(r, n - 2, label), i;
    for (i = n - 1; i >= 0; i--) put_bit(r, v32_descramble(&r->dscr, 0, (q >> i) & 1));
}

/* the Viterbi decoder at the rate's set, on a point; its decision's point
 * to *dr, *di */
static void vit_point(hst_frx_t *r, int32_t zr, int32_t zi, int32_t *dr, int32_t *di)
{
    int hard, x, y, out = v32_vit_step(&r->vit, zr / 2, zi / 2, &hard);
    v32_point(r->code - 2, hard, &x, &y);
    *dr = x * U; *di = y * U;
    if (out >= 0) data_bits(r, out);
}

static void enter_data(hst_frx_t *r)
{
    int k, first = -1;
    r->state = HST_FRX_DATA;
    r->mlock = r->mse0;            /* the error before the data (the first data points, against A..D, are far off) */
    r->pz = r->pz8 = r->pzl = (int64_t)set_pw[r->code] * U * U;
    if (r->code == 2) return;
    /* the trellis decoder from the oldest of the last 8 points off the 4 */
    v32_vit_init(&r->vit, r->code - 2);
    for (k = 7; k >= 0 && first < 0; k--) if ((r->offw >> k) & 1) first = k;
    for (k = first; k >= 0; k--) {
        int32_t dr, di;
        vit_point(r, r->hzr[k], r->hzi[k], &dr, &di);
    }
}

static void power(hst_frx_t *r, int32_t dr, int32_t di)
{
    int32_t er = r->q.zr - dr, ei = r->q.zi - di;
    int64_t s = ((int64_t)dr * dr + (int64_t)di * di) / 256, e = ((int64_t)er * er + (int64_t)ei * ei) / 256;
    r->sig += (s - r->sig) / 64;
    r->err += (e - r->err) / 64;
}

/* The error against the point decided, smoothed over 16 symbols (from the
 * first symbol's: qamrx's own mse starts at 0 dB at qamrx_sync and takes
 * 64 symbols a step of e, which the matched filter has left long before);
 * against the set's mean power in data mode above 4800. The fast steps end
 * once it has held under MSE_OK; in data mode, the same over 256 symbols
 * (mlock) over lock_mse[code] for HST_FRX_LOCK_SYMS is the lock lost */
static void hold(hst_frx_t *r, int32_t dr, int32_t di)
{
    int64_t er = r->q.zr - dr, ei = r->q.zi - di, d2 = (int64_t)dr * dr + (int64_t)di * di, e2;
    if (r->state == HST_FRX_DATA && r->code > 2) d2 = (int64_t)set_pw[r->code] * U * U;
    /* past 2^22 (4096 units) the quotient is over the cap below anyway;
     * held there, the square shifted stays in int64 */
    er = er > E_MAX ? E_MAX : er < -E_MAX ? -E_MAX : er;
    ei = ei > E_MAX ? E_MAX : ei < -E_MAX ? -E_MAX : ei;
    e2 = d2 > 0 ? ((er * er + ei * ei) << 16) / d2 : 4 << 16;
    if (e2 > 4 << 16) e2 = 4 << 16;
    r->mse = r->nsym <= 1 ? (int32_t)e2 : r->mse + (int32_t)((e2 - r->mse) / 16);
    if (r->mse < MSE_OK) { if (r->held < HST_FRX_HOLD) r->held++; }
    else r->held = 0;
    if (r->held >= HST_FRX_HOLD) r->fast = 0;
    if (r->state == HST_FRX_DATA) {
        r->mlock += (int32_t)((e2 - r->mlock) / 256);
        if (r->mlock <= lock_mse[r->code]) r->bad = 0;
        else r->bad++;                /* the gain loop's freezes too: one that does not end
                                       * (the power over 10 dB down) is a loss */
    }
}

/* x / (2 pt), pt = set_pw[code] U^2 = set_pw x 2^20, exactly as the 64-bit
 * division would give it (two truncating divisions by positive numbers
 * are one), with no 64-bit division by a variable: x / 2^21 (here |x| <
 * 2^47, the quotient under 2^26), then by the set's power as a constant */
typedef char u_is_2_10[(U == 1024) ? 1 : -1];
static int32_t div_2pt(int64_t x, int code)
{
    int32_t y = (int32_t)(x / ((int64_t)1 << 21));
    switch (set_pw[code]) {
    case 42: return y / 42;
    case 41: return y / 41;
    default: return y / 40;
    }
}

/* the gain set at once by the root of pt / pz, Q14 (pt < 2^26: the shift fits) */
static void gain_set(hst_frx_t *r, int64_t pt, int64_t pz)
{
    int64_t q = isqrt_s64((pt << 28) / pz), g = (int64_t)r->g * q / 16384;
    r->g = r->ga = (int32_t)(g < G_MIN ? G_MIN : g > G_MAX ? G_MAX : g);
    r->pz = r->pz8 = r->pzl = pt;
    r->gcnt = -G_FLUSH;
    r->pref = r->pw;                  /* the signal's level is the new one */
}

/* the data mode's gain loop, on a point */
static void gain(hst_frx_t *r)
{
    int64_t pt = (int64_t)set_pw[r->code] * U * U, p, d, dl;
    int32_t lo, hi;
    int step, small;
    p = (int64_t)r->q.zr * r->q.zr + (int64_t)r->q.zi * r->q.zi;
    if (p > 16 * pt) p = 16 * pt;
    if (r->gcnt < 0) {                /* the equaliser's line still holds the old gain */
        r->gcnt++;
        r->gstep = 1;
        return;
    }
    r->pz += (p - r->pz) / 32;
    r->pz8 += (p - r->pz8) / 8;
    r->pzl += (p - r->pzl) / 128;
    d = pt - r->pz;
    dl = pt - r->pzl;
    step = d > pt / 4 || d < -pt / 4;
    small = !step && (dl > pt / 6 || dl < -pt / 6);
    r->gstep = step || r->pz8 * 10 < pt * 7 || r->pz8 * 10 > pt * 13;
    if (step || small) {
        if (++r->gcnt >= G_WAIT && r->pz > pt / 10 && r->pz < 10 * pt)
            gain_set(r, pt, step ? r->pz : r->pzl);
        if (step) return;
    } else r->gcnt = 0;
    {
        /* g d / 2 pt, then 2^-8 or 2^-10 */
        int32_t q = div_2pt(r->g * d, r->code);
        r->g += d > pt / 16 || d < -pt / 16 ? q / 256 : q / 1024;
    }
    if ((r->nsym & (G_LEAKN - 1)) == 0) r->g += (r->ga - r->g) / G_LEAK;
    lo = (int32_t)((int64_t)r->ga * G_LO >> 13);
    hi = (int32_t)((int64_t)r->ga * G_HI >> 13);
    lo = lo < G_MIN ? G_MIN : lo;
    hi = hi > G_MAX ? G_MAX : hi;
    r->g = r->g < lo ? lo : r->g > hi ? hi : r->g;
}

/* qamrx's update with nothing adapted but the clock: its own point as the
 * decision, a zero error, so that the timing loop's integrator still
 * corrects the clock (flags 0 stopped it: a 100 ms dropout at 9600 with
 * 100 ppm then lost lock) */
static void clock_only(hst_frx_t *r)
{
    qamrx_update(&r->q, r->q.zr, r->q.zi, QRX_TIMING, MU_DATA);
}

static int  sw_training(hst_frx_t *r, int32_t zr, int32_t zi);
static int  sw_symbol(hst_frx_t *r, int32_t zr, int32_t zi, int32_t *dr, int32_t *di);

static void symbol(hst_frx_t *r)
{
    int32_t zr = r->q.zr, zi = r->q.zi, dr = 0, di = 0;
    int k, st = 0, x, y;
    if (r->sw_code && --r->sw_left <= 0) {
        /* armed 2 s without a switch: the generic acquisition */
        hst_frx_store(r, &r->eq);
        listen(r, HST_FRX_UNLOCKED);
        return;
    }
    if (r->off >= FCD_CLAMP || r->pw * 10 < r->pref) {
        /* no carrier (yet): nothing decided, nothing adapted, no bits; the
         * carrier's phase moves on, and the clock's correction (a zero
         * error: its integrator alone); the level taken as it was */
        clock_only(r);
        if (r->state == HST_FRX_DATA) r->pz = r->pz8 = r->pzl = (int64_t)set_pw[r->code] * U * U;
        r->gcnt = 0;
        return;
    }
    for (k = 7; k > 0; k--) { r->hzr[k] = r->hzr[k - 1]; r->hzi[k] = r->hzi[k - 1]; }
    r->hzr[0] = zr; r->hzi[0] = zi;
    if (r->nsym < (1 << 30)) r->nsym++;
    if (r->state == HST_FRX_TRAIN || r->code == 2) {
        st = slice4(zr, zi);
        v32_state_point(st, &x, &y);
        dr = x * U; di = y * U;
        trn_watch(r, st);
    }
    if (r->state == HST_FRX_TRAIN) {
        int64_t er = zr - dr, ei = zi - di, thr = r->mse0 * OFF2_MSE;
        uint32_t w;
        int off, n;
        thr = thr < OFF2_MIN ? OFF2_MIN : thr > OFF2_MAX ? OFF2_MAX : thr;
        off = er * er + ei * ei > thr;
        r->offw = ((r->offw << 1) | (uint32_t)off) & 0xFFu;
        /* the error before the last 8 points, while none of them is off,
         * then held: so that data's first points, which raise the error,
         * do not shut the gate before 4 of them are counted (on a 20 dB
         * line the error is near -19 dB, and the hold never comes). Taken
         * from before the window (the error over 16 symbols 8 back does
         * not overlap it): r->mse itself, taken then, was biased low on a
         * poor line, and data mode was declared at 6 dB */
        if (r->offw == 0) r->mse0 = r->msh[r->nsym & 7];
        r->msh[r->nsym & 7] = r->mse;
        r->sh = ((r->sh << 2) | (uint32_t)st) & 0xFFFFu;
        if (r->nsym >= 8 && ((r->sh ^ (r->sh >> 4)) & 0x0FFFu) == 0 && (r->sh & 3u) != ((r->sh >> 2) & 3u)) r->alt = 0;
        else if (r->alt < (1 << 30)) r->alt++;
        /* S and S-bar have power at 600, 1800 and 3000 Hz alone: the
         * equaliser, adapted on them, would wander everywhere else. A point
         * off the 4 is likely data's, and its decision wrong: nothing is
         * adapted on it (the carrier's phase moves on) */
        if (off) clock_only(r);
        else qamrx_update(&r->q, dr, di, TRAIN_FLAGS(r->fast) & (r->alt > 0 && r->nsym > 8 ? ~0 : ~QRX_EQ),
                          r->fast ? MU_FAST : MU_SLOW);
        power(r, dr, di);
        hold(r, dr, di);
        for (n = 0, w = r->offw; w; w &= w - 1) n++;
        if (r->nsym >= 32 && (!r->fast || r->mse0 < MSE_GATE) && (r->code == 2 ? r->alt >= ALT_CLEAR : n > 3))
            enter_data(r);
        else if (r->nsym >= HST_FRX_TRAIN_SYMS) {
            r->eq.valid = 0;
            listen(r, HST_FRX_TRAINFAIL);
        }
        return;
    }
    /* data */
    if (r->sw_code && sw_training(r, zr, zi)) {
        /* armed, S or S-bar: a training instead; from the equaliser as it is */
        hst_frx_store(r, &r->eq);
        listen(r, HST_FRX_IDLE);
        return;
    }
    if (r->code == 2) {
        int q = decode(r, 0, v32_abcd_label[st]);
        if (r->trn >= 0) {
            /* TRN: ones, and the descrambler as TRN leaves it */
            put_bit(r, 1); put_bit(r, 1);
            r->dscr.reg = r->dir[r->trn].reg;
        } else {
            put_bit(r, v32_descramble(&r->dscr, 0, (q >> 1) & 1));
            put_bit(r, v32_descramble(&r->dscr, 0, q & 1));
        }
        if (r->gstep) clock_only(r);
        else qamrx_update(&r->q, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING, r->fast ? MU_DFAST : MU_DATA);
    } else {
        if (!r->sw_code) vit_point(r, zr, zi, &dr, &di);
        else if (sw_symbol(r, zr, zi, &dr, &di)) {
            /* both codes decoded: the decision not known, nothing adapted */
            clock_only(r);
            return;
        }
        if (r->gstep) clock_only(r);
        else qamrx_update(&r->q, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING | QRX_DENSE, r->fast ? MU_DFAST : MU_DATA);
    }
    power(r, dr, di);
    hold(r, dr, di);
    gain(r);
    if (r->bad >= HST_FRX_LOCK_SYMS) {
        /* armed, the likelier cause is the far end's new code: the
         * equaliser is kept for the acquisition */
        if (r->sw_code) hst_frx_store(r, &r->eq);
        else r->eq.valid = 0;
        r->nbits = 0;                 /* nothing from the symbol that lost it */
        listen(r, HST_FRX_UNLOCKED);
    }
}

int hst_frx_tick(hst_frx_t *r, int16_t x)
{
    int16_t y = hst_gf_tick(&r->gf, x);
    int32_t v;
    r->nbits = 0;
    r->pw += ((int64_t)y * y - r->pw) / (1 << AB_K);
    if (r->state != HST_FRX_TRAIN && r->state != HST_FRX_DATA) {
        (void)qamrx_tick(&r->q, y);    /* stopped: its ring and its power only */
        if (ab_hear(r, y)) start(r);
        return 0;
    }
    /* the carrier: a dropout clamps; a loss is NOCARRIER, its equaliser
     * kept. Once counting, the count goes on until the power is back (a
     * level between the two would otherwise clamp for ever) */
    if (r->pw < FCD_OFF || r->pw * 10 < r->pref || (r->off > 0 && (r->pw <= FCD_ON || r->pw * 8 <= r->pref))) {
        if (++r->off >= FCD_LOSS) {
            hst_frx_store(r, &r->eq);
            listen(r, HST_FRX_NOCARRIER);
            return 0;
        }
    } else {
        r->off = 0;
        /* the level follows the signal while the receiver is well, within 3 dB */
        if (!r->gstep && r->pw * 2 > r->pref && r->pw < 2 * r->pref) r->pref += (r->pw - r->pref) / 4096;
    }
    /* rounded (an arithmetic shift, DESIGN.md): y itself at G_ONE */
    v = (int32_t)(((int64_t)y * r->g + (1 << 13)) >> 14);
    v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    if (qamrx_tick(&r->q, (int16_t)v)) symbol(r);
    return r->nbits;
}

/* ---------------- a switch by marks (hst.h) ---------------- */

void hst_ftx_marks_switch(hst_ftx_t *t, int new_code)
{
    new_code = new_code < 2 ? 2 : new_code > 6 ? 6 : new_code;
    if (t->seg != HST_SEG_DATA) t->code = new_code;
    else if (t->code == 2 || new_code == 2) hst_ftx_train(t, new_code, 0);
    else {
        t->mcode = new_code;
        t->marks = HST_MARKS_SYMS;
    }
}
int hst_ftx_marking(const hst_ftx_t *t) { return t->marks; }
/* make hst-thresh (docs/limits.md): BER 1e-5 at 13.5, 16.5, 21.0, 23.5 dB
 * of SNR at codes 3..6; the receiver's error 3 dB above, -17.76, -20.54,
 * -23.78, -24.13 dB (65536 = 0 dB) */
const int32_t hst_good_mse[7] = { 0, 0, 0, 1098, 579, 274, 253 };
int32_t hst_frx_mse(const hst_frx_t *r) { return r->state == HST_FRX_DATA ? r->mlock : 4 << 16; }
/* Armed, S or S-bar (V.32's, ours, V.17's segment 1): HST_SW_ALT symbols
 * in a row each within 1.2 units of A..D, alternating A B or C D */
static int sw_training(hst_frx_t *r, int32_t zr, int32_t zi)
{
    int st = slice4(zr, zi), x, y, p1 = (int)(r->sw_sh & 3u), p2 = (int)((r->sw_sh >> 2) & 3u);
    int64_t er, ei;
    v32_state_point(st, &x, &y);
    er = zr - (int64_t)x * U; ei = zi - (int64_t)y * U;
    if (er * er + ei * ei <= OFF2_MIN && st == p2 && (st ^ p1) == 1) r->sw_alt++;
    else r->sw_alt = 0;
    r->sw_sh = ((r->sw_sh << 2) | (uint32_t)st) & 0xFu;
    return r->sw_alt >= HST_SW_ALT;
}

/* a label of the new code's decoder: its bits, ones or not */
static void sw_new(hst_frx_t *r, int label)
{
    int n = r->sw_code, y = (int)r->sw_y, q, i, ones = 1;
    if (label < 0) return;
    q = v32_decode_bits(n - 2, label, &y);
    r->sw_y = y;
    for (i = n - 1; i >= 0; i--) ones &= v32_descramble(&r->sw_dscr, 0, (q >> i) & 1);
    r->sw_run = ones ? r->sw_run + 1 : 0;
}

/* The ones at the code in force have ended, after HST_SW_ONES or more,
 * at the label just decoded (the point V32_VDEPTH - 1 before this one):
 * the new code's decoder from there, on the points kept (that one to this
 * one), every state alike; its descrambler and differential memory as the
 * last of the ones left the code in force's: the transmitter's encoders
 * run on through the change */
static void sw_start(hst_frx_t *r, const v32_scr_t *d0, int y0)
{
    int k, out = -1;
    r->sw_dscr = *d0;
    r->sw_y = y0;
    v32_vit_init(&r->vit2, r->sw_code - 2);
    for (k = 0; k < 8; k++) r->vit2.pm[k] = 0;
    for (k = V32_VDEPTH - 1; k >= 0; k--) {
        int j = (r->sw_pos - 1 - k) & (HST_SW_RING - 1);
        out = v32_vit_step(&r->vit2, r->sw_zr[j] / 2, r->sw_zi[j] / 2, NULL);
    }
    r->sw_win = HST_SW_WIN;
    r->sw_run = 0;
    sw_new(r, out);
}

/* a label decided at the code in force, armed: its bits given unless both
 * codes are being decoded; the ones counted, and where they end after
 * HST_SW_ONES, the new code's decoder started */
static void sw_watch(hst_frx_t *r, int label)
{
    v32_scr_t d0 = r->dscr;
    int y0 = (int)r->yprev, n0 = (int)r->nbits, ones = 1, k;
    data_bits(r, label);
    for (k = n0; k < r->nbits; k++) ones &= r->bits[k];
    if (r->sw_win) { r->nbits = n0; return; }
    if (ones) {
        if (r->sw_ones < HST_SW_IDLE) r->sw_ones++;
        return;
    }
    if (r->sw_ones >= HST_SW_ONES && r->code > 2 && r->sw_code > 2) {
        r->nbits = n0;
        sw_start(r, &d0, y0);
    }
    r->sw_ones = 0;
}

/* A data symbol at code 3..6, armed; 1 when the decision (*dr, *di) is
 * not to be adapted on: while both codes are decoded, at the switch, and
 * from HST_SW_HOLD symbols of ones decoded (the points coming in then are
 * HST_SW_HOLD + V32_VDEPTH - 1 into the ones, and the new code's come from
 * HST_MARKS_SYMS / 2 on, its decisions wrong at the code in force: fed
 * them for 20-odd symbols, the carrier loop was kicked off and the data
 * after the marks lost for 200 symbols) to HST_SW_IDLE (ones going on that
 * long are an idle line, not marks) */
static int sw_symbol(hst_frx_t *r, int32_t zr, int32_t zi, int32_t *dr, int32_t *di)
{
    int hard, x, y, out, win = r->sw_win != 0;
    r->sw_zr[r->sw_pos] = zr; r->sw_zi[r->sw_pos] = zi;
    r->sw_pos = (r->sw_pos + 1) & (HST_SW_RING - 1);
    out = v32_vit_step(&r->vit, zr / 2, zi / 2, &hard);
    v32_point(r->code - 2, hard, &x, &y);
    *dr = x * U; *di = y * U;
    if (out >= 0) sw_watch(r, out);
    if (!win) return r->sw_win != 0 || (r->sw_ones >= HST_SW_HOLD && r->sw_ones < HST_SW_IDLE);
    sw_new(r, v32_vit_step(&r->vit2, zr / 2, zi / 2, NULL));
    if (r->sw_run >= HST_SW_RUN) {
        /* the switch: the new code's decoders are the receiver's */
        r->vit = r->vit2;
        r->dscr = r->sw_dscr;
        r->yprev = r->sw_y;
        r->code = r->sw_code;
        r->sw_code = 0;
        r->sw_win = 0;
    } else if (--r->sw_win == 0) r->sw_ones = 0;   /* none: the code in force goes on */
    return 1;
}

void hst_frx_arm_switch(hst_frx_t *r, int new_code)
{
    new_code = new_code < 2 ? 2 : new_code > 6 ? 6 : new_code;
    if (r->state != HST_FRX_DATA) {
        hst_frx_set_code(r, new_code);
        return;
    }
    if (new_code == r->code) { r->sw_code = 0; r->sw_win = 0; return; }
    r->sw_code = new_code;
    r->sw_left = HST_SW_SYMS;
    r->sw_ones = r->sw_win = r->sw_run = 0;
    r->sw_alt = 0; r->sw_sh = 0;
}
int hst_frx_armed(const hst_frx_t *r) { return r->sw_code; }
