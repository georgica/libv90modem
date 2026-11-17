/* modem/v90adec, the analogue modem's PCM data decoder, against the
 * library's digital transmitter (v90tx: B1d, then data) for every Sr and
 * ld, both laws and a range of K: the octets it sends, decoded and
 * descrambled, must give back 48 data frames of ones and then exactly the
 * bits the transmitter took. Then what a decision error does: a Ucode
 * outside the constellation is reported, a wrong sign spoils at most the
 * frame it is in and the next one. */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "test.h"
#include "v90adec.h"
#include "v90_tx.h"
#include "v90_pcm.h"
#include "v90_scr.h"

typedef struct { uint32_t s; } prng_t;
static int prng_bit(prng_t *p) { p->s = p->s * 1103515245u + 12345u; return (int)((p->s >> 17) & 1); }

static prng_t src_rng;
static int src_bit(void *ctx) { (void)ctx; return prng_bit(&src_rng); }

/* M points per interval, every `step`-th Ucode from `top` down, a different
 * top in each interval */
static void make_map(v90_map_t *m, int K, int Sr, int ld, int M, int step)
{
    int i, k;
    memset(m, 0, sizeof *m);
    m->K = (uint8_t)K; m->Sr = (uint8_t)Sr; m->ld = (uint8_t)ld;
    if (Sr) { m->a1 = 60; m->a2 = -58; m->b1 = 10; m->b2 = -10; }
    for (i = 0; i < 6; i++) {
        uint8_t mask[16];
        memset(mask, 0, sizeof mask);
        for (k = 0; k < M; k++) { int u = 127 - i - k * step; mask[u >> 3] |= (uint8_t)(1 << (u & 7)); }
        v90_map_set_points(m, i, mask);
    }
}

static v90tx_t tx;

/* frames: data frames compared after B1d; returns bit errors (-1: a frame
 * was reported bad) */
static int32_t round_trip(int alaw, const v90_map_t *m, int frames, int32_t *nbits)
{
    v90adec_t d;
    v90_scr_t scr;
    prng_t ref;
    uint8_t u[6], pos[6], bits[V90_MAX_D];
    int32_t n, err = 0, total = (48 + frames) * 6;
    int D = V90_MAP_D(m), i, f = 0, bad = 0;
    src_rng.s = 77; ref.s = 77;
    v90tx_init(&tx, alaw, 90);
    v90tx_set_getbit(&tx, src_bit, 0);
    if (v90tx_set_map(&tx, V90TX_MAP_DATA, m) != 0) return -2;
    (void)v90tx_select(&tx, V90TX_B1D);
    CHECK_EQ_I(v90adec_init(&d, m), 0);
    v90_scr_init(&scr);
    *nbits = 0;
    for (n = 0; n < total; n++) {
        uint8_t o = v90tx_tick(&tx);
        u[n % 6] = (uint8_t)v90_octet_ucode(alaw, o);
        pos[n % 6] = (uint8_t)v90_octet_sign(o);
        if (n % 6 != 5) continue;
        if (v90adec_frame(&d, u, pos, bits) != D) bad = 1;
        for (i = 0; i < D; i++) {
            int b = v90_descramble_gpc(&scr, bits[i]);
            int want = f < 48 ? 1 : prng_bit(&ref);
            if (b != want) err++;
            (*nbits)++;
        }
        f++;
    }
    return bad ? -1 : err;
}

int main(void)
{
    static const struct { int K, M, step; } KM[] = {
        { 15, 7, 9 }, { 21, 12, 5 }, { 27, 23, 4 }, { 33, 46, 2 }, { 36, 64, 1 }, { 39, 92, 1 } };
    v90_map_t m;
    int sr, ld, k, alaw;
    int32_t nb = 0, e;

    printf("round trip through the digital transmitter: B1d (48 frames of ones), then 400 data frames\n");
    for (alaw = 0; alaw <= 1; alaw++)
        for (sr = 0; sr <= 3; sr++)
            for (ld = 0; ld <= 3; ld++) {
                int32_t tot_e = 0, tot_b = 0;
                for (k = 0; k < (int)(sizeof KM / sizeof KM[0]); k++) {
                    if (KM[k].K + 6 - sr > 42 + 3) continue;
                    make_map(&m, KM[k].K, sr, ld, KM[k].M, KM[k].step);
                    if (v90_map_check(&m) != 0) continue;
                    e = round_trip(alaw, &m, 400, &nb);
                    CHECK_EQ_I(e, 0);
                    tot_e += e < 0 ? 1 : e; tot_b += nb;
                }
                printf("  %s Sr %d ld %d: %" PRId32 " bits, %" PRId32 " wrong\n", alaw ? "A-law " : "mu-law", sr, ld, tot_b, tot_e);
            }

    printf("decision errors\n");
    {
        v90adec_t d;
        uint8_t u[6], pos[6], bits[V90_MAX_D], ref[4][V90_MAX_D];
        int D, i, f, r, diff[4];
        make_map(&m, 30, 2, 1, 40, 3);
        D = V90_MAP_D(&m);
        /* four frames of fixed symbols, decoded clean, then with one sign of
         * the second frame flipped */
        for (r = 0; r < 2; r++) {
            v90adec_init(&d, &m);
            for (f = 0; f < 4; f++) {
                int ret;
                for (i = 0; i < 6; i++) { u[i] = m.C[i][(f * 7 + i * 3) % 5];   /* labels < 5: R0 < 2^K */ pos[i] = (uint8_t)((f + i) & 1); }
                if (r == 1 && f == 1) pos[3] ^= 1;
                ret = v90adec_frame(&d, u, pos, bits);
                CHECK_EQ_I(ret, D);
                if (r == 0) memcpy(ref[f], bits, (size_t)D);
                else { diff[f] = 0; for (i = 0; i < D; i++) diff[f] += bits[i] != ref[f][i]; }
            }
        }
        printf("  one sign wrong in frame 1 (Sr 2): bits wrong per frame %d %d %d %d\n", diff[0], diff[1], diff[2], diff[3]);
        CHECK_EQ_I(diff[0], 0);
        CHECK(diff[1] > 0);
        CHECK_EQ_I(diff[3], 0);
        /* a Ucode that is not a member: reported */
        v90adec_init(&d, &m);
        for (i = 0; i < 6; i++) { u[i] = m.C[i][0]; pos[i] = 1; }
        u[2] = (uint8_t)(m.C[2][0] - 1);
        r = v90adec_frame(&d, u, pos, bits);
        printf("  a Ucode outside the constellation: %d (D %d)\n", r, D);
        CHECK_EQ_I(r, -D);
        /* labels beyond 2^K: K = 15 with 7 points (7^6 > 2^15) */
        make_map(&m, 15, 0, 0, 7, 9);
        v90adec_init(&d, &m);
        for (i = 0; i < 6; i++) { u[i] = m.C[i][6]; pos[i] = 1; }
        r = v90adec_frame(&d, u, pos, bits);
        printf("  labels of R0 = 7^6 - 1 >= 2^15: %d\n", r);
        CHECK_EQ_I(r, -(15 + 6));
        /* a map the encoder could not use */
        m.M[0] = 0;
        CHECK(v90adec_init(&d, &m) != 0);
    }
    T_REPORT("v90adec");
}
