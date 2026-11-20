/* x2's levels A. See x2tab.h; the data banks are in x2bank.c. Facts only,
 * with their origin as docs/history/x2-facts.md gives it; no table file of
 * D-Modem (AGPL-3.0) or courier-emu (no licence) is copied. */
#include "x2tab.h"

/* Levels A ("Mapping"): D-Modem's (https://github.com/D-Modem/D-Modem,
 * docs/dsp/x2-details.md), and the table at Ie030002 (I-modem 3.00.02)
 * DSP d238-d240, read by our disassembly. */
const uint8_t x2_levels_a[X2_NLEV1] = { 0xa5, 0xa7, 0xad, 0xaf, 0xb7, 0xbd, 0xc5, 0xcf, 0xe5 };
