/* x2's level tables at rate index 1 (32000 bit/s): see x2tab.c for where
 * each comes from (docs/history/x2-facts.md, "Mapping" and "Pads").
 * Each list holds the positive mu-law octets of the 9 levels, largest
 * magnitude first (smallest octet first). */
#ifndef V90D_MODEM_X2TAB_H
#define V90D_MODEM_X2TAB_H
#include <stdint.h>

#define X2_NLEV1 9                      /* levels per position at index 1 */

extern const uint8_t x2_levels_a[X2_NLEV1];   /* sync, records, the start's first frame */
extern const uint8_t x2_levels_b[X2_NLEV1];   /* data at index 1 */

/* Index 1 behind the network's digital pad, per pad mode (0 none, 1 3 dB,
 * 2 6 dB): the level the client receives, and the octet the server sends
 * for it. Mode 0 is levels B both ways. */
extern const uint8_t x2_pad_recv[3][X2_NLEV1];
extern const uint8_t x2_pad_sent[3][X2_NLEV1];

#endif
