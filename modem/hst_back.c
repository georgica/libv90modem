/* HST's back channel: the 8-DPSK transmitter (hst.h; hst-facts.md, "Back
 * channel").
 *
 * Each speed has a pulse train (hst_blane_t): unit points at 22.5 + 45 k
 * degrees, times the gain for the line level, shaped by the polyphase
 * root-raised-cosine pulse of hst_tabs (HST_BL symbols, HST_Bxxx_P phases a
 * symbol, HST_Bxxx_STEP phases a sample), then moved to its carrier. The
 * speed being sent clocks its train; the other one, after a change of
 * speed, takes zeros until its last symbols have left the pulse. Both
 * carriers run from the first tick, so each is a fixed function of time. */
#include "hst.h"
#include "sintab.h"

/* per lane: carrier, baud, pulse phases a symbol, phases a sample, the pulse */
static const struct {
    int fc, baud, P, step;
    const int16_t *pulse;
} lanes[2] = {
    { 375, 150, HST_B450_P, HST_B450_STEP, &hst_bpulse450[0][0] },
    { 350, 100, HST_B300_P, HST_B300_STEP, &hst_bpulse300[0][0] },
};
static const int rms_q12[2] = { HST_B450_RMS_Q12, HST_B300_RMS_Q12 };

static int lane_of(int bps) { return bps == 300 ? 1 : 0; }

/* Q15 sine of a 32-bit phase, interpolated between sintab's entries */
static int32_t sin_q15(uint32_t ph)
{
    int32_t s0 = sintab[(ph >> 22) & SIN_MASK], s1 = sintab[((ph >> 22) + 1) & SIN_MASK];
    return s0 + (((s1 - s0) * (int32_t)((ph >> 7) & 0x7FFF)) >> 15);
}

void hst_btx_init(hst_btx_t *t, int bps, int level, v32_getbit_fn getbit, void *user)
{
    int l, j;
    for (l = 0; l < 2; l++) {
        hst_blane_t *L = &t->lane[l];
        for (j = 0; j < HST_BL; j++) L->si[j] = L->sq[j] = 0;
        L->acc = lanes[l].P;
        L->left = 0;
        L->cph = 0;
        L->dph = (uint32_t)(((uint64_t)(uint32_t)lanes[l].fc << 32) / 8000u);
        L->gain = (int32_t)((int64_t)(level > 0 ? level : 0) * 4096 / rms_q12[l]);
    }
    t->bps = t->want = bps == 300 ? 300 : 450;
    t->mode = HST_BTX_SILENT;
    t->phase = 0;
    t->npre = 0;
    t->newsym = 0;
    scr_init(&t->scr);
    t->getbit = getbit; t->user = user;
}

/* from the next symbol of the active lane, the speed asked for */
static void take_speed(hst_btx_t *t)
{
    hst_blane_t *N;
    if (t->want == t->bps) return;
    t->bps = t->want;
    N = &t->lane[lane_of(t->bps)];
    if (N->left == 0) N->acc = lanes[lane_of(t->bps)].P;     /* idle: a symbol at once */
}

void hst_btx_preamble(hst_btx_t *t)
{
    hst_blane_t *L;
    take_speed(t);
    L = &t->lane[lane_of(t->bps)];
    if (L->left == 0) L->acc = lanes[lane_of(t->bps)].P;
    t->mode = HST_BTX_PREAMBLE;
    t->npre = HST_BTX_PRE_MS * lanes[lane_of(t->bps)].baud / 1000;
    t->phase = 0;
    scr_init(&t->scr);
}

void hst_btx_set_speed(hst_btx_t *t, int bps) { t->want = bps == 300 ? 300 : 450; }

void hst_btx_stop(hst_btx_t *t) { t->mode = HST_BTX_SILENT; }

/* the next symbol of the speed being sent: returns 0 (and a zero symbol)
 * when silent, 1 when a symbol is sent */
static int next_symbol(hst_btx_t *t, int32_t *si, int32_t *sq)
{
    int b, tri = 0, idx;
    int32_t g = t->lane[lane_of(t->bps)].gain;
    if (t->mode == HST_BTX_SILENT) { *si = *sq = 0; return 0; }
    for (b = 0; b < 3; b++) {                       /* the tribit's LSB first */
        int di = t->mode == HST_BTX_PREAMBLE ? 1 : t->getbit(t->user) & 1;
        tri |= scr17_scramble(&t->scr, di) << b;
    }
    if (t->mode == HST_BTX_PREAMBLE && --t->npre <= 0) t->mode = HST_BTX_DATA;
    t->phase = (t->phase + hst_bstep[tri]) & 7;
    idx = SIN_N / 16 + t->phase * (SIN_N / 8);      /* 22.5 + 45 k degrees */
    *si = (int32_t)(((int64_t)g * sintab[(idx + SIN_N / 4) & SIN_MASK]) >> 15);
    *sq = (int32_t)(((int64_t)g * sintab[idx & SIN_MASK]) >> 15);
    return 1;
}

int16_t hst_btx_tick(hst_btx_t *t)
{
    int l, j, act;
    int32_t v = 0;
    t->newsym = 0;
    /* a change of speed at the old speed's symbol boundary; the old lane drains below */
    if (t->lane[lane_of(t->bps)].acc >= lanes[lane_of(t->bps)].P) take_speed(t);
    act = lane_of(t->bps);
    for (l = 0; l < 2; l++) {
        hst_blane_t *L = &t->lane[l];
        if (l == act || L->left > 0) {
            if (L->acc >= lanes[l].P) {
                int32_t si = 0, sq = 0;
                L->acc -= lanes[l].P;
                for (j = HST_BL - 1; j > 0; j--) { L->si[j] = L->si[j - 1]; L->sq[j] = L->sq[j - 1]; }
                if (l == act) t->newsym = next_symbol(t, &si, &sq);
                L->si[0] = si; L->sq[0] = sq;
                if (si || sq) L->left = HST_BL; else if (L->left > 0) L->left--;
            }
            if (L->left > 0) {
                const int16_t *h = lanes[l].pulse + L->acc * HST_BL;
                int64_t bi = 0, bq = 0;
                int32_t c, s;
                for (j = 0; j < HST_BL; j++) { bi += (int64_t)L->si[j] * h[j]; bq += (int64_t)L->sq[j] * h[j]; }
                bi >>= 14; bq >>= 14;                    /* Q14 pulse */
                c = sin_q15(L->cph + (1u << 30));
                s = sin_q15(L->cph);
                v += (int32_t)((bi * c - bq * s) >> 15);
            }
            L->acc += lanes[l].step;
        }
        L->cph += L->dph;
    }
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    return (int16_t)v;
}
