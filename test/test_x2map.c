/* x2's downstream mapper (modem/x2map, modem/x2tab, modem/x2bank;
 * docs/history/x2-facts.md, "Mapping", "Free sign", "Pads", "Banks").
 * Against a plain model of the facts written here, from sim/g711's decoder;
 * index 1's pad octets against tools/x2_pad.c's rule; every bank (rate
 * index 1..15, pad mode 0..2) against the facts' B and level counts, and
 * each through frame and unframe. */
#include <inttypes.h>
#include <string.h>
#include "test.h"
#include "g711.h"
#include "x2map.h"
#include "x2tab.h"
#include "x2call.h"
#include "../tools/x2_pad.h"

/* the facts' lists, as published ("Mapping", "Pads") */
static const uint8_t f_lev_a[9] = { 0xa5, 0xa7, 0xad, 0xaf, 0xb7, 0xbd, 0xc5, 0xcf, 0xe5 };
static const uint8_t f_lev_b[9] = { 0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf };
static const uint8_t f_recv3[9] = { 0xa5, 0xa8, 0xab, 0xae, 0xb4, 0xba, 0xc0, 0xcc, 0xe0 };
static const uint8_t f_sent3[9] = { 0x9d, 0x9f, 0xa3, 0xa7, 0xac, 0xb2, 0xba, 0xc5, 0xdb };
static const uint8_t f_sent6[9] = { 0x95, 0x98, 0x9b, 0x9e, 0xa4, 0xaa, 0xb0, 0xbc, 0xd3 };

/* the octet the client receives when o is sent across pad m */
static uint8_t pad_apply(uint8_t o, int m)
{
    static const int32_t gain[3] = { 32768, 23198, 16423 };
    int32_t v = ulaw_to_linear(o);
    int32_t a = v < 0 ? -v : v;
    a = (int32_t)(((int64_t)a * gain[m] + 16384) >> 15);
    return linear_to_ulaw(v < 0 ? -a : a);
}

/* the facts' mapper, step by step (the lists sorted largest magnitude
 * first, so a larger digit is a smaller level) */
typedef struct { int last; int64_t rds; } ref_t;
static void ref_frame(ref_t *r, int lev_a, int pad, const uint8_t *bits, uint8_t out[6])
{
    const uint8_t *lv = lev_a ? f_lev_a : pad == 1 ? f_sent3 : pad == 2 ? f_sent6 : f_lev_b;
    uint32_t x = 0;
    int d[6], i, k, fr = 0, neg;
    int32_t sum = 0;
    for (k = 0; k < 19; k++) x |= (uint32_t)bits[5 + k] << k;
    for (i = 0; i < 6; i++) { d[i] = (int)(x % 9); x /= 9; }
    for (i = 1; i < 6; i++) if (d[i] <= d[fr]) fr = i;     /* largest, the last of equals */
    for (i = 0, k = 0; i < 6; i++) {
        if (i == fr) continue;
        r->last ^= bits[k++];
        out[i] = r->last ? lv[d[i]] : (uint8_t)(lv[d[i]] & 0x7f);
        sum += ulaw_to_linear(out[i]);
    }
    if (lev_a)
        neg = sum > 805 * 4;
    else
        neg = r->rds + sum > 0;
    out[fr] = neg ? (uint8_t)(lv[d[fr]] & 0x7f) : lv[d[fr]];
    r->rds += sum + ulaw_to_linear(out[fr]);
}

static uint32_t rng = 0x9e3779b9u;
static uint32_t xs32(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void rand_bits(uint8_t *bits)
{
    uint32_t r = xs32(), x = xs32() % 524288u;  /* 2^19 */
    int k;
    for (k = 0; k < 5; k++) bits[k] = (uint8_t)((r >> k) & 1);
    for (k = 0; k < 19; k++) bits[5 + k] = (uint8_t)((x >> k) & 1);
}

/* signs (5) and the number x into a frame's bits */
static void set_bits(uint8_t *bits, unsigned signs, uint32_t x)
{
    int k;
    for (k = 0; k < 5; k++) bits[k] = (uint8_t)((signs >> k) & 1);
    for (k = 0; k < 19; k++) bits[5 + k] = (uint8_t)((x >> k) & 1);
}

static int in_list(uint8_t o, const uint8_t *l)
{
    int i;
    for (i = 0; i < 9; i++) if ((o | 0x80) == l[i]) return i;
    return -1;
}

/* n random frames: the mapper against the model, each octet from its list,
 * the frame back through unframe as the client receives it behind the pad;
 * returns the number of frames that differ */
static int run(int lev_a, int pad, int n, int *nneg)
{
    x2map_t m, u;
    ref_t r = { 1, 0 };
    const uint8_t *lv = lev_a ? f_lev_a : pad == 1 ? f_sent3 : pad == 2 ? f_sent6 : f_lev_b;
    uint8_t bits[24], back[24], out[6], want[6], rx[6];
    int f, i, bad = 0;
    x2map_init(&m, 1, pad, lev_a);
    x2map_init(&u, 1, pad, lev_a);
    *nneg = 0;
    for (f = 0; f < n; f++) {
        int ok = 1;
        rand_bits(bits);
        x2map_frame(&m, bits, out);
        ref_frame(&r, lev_a, pad, bits, want);
        ok &= memcmp(out, want, 6) == 0;
        for (i = 0; i < 6; i++) ok &= in_list(out[i], lv) >= 0;
        for (i = 0; i < 6; i++) rx[i] = lev_a ? out[i] : pad_apply(out[i], pad);
        ok &= x2map_unframe(&u, rx, back) == 24 && memcmp(back, bits, 24) == 0;
        for (i = 0; i < 6; i++) *nneg += !(out[i] & 0x80);
        if (!ok) bad++;
    }
    CHECK_EQ_I(m.rds, r.rds);
    return bad;
}

/* ---- every bank (x2-facts.md "Banks") ---- */

/* B per rate index; levels per position, the first and then the others, in
 * pad modes 0, 1 and 2 */
static const int f_b[15] = { 19, 22, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37 };
static const int f_cnt[3][15][2] = {
    { { 9, 9 }, { 12, 13 }, { 18, 18 }, { 21, 20 }, { 27, 22 }, { 28, 25 }, { 28, 29 }, { 32, 32 },
      { 41, 35 }, { 42, 40 }, { 47, 45 }, { 50, 51 }, { 52, 62 }, { 64, 64 }, { 82, 70 } },
    { { 9, 9 }, { 12, 13 }, { 18, 18 }, { 28, 19 }, { 19, 26 }, { 23, 26 }, { 27, 29 }, { 38, 31 },
      { 32, 40 }, { 42, 40 }, { 47, 45 }, { 55, 50 }, { 58, 57 }, { 70, 63 }, { 72, 72 } },
    { { 9, 9 }, { 12, 13 }, { 18, 18 }, { 21, 20 }, { 27, 22 }, { 28, 25 }, { 28, 29 }, { 32, 32 },
      { 41, 35 }, { 42, 40 }, { 47, 45 }, { 50, 51 }, { 52, 62 }, { 64, 64 }, { 82, 70 } },
};

/* the facts' mapper with the bank's lists: the digits in mixed radix, the
 * free sign at the largest level received (the last of equals), the MD
 * signs on the others in position order */
static void ref_bank(ref_t *r, const x2map_t *m, const uint8_t *bits, uint8_t out[6])
{
    const uint8_t *rv[6], *sn[6];
    uint64_t x = 0;
    int b = x2map_bits(m) - 5, d[6], i, k, fr = 0, neg;
    int32_t sum = 0;
    for (k = 0; k < b; k++) x |= (uint64_t)bits[5 + k] << k;
    for (i = 0; i < 6; i++) {
        int n = x2map_list(m, i, &rv[i], &sn[i]);
        d[i] = (int)(x % (uint64_t)n); x /= (uint64_t)n;
    }
    for (i = 1; i < 6; i++) if (rv[i][d[i]] <= rv[fr][d[fr]]) fr = i;
    for (i = 0, k = 0; i < 6; i++) {
        if (i == fr) continue;
        r->last ^= bits[k++];
        out[i] = r->last ? sn[i][d[i]] : (uint8_t)(sn[i][d[i]] & 0x7f);
        sum += ulaw_to_linear(out[i]);
    }
    neg = r->rds + sum > 0;
    out[fr] = neg ? (uint8_t)(sn[fr][d[fr]] & 0x7f) : sn[fr][d[fr]];
    r->rds += sum + ulaw_to_linear(out[fr]);
}

/* each bank: its B and level counts; each list sorted, the octets sent
 * arriving behind the pad as the levels (but for the two of mode 1 no
 * octet arrives as); enough digits for B bits; frames as the model's,
 * and back through the pad and unframe */
static void banks(void)
{
    int mode, n, p, i, f, k;
    long bad = 0, odd = 0, wrong = 0, frames = 0;
    for (mode = 0; mode < 3; mode++)
        for (n = 1; n <= 15; n++) {
            x2map_t m, u;
            ref_t r = { 1, 0 };
            const uint8_t *rv, *sn;
            double prod = 1;
            int short_of = 0;
            x2map_init(&m, n, mode, 0);
            if (x2map_bits(&m) != f_b[n - 1] + 5) { bad++; printf("  index %d mode %d: %d bits\n", n, mode, x2map_bits(&m)); continue; }
            for (p = 0; p < 6; p++) {
                int c = x2map_list(&m, p, &rv, &sn);
                if (c != f_cnt[mode][n - 1][p > 0]) { bad++; short_of = 1; continue; }
                prod *= c;
                for (i = 0; i < c; i++) {
                    if (!(rv[i] & 0x80) || !(sn[i] & 0x80) || (i && rv[i] <= rv[i - 1])) bad++;
                    if (mode == 0 ? sn[i] != rv[i] : pad_apply(sn[i], mode) != rv[i]) {
                        /* x2-facts.md "Banks": received e2 sent dc, received
                           f1 sent ed, in mode 1 only */
                        if (mode == 1 && ((rv[i] == 0xe2 && sn[i] == 0xdc) || (rv[i] == 0xf1 && sn[i] == 0xed))) odd++;
                        else bad++;
                    }
                }
            }
            if (short_of) continue;     /* (not the lists to frame with) */
            if (prod < (double)((uint64_t)1 << f_b[n - 1])) bad++;
            x2map_init(&u, n, mode, 0);
            for (f = 0; f < 2000; f++) {
                uint8_t bits[64], back[64], out[6], want[6], rx[6];
                int nb = x2map_bits(&m), ok;
                for (k = 0; k < nb; k++) bits[k] = (uint8_t)(xs32() & 1);
                x2map_frame(&m, bits, out);
                ref_bank(&r, &m, bits, want);
                ok = memcmp(out, want, 6) == 0;
                for (i = 0; i < 6; i++) rx[i] = x2call_level(&u, i, pad_apply(out[i], mode));
                ok &= x2map_unframe(&u, rx, back) == nb && memcmp(back, bits, (size_t)nb) == 0;
                if (!ok) wrong++;
                frames++;
            }
            if (m.rds != r.rds) wrong++;
        }
    printf("x2map: 45 banks, %ld entries not as the facts, %ld mode 1 levels no octet arrives as;"
           " %ld frames, %ld not as the model or not back\n", bad, odd, frames, wrong);
    CHECK_EQ_I(bad, 0); CHECK_EQ_I(odd, 49); CHECK_EQ_I(wrong, 0); CHECK_EQ_I(frames, 45 * 2000);

    /* x2-facts.md "Banks": mode 2 receives mode 0's lists, position for
       position (and sends other octets for them) */
    {
        int diff = 0;
        for (n = 1; n <= 15; n++) {
            x2map_t m0, m2;
            x2map_init(&m0, n, 0, 0);
            x2map_init(&m2, n, 2, 0);
            for (p = 0; p < 6; p++) {
                const uint8_t *rv0, *sn0, *rv2, *sn2;
                int c0 = x2map_list(&m0, p, &rv0, &sn0), c2 = x2map_list(&m2, p, &rv2, &sn2);
                if (c0 != c2 || memcmp(rv0, rv2, (size_t)c0) != 0) {
                    diff++;
                    printf("  index %d position %d: mode 2 receives other levels than mode 0\n", n, p);
                }
            }
        }
        printf("x2map: mode 2's received lists as mode 0's at %d of 90 positions\n", 90 - diff);
        CHECK_EQ_I(diff, 0);
    }

    /* where the lists differ, the levels decide, not the digits: index 12
       in mode 0, all digits 0 are 9e at position 0 and 9f at the others;
       the free sign goes to position 0 (the largest level), the signs
       11111 to 1..5: - + - + - */
    {
        x2map_t m;
        uint8_t bits[64], out[6];
        x2map_init(&m, 12, 0, 0);
        memset(bits, 0, sizeof bits);
        for (k = 0; k < 5; k++) bits[k] = 1;
        x2map_frame(&m, bits, out);
        CHECK(out[1] == 0x1f && out[2] == 0x9f && out[3] == 0x1f && out[4] == 0x9f && out[5] == 0x1f);
        CHECK_EQ_I(out[0], 0x9e);
    }
}

int main(void)
{
    x2map_t m, u;
    ref_t r = { 1, 0 };
    uint8_t bits[24], back[24], out[6], want[6];
    int i, f, nneg, bad;

    /* the tables: as the facts publish them; index 1's octets sent behind
       each pad as tools/x2_pad.c derives them from the levels received */
    CHECK(memcmp(x2_levels_a, f_lev_a, 9) == 0);
    {
        static const uint8_t *const f_recv[3] = { f_lev_b, f_recv3, f_lev_b };
        static const uint8_t *const f_sent[3] = { f_lev_b, f_sent3, f_sent6 };
        const uint8_t *rv, *sn;
        int p;
        for (bad = 0, f = 0; f < 3; f++) {
            x2map_init(&m, 1, f, 0);
            for (p = 0; p < 6; p++) {
                if (x2map_list(&m, p, &rv, &sn) != 9) { bad++; continue; }
                if (memcmp(rv, f_recv[f], 9) != 0 || memcmp(sn, f_sent[f], 9) != 0) bad++;
                for (i = 0; i < 9; i++) {
                    if (sn[i] != x2_pad_octet(rv[i], f)) bad++;
                    if (pad_apply(sn[i], f) != rv[i]) bad++;
                }
            }
        }
        CHECK_EQ_I(bad, 0);
    }

    x2map_init(&m, 1, 0, 0);
    CHECK_EQ_I(x2map_bits(&m), 24);
    x2map_init(&m, 1, 0, 1);
    CHECK_EQ_I(x2map_bits(&m), 24);
    x2map_init(&m, 0, 0, 0);
    CHECK_EQ_I(x2map_bits(&m), 0);
    x2map_init(&m, 16, 0, 0);
    CHECK_EQ_I(x2map_bits(&m), 0);
    x2map_init(&m, 1, 3, 0);
    CHECK_EQ_I(x2map_bits(&m), 0);
    /* levels A are index 1's (the start-up's) alone: none at index 2, no
       bits, a frame of zeros, nothing to unframe */
    x2map_init(&m, 2, 0, 1);
    CHECK_EQ_I(x2map_bits(&m), 0);
    memset(bits, 0, sizeof bits);
    x2map_frame(&m, bits, out);
    CHECK(out[0] == 0xff && out[1] == 0xff && out[2] == 0xff && out[3] == 0xff && out[4] == 0xff && out[5] == 0xff);
    out[0] = out[1] = out[2] = out[3] = out[4] = out[5] = 0xa5;
    CHECK_EQ_I(x2map_unframe(&m, out, back), -1);

    /* all digits 3: equal levels; the signs go to positions 0..4 (the first
       of equals), the free sign to 5. Signs 11111 from last = 1: - + - + - */
    x2map_init(&m, 1, 0, 0);
    set_bits(bits, 0x1f, 3u * (1 + 9 + 81 + 729 + 6561 + 59049));
    x2map_frame(&m, bits, out);
    CHECK(out[0] == 0x2e && out[1] == 0xae && out[2] == 0x2e && out[3] == 0xae && out[4] == 0x2e);
    CHECK_EQ_I(out[5] | 0x80, 0xae);
    CHECK_EQ_I(m.last, 0);
    /* the next frame's signs carry on from last: 00000 keeps them negative */
    set_bits(bits, 0, 3u * (1 + 9 + 81 + 729 + 6561 + 59049));
    x2map_frame(&m, bits, out);
    CHECK(out[0] == 0x2e && out[1] == 0x2e && out[2] == 0x2e && out[3] == 0x2e && out[4] == 0x2e);

    /* digits 0 0 3 3 3 3: positions 0 and 1 hold the largest level; the
       first of them takes a sign, the free sign goes to 1. Signs 11111:
       - + - + - on positions 0, 2, 3, 4, 5 */
    x2map_init(&m, 1, 0, 0);
    set_bits(bits, 0x1f, 3u * (81 + 729 + 6561 + 59049));
    x2map_frame(&m, bits, out);
    CHECK(out[0] == 0x25 && out[2] == 0xae && out[3] == 0x2e && out[4] == 0xae && out[5] == 0x2e);
    CHECK_EQ_I(out[1] | 0x80, 0xa5);

    /* digits 5 2 7 2 4 6: the smallest levels are at 2, 5, 0, 4, then 1
       and 3 equal: 1 takes the sign, 3 the free one. Signs 11111 on
       positions 0, 1, 2, 4, 5: - + - + - */
    x2map_init(&m, 1, 0, 0);
    set_bits(bits, 0x1f, 5u + 2u * 9 + 7u * 81 + 2u * 729 + 4u * 6561 + 6u * 59049);
    x2map_frame(&m, bits, out);
    CHECK(out[0] == 0x39 && out[1] == 0xab && out[2] == 0x4b && out[4] == 0xb3 && out[5] == 0x3f);
    CHECK_EQ_I(out[3] | 0x80, 0xab);
    x2map_init(&u, 1, 0, 0);
    CHECK_EQ_I(x2map_unframe(&u, out, back), 24);
    CHECK(memcmp(back, bits, 24) == 0);

    /* not a level: refused */
    out[2] = 0xa6;
    x2map_init(&u, 1, 0, 0);
    CHECK_EQ_I(x2map_unframe(&u, out, back), -1);

    /* 10000 random frames each: levels B, levels A, and levels B behind
       the 3 dB and 6 dB pads (sent as the pad pairs' sending octets);
       levels A is sent unchanged whatever the pad */
    CHECK_EQ_I(run(0, 0, 10000, &nneg), 0);
    CHECK(nneg > 0);
    CHECK_EQ_I(run(1, 0, 10000, &nneg), 0);
    CHECK(nneg > 0);
    CHECK_EQ_I(run(0, 1, 10000, &nneg), 0);
    CHECK_EQ_I(run(0, 2, 10000, &nneg), 0);
    CHECK_EQ_I(run(1, 2, 10000, &nneg), 0);

    /* levels A's free sign: negative exactly when the other five sum to
       more than 805 (14-bit units); both cases occur */
    {
        int seen[2] = { 0, 0 }, wrong = 0;
        x2map_init(&m, 1, 0, 1);
        for (f = 0; f < 10000; f++) {
            int d[6], fr = 0, k;
            uint32_t x = 0;
            int32_t sum = 0;
            rand_bits(bits);
            x2map_frame(&m, bits, out);
            for (k = 0; k < 19; k++) x |= (uint32_t)bits[5 + k] << k;
            for (i = 0; i < 6; i++) { d[i] = (int)(x % 9); x /= 9; }
            for (i = 1; i < 6; i++) if (d[i] <= d[fr]) fr = i;
            for (i = 0; i < 6; i++) if (i != fr) sum += ulaw_to_linear(out[i]) / 4;
            if ((sum > 805) != !(out[fr] & 0x80)) wrong++;
            seen[sum > 805]++;
        }
        CHECK_EQ_I(wrong, 0);
        CHECK(seen[0] > 0 && seen[1] > 0);
        /* the nearest sums each side (805 itself cannot occur): digits
           5 2 6 2 0 0, signs 00111, the five sum to 807: free (position 5)
           negative; digits 4 3 0 4 0 0, signs 10111, 801: positive */
        x2map_init(&m, 1, 0, 1);
        set_bits(bits, 0x1c, 1967);
        x2map_frame(&m, bits, out);
        CHECK_EQ_I(out[5], 0x25);
        x2map_init(&m, 1, 0, 1);
        set_bits(bits, 0x1d, 2947);
        x2map_frame(&m, bits, out);
        CHECK_EQ_I(out[5], 0xa5);
    }

    /* the sync in levels A, then the data levels: last and the running
       sum carry on. Levels A's rule lets the sum drift (it is a frame's
       sum against +805, not zero); the data levels' free sign brings it
       back and then keeps it down */
    {
        int64_t peak = 0;
        x2map_init(&m, 1, 0, 1);
        r.last = 1; r.rds = 0;
        for (bad = 0, f = 0; f < 3000; f++) {
            int lev_a = f < 500;
            m.levels_a = lev_a;
            rand_bits(bits);
            x2map_frame(&m, bits, out);
            ref_frame(&r, lev_a, 0, bits, want);
            if (memcmp(out, want, 6) != 0) bad++;
            if (f >= 1000) {
                int64_t a = m.rds < 0 ? -m.rds : m.rds;
                if (a > peak) peak = a;
            }
        }
        CHECK_EQ_I(bad, 0);
        CHECK_EQ_I(m.rds, r.rds);
        CHECK(peak < 6 * 8000);
        printf("x2map: data levels, running sum within +-%" PRId64 " from 500 frames after the switch\n", peak);
    }

    banks();
    T_REPORT("test_x2map");
}
