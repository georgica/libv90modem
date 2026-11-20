/* The pad octets of x2 at rate index 1 (tools/x2_pad.c): the octet the
 * server sends so that, after the network's digital pad, the client
 * receives the level it expects. The pad model (docs/history/x2-facts.md,
 * "Pads"): decode the mu-law octet (G.711), multiply in Q15 by 23198
 * (-3 dB) or 16423 (-6 dB) with rounding, encode again to the nearest
 * octet. pad_apply below states that model again on its own, from
 * sim/g711, so that the tool is checked against it and not against
 * itself. */
#include "test.h"
#include "../sim/g711.h"
#include "../tools/x2_pad.h"

/* the octet the client receives when o is sent across pad m */
static int pad_apply(int o, int m)
{
    static const int32_t gain[3] = { 32768, 23198, 16423 };
    int32_t v = ulaw_to_linear((uint8_t)o);
    int32_t a = v < 0 ? -v : v;
    a = (int32_t)(((int64_t)a * gain[m] + 16384) >> 15);
    return linear_to_ulaw(v < 0 ? -a : a);
}

/* x2-facts.md "Pads", index 1: the levels the client receives and the
 * octets the Quad x2 server (QF060003) sends for them, per pad mode */
static const int levels_b[9] = { 0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf };
static const int recv3[9] = { 0xa5, 0xa8, 0xab, 0xae, 0xb4, 0xba, 0xc0, 0xcc, 0xe0 };
static const int quad3[9] = { 0x9d, 0x9f, 0xa3, 0xa7, 0xac, 0xb2, 0xba, 0xc5, 0xdb };
static const int quad6[9] = { 0x95, 0x98, 0x9b, 0x9e, 0xa4, 0xaa, 0xb0, 0xbc, 0xd3 };

int main(void)
{
    int i, m, o, derived = 0;

    /* pad 0 is the identity */
    CHECK_EQ_I(x2_pad_octet(0xa5, 0), 0xa5);
    for (o = 0; o < 256; o++)
        if (x2_pad_octet(o, 0) != o) break;
    CHECK_EQ_I(o, 256);

    /* each of the received levels at index 1, pads 1 and 2: the octet
       the server sends, padded, gives the level back, and it is the
       Quad's octet for that level */
    for (i = 0; i < 9; i++) for (m = 1; m <= 2; m++) {
        int lv = m == 1 ? recv3[i] : levels_b[i];
        int want = m == 1 ? quad3[i] : quad6[i];
        o = x2_pad_octet(lv, m);
        CHECK(o >= 0 && pad_apply(o, m) == lv);
        CHECK_EQ_I(o, want);
        if (o == want) derived++;
    }
    /* behind 6 dB the client receives levels B themselves; behind 3 dB
       levels B's 0xb3 cannot arrive at all, which is why the Quad's
       3 dB list is not levels B */
    CHECK_EQ_I(x2_pad_octet(0xb3, 1), -1);

    /* where two octets arrive as the same level (3 dB: 0xb1 and 0xb2 give
       0xba; 6 dB: 0xd2 and 0xd3 give 0xdf), the smaller magnitude */
    CHECK(pad_apply(0xb1, 1) == 0xba && pad_apply(0xb2, 1) == 0xba);
    CHECK(pad_apply(0xd2, 2) == 0xdf && pad_apply(0xd3, 2) == 0xdf);
    CHECK_EQ_I(x2_pad_octet(0xba, 1), 0xb2);
    CHECK_EQ_I(x2_pad_octet(0xdf, 2), 0xd3);

    /* the sign follows: a negative level gives the negative octet */
    CHECK_EQ_I(x2_pad_octet(0x25, 2), 0x15);
    CHECK_EQ_I(x2_pad_octet(0x33, 1), -1);

    /* out of range */
    CHECK_EQ_I(x2_pad_octet(0xa5, 3), -1);
    CHECK_EQ_I(x2_pad_octet(0x100, 0), -1);
    CHECK_EQ_I(x2_pad_octet(-1, 0), -1);

    printf("x2_pad: index 1, %d of 18 pad pairs derived by the model, "
           "%d to be taken from the Quad\n", derived, 18 - derived);
    T_REPORT("test_x2_pad");
}
