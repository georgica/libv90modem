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
