/* modem/pcmtx: the PCM upstream carried from the network clock on to ours.
 *
 * 1. With our clock equal to the network's, a symbol put at a sample
 *    boundary comes out PCMTX_DELAY samples later, exactly.
 * 2. One symbol per network sample: over 10 s the symbols taken match the
 *    clock ratio within one.
 * 3. The round trip of the test line (test/v90link.h): our samples resampled
 *    on to the network clock (sim/resamp's windowed sinc), then a codec's
 *    band limit (3.4 kHz). What arrives must be one fixed response of the
 *    symbols, at any clock offset: a response fitted on the first 2.5 s
 *    predicts the next 5 s (two turns of the phase between the clocks at
 *    100 ppm) to within the error bound below.
 * 4. pcmtx_slip(): the response moves by the fraction asked for (group
 *    delay at 1 kHz), nothing else changes.
 * 5. Full scale: a constant symbol at either end of the 16-bit range comes
 *    out at that end (saturated, never wrapped).
 * Host only (the reference computations are double). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "../modem/pcmtx.h"
#include "../sim/resamp.h"
#include "../sim/noise.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define ONE32 ((int64_t)1 << 32)
#define NF    31                /* fitted response, taps */
#define NLP   63                /* codec band limit, taps */
#define NMAX  64000

static double sym[NMAX], ylp[NMAX], slp[NMAX];
static double lp[NLP];

static void make_lp(void)
{
    int i;
    double s = 0;
    for (i = 0; i < NLP; i++) {
        double t = i - (NLP - 1) / 2.0, w = 0.5 + 0.5 * cos(M_PI * t / ((NLP + 1) / 2.0));
        double x = 2 * 3400.0 / 8000.0 * t;
        lp[i] = (fabs(t) < 1e-12 ? 1.0 : sin(M_PI * x) / (M_PI * x)) * w * (2 * 3400.0 / 8000.0);
        s += lp[i];
    }
    for (i = 0; i < NLP; i++) lp[i] /= s;
}

static void filt(const double *x, double *y, int n)
{
    int i, k;
    for (i = 0; i < n; i++) {
        double a = 0;
        for (k = 0; k < NLP; k++) if (i - k >= 0) a += lp[k] * x[i - k];
        y[i] = a;
    }
}

/* least squares: y[n] ~ sum h[k] x[n - k + off], n in [a, b) */
static void fit(const double *x, const double *y, int a, int b, int off, double *h)
{
    static double R[NF][NF + 1];
    int i, j, k, n;
    memset(R, 0, sizeof R);
    for (n = a; n < b; n++)
        for (i = 0; i < NF; i++) {
            double xi = x[n - i + off];
            for (j = 0; j < NF; j++) R[i][j] += xi * x[n - j + off];
            R[i][NF] += xi * y[n];
        }
    /* Gauss-Jordan */
    for (i = 0; i < NF; i++) {
        double p = R[i][i];
        for (k = i; k <= NF; k++) R[i][k] /= p;
        for (j = 0; j < NF; j++) if (j != i) {
            double f = R[j][i];
            for (k = i; k <= NF; k++) R[j][k] -= f * R[i][k];
        }
    }
    for (i = 0; i < NF; i++) h[i] = R[i][NF];
}

/* error of the fitted response over [a, b), dB re the signal */
static double resid_db(const double *x, const double *y, int a, int b, int off, const double *h)
{
    double e = 0, s = 0;
    int n, k;
    for (n = a; n < b; n++) {
        double p = 0;
        for (k = 0; k < NF; k++) p += h[k] * x[n - k + off];
        e += (y[n] - p) * (y[n] - p);
        s += y[n] * y[n];
    }
    return 10 * log10(e / s + 1e-30);
}

/* group delay of h at 1 kHz, samples (relative to tap 0) */
static double gdelay(const double *h)
{
    double re = 0, im = 0, w = 2 * M_PI * 1000.0 / 8000.0, ph;
    int k;
    for (k = 0; k < NF; k++) { re += h[k] * cos(w * k); im -= h[k] * sin(w * k); }
    ph = atan2(im, re);
    return -ph / w;
}

/* symbols (white, uniform, RMS about 3000) through pcmtx on a clock ppm off
 * the network's, our samples back on to the network clock as the test line
 * does; slip_at >= 0: pcmtx_slip(slip) before that symbol. Returns the
 * network samples made (in ylp, band limited; slp: the symbols band
 * limited). */
static int roundtrip(double ppm, int nsym, int slip_at, uint32_t slip, uint32_t *nput)
{
    static double y[NMAX];
    pcmtx_t t;
    sresamp_t rs;
    rng_t g;
    int64_t step = (int64_t)llround((1.0 + ppm * 1e-6) * (double)ONE32);
    int ny = 0, ns = 0;
    long k;
    rng_seed(&g, 7);
    pcmtx_init(&t);
    sresamp_init(&rs, 1.0 / (1.0 + ppm * 1e-6));
    for (k = 0; ny < nsym && k < 2 * NMAX; k++) {
        double o[4];
        int i, n;
        pcmtx_clock(&t);
        while (pcmtx_due(&t)) {
            int16_t s;
            if (ns == slip_at) pcmtx_slip(&t, slip);
            s = ns < NMAX ? (int16_t)lround(5196.0 * (2.0 * rng_uniform(&g) - 1.0)) : 0;
            if (ns < NMAX) sym[ns] = s;
            ns++;
            pcmtx_put(&t, s, step);
        }
        n = sresamp_push(&rs, (double)pcmtx_out(&t), o, 4);
        for (i = 0; i < n && ny < nsym; i++) y[ny++] = o[i];
    }
    *nput = t.nsym;
    filt(y, ylp, ny);
    filt(sym, slp, ny);
    return ny;
}

int main(void)
{
    make_lp();

    /* 1. our clock is the network's: a pure delay */
    {
        pcmtx_t t;
        rng_t g;
        static int16_t in[4000], out[4000];
        int k, bad = 0, due = 0;
        rng_seed(&g, 3);
        pcmtx_init(&t);
        for (k = 0; k < 4000; k++) {
            pcmtx_clock(&t);
            due += pcmtx_due(&t) != 1;
            in[k] = (int16_t)lround(20000.0 * (2.0 * rng_uniform(&g) - 1.0));
            pcmtx_put(&t, in[k], ONE32);
            due += pcmtx_due(&t) != 0;
            out[k] = pcmtx_out(&t);
        }
        for (k = PCMTX_DELAY; k < 4000; k++) bad += out[k] != in[k - PCMTX_DELAY];
        printf("same clock: %d of %d samples differ from the symbols %d samples earlier\n", bad, 4000 - PCMTX_DELAY, PCMTX_DELAY);
        CHECK_EQ_I(bad, 0);
        CHECK_EQ_I(due, 0);
    }

    /* 2. symbols per network sample */
    {
        static const double P[] = { 0, 100, -100, 1000, -1000 };
        int i;
        for (i = 0; i < 5; i++) {
            pcmtx_t t;
            int64_t step = (int64_t)llround((1.0 + P[i] * 1e-6) * (double)ONE32);
            long k, n = 80000;
            int most = 0;
            pcmtx_init(&t);
            for (k = 0; k < n; k++) {
                int c = 0;
                pcmtx_clock(&t);
                while (pcmtx_due(&t)) { pcmtx_put(&t, 1000, step); c++; }
                if (c > most) most = c;
                (void)pcmtx_out(&t);
            }
            printf("%+6.0f ppm: %u symbols in %ld samples (%.1f expected), at most %d in one sample\n",
                   P[i], (unsigned)t.nsym, n, n / (1.0 + P[i] * 1e-6), most);
            CHECK_NEAR((double)t.nsym, n / (1.0 + P[i] * 1e-6), 1.0);
            CHECK(most <= 2);
        }
    }

    /* 3. the round trip: one fixed response at any clock offset */
    {
        static const double P[] = { 0, 30, 100, -100, 300 };
        int i;
        printf("round trip through the test line's resampler, 3.4 kHz band limit:\n");
        for (i = 0; i < 5; i++) {
            double h[NF], r, d;
            uint32_t np;
            int ny = roundtrip(P[i], 60000, -1, 0, &np);
            fit(slp, ylp, 1000, 20000, -(PCMTX_DELAY - NF / 2), h);
            r = resid_db(slp, ylp, 20000, ny, -(PCMTX_DELAY - NF / 2), h);
            d = gdelay(h);
            printf("  %+5.0f ppm: fitted on 0.1..2.5 s, error over 2.5..7.5 s %.1f dB, group delay %.3f\n", P[i], r, d);
            CHECK(r < -60.0);
        }
    }

    /* 4. a slip moves the response by exactly that much */
    {
        static const uint32_t S[] = { 0x8000, 0x4000, 0xC000, 0x0123 };
        static const double P[] = { 0, 100, -100, 0 };
        int i;
        for (i = 0; i < 4; i++) {
            double h0[NF], h1[NF], r0, r1, dd;
            uint32_t np;
            int off = -(PCMTX_DELAY - NF / 2), ny = roundtrip(P[i], 40000, 20000, S[i], &np);
            fit(slp, ylp, 1000, 19000, off, h0);
            fit(slp, ylp, 21000, ny, off, h1);
            r0 = resid_db(slp, ylp, 1000, 19000, off, h0);
            r1 = resid_db(slp, ylp, 21000, ny, off, h1);
            dd = gdelay(h1) - gdelay(h0);
            printf("  slip %.4f at %+4.0f ppm: delay %+.4f symbols, errors %.1f / %.1f dB\n",
                   S[i] / 65536.0, P[i], dd, r0, r1);
            CHECK_NEAR(dd, S[i] / 65536.0, 0.002);
            CHECK(r0 < -60.0 && r1 < -60.0);
        }
    }

    /* 5. full scale */
    {
        static const int16_t V[] = { 32767, -32768 };
        int i, k;
        for (i = 0; i < 2; i++) {
            pcmtx_t t;
            int16_t o = 0;
            int64_t step = (int64_t)llround((1.0 + 300e-6) * (double)ONE32);
            int wrong = 0;
            pcmtx_init(&t);
            for (k = 0; k < 2000; k++) {
                pcmtx_clock(&t);
                while (pcmtx_due(&t)) pcmtx_put(&t, V[i], step);
                o = pcmtx_out(&t);
                if (k > 200 && (V[i] > 0 ? o < 32700 : o > -32700)) wrong++;
            }
            printf("constant %d: last output %d, %d outputs off\n", V[i], o, wrong);
            CHECK_EQ_I(wrong, 0);
        }
    }

    T_REPORT("pcmtx");
}
