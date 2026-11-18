/* modem/v92aseq: SUVd, CPd and Ed as the analogue modem's TRN2d decoder
 * hands them over, one data frame of bits at a time.
 *
 * The reference is the library's own builder and parser (../v92/v92_seq):
 * a stream of data frames of D bits is made as the digital modem sends it
 * (TRN2d ones, SUVd, CPd, SUVd', CPd', Ed, then data), each sequence built
 * with v92_cpd_build() / v92_suvd_build() at unit D, and every CPd taken in
 * must equal what v92_cpd_parse() makes of the same bits. Then:
 *   - every D from 6 to 72 and CPd sizes from the smallest to the largest
 *     Table 30 allows (the CRC ending on a data frame boundary, so that the
 *     fill is a whole frame of zeros, among them);
 *   - Ed reported once, for the first frame of zeros after the last
 *     sequence, and not for zeros elsewhere;
 *   - a CPd with one bit wrong is not taken, and the next one is;
 *   - nothing is reported on ones, zeros or random bits alone. */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../modem/v92aseq.h"
#include "../sim/noise.h"

#define MAXB (V92_CPD_MAX_BITS + 200)

static uint8_t stream[8 * MAXB];

/* a CPd of the given size: lc points in each of nset sets, ncoef taps */
static void make_cpd(v92_cpd_t *c, rng_t *g, int nset, int lc, int ncoef, int ack)
{
    int s, i;
    memset(c, 0, sizeof *c);
    c->has_mod = c->has_filt = c->has_const = 1;
    c->drn = (uint8_t)(1 + (int)(rng_uniform(g) * 19));
    c->map.trellis = (uint8_t)(rng_uniform(g) * 3);
    c->e2u_extend = (uint8_t)(rng_uniform(g) < 0.5);
    c->ack = (uint8_t)ack;
    for (i = 0; i < 12; i++) c->map.M[i] = (uint8_t)(2 + (int)(rng_uniform(g) * 120));
    c->filt.gain4 = (uint16_t)(1 + (int)(rng_uniform(g) * 65000));
    if (ncoef > 0) {
        int a = ncoef / 4, b = ncoef / 3;
        if (a > 320) a = 320;
        if (b > 320) b = 320;
        c->filt.lz1 = (uint16_t)a; c->filt.lp1 = (uint16_t)b;
        c->filt.lz2 = (uint16_t)(ncoef - a - b > 320 ? 320 : ncoef - a - b);
        c->filt.lp2 = (uint16_t)(ncoef - a - b - c->filt.lz2);
        for (i = 0; i < ncoef; i++) c->filt.coef[i] = (int16_t)(int)(65535.0 * rng_uniform(g) - 32768.0);
    } else {
        c->has_filt = 0;
    }
    for (i = 0; i < 6; i++) c->map.cidx[i] = (uint8_t)(i % nset);
    for (s = 0; s < nset; s++) {
        c->map.lc[s] = (uint8_t)lc;
        for (i = 0; i < lc; i++) c->map.pt[s][i] = (uint16_t)(1 + i * 250 + s);
    }
}

/* append a sequence (built with unit D) to the stream; returns its length */
static int put_seq(int *pos, const uint8_t *bits, int n) { memcpy(stream + *pos, bits, (size_t)n); *pos += n; return n; }

static void put_const(int *pos, int bit, int n) { memset(stream + *pos, bit, (size_t)n); *pos += n; }

static int same_cpd(const v92_cpd_t *a, const v92_cpd_t *b) { return memcmp(a, b, sizeof *a) == 0; }

int main(void)
{
    static uint8_t bits[MAXB];
    static v92_cpd_t want, want2, got, ref;
    rng_t g;
    v92aseq_t s;
    int D, cases = 0, frame_fill = 0;

    rng_seed(&g, 11);

    /* every D, sizes small to large; the stream as the digital modem sends it */
    for (D = 6; D <= 72; D++) {
        static const int SZ[][3] = { { 1, 1, 0 }, { 1, 64, 192 }, { 2, 41, 100 }, { 6, 128, 384 }, { 1, 7, 3 } };
        int z;
        for (z = 0; z < 5; z++) {
            int pos = 0, f, nfr, ev_suvd = 0, ev_cpd = 0, ev_ed = 0, ev_bad = 0, ed_frame = -1, want_ed, n, e1;
            int cpd_ok = 1, len1;
            v92_suvd_t sd, sg;
            memset(&sd, 0, sizeof sd);
            memset(&sg, 0, sizeof sg);
            make_cpd(&want, &g, SZ[z][0], SZ[z][1], SZ[z][2], 0);
            want.map.K = (uint8_t)V92_UP_K(want.drn);          /* as the parser sets it */
            want2 = want; want2.ack = 1;
            /* the parser's view of the bits is the reference */
            n = v92_cpd_build(&want, D, bits);
            CHECK_EQ_I(v92_cpd_parse(bits, n, D, &ref), n);
            CHECK(same_cpd(&ref, &want));
            /* the CRC ends on a data frame boundary: the fill is a frame of zeros */
            len1 = v92_cpd_build(&want, 1, bits);
            if ((len1 - 1) % D == 0) frame_fill++;
            put_const(&pos, 1, 40 * D);                         /* TRN2d: scrambled ones */
            n = v92_suvd_build(&sd, D, bits); put_seq(&pos, bits, n);
            n = v92_suvd_build(&sd, D, bits); put_seq(&pos, bits, n);
            n = v92_cpd_build(&want, D, bits); put_seq(&pos, bits, n);
            sd.ack = 1;
            n = v92_suvd_build(&sd, D, bits); put_seq(&pos, bits, n);
            n = v92_cpd_build(&want2, D, bits); put_seq(&pos, bits, n);
            want_ed = pos / D;                                  /* Ed straight after a CPd' */
            put_const(&pos, 0, 2 * D);                          /* Ed */
            for (e1 = 0; e1 < 20 * D; e1++) stream[pos++] = (uint8_t)(rng_uniform(&g) < 0.5);   /* B1d, data */
            nfr = pos / D;
            v92aseq_init(&s);
            memset(&got, 0, sizeof got);
            for (f = 0; f < nfr; f++) {
                int ev = v92aseq_frame(&s, stream + f * D, D, &sg, &got);
                if (ev & V92AS_SUVD) ev_suvd++;
                if ((ev & V92AS_BAD) && f < want_ed) ev_bad++;    /* (random data after Ed may hold a frame sync) */
                if (ev & V92AS_ED) { ev_ed++; if (ed_frame < 0) ed_frame = f; }
                if (ev & V92AS_CPD) { ev_cpd++; cpd_ok &= same_cpd(&got, ev_cpd == 1 ? &want : &want2); }
            }
            CHECK_EQ_I(ev_suvd, 3);
            CHECK_EQ_I(ev_cpd, 2);
            CHECK_EQ_I(cpd_ok, 1);
            CHECK_EQ_I(ev_ed, 1);
            CHECK_EQ_I(ed_frame, want_ed);
            CHECK_EQ_I(ev_bad, 0);
            CHECK_EQ_I(sg.ack, 1);
            cases++;
        }
    }
    printf("%d streams (D = 6..72, CPd of 2 to %d words), %d with a whole frame of fill after the CRC\n",
           cases, 2 + 6 + 4 + 384 + 5 + 768 + 1, frame_fill);
    CHECK(frame_fill >= 3);

    /* one bit wrong: that CPd is not taken, the next one is */
    {
        int D2 = 30, pos = 0, f, ncpd = 0, nbad = 0, k, n;
        v92_suvd_t sg;
        make_cpd(&want, &g, 2, 50, 150, 0);
        n = v92_cpd_build(&want, D2, bits);
        (void)v92_cpd_parse(bits, n, D2, &ref);
        want.map.K = ref.map.K;
        put_const(&pos, 1, 10 * D2);
        k = pos;
        put_seq(&pos, bits, n);
        stream[k + 1234] ^= 1;                                  /* inside the points */
        put_seq(&pos, bits, n);
        put_const(&pos, 1, 10 * D2);
        v92aseq_init(&s);
        for (f = 0; f < pos / D2; f++) {
            int ev = v92aseq_frame(&s, stream + f * D2, D2, &sg, &got);
            if (ev & V92AS_CPD) { ncpd++; CHECK(same_cpd(&got, &want)); }
            if (ev & V92AS_BAD) nbad++;
        }
        printf("a bit wrong in the first of two CPd: %d taken, %d bad\n", ncpd, nbad);
        CHECK_EQ_I(ncpd, 1);
        CHECK_EQ_I(nbad, 1);
    }

    /* nothing out of ones, zeros, or random bits */
    {
        int D2 = 24, f, ev_all = 0;
        v92_suvd_t sg;
        v92aseq_init(&s);
        for (f = 0; f < 2000; f++) {
            uint8_t b[24];
            int i;
            for (i = 0; i < D2; i++) b[i] = (uint8_t)(f < 500 ? 1 : f < 1000 ? 0 : rng_uniform(&g) < 0.5);
            ev_all |= v92aseq_frame(&s, b, D2, &sg, &got) & (V92AS_SUVD | V92AS_CPD | V92AS_ED);
            if (f == 999) CHECK_EQ_I(ev_all, 0);
        }
        printf("ones, zeros, random: events 0x%x\n", ev_all);
        CHECK_EQ_I(ev_all, 0);
    }
    T_REPORT("v92aseq");
}
