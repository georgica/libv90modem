/* x2's level tables at rate index 1. See x2tab.h. Facts only, each with its
 * origin as docs/history/x2-facts.md gives it; no table file of D-Modem
 * (AGPL-3.0) or courier-emu (no licence) is copied. */
#include "x2tab.h"

/* Levels A ("Mapping"): D-Modem's (https://github.com/D-Modem/D-Modem,
 * docs/dsp/x2-details.md), and the table at Ie030002 (I-modem 3.00.02)
 * DSP d238-d240, read by our disassembly. */
const uint8_t x2_levels_a[X2_NLEV1] = { 0xa5, 0xa7, 0xad, 0xaf, 0xb7, 0xbd, 0xc5, 0xcf, 0xe5 };

/* Levels B ("Mapping"): the I-modem's line octets after E, as D-Modem and
 * courier-emu (https://github.com/cryan209/courier-emu) observed them; not
 * a stored table in either firmware image. */
const uint8_t x2_levels_b[X2_NLEV1] = { 0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf };

/* The levels received behind each pad ("Pads"): read from the Quad x2
 * firmware QF060003, as published by D-Modem (dsp/x2_tables.cpp, the bank
 * constructor run in courier-emu). Behind 3 dB levels B's b3 cannot arrive,
 * hence the different list; behind 6 dB the client receives levels B. */
const uint8_t x2_pad_recv[3][X2_NLEV1] = {
    { 0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf },
    { 0xa5, 0xa8, 0xab, 0xae, 0xb4, 0xba, 0xc0, 0xcc, 0xe0 },
    { 0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf },
};

/* The octets sent for them: generated from the rows above by
 * tools/x2_pad.c's rule (x2_pad_octet: decode, x Q15 23198 or 16423,
 * re-encode; of two octets the smaller magnitude), not typed from the
 * Quad's; they equal the Quad's entry for entry. test/test_x2map.c checks
 * them against x2_pad_octet. */
const uint8_t x2_pad_sent[3][X2_NLEV1] = {
    { 0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf },
    { 0x9d, 0x9f, 0xa3, 0xa7, 0xac, 0xb2, 0xba, 0xc5, 0xdb },
    { 0x95, 0x98, 0x9b, 0x9e, 0xa4, 0xaa, 0xb0, 0xbc, 0xd3 },
};
