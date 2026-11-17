/* Answer tone detector of a calling modem: is the tone that the answering
 * modem sends ANSam (V.8 7.2: 2100 Hz, its envelope amplitude-modulated by
 * 15 Hz between 0.8 and 1.2 of its average) or ANS (V.25: 2100 Hz with a
 * constant envelope)? V.8 8.1.1 lets the caller send CM only after ANSam;
 * after ANS it goes the pre-V.8 way.
 *
 * How: the received signal is brought down from 2100 Hz and summed over
 * blocks of 5 ms (40 samples). A block counts as tone when its 2100 Hz
 * component holds at least half of the block's power and is above the level
 * floor. The 15 Hz part of the envelope is measured over windows of 40 blocks
 * (200 ms, three periods of 15 Hz): about 20 % of the envelope's mean is
 * ANSam, about 0 is ANS. A block where the phase reversed (both tones may
 * have them, every 450 ms) loses its amplitude; it is bridged with the block
 * before it so that it does not look like amplitude modulation (V.8 7.2
 * NOTE 1: allow for transients). Two windows in a row that agree decide.
 *
 * Fixed-point, 8000 samples per second, one sample at a time, no allocation. */
#ifndef V90D_MODEM_ANSTONE_H
#define V90D_MODEM_ANSTONE_H
#include <stdint.h>

#define ANST_BLOCK   40          /* samples per block (5 ms) */
#define ANST_WINDOW  40          /* blocks per window (200 ms) */

typedef enum {
    ANST_NONE = 0,               /* nothing decided yet */
    ANST_ANS,                    /* a 2100 Hz tone without 15 Hz modulation (V.25) */
    ANST_ANSAM                   /* ANSam (V.8) */
} anst_result;

typedef struct {
    uint32_t ph, dph;            /* 2100 Hz local oscillator */
    int32_t  bi, bq;             /* block sums */
    int64_t  bpow;               /* block power */
    int      bn;                 /* samples in the block so far */
    int32_t  last_amp;           /* the last block's amplitude, for bridging a reversal */
    uint32_t amph;               /* 15 Hz phase, per block */
    int64_t  wsum, wc, ws;       /* window: mean and 15 Hz correlations of the envelope */
    int      wn;                 /* blocks in the window so far */
    int      prev;               /* the last window's verdict: ANST_*, -1 = none */
    int      reversals;          /* blocks bridged so far */
    int32_t  min_amp;            /* the level floor, as a block amplitude */
    long     samples;            /* since anst_init */
    anst_result result;
    long     at;                 /* samples when result was decided */
} anst_t;

/* floor_rms: the weakest tone to take, RMS on the 16-bit scale (0 = the
 * default, -43 dBm0: the V.21 / V.25 receive threshold) */
void        anst_init(anst_t *d, int floor_rms);
/* one received sample; returns the result so far (it does not change once
 * decided; anst_init starts again) */
anst_result anst_tick(anst_t *d, int16_t x);

#endif
