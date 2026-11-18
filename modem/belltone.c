/* The Bell answer tone detector. See belltone.h. */
#include <string.h>
#include "belltone.h"
#include "sintab.h"

#define FS 8000
#define FLOOR_RMS 112            /* -43 dBm0 (0 dBm0 = 15770) */

void belltone_init(belltone_t *b, int floor_rms)
{
    int32_t f = floor_rms > 0 ? floor_rms : FLOOR_RMS;
    memset(b, 0, sizeof *b);
    b->dph = (uint32_t)(((uint64_t)BELLTONE_HZ << 32) / FS);
    b->floor_pw = (int64_t)BELLTONE_BLOCK * f * f;
}

int belltone_tick(belltone_t *b, int16_t x)
{
    int32_t c = sintab[((b->ph >> 22) + SIN_N / 4) & SIN_MASK], s = sintab[(b->ph >> 22) & SIN_MASK];
    b->ph += b->dph;
    /* products in Q15 of the sample scale: a tone of amplitude A sums to
     * N A / 2 over a block */
    b->re += ((int32_t)x * c) >> 15; b->im += ((int32_t)x * s) >> 15;
    b->pw += (int32_t)x * x;
    if (++b->n < BELLTONE_BLOCK) return b->blocks >= BELLTONE_BLOCKS;
    {
        /* a tone of amplitude A: |sum|^2 = N^2 A^2 / 4, the block power N A^2 / 2:
         * more than half of the block in the tone is |sum|^2 * 4 > N * power */
        int64_t m = b->re * b->re + b->im * b->im;
        int on = b->pw >= b->floor_pw && m * 4 > b->pw * BELLTONE_BLOCK;
        b->blocks = on ? (b->blocks < BELLTONE_BLOCKS ? b->blocks + 1 : BELLTONE_BLOCKS) : 0;
        b->n = 0; b->re = b->im = b->pw = 0;
    }
    return b->blocks >= BELLTONE_BLOCKS;
}
