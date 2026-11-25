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
#include "hst.h"
#include "sintab.h"
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
    for (i = 0; i < 2 * HST_GUARD_N; i++) g->x[i] = 0;
    g->pos = 0;
}

int16_t hst_gf_tick(hst_gf_t *g, int16_t in)
{
    const int16_t *x;
    int64_t acc;
    int i;
    if (--g->pos < 0) g->pos = HST_GUARD_N - 1;
    g->x[g->pos] = g->x[g->pos + HST_GUARD_N] = in;
    x = &g->x[g->pos];                 /* x[i]: i samples ago, i < HST_GUARD_N */
    acc = (int64_t)hst_guard[HST_GUARD_N / 2] * x[HST_GUARD_N / 2];
    for (i = 0; i < HST_GUARD_N / 2; i++)
        acc += (int64_t)hst_guard[i] * ((int32_t)x[i] + x[HST_GUARD_N - 1 - i]);
    acc = (acc + (1 << 14)) >> 15;
    return (int16_t)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
}

/* ---------------- the forward receiver (hst.h) ---------------- */

#define U          HST_FRX_U
/* AB: each tone at least 1/16 of the power: (64 A / 2)^2 >= 128 x A^2 / 2 x 16 / 8 */
#define AB_K       6               /* the tones' and the power's smoothing, 2^-6 a sample */
#define AB_SHARE   128             /* |tone x 64|^2 against the power: 1/16 of it */
#define AB_PW_MIN  ((int64_t)112 * 112)   /* -43 dBm0 (RMS 112) */
#define AB_RUN     (HST_FRX_AB_MS * 8)
#define DPH600     322122547u      /* 600 / 8000 of 2^32, set to 0 every 40 samples */
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

/* nearest of A B C D (V.32's decision regions, quadrants turned by 2 + j:
 * independent of scale) */
static int slice4(int32_t zr, int32_t zi)
{
    int64_t a = 2 * (int64_t)zr - zi, b = 2 * (int64_t)zi + zr;
    if (a >= 0) return b >= 0 ? 2 : 1;
    return b >= 0 ? 3 : 0;
}

/* q's taps the transmit pulse's matched filter, demodulating: tap k, (36 -
 * k) T/3 before the on-time sample, is g(u) e^(-j w u) at u = (36 - k) T/3,
 * which hst_fpulse holds (up to a fixed turn) at (150 - 10 (36 - k) / 3)
 * thirds of a sample from its start (g is even); between the table's thirds,
 * linear */
static void mf_taps(qamrx_t *q)
{
    int k;
    for (k = 0; k < QRX_NTAP; k++) {
        int n9 = 450 - (QRX_CENTER - k) * 10, n = n9 / 3, f = n9 % 3;
        const int16_t *a = hst_fpulse[n % 10][n / 10], *b = hst_fpulse[(n + 1) % 10][(n + 1) / 10];
        q->cr[k] = (a[0] * (3 - f) + b[0] * f) * MF_GAIN;
        q->ci[k] = (a[1] * (3 - f) + b[1] * f) * MF_GAIN;
    }
}

void hst_frx_init(hst_frx_t *r)
{
    int i;
    hst_gf_init(&r->gf);
    qamrx_init(&r->q, 2400, 3, 4);
    v32_vit_init(&r->vit, 1);
    r->state = HST_FRX_IDLE;
    r->code = HST_OPEN_CODE;
    r->stored = 0;
    r->n40 = 0; r->ph6 = 0;
    r->lr = r->li = r->ur = r->ui = 0;
    r->pw = r->tr = r->ti = r->fr = r->fi = 0;
    r->l0r = r->l0i = r->u0r = r->u0i = 0;
    r->abrun = 0;
    r->nsym = r->held = 0; r->fast = 1;
    r->offw = 0; r->sh = 0; r->alt = 0; r->mse = r->mse0 = 65536;
    for (i = 0; i < 8; i++) r->hzr[i] = r->hzi[i] = 0;
    for (i = 0; i < 4; i++) { r->dir[i].reg = 0; r->ones[i] = 0; }
    r->trn = -1;
    r->dscr.reg = 0; r->yprev = 0;
    r->sig = r->err = 0;
    r->nbits = 0;
    for (i = 0; i < 8; i++) r->bits[i] = 0;
}

void hst_frx_set_code(hst_frx_t *r, int code)
{
    code = code < 2 ? 2 : code > 6 ? 6 : code;
    if (r->state == HST_FRX_DATA && code != r->code && code > 2) v32_vit_init(&r->vit, code - 2);
    r->code = code;
}

int hst_frx_state(const hst_frx_t *r) { return r->state; }

void hst_frx_store(const hst_frx_t *r, hst_eqstore_t *s)
{
    int k;
    for (k = 0; k < QRX_NTAP; k++) { s->cr[k] = r->q.cr[k]; s->ci[k] = r->q.ci[k]; }
    s->freq = r->q.freq;
    s->valid = 1;
}

void hst_frx_restore(hst_frx_t *r, const hst_eqstore_t *s)
{
    int k;
    r->q.running = 0;                 /* the ring and the power go on; no symbols */
    r->state = HST_FRX_IDLE;
    r->abrun = 0;
    r->tr = r->ti = r->fr = r->fi = 0;
    r->stored = s->valid != 0;
    if (r->stored) {
        for (k = 0; k < QRX_NTAP; k++) { r->q.cr[k] = s->cr[k]; r->q.ci[k] = s->ci[k]; }
        r->q.freq = s->freq;
    }
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
    int32_t cr[QRX_NTAP], ci[QRX_NTAP], freq = r->q.freq, delay;
    int k, i;
    /* the turn of 600 x conj(3000) is 2400 Hz times the time of a symbol's
     * centre */
    int arg = turn1024(r->tr, r->ti);
    /* in 1/1536 sample: a symbol 5120, a centre at arg x 5 from a sample
     * whose n40 is 0; from the next sample, n40 (already moved on) */
    delay = (arg * 5 - r->n40 * 1536) % 5120;
    if (delay < 0) delay += 5120;
    if (r->stored)
        for (k = 0; k < QRX_NTAP; k++) { cr[k] = r->q.cr[k]; ci[k] = r->q.ci[k]; }
    qamrx_sync(&r->q, delay / 6);
    if (r->stored) {
        for (k = 0; k < QRX_NTAP; k++) { r->q.cr[k] = cr[k]; r->q.ci[k] = ci[k]; }
        r->q.freq = freq;
    } else {
        mf_taps(&r->q);
        r->q.freq = ab_freq(r);
    }
    r->state = HST_FRX_TRAIN;
    r->nsym = r->held = 0; r->fast = 1;
    r->offw = 0; r->sh = 0; r->alt = 0; r->mse = r->mse0 = 65536;
    for (i = 0; i < 4; i++) { r->dir[i].reg = 0; r->ones[i] = 0; }
    r->trn = -1;
    r->sig = r->err = 0;
}

/* AB: both band-edge tones and the power, a sample; 1 when it has been
 * there AB_RUN samples */
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
    r->pw += ((int64_t)y * y - r->pw) / (1 << AB_K);
    if (++r->n40 == 40) { r->n40 = 0; r->ph6 = 0; } else r->ph6 += DPH600;
    l2 = (int64_t)r->lr * r->lr + (int64_t)r->li * r->li;
    u2 = (int64_t)r->ur * r->ur + (int64_t)r->ui * r->ui;
    if (r->pw < AB_PW_MIN || l2 < AB_SHARE * r->pw || u2 < AB_SHARE * r->pw) {
        r->abrun = 0;
        r->tr = r->ti = r->fr = r->fi = 0;
        r->state = HST_FRX_IDLE;
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
 * against the set's mean power in data mode above 4800;
 * the fast steps end once it has held under MSE_OK */
static void hold(hst_frx_t *r, int32_t dr, int32_t di)
{
    int64_t er = r->q.zr - dr, ei = r->q.zi - di, d2 = (int64_t)dr * dr + (int64_t)di * di, e2;
    if (r->state == HST_FRX_DATA && r->code > 2) d2 = (int64_t)40 * U * U;     /* the sets' mean power */
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
}

static void symbol(hst_frx_t *r)
{
    int32_t zr = r->q.zr, zi = r->q.zi, dr = 0, di = 0;
    int k, st = 0, x, y;
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
        if (r->offw == 0) r->mse0 = r->mse;
        r->sh = ((r->sh << 2) | (uint32_t)st) & 0xFFFFu;
        if (r->nsym >= 8 && ((r->sh ^ (r->sh >> 4)) & 0x0FFFu) == 0 && (r->sh & 3u) != ((r->sh >> 2) & 3u)) r->alt = 0;
        else if (r->alt < (1 << 30)) r->alt++;
        /* S and S-bar have power at 600, 1800 and 3000 Hz alone: the
         * equaliser, adapted on them, would wander everywhere else. A point
         * off the 4 is likely data's, and its decision wrong: nothing is
         * adapted on it (the carrier's phase moves on) */
        qamrx_update(&r->q, dr, di, off ? 0 : TRAIN_FLAGS(r->fast) & (r->alt > 0 && r->nsym > 8 ? ~0 : ~QRX_EQ),
                     r->fast ? MU_FAST : MU_SLOW);
        power(r, dr, di);
        hold(r, dr, di);
        for (n = 0, w = r->offw; w; w &= w - 1) n++;
        if (r->nsym >= 32 && (!r->fast || r->mse0 < MSE_GATE) && (r->code == 2 ? r->alt >= ALT_CLEAR : n > 3))
            enter_data(r);
        return;
    }
    /* data */
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
        qamrx_update(&r->q, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING, r->fast ? MU_DFAST : MU_DATA);
    } else {
        vit_point(r, zr, zi, &dr, &di);
        qamrx_update(&r->q, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING | QRX_DENSE, r->fast ? MU_DFAST : MU_DATA);
    }
    power(r, dr, di);
    hold(r, dr, di);
}

int hst_frx_tick(hst_frx_t *r, int16_t x)
{
    int16_t y = hst_gf_tick(&r->gf, x);
    r->nbits = 0;
    if (r->state == HST_FRX_IDLE || r->state == HST_FRX_AB) {
        (void)qamrx_tick(&r->q, y);    /* not running: its ring and its power only */
        if (ab_hear(r, y)) start(r);
        return 0;
    }
    if (qamrx_tick(&r->q, y)) symbol(r);
    return r->nbits;
}
