/* x2's digital pads: the octet the server sends for a level the client
 * receives (x2_pad.h). A host tool, not part of the library: it derives
 * the pad octet pairs that docs/history/x2-facts.md ("Pads") lists, from
 * the network's pad model alone.
 *
 * The model: the network decodes the mu-law octet (G.711, sim/g711.c),
 * multiplies the linear value in Q15 by 23198 (-3 dB) or 16423 (-6 dB),
 * rounds, and encodes it again to the nearest octet. At rate index 1 the
 * octets it gives are the Quad x2 server's (QF060003), entry for entry,
 * where two octets arrive as the same level taking the one of smaller
 * magnitude. */
#include <stdint.h>
#include "x2_pad.h"
#include "../sim/g711.h"

/* Q15 gains of pad modes 0, 1, 2: 1, 10^(-3/20), 10^(-6/20) */
static const int32_t pad_gain[3] = { 32768, 23198, 16423 };

/* the octet the client receives when o is sent across pad mode m */
static int pad_apply(int o, int m)
{
    int32_t v = ulaw_to_linear((uint8_t)o);
    int32_t a = v < 0 ? -v : v;
    a = (int32_t)(((int64_t)a * pad_gain[m] + 16384) >> 15);
    return linear_to_ulaw(v < 0 ? -a : a);
}

int x2_pad_octet(int recv_octet, int pad_mode)
{
    int o, sign;
    if (recv_octet < 0 || recv_octet > 0xff || pad_mode < 0 || pad_mode > 2)
        return -1;
    if (pad_mode == 0)
        return recv_octet;
    /* the octets of the same sign, from the smallest magnitude (0x7f
       below the sign bit) to the largest: the first that arrives as
       recv_octet */
    sign = recv_octet & 0x80;
    for (o = 0x7f; o >= 0; o--)
        if (pad_apply(sign | o, pad_mode) == recv_octet)
            return sign | o;
    return -1;
}
