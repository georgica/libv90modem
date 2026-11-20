/* x2's downstream mapper (docs/history/x2-facts.md, "Mapping", "Free
 * sign"): MD + B bits, already scrambled (the caller runs GPC, v90/v90_scr),
 * into one frame of 6 mu-law octets, and back.
 *
 * The first MD bits are signs; the next B, LSB first, a number x. Position
 * i (0..5) takes digit x mod n_i, then x /= n_i, and the digit selects a
 * level from the position's list (largest magnitude first). The MD signs go,
 * in position order, to the MD positions of the smallest levels (of equal
 * levels, the first position), each sent as a change: last ^= bit, the
 * octet positive when last is 1. The remaining position carries the free
 * sign: in levels A negative when the other five sum to more than 805 in
 * D-Modem's 14-bit units; in the data levels negative when the running sum
 * of all values sent (this frame's five included) is above zero.
 *
 * Index 1 only so far (B = 19, MD = 5, 9 levels per position). Bits are one
 * per byte (0/1), element 0 first. */
#ifndef V90D_MODEM_X2MAP_H
#define V90D_MODEM_X2MAP_H
#include <stdint.h>

#define X2MAP_N 6           /* octets per frame */

typedef struct {
    int     index;          /* rate index; 1 is the only bank so far */
    int     pad_mode;       /* 0 none, 1 3 dB, 2 6 dB: the data levels only */
    int     levels_a;       /* levels A (sync, records) instead of the data
                               levels; the owner may change it between
                               frames, last and rds carry on */
    int     last;           /* the differential sign: 1 positive */
    int64_t rds;            /* the running sum of the linear values sent */
} x2map_t;

/* last positive, the running sum zero: the first sync frame, and again
 * after a rate change's scrambler reset */
void x2map_init(x2map_t *m, int index, int pad_mode, int levels_a);

/* bits per frame, B + MD (24 at index 1); 0 for a bank that does not
 * exist yet (any other index, in levels A as in the data levels) */
int x2map_bits(const x2map_t *m);

/* one frame from x2map_bits(m) scrambled bits; six 0xff (zero) for a bank
 * that does not exist yet */
void x2map_frame(x2map_t *m, const uint8_t *bits, uint8_t out[X2MAP_N]);

/* the inverse (for the tests and the simulated client): the bits of one
 * frame from the octets as the client receives them (behind the pad of
 * pad_mode, in the data levels; the two lists of 3 dB share an octet, so
 * the octets sent are not taken). Returns the number of bits, or -1 when
 * an octet is not a level. The free sign is not checked. Uses last only;
 * keep a separate x2map_t. */
int x2map_unframe(x2map_t *m, const uint8_t in[X2MAP_N], uint8_t *bits);

#endif
