/* x2's training script (modem/x2tx) against docs/history/x2-facts.md,
 * "Phase 3 script", "Records" and "Mapping": each segment's length and
 * content, read back the way a client would (the signs through GPC's
 * descrambler from state 0, the frames through x2map_unframe). */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../modem/x2tx.h"
#include "../modem/x2rec.h"
#include "../modem/x2tab.h"
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

        /* the record: 4 frames in levels A, F33C 7FFF 0000 with its CRC,
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

            /* data: the owner's bits */
            x2tx_data(&T, 1, 0, src_get, NULL);
            {
                uint32_t keep = rng; long errs = 0, nb = 0, notdata = 0;
                for (f = 0; f < 2000; f++) {
                    for (i = 0; i < 6; i++) fr[i] = x2tx_tick(&T);
                    if (T.seg != X2TX_DATA) notdata++;
                    n = x2map_unframe(&mb, fr, bits);
                    if (n != 24) { errs += 24; continue; }
                    for (i = 0; i < n; i++) {
                        int b = v90_descramble_gpc(&D, bits[i]);
                        keep ^= keep << 13; keep ^= keep >> 17; keep ^= keep << 5;
                        if (b != (int)(keep & 1)) errs++;
                        nb++;
                    }
                }
                printf("  x2tx data: %ld bits, %ld wrong; %d bit/s\n", nb, errs, x2tx_rate(&T));
                CHECK_EQ_I(notdata, 0); CHECK_EQ_I(errs, 0); CHECK_EQ_I(nb, 48000); CHECK_EQ_I(x2tx_rate(&T), 32000);
            }
        }
    }
    T_REPORT("x2tx");
}
