/* The V.8bis messages (modem/v8bismsg; docs/history/k56-facts.md, "V.8bis
 * messages", "NS block", "MS test", "Recording"): FCS, our CL, ACK(1), the
 * framing, the receiver, SM56's CL test and the MS test. */
#include <string.h>
#include "test.h"
#include "v8bismsg.h"

static const uint8_t cl9[16] = { 0x12, 0xc9, 0x80, 0x80, 0x81, 0xc2, 0x09, 0xb5, 0x02, 0x00,
                                 0x94, 0x81, 0x83, 0x42, 0x46, 0xe0 };
static const uint8_t cl7[13] = { 0x12, 0xc9, 0x80, 0x80, 0x80, 0x07, 0xb5, 0x02, 0x00, 0x94,
                                 0x81, 0x83, 0x61 };
static const uint8_t sm56_ms[13] = { 0x11, 0xc9, 0x80, 0x80, 0x80, 0x07, 0xb5, 0x02, 0x00, 0x94,
                                     0x81, 0x83, 0x21 };
static const uint8_t real_ms[16] = { 0x11, 0xc9, 0x80, 0x80, 0x81, 0xc2, 0x09, 0xb5, 0x02, 0x00,
                                     0x94, 0x81, 0x81, 0x02, 0x47, 0x85 };

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* feed bits; the last non-zero return */
static int feed(v8bis_rx_t *r, const uint8_t *bits, int nb, uint8_t *o)
{
    int i, res = 0, x;
    for (i = 0; i < nb; i++) {
        x = v8bis_rx_bit(r, bits[i], o);
        if (x) res = x;
    }
    return res;
}

static void fcs_of(const uint8_t *o, int n, uint8_t *lo, uint8_t *hi)
{
    uint16_t f = v8bis_fcs16(o, n);
    *lo = (uint8_t)(f & 0xff); *hi = (uint8_t)(f >> 8);
}

int main(void)
{
    uint8_t o[V8B_MAXOCT + 4], b[V8B_MAXBITS], back[V8B_MAXOCT + 4], lo, hi, m[V8B_MAXOCT];
    v8bis_rx_t r;
    int n, nb, i, k, res, len_i, bad = 0, neg = 0;

    CHECK(v8bis_fcs16((const uint8_t *)"123456789", 9) == 0x906e);   /* CRC-16/X.25 */

    /* the recorded and computed FCS octets, low octet first */
    fcs_of(cl9, 16, &lo, &hi); CHECK(lo == 0x23 && hi == 0x50);
    fcs_of(real_ms, 16, &lo, &hi); CHECK(lo == 0x71 && hi == 0xd8);
    { uint8_t ack = 0x14; fcs_of(&ack, 1, &lo, &hi); CHECK(lo == 0xdd && hi == 0xa6); }
    fcs_of(cl7, 13, &lo, &hi); CHECK(lo == 0xd6 && hi == 0x36);
    fcs_of(sm56_ms, 13, &lo, &hi); CHECK(lo == 0x25 && hi == 0x7a);

    /* our CL, both forms; the default is the real server's */
    n = v8bis_cl_octets(V8B_NS9, V8B_VERS_2, o);
    CHECK_EQ_I(n, 16); CHECK(memcmp(o, cl9, 16) == 0);
    CHECK(v8bis_cl_is_k56(o + 6, n - 6));
    n = v8bis_cl_octets(V8B_NS7, V8B_VERS_BOTH, o);
    CHECK_EQ_I(n, 13); CHECK(memcmp(o, cl7, 13) == 0);
    CHECK(v8bis_cl_is_k56(o + 5, n - 5));
    /* versions 1 and 2 (0.7.2's default): the same block but its version
     * octet, 0x43; FCS ff 0a (the analysis' worked frame); SM56's test
     * passes it; version 1 alone (0x41) it would not */
    n = v8bis_cl_octets(V8B_NS9, V8B_VERS_BOTH, o);
    CHECK_EQ_I(n, 16); CHECK(memcmp(o, cl9, 13) == 0 && o[13] == 0x43 && memcmp(o + 14, cl9 + 14, 2) == 0);
    fcs_of(o, 16, &lo, &hi); CHECK(lo == 0xff && hi == 0x0a);
    CHECK(v8bis_cl_is_k56(o + 6, n - 6));
    n = v8bis_cl_octets(V8B_NS9, V8B_VERS_1, o);
    CHECK_EQ_I(o[13], 0x41); CHECK(!v8bis_cl_is_k56(o + 6, n - 6));
    n = v8bis_cl_octets(V8B_NS7, V8B_VERS_2, o);
    CHECK_EQ_I(n, 13); CHECK(memcmp(o, cl7, 13) == 0);         /* the 7-octet form keeps 0x61 */
    /* the version an octet selects: its highest version bit, 0x20 and 0x40 not */
    CHECK_EQ_I(v8bis_version_of(0x02), 2); CHECK_EQ_I(v8bis_version_of(0x03), 2);
    CHECK_EQ_I(v8bis_version_of(0x01), 1); CHECK_EQ_I(v8bis_version_of(0x21), 1);
    CHECK_EQ_I(v8bis_version_of(0x61), 1); CHECK_EQ_I(v8bis_version_of(0x60), 0);
    CHECK_EQ_I(v8bis_version_of(0x00), 0); CHECK_EQ_I(v8bis_version_of(0x10), 5);
    CHECK_EQ_I(v8bis_version_of(0x42), 2); CHECK_EQ_I(v8bis_version_of(0xff), 5);
#ifndef V8B_NS_DEFAULT_OVERRIDDEN
    CHECK_EQ_I(V8B_NS_DEFAULT, V8B_NS9);   /* -DV8B_NS_DEFAULT=V8B_NS7 builds say so with -DV8B_NS_DEFAULT_OVERRIDDEN */
#endif
    n = v8bis_ack1_octets(o);
    CHECK_EQ_I(n, 1); CHECK_EQ_I(o[0], 0x14);
    CHECK_EQ_I(V8B_MSG_MS, 1); CHECK_EQ_I(V8B_MSG_CL, 2); CHECK_EQ_I(V8B_MSG_ACK1, 4);

    /* SM56's test of a CL block: each condition */
    memcpy(o, cl9 + 6, 10);
    CHECK(v8bis_cl_is_k56(o, 10));
    CHECK(!v8bis_cl_is_k56(o, 9));              /* block not whole */
    o[0] = 6; CHECK(!v8bis_cl_is_k56(o, 10)); o[0] = 9;
    o[1] = 0xb4; CHECK(!v8bis_cl_is_k56(o, 10)); o[1] = 0xb5;
    o[2] = 0x03; CHECK(!v8bis_cl_is_k56(o, 10)); o[2] = 0x02;
    o[6] = 0x82; CHECK(!v8bis_cl_is_k56(o, 10)); o[6] = 0x83;
    o[7] = 0x41; CHECK(!v8bis_cl_is_k56(o, 10)); o[7] = 0x42;
    o[7] = 0x02; CHECK(!v8bis_cl_is_k56(o, 10)); o[7] = 0x42;
    CHECK(v8bis_cl_is_k56(o, 10));

    /* the MS test: SM56's MS and the real client's, which a fixed skip of
     * four level 1 blocks rejects (C3) */
    CHECK_EQ_I(v8bis_ms_is_k56(sm56_ms, 13), 1);
    CHECK_EQ_I(v8bis_ms_is_k56(real_ms, 16), 1);
    memcpy(m, sm56_ms, 13); m[11] = 0x82; CHECK_EQ_I(v8bis_ms_is_k56(m, 13), 0);   /* o[6] bit 0 clear */
    memcpy(m, sm56_ms, 13); m[6] = 0xb4;   CHECK_EQ_I(v8bis_ms_is_k56(m, 13), 0);
    memcpy(m, sm56_ms, 13); m[1] = 0x89; memmove(m + 5, m + 6, 7);
    CHECK_EQ_I(v8bis_ms_is_k56(m, 12), 0);                                          /* no NS block */
    memcpy(m, sm56_ms, 13); m[0] = 0x12;   CHECK_EQ_I(v8bis_ms_is_k56(m, 13), 0);   /* a CL */
    memcpy(m, sm56_ms, 13); m[1] = 0x89;   CHECK_EQ_I(v8bis_ms_is_k56(m, 13), 0);   /* NS bit clear */
    CHECK_EQ_I(v8bis_ms_is_k56(sm56_ms, 12), 0);                                    /* block cut short */
    CHECK_EQ_I(v8bis_ms_is_k56(real_ms, 5), 0);                                     /* tree cut short */
    CHECK_EQ_I(v8bis_ms_is_k56(sm56_ms, 0), 0);
    /* the MS's version octet: SM56's 0x21, the real client's 0x02; none
     * where the MS test fails */
    CHECK_EQ_I(v8bis_ms_version(sm56_ms, 13), 0x21);
    CHECK_EQ_I(v8bis_ms_version(real_ms, 16), 0x02);
    CHECK_EQ_I(v8bis_ms_version(sm56_ms, 12), -1);
    memcpy(m, sm56_ms, 13); m[1] = 0x89;   CHECK_EQ_I(v8bis_ms_version(m, 13), -1);
    memcpy(m, sm56_ms, 13); m[3] = 0x01;   CHECK_EQ_I(v8bis_ms_is_k56(m, 13), 0);   /* S NPar not closed */
    /* a three-level S Par(2) block: I SPar(1) bit 1 with its block (NPar(2) 0xc1,
     * bit 8 set: NPar(2) only), S SPar(1) data, NPar(2) 0x41 (delimiter, bit 8
     * clear), SPar(2) 0x41 (bit 1 set), its NPar(3) block 0x40; then the NS field */
    {
        uint8_t x[24];
        int j = 0;
        x[j++] = 0x11; x[j++] = 0xc9; x[j++] = 0x81; x[j++] = 0xc1;
        x[j++] = 0x80; x[j++] = 0x81; x[j++] = 0x41;
        x[j++] = 0x41; x[j++] = 0x40;
        memcpy(x + j, sm56_ms + 5, 8); j += 8;
        CHECK_EQ_I(v8bis_ms_is_k56(x, j), 1);
        x[8] = 0x00;                       /* the NPar(3) block never closes */
        CHECK_EQ_I(v8bis_ms_is_k56(x, j), 0);
    }

    /* framing: preamble, flags, stuffing; then back through the receiver */
    n = v8bis_cl_octets(V8B_NS9, V8B_VERS_2, o);
    nb = v8bis_frame(o, n, b);
    CHECK(nb > 0 && nb <= V8B_MAXBITS);
    for (i = 0; i < 32; i++) CHECK_EQ_I(b[i], 1);                 /* ones preamble */
    { static const uint8_t fl[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
      CHECK(memcmp(b + 32, fl, 8) == 0 && memcmp(b + 40, fl, 8) == 0 && memcmp(b + 48, fl, 8) == 0);
      CHECK(memcmp(b + nb - 8, fl, 8) == 0); }
    v8bis_rx_init(&r);
    res = feed(&r, b, nb, back);
    CHECK_EQ_I(res, 16); CHECK(memcmp(back, cl9, 16) == 0);

    /* the recorded CL: the bits after the flags are the octets and the FCS, LSB first */
    { uint8_t rec[18]; memcpy(rec, cl9, 16); rec[16] = 0x23; rec[17] = 0x50;
      /* unstuff the frame body and compare with rec */
      { int ones = 0, nbody = 0, bit;
        uint8_t body[20]; memset(body, 0, sizeof body);
        for (i = 32 + 24; i < nb - 8; i++) {
            bit = b[i];
            if (ones == 5) { CHECK_EQ_I(bit, 0); ones = 0; continue; }
            ones = bit ? ones + 1 : 0;
            if (bit) body[nbody / 8] |= (uint8_t)(1 << (nbody % 8));
            nbody++;
        }
        CHECK_EQ_I(nbody, 18 * 8); CHECK(memcmp(body, rec, 18) == 0); } }

    /* the ACK(1) frame, and stuffing: 0xff, 0x7f have runs of ones */
    n = v8bis_ack1_octets(o);
    nb = v8bis_frame(o, n, b);
    v8bis_rx_init(&r);
    res = feed(&r, b, nb, back);
    CHECK_EQ_I(res, 1); CHECK_EQ_I(back[0], 0x14);
    o[0] = 0xff; o[1] = 0xff; o[2] = 0x7f; o[3] = 0xf8;
    nb = v8bis_frame(o, 4, b);
    { int ones = 0, stuffed = 0;
      for (i = 32 + 24; i < nb - 8; i++) {
          if (ones == 5) { stuffed++; ones = 0; continue; }
          ones = b[i] ? ones + 1 : 0;
      }
      CHECK(stuffed >= 3); }
    v8bis_rx_init(&r);
    res = feed(&r, b, nb, back);
    CHECK_EQ_I(res, 4); CHECK(memcmp(back, o, 4) == 0);

    /* every message above and 200 random ones of 1..32 octets */
    for (k = 0; k < 205; k++) {
        if (k == 0) { n = 13; memcpy(o, sm56_ms, 13); }
        else if (k == 1) { n = 16; memcpy(o, real_ms, 16); }
        else if (k == 2) { n = 13; memcpy(o, cl7, 13); }
        else if (k == 3) { n = 32; memset(o, 0xff, 32); }
        else if (k == 4) { n = 1; o[0] = 0x00; }
        else { n = 1 + (int)(rnd() % V8B_MAXOCT); for (i = 0; i < n; i++) o[i] = (uint8_t)rnd(); }
        nb = v8bis_frame(o, n, b);
        CHECK(nb > 0 && nb <= V8B_MAXBITS);
        v8bis_rx_init(&r);
        res = feed(&r, b, nb, back);
        if (res != n || memcmp(back, o, (size_t)n) != 0) bad++;
        /* two messages back to back through one receiver */
        res = feed(&r, b, nb, back);
        if (res != n || memcmp(back, o, (size_t)n) != 0) bad++;
        /* a flipped bit between the opening and closing flags: never a message */
        { int pos = 32 + 24 + (int)(rnd() % (unsigned)(nb - 32 - 24 - 8));
          b[pos] ^= 1;
          v8bis_rx_init(&r);
          res = feed(&r, b, nb, back);
          if (res > 0) bad++;
          if (res < 0) neg++;
          b[pos] ^= 1; }
    }
    CHECK_EQ_I(bad, 0);
    CHECK(neg > 150);

    /* a flipped bit well away from any run: -1 */
    n = v8bis_cl_octets(V8B_NS9, V8B_VERS_2, o);
    nb = v8bis_frame(o, n, b);
    b[32 + 24 + 20] ^= 1;
    v8bis_rx_init(&r);
    CHECK_EQ_I(feed(&r, b, nb, back), -1);

    /* a frame of fewer than three octets between flags is invalid; too long also */
    { uint8_t two[2] = { 0x14, 0x14 };
      static const uint8_t fl[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
      int p = 0;
      for (i = 0; i < 32; i++) b[p++] = 1;
      memcpy(b + p, fl, 8); p += 8;
      for (i = 0; i < 16; i++) b[p++] = (uint8_t)((two[i / 8] >> (i % 8)) & 1);
      memcpy(b + p, fl, 8); p += 8;
      v8bis_rx_init(&r);
      CHECK_EQ_I(feed(&r, b, p, back), -1); }
    { uint8_t big[V8B_MAXBITS * 2]; int p = 0;
      for (i = 0; i < 40; i++) big[p++] = 1;
      big[p++] = 0; for (i = 0; i < 6; i++) big[p++] = 1; big[p++] = 0;
      for (i = 0; i < 400; i++) big[p++] = (uint8_t)(i % 2);     /* alternating bits */
      big[p++] = 0; for (i = 0; i < 6; i++) big[p++] = 1; big[p++] = 0;
      v8bis_rx_init(&r);
      CHECK_EQ_I(feed(&r, big, p, back), -1); }
    /* a frame not a whole number of octets */
    { int p = 0; static const uint8_t fl[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
      memcpy(b + p, fl, 8); p += 8;
      for (i = 0; i < 27; i++) b[p++] = (uint8_t)(i % 2);
      memcpy(b + p, fl, 8); p += 8;
      v8bis_rx_init(&r);
      CHECK_EQ_I(feed(&r, b, p, back), -1); }

    /* the abort: seven ones inside a frame end it, once, as invalid */
    n = v8bis_cl_octets(V8B_NS9, V8B_VERS_2, o);
    nb = v8bis_frame(o, n, b);
    { uint8_t ab[V8B_MAXBITS + 16]; int p = 32 + 24 + 40, res2 = 0;
      memcpy(ab, b, (size_t)p);
      for (i = 0; i < 9; i++) ab[p++] = 1;        /* then an idle mark */
      v8bis_rx_init(&r);
      for (i = 0; i < p; i++) {
          int x = v8bis_rx_bit(&r, ab[i], back);
          if (x == -1) res2++; else CHECK_EQ_I(x, 0);
      }
      CHECK_EQ_I(res2, 1); }
    /* the idle mark after a good closing flag gives nothing, and the receiver
     * takes the next frame */
    nb = v8bis_frame(o, n, b);
    { int cnt = 0, x;
      v8bis_rx_init(&r);
      for (i = 0; i < nb; i++) { x = v8bis_rx_bit(&r, b[i], back); if (x) cnt++; }
      CHECK_EQ_I(cnt, 1);
      for (i = 0; i < 40; i++) CHECK_EQ_I(v8bis_rx_bit(&r, 1, back), 0);
      CHECK_EQ_I(feed(&r, b, nb, back), n); }
    /* flags sharing their zeros: 0 111111 0 111111 0 then the body */
    { static const uint8_t sh[15] = { 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0 };
      uint8_t sb[V8B_MAXBITS]; int p = 0;
      for (i = 0; i < 20; i++) sb[p++] = 1;
      memcpy(sb + p, sh, 15); p += 15;
      memcpy(sb + p, b + 32 + 24, (size_t)(nb - 32 - 24)); p += nb - 32 - 24;
      v8bis_rx_init(&r);
      CHECK_EQ_I(feed(&r, sb, p, back), n);
      CHECK(memcmp(back, o, (size_t)n) == 0); }
    CHECK_EQ_I(v8bis_frame(o, 0, b), 0);
    CHECK_EQ_I(v8bis_frame(o, V8B_MAXOCT + 1, b), 0);

    /* hostile input: random bits, biased to long runs, never crash and only
     * return -1, 0 or a count of 1..32 whose octets the buffer holds */
    v8bis_rx_init(&r);
    { int run = 0, cur = 1, x;
      for (i = 0; i < 400000; i++) {
          if (run == 0) { run = 1 + (int)(rnd() % 12); cur = (int)(rnd() & 1); }
          run--;
          x = v8bis_rx_bit(&r, (rnd() % 17 == 0) ? !cur : cur, back);
          CHECK(x >= -1 && x <= V8B_MAXOCT);
      } }
    /* random and truncated buffers into the parsers */
    for (k = 0; k < 20000; k++) {
        uint8_t h[40];
        int len = (int)(rnd() % 40);
        for (i = 0; i < len; i++) h[i] = (uint8_t)rnd();
        if (k & 1) h[0] = (uint8_t)((h[0] & 0xf0) | 1);
        if (k & 2 && len > 1) h[1] |= 0x40;
        res = v8bis_ms_is_k56(h, len);
        CHECK(res == 0 || res == 1);
        res = v8bis_cl_is_k56(h, len);
        CHECK(res == 0 || res == 1);
    }
    for (len_i = 0; len_i < 16; len_i++) {
        /* every prefix of the real client MS: only the whole one passes;
         * copied to an exact-size heap-free buffer so ASan sees an overrun */
        uint8_t pre[16];
        memcpy(pre, real_ms, (size_t)len_i);
        CHECK_EQ_I(v8bis_ms_is_k56(pre, len_i), 0);
        CHECK_EQ_I(v8bis_cl_is_k56(pre, len_i), 0);
    }
    for (len_i = 0; len_i < 10; len_i++) {
        uint8_t pre[10];
        memcpy(pre, cl9 + 6, (size_t)len_i);
        CHECK_EQ_I(v8bis_cl_is_k56(pre, len_i), 0);
    }

    /* pure preamble and flags: nothing */
    { int p = 0; static const uint8_t fl[8] = { 0, 1, 1, 1, 1, 1, 1, 0 };
      for (i = 0; i < 60; i++) b[p++] = 1;
      memcpy(b + p, fl, 8); p += 8; memcpy(b + p, fl, 8); p += 8;
      v8bis_rx_init(&r);
      CHECK_EQ_I(feed(&r, b, p, back), 0); }

    T_REPORT("test_v8bismsg");
}
