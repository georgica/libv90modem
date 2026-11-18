/* The Bell answer tone, 2225 Hz, as a calling modem that offers Bell 212A or
 * Bell 103 listens for it in place of (or next to) the 2100 Hz of V.25 / V.8.
 * Modems of the time take 2225 Hz within some 10 Hz for an answering Bell
 * modem (the TDK 73K222 data sheet's detector); it must not be taken for
 * the 2100 Hz answer tones, nor for the 2250 Hz of a V.22 answerer's
 * unscrambled ones (which tells the V.22 / V.22bis start-up, not Bell).
 *
 * How: the received signal is brought down from 2225 Hz and summed over
 * blocks of 40 ms (320 samples). 2250 Hz turns once in a block and sums to
 * nothing; a tone within about 11 Hz of 2225 keeps more than half of its
 * power in the sum. A block is tone when that sum holds more than half of
 * the block's power and the block is above the floor (-43 dBm0, the V.25
 * receive threshold). BELLTONE_BLOCKS blocks in a row decide.
 *
 * Fixed point, 8000 samples per second, one sample at a time, no allocation. */
#ifndef V90D_MODEM_BELLTONE_H
#define V90D_MODEM_BELLTONE_H
#include <stdint.h>

#define BELLTONE_HZ     2225
#define BELLTONE_BLOCK   320     /* samples per block (40 ms) */
#define BELLTONE_BLOCKS    4     /* blocks in a row: 160 ms, decided within 200 ms */

typedef struct {
    uint32_t ph, dph;
    int64_t  re, im, pw;
    int      n, blocks;
    int64_t  floor_pw;           /* the least block power that counts */
} belltone_t;

/* floor_rms: the weakest tone to take, RMS on the 16-bit scale (0 = the
 * default, -43 dBm0) */
void belltone_init(belltone_t *b, int floor_rms);
/* one received sample; 1 while 2225 Hz has been there BELLTONE_BLOCKS blocks
 * in a row */
int  belltone_tick(belltone_t *b, int16_t x);

#endif
