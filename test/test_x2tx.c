/* x2's training script (modem/x2tx) against docs/history/x2-facts.md,
 * "Phase 3 script", "Records" and "Mapping": each segment's length and
 * content, read back the way a client would (the signs through GPC's
 * descrambler from state 0, the frames through x2map_unframe). */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../modem/x2tx.h"
#include "../modem/x2d.h"
#include "../modem/x2rec.h"
#include "../modem/x2tab.h"
#include "../sim/x2call.h"
#include "../v90/v90_scr.h"

/* the preamble's 22 octets as the facts list them (RE (D-Modem)) */
static const char *pre_text = "ab bd ab 2b 3d 2b ab 2b 3d bd 3d bd ab bd 3d 2b 3d bd 3d 2b ab 2b";

static uint32_t rng = 12345;
static int rbit(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (int)(rng & 1); }
static int src_get(void *u) { (void)u; return rbit(); }

static x2tx_t T;
static v90_scr_t D;           /* the client's descrambler */

/* a TRN-type octet: its level, and the data bit its sign carries */
static int sign_bit(uint8_t o) { return v90_descramble_gpc(&D, (o & 0x80) ? 0 : 1); }

/* the owner's bits as the client expects them: src_get's generator, run
 * on from where the data began */
static uint32_t keep;
static int want_bit(void) { keep ^= keep << 13; keep ^= keep >> 17; keep ^= keep << 5; return (int)(keep & 1); }

/* a frame as the client takes it: behind m's pad, each octet the level
 * whose octet sent arrives as it (sim/x2call) */
static int take(x2map_t *m, const uint8_t fr[6], uint8_t *bits)
{
    uint8_t rx[6];
    int i;
    for (i = 0; i < 6; i++) rx[i] = m->levels_a ? fr[i] : x2call_level(m, i, x2call_pad(fr[i], m->pad_mode));
    return x2map_unframe(m, rx, bits);
}

/* n data frames of the owner's bits in the client's map m */
static void data_frames(x2map_t *m, int n, long *nb, long *errs, long *notdata)
{
    uint8_t fr[6], bits[64];
    int f, i, k;
    for (f = 0; f < n; f++) {
        for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
        if (T.seg != X2TX_DATA) (*notdata)++;
        k = take(m, fr, bits);
        if (k <= 0) { *errs += 64; continue; }
        for (i = 0; i < k; i++) {
            if (v90_descramble_gpc(&D, bits[i]) != want_bit()) (*errs)++;
            (*nb)++;
        }
    }
}

/* One rate change of the client's, from data in the client's map mb: its
 * MP (x2tx_data with the index and pad it reports, x2tx_go) three octets
 * into a data frame. That frame ends with data; then 8 frames with each
 * position at hi (hi[0] position 0, hi[1] the others) in positions 0..2
 * and turned negative in 3..5, and one the other way round, no
 * differential signs; GPC from zero and the sign positive; our record in
 * index 1's levels for the pad, 10 times 733C and 33 times F33C; one frame
 * of ones in them, 681 of ones at the new rate; data, the owner's bits on
 * from where they stopped, 1000 frames. mb follows. */
typedef struct { long mbad, nak, ack, rbad, order, pad_bad, ones_b, ones_n, nb, errs, notdata; } rc_t;
static void rate_change(x2map_t *mb, int index, int pad, const uint8_t hi[2], rc_t *o)
{
    x2map_t mr;
    uint8_t fr[6], bits[64], rb[96]; uint16_t w[3];
    int f, i, k, n, r, j;
    memset(o, 0, sizeof *o);
    x2tx_data(&T, index, pad, src_get, NULL);
    for (i = 0; i < 3; i++) fr[i] = x2tx_tick(&T);
    x2tx_go(&T);
    for (; i < 6; i++) fr[i] = x2tx_tick(&T);
    n = take(mb, fr, bits);
    for (i = 0; i < n; i++) if (v90_descramble_gpc(&D, bits[i]) != want_bit()) o->errs++;
    if (n <= 0) o->errs += 64;
    for (f = 0; f < 9; f++)
        for (i = 0; i < 6; i++) {
            uint8_t h = hi[i > 0];
            if (x2tx_tick(&T) != (((i < 3) == (f < 8)) ? h : (uint8_t)(h & 0x7f))) o->mbad++;
        }
    v90_scr_init(&D); x2map_init(&mr, 1, pad, 0);
    for (r = 0; r < 43; r++) {
        for (j = 0; j < 4; j++) {
            for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
            if (take(&mr, fr, bits) != 24) { o->rbad++; continue; }
            for (i = 0; i < 24; i++) rb[j * 24 + i] = (uint8_t)v90_descramble_gpc(&D, bits[i]);
        }
        if (x2rec_parse(rb, 96, w, 3) == 3 && (w[0] & 0x7fff) == 0x733C && w[1] == X2TX_REC_W1 && w[2] == 0) {
            if (w[0] >> 15) o->ack++;
            else { o->nak++; if (o->ack) o->order++; }
        } else {
            o->rbad++;
        }
        for (i = X2REC_BITS(3); i < 96; i++) if (rb[i]) o->pad_bad++;
    }
    for (f = 0; f < 1 + X2TX_START_FRAMES; f++) {
        for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
        if (f == 0 && T.seg != X2TX_START) o->ones_b = -100;
        if (f == 1) mr.index = index;           /* the rate asked for */
        n = take(&mr, fr, bits);
        for (i = 0, k = 0; i < n; i++) k += v90_descramble_gpc(&D, bits[i]);
        if (n > 0 && k == n) { if (f == 0) o->ones_b++; else o->ones_n++; }
    }
    *mb = mr;
    data_frames(mb, 1000, &o->nb, &o->errs, &o->notdata);
}

int main(void)
{
    uint8_t want[22];
    long k, bad;
    int f, i;

    for (i = 0; i < 22; i++) {
        unsigned v; sscanf(pre_text + 3 * i, "%2x", &v); want[i] = (uint8_t)v;
    }

    x2tx_init(&T);
    v90_scr_init(&D);
    /* silent until started */
    for (k = 0, bad = 0; k < 1000; k++) if (x2tx_tick(&T) != 0x7f) bad++;
    CHECK_EQ_I(bad, 0); CHECK_EQ_I(T.seg, X2TX_SILENCE);
    x2tx_start(&T);

    /* lead: 20 x 7f */
    for (k = 0, bad = 0; k < X2TX_LEAD_N; k++) if (x2tx_tick(&T) != 0x7f) bad++;
    CHECK_EQ_I(bad, 0); CHECK_EQ_I(T.seg, X2TX_PRE);

    /* preamble: the 22 octets 16 times, then their first 11 with bit 7 turned */
    for (k = 0, bad = 0; k < 352; k++) if (x2tx_tick(&T) != want[k % 22]) bad++;
    for (k = 0; k < 11; k++) if (x2tx_tick(&T) != (want[k] ^ 0x80)) bad++;
    CHECK_EQ_I(bad, 0); CHECK_EQ_I(T.seg, X2TX_TRN);

    /* TRN: 20004 scrambled ones, GPC from state 0, b1 for a scrambled 0, 31
     * for a 1; the first is GPC's first output for a one from zero: 1 */
    {
        long lv = 0, ones = 0; uint8_t first = 0;
        for (k = 0; k < X2TX_TRN_N; k++) {
            uint8_t o = x2tx_tick(&T);
            if (k == 0) first = o;
            if ((o & 0x7f) != 0x31) lv++;
            ones += sign_bit(o);
        }
        printf("  x2tx TRN: %ld octets, %ld not at b1 / 31, %ld ones after the descrambler\n", (long)X2TX_TRN_N, lv, ones);
        CHECK_EQ_I(first, 0x31);
        CHECK_EQ_I(lv, 0); CHECK_EQ_I(ones, X2TX_TRN_N);
        CHECK_EQ_I(T.seg, X2TX_JW);
    }

    /* J words: 1200 samples, 09B0 99 times then 099F, LSB first, GPC on */
    {
        long lv = 0, words_ok = 0;
        for (i = 0; i < 100; i++) {
            unsigned w = 0;
            for (k = 0; k < 12; k++) {
                uint8_t o = x2tx_tick(&T);
                if ((o & 0x7f) != 0x31) lv++;
                w |= (unsigned)sign_bit(o) << k;
            }
            if (w == (i < 99 ? 0x09B0u : 0x099Fu)) words_ok++;
        }
        printf("  x2tx J words: %ld of 100 as expected\n", words_ok);
        CHECK_EQ_I(lv, 0); CHECK_EQ_I(words_ok, 100); CHECK_EQ_I(T.seg, X2TX_ONES);
    }

    /* ones: 6000 at c1 / 41 */
    {
        long lv = 0, ones = 0;
        for (k = 0; k < X2TX_ONES_N; k++) {
            uint8_t o = x2tx_tick(&T);
            if ((o & 0x7f) != 0x41) lv++;
            ones += sign_bit(o);
        }
        CHECK_EQ_I(lv, 0); CHECK_EQ_I(ones, X2TX_ONES_N); CHECK_EQ_I(T.seg, X2TX_ALIGN);
    }

    /* alignment: 192 frames, frame f the pattern turned f places left */
    {
        static const uint8_t pat[6] = { 0x9e, 0x28, 0xa1, 0x21, 0xa8, 0x1e };
        for (f = 0, bad = 0; f < 192; f++)
            for (i = 0; i < 6; i++) if (x2tx_tick(&T) != pat[(i + f) % 6]) bad++;
        CHECK_EQ_I(bad, 0); CHECK_EQ_I(T.seg, X2TX_SYNC);
    }

    /* sync: frames that decode as ones in levels A, 1667 of them though go
     * comes early; the client's x2map from the first sync frame on */
    {
        x2map_t m; uint8_t fr[6], bits[32];
        long frames = 0, ones = 0, nbits = 0, notlv = 0;
        x2map_init(&m, 1, 0, 1);
        x2tx_go(&T);
        while (T.seg == X2TX_SYNC || (T.seg == X2TX_REC && T.n == 0)) {
            int n;
            for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
            if (T.seg != X2TX_SYNC) break;
            n = x2map_unframe(&m, fr, bits);
            if (n != 24) { notlv++; continue; }
            for (i = 0; i < n; i++) { ones += v90_descramble_gpc(&D, bits[i]); nbits++; }
            frames++;
        }
        printf("  x2tx sync: %ld frames, %ld of %ld bits ones\n", frames, ones, nbits);
        CHECK_EQ_I(frames, X2TX_SYNC_MIN / 6); CHECK_EQ_I(notlv, 0); CHECK_EQ_I(ones, nbits);
        CHECK_EQ_I(T.seg, X2TX_REC);

        /* the record: 4 frames in levels A, F33C 7FFF 0000 with its CRC
         * (word 1: the rate indices we have banks for, bit n - 1 for index
         * n as the MP's word 1: all 15; x2-facts.md "Records"),
         * zeros to 96 bits; whole records until go */
        {
            uint8_t rb[96]; uint16_t w[3];
            int nrec = 0, good = 0, pad_ok = 1, j = 0;
            /* (the first frame was read above) */
            for (;;) {
                int n = x2map_unframe(&m, fr, bits);
                CHECK_EQ_I(n, 24);
                for (i = 0; i < 24; i++) rb[j * 24 + i] = (uint8_t)v90_descramble_gpc(&D, bits[i]);
                if (++j == 4) {
                    nrec++; j = 0;
                    if (x2rec_parse(rb, 96, w, 3) == 3 && w[0] == 0xF33C && w[1] == 0x7FFF && w[2] == 0) good++;
                    for (i = X2REC_BITS(3); i < 96; i++) if (rb[i]) pad_ok = 0;
                }
                if (nrec == 3 && j == 1) x2tx_go(&T);     /* in the middle of the fourth */
                for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
                if (T.seg != X2TX_REC) break;
            }
            printf("  x2tx records: %d sent, %d decode as F33C 7FFF 0000\n", nrec, good);
            CHECK_EQ_I(nrec, 4); CHECK_EQ_I(good, 4); CHECK(pad_ok); CHECK_EQ_I(j, 0);
            CHECK_EQ_I(T.seg, X2TX_START);
        }

        /* the start: one frame of ones in levels A, then 680 in the data
         * levels (index 1, no pad: levels B); the signs carry on */
        {
            long ok = 0;
            x2map_t mb = m;
            int n = x2map_unframe(&m, fr, bits);
            for (i = 0, k = 0; i < n; i++) k += v90_descramble_gpc(&D, bits[i]);
            CHECK_EQ_I(n, 24); CHECK_EQ_I(k, 24);
            mb = m; mb.levels_a = 0; mb.index = 1; mb.pad_mode = 0;
            for (f = 1; f < X2TX_START_FRAMES; f++) {
                for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
                n = x2map_unframe(&mb, fr, bits);
                if (n != 24) continue;
                for (i = 0, k = 0; i < n; i++) k += v90_descramble_gpc(&D, bits[i]);
                if (k == 24) ok++;
            }
            printf("  x2tx start: %ld of 680 frames ones in levels B\n", ok);
            CHECK_EQ_I(ok, 680);

            /* data: the owner's bits, at 32000 */
            x2tx_data(&T, 1, 0, src_get, NULL);
            keep = rng;
            {
                long errs = 0, nb = 0, notdata = 0;
                data_frames(&mb, 2000, &nb, &errs, &notdata);
                printf("  x2tx data: %ld bits, %ld wrong; %d bit/s\n", nb, errs, x2tx_rate(&T));
                CHECK_EQ_I(notdata, 0); CHECK_EQ_I(errs, 0); CHECK_EQ_I(nb, 48000); CHECK_EQ_I(x2tx_rate(&T), 32000);
            }

            /* the client's rate changes (x2-facts.md "Rate change"): to index
             * 1 again; to 12; from 12 to 14, where the marker is 52000's
             * largest levels, 9e at position 0 and 9f at the others (the
             * I-modem's); from 14 to 8 with an MP that reports a 6 dB pad
             * (the start-up's none): from the MP on, all we send is for the
             * pad it reports, the marker too (index 14's largest levels
             * sent for 6 dB: 8d, 8e), the records in index 1's levels for
             * it, and the new rate */
            {
                static const struct { int index, pad, rate; uint8_t hi[2]; } rc[4] = {
                    { 1, 0, 32000, { 0xa5, 0xa5 } }, { 12, 0, 52000, { 0xa5, 0xa5 } },
                    { 14, 0, 54666, { 0x9e, 0x9f } }, { 8, 2, 46666, { 0x8d, 0x8e } } };
                int j;
                for (j = 0; j < 4; j++) {
                    rc_t o;
                    rate_change(&mb, rc[j].index, rc[j].pad, rc[j].hi, &o);
                    printf("  x2tx rate change to index %d, pad mode %d: marker %ld bad; records %ld 733C then %ld F33C (%ld bad);"
                           " ones %ld + %ld frames; data %ld bits on, %ld wrong; %d bit/s\n", rc[j].index, rc[j].pad,
                           o.mbad, o.nak, o.ack, o.rbad, o.ones_b, o.ones_n, o.nb, o.errs, x2tx_rate(&T));
                    CHECK_EQ_I(o.mbad, 0);
                    CHECK_EQ_I(o.nak, 10); CHECK_EQ_I(o.ack, 33); CHECK_EQ_I(o.rbad, 0); CHECK_EQ_I(o.order, 0); CHECK_EQ_I(o.pad_bad, 0);
                    CHECK_EQ_I(o.ones_b, 1); CHECK_EQ_I(o.ones_n, X2TX_START_FRAMES);
                    CHECK_EQ_I(o.notdata, 0); CHECK_EQ_I(o.errs, 0);
                    CHECK_EQ_I(o.nb, 1000L * x2map_bits(&T.map));
                    CHECK_EQ_I(x2tx_rate(&T), rc[j].rate);
                    CHECK_EQ_I(T.map.index, rc[j].index); CHECK_EQ_I(T.map.pad_mode, rc[j].pad);
                }
            }
        }
    }

    /* our record's word 1 names every index we have a bank for, in each
       pad mode, and X2D_RATE_MASK is it */
    {
        x2map_t m;
        int n, mode;
        CHECK_EQ_I(X2TX_REC_W1, 0x7FFF); CHECK_EQ_I(X2D_RATE_MASK, X2TX_REC_W1);
        for (n = 1, bad = 0; n <= 15; n++)
            for (mode = 0; mode < 3; mode++) {
                x2map_init(&m, n, mode, 0);
                if (((X2TX_REC_W1 >> (n - 1)) & 1) != (x2map_bits(&m) > 0)) bad++;
            }
        CHECK_EQ_I(bad, 0);
    }
    T_REPORT("x2tx");
}
