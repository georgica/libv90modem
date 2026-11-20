/* x2's downstream mapper. See x2map.h.
 *
 * The rules are docs/history/x2-facts.md's "Mapping" and "Free sign": as
 * D-Modem (https://github.com/D-Modem/D-Modem, docs/dsp/x2-details.md
 * "Data") and courier-emu (https://github.com/cryan209/courier-emu, its
 * specification 10, 11) describe them. Facts only; no code of either. */
#include "x2map.h"
#include "x2tab.h"
#include "v90_pcm.h"

#define B1   19             /* index 1: B */
#define MD   5              /* signs per frame */
#define NB   (MD + B1)
#define NLEV X2_NLEV1

/* the free sign of levels A: negative when the other five sum to more than
 * 805 in D-Modem's 14-bit units (X2_FRAME_DC), our linear / 4 */
#define FRAME_DC_LIN (805 * 4)

/* the octets sent, and those the client receives, for each digit */
static const uint8_t *sent_list(const x2map_t *m)
{
    return m->levels_a ? x2_levels_a : x2_pad_sent[m->pad_mode];
}
static const uint8_t *recv_list(const x2map_t *m)
{
    return m->levels_a ? x2_levels_a : x2_pad_recv[m->pad_mode];
}

static int valid(const x2map_t *m)
{
    return m->levels_a || (m->index == 1 && m->pad_mode >= 0 && m->pad_mode <= 2);
}

/* The position of the free sign: the largest level, of equal levels the
 * last; the others, in position order, carry the MD signs. One list for
 * every position at index 1, sorted largest first, so the smallest digit
 * is the largest level. */
static int free_pos(const int d[X2MAP_N])
{
    int i, f = 0;
    for (i = 1; i < X2MAP_N; i++) if (d[i] <= d[f]) f = i;
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
    return valid(m) ? NB : 0;
}

void x2map_frame(x2map_t *m, const uint8_t *bits, uint8_t out[X2MAP_N])
{
    const uint8_t *lv;
    uint32_t x = 0;
    int32_t sum = 0;
    int d[X2MAP_N], i, k, f, neg;
    if (!valid(m)) {
        for (i = 0; i < X2MAP_N; i++) out[i] = 0xff;
        return;
    }
    lv = sent_list(m);
    for (k = 0; k < B1; k++) x |= (uint32_t)(bits[MD + k] & 1) << k;
    for (i = 0; i < X2MAP_N; i++) { d[i] = (int)(x % NLEV); x /= NLEV; }
    f = free_pos(d);
    for (i = 0, k = 0; i < X2MAP_N; i++) {
        if (i == f) continue;
        m->last ^= bits[k++] & 1;
        out[i] = m->last ? lv[d[i]] : (uint8_t)(lv[d[i]] & 0x7f);
        sum += v90_octet_linear(0, out[i]);
    }
    /* levels A: the frame's sum; the data levels: the running sum of all
       values sent, this frame's five included */
    if (m->levels_a)
        neg = sum > FRAME_DC_LIN;
    else
        neg = m->rds + sum > 0;
    out[f] = neg ? (uint8_t)(lv[d[f]] & 0x7f) : lv[d[f]];
    m->rds += sum + v90_octet_linear(0, out[f]);
}

int x2map_unframe(x2map_t *m, const uint8_t in[X2MAP_N], uint8_t *bits)
{
    const uint8_t *lv;
    uint32_t x = 0;
    int d[X2MAP_N], i, j, k, f;
    if (!valid(m)) return -1;
    lv = recv_list(m);
    for (i = 0; i < X2MAP_N; i++) {
        for (j = 0; j < NLEV && lv[j] != (in[i] | 0x80); j++) ;
        if (j == NLEV) return -1;
        d[i] = j;
    }
    f = free_pos(d);
    for (i = 0, k = 0; i < X2MAP_N; i++) {
        int pos;
        if (i == f) continue;
        pos = (in[i] & 0x80) != 0;
        bits[k++] = (uint8_t)(pos ^ m->last);
        m->last = pos;
    }
    for (i = X2MAP_N - 1; i >= 0; i--) x = x * NLEV + (uint32_t)d[i];
    for (k = 0; k < B1; k++) bits[MD + k] = (uint8_t)((x >> k) & 1);
    return NB;
}
