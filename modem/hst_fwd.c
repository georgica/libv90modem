/* HST's forward channel (hst.h; hst-facts.md, "Forward channel"): the
 * receiver's guard high-pass. */
#include "hst.h"

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
