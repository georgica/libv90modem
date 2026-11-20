/* x2's downstream mapper. See x2map.h.
 *
 * The rules are docs/history/x2-facts.md's "Mapping" and "Free sign": as
 * D-Modem (https://github.com/D-Modem/D-Modem, docs/dsp/x2-details.md
 * "Data") and courier-emu (https://github.com/cryan209/courier-emu, its
 * specification 10, 11) describe them. Facts only; no code of either. */
#include "x2map.h"
#include "x2tab.h"
#include "v90_pcm.h"

#define MD   5              /* signs per frame */

/* the free sign of levels A: negative when the other five sum to more than
 * 805 in D-Modem's 14-bit units (X2_FRAME_DC), our linear / 4 */
#define FRAME_DC_LIN (805 * 4)

/* B of levels A: index 1's, 9 levels in each position */
#define B_A  19

/* the bank in use, or NULL: levels A (index 1 only) have none */
static const x2_bank_t *bank(const x2map_t *m)
{
    if (m->levels_a || m->index < 1 || m->index > X2_NIDX || m->pad_mode < 0 || m->pad_mode >= X2_MODES)
        return 0;
    return &x2_banks[m->pad_mode][m->index - 1];
}

static int valid(const x2map_t *m)
{
    return m->levels_a ? m->index == 1 : bank(m) != 0;
}

int x2map_list(const x2map_t *m, int pos, const uint8_t **recv, const uint8_t **sent)
{
    const x2_bank_t *b;
    const x2_list_t *l;
    if (!valid(m) || pos < 0 || pos >= X2MAP_N) return 0;
    if (m->levels_a) { *recv = *sent = x2_levels_a; return X2_NLEV1; }
    b = bank(m);
    l = &x2_bank_lists[b->list[pos > 0]];
    *recv = x2_bank_pool + l->recv;
    *sent = x2_bank_pool + l->sent;
    return l->n;
}

/* The position of the free sign: the largest level received (the
 * smallest octet), of equal levels the last; the others, in position
 * order, carry the MD signs. The lists of the positions may differ, so the
 * levels decide, not the digits. */
static int free_pos(const uint8_t lev[X2MAP_N])
{
    int i, f = 0;
    for (i = 1; i < X2MAP_N; i++) if (lev[i] <= lev[f]) f = i;
    return f;
}

void x2map_init(x2map_t *m, int index, int pad_mode, int levels_a)
{
    m->index = index;
    m->pad_mode = pad_mode;
    m->levels_a = levels_a;
    m->last = 1;
    m->rds = 0;
}

int x2map_bits(const x2map_t *m)
{
    if (!valid(m)) return 0;
    return MD + (m->levels_a ? B_A : bank(m)->b);
}

void x2map_frame(x2map_t *m, const uint8_t *bits, uint8_t out[X2MAP_N])
{
    const uint8_t *rv[X2MAP_N], *sn[X2MAP_N];
    uint8_t lev[X2MAP_N], o[X2MAP_N];
    uint64_t x = 0;
    int32_t sum = 0;
    int nb = x2map_bits(m), i, k, f, neg;
    if (!nb) {
        for (i = 0; i < X2MAP_N; i++) out[i] = 0xff;
        return;
    }
    /* B up to 37 bits: the number in 64 bits */
    for (k = 0; k < nb - MD; k++) x |= (uint64_t)(bits[MD + k] & 1) << k;
    for (i = 0; i < X2MAP_N; i++) {
        uint32_t n = (uint32_t)x2map_list(m, i, &rv[i], &sn[i]), d = (uint32_t)(x % n);
        x /= n;
        lev[i] = rv[i][d]; o[i] = sn[i][d];
    }
    f = free_pos(lev);
    for (i = 0, k = 0; i < X2MAP_N; i++) {
        if (i == f) continue;
        m->last ^= bits[k++] & 1;
        out[i] = m->last ? o[i] : (uint8_t)(o[i] & 0x7f);
        sum += v90_octet_linear(0, out[i]);
    }
    /* levels A: the frame's sum; the data levels: the running sum of all
       values sent, this frame's five included */
    if (m->levels_a)
        neg = sum > FRAME_DC_LIN;
    else
        neg = m->rds + sum > 0;
    out[f] = neg ? (uint8_t)(o[f] & 0x7f) : o[f];
    m->rds += sum + v90_octet_linear(0, out[f]);
}

int x2map_unframe(x2map_t *m, const uint8_t in[X2MAP_N], uint8_t *bits)
{
    const uint8_t *rv[X2MAP_N], *sn;
    uint8_t lev[X2MAP_N];
    uint64_t x = 0;
    int nb = x2map_bits(m), n[X2MAP_N], d[X2MAP_N], i, j, k, f;
    if (!nb) return -1;
    for (i = 0; i < X2MAP_N; i++) {
        n[i] = x2map_list(m, i, &rv[i], &sn);
        lev[i] = (uint8_t)(in[i] | 0x80);
        for (j = 0; j < n[i] && rv[i][j] != lev[i]; j++) ;
        if (j == n[i]) return -1;
        d[i] = j;
    }
    f = free_pos(lev);
    for (i = 0, k = 0; i < X2MAP_N; i++) {
        int pos;
        if (i == f) continue;
        pos = (in[i] & 0x80) != 0;
        bits[k++] = (uint8_t)(pos ^ m->last);
        m->last = pos;
    }
    for (i = X2MAP_N - 1; i >= 0; i--) x = x * (uint64_t)n[i] + (uint64_t)d[i];
    if (x >> (nb - MD)) return -1;      /* digits past B bits: not a frame we send */
    for (k = 0; k < nb - MD; k++) bits[MD + k] = (uint8_t)((x >> k) & 1);
    return nb;
}
