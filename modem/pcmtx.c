/* PCM upstream transmit clock and interpolator. See pcmtx.h. */
#include <string.h>
#include "pcmtx.h"
#include "v34_tabs.h"

#define ONE32 ((int64_t)1 << 32)

void pcmtx_init(pcmtx_t *t)
{
    memset(t, 0, sizeof *t);
    t->tt = ONE32;              /* the first symbol instant: the first sample */
    t->since = 4 * ONE32;
}

void pcmtx_clock(pcmtx_t *t)
{
    t->tt -= ONE32;
    if (t->since < 4 * ONE32) t->since += ONE32;
}

int pcmtx_due(const pcmtx_t *t) { return t->tt <= 0; }

void pcmtx_put(pcmtx_t *t, int16_t sym, int64_t step)
{
    t->ring[t->pos] = sym;
    t->pos = (t->pos + 1) & (PCMTX_RING - 1);
    t->since = -t->tt;          /* the instant was tt before this sample */
    t->tt += step > 0 ? step : ONE32;
    t->nsym++;
}

/* The bank's branch p yields the signal 31 + p / 32 samples before the
 * newest input it is laid on. The output is the symbols' signal at
 * PCMTX_DELAY (33) samples before now, and the newest symbol's instant was
 * `since` ago: laid on the newest symbol (j = 0) that is 33 - since samples
 * before it, laid on the one before (j = 1) 32 - since. Of the two, the one
 * that gives a branch in 0..32. */
int16_t pcmtx_out(const pcmtx_t *t)
{
    int64_t a, acc0 = 0, acc1 = 0, v;
    int j, p, i, base;
    int32_t f;
    const int16_t *h0, *h1;
    if (t->since < ONE32) { j = 1; a = ONE32 - t->since; }
    else { j = 0; a = 2 * ONE32 - t->since; if (a < 0) a = 0; }
    p = (int)(a >> 27);
    f = (int32_t)((a >> 12) & 0x7fff);
    if (p >= V34_IP) { p = V34_IP - 1; f = 0x8000; }
    h0 = v34_interp[p]; h1 = v34_interp[p + 1];
    base = t->pos - 1 - j;
    for (i = 0; i < V34_IN; i++) {
        int32_t s = t->ring[(base - i) & (PCMTX_RING - 1)];
        acc0 += s * h0[i];
        acc1 += s * h1[i];
    }
    v = acc0 + (((acc1 - acc0) * f) >> 15);     /* Q14 */
    v = (v + 8192) >> 14;
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

void pcmtx_slip(pcmtx_t *t, uint32_t frac_q16)
{
    t->tt += (int64_t)frac_q16 << 16;
}
