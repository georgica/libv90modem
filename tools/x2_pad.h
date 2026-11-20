/* x2's digital pads: the octet the server sends for a level the client
 * receives (tools/x2_pad.c; docs/history/x2-facts.md, "Pads"). A host
 * tool, not part of the library. */
#ifndef V90D_TOOLS_X2_PAD_H
#define V90D_TOOLS_X2_PAD_H

/* The mu-law octet the server sends so that, after a digital pad of
 * 0 / 3 / 6 dB (pad_mode 0 / 1 / 2: the octet decoded, scaled in Q15 by
 * 32768 / 23198 / 16423 with rounding, encoded to the nearest octet), the
 * client receives recv_octet. Of several such octets, the one of the
 * smallest magnitude. -1 when no octet does that, or for an argument out
 * of range. */
int x2_pad_octet(int recv_octet, int pad_mode);

#endif
