/* Test channel for the V.90 PCM downstream. See dschan.h. */
#include <string.h>
#include <math.h>
#include "dschan.h"
#include "../v90/v90_pcm.h"
#include "../modem/pcmenc.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NFFT 8192               /* 32 ms at DSCH_OS * 8000 */

typedef struct { double re, im; } cx;

static cx cmul(cx a, cx b) { cx r; r.re = a.re * b.re - a.im * b.im; r.im = a.re * b.im + a.im * b.re; return r; }
static cx cdiv(cx a, cx b)
{
    double d = b.re * b.re + b.im * b.im;
    cx r; r.re = (a.re * b.re + a.im * b.im) / d; r.im = (a.im * b.re - a.re * b.im) / d;
    return r;
}
static cx csqrt_(cx a)
{
    double m = sqrt(a.re * a.re + a.im * a.im);
    cx r;
    r.re = sqrt((m + a.re) / 2);
    r.im = sqrt((m - a.re) / 2);
    if (a.im < 0) r.im = -r.im;
    return r;
}

/* 8th order Chebyshev type I low-pass, 0.1 dB ripple, edge fc, unity at DC */
static cx cheb8(double f, double fc)
{
    const int N = 8;
    double eps = sqrt(pow(10.0, 0.01) - 1), v0 = asinh(1 / eps) / N, wc = 2 * M_PI * fc;
    cx s, h, num;
    int k;
    s.re = 0; s.im = 2 * M_PI * f;
    h.re = 1; h.im = 0;
    for (k = 1; k <= N; k++) {
        double th = M_PI * (2 * k - 1) / (2.0 * N);
        cx p, d;
        p.re = -wc * sinh(v0) * sin(th); p.im = wc * cosh(v0) * cos(th);
        d.re = s.re - p.re; d.im = s.im - p.im;
        num.re = -p.re; num.im = -p.im;         /* H(0) = 1 */
        h = cmul(h, cdiv(num, d));
    }
    return h;
}

static cx hp2(double f, double fh)              /* Butterworth: s^2 / (s^2 + sqrt2 w s + w^2) */
{
    double w = 2 * M_PI * fh, x = 2 * M_PI * f;
    cx n, d;
    n.re = -x * x; n.im = 0;
    d.re = w * w - x * x; d.im = sqrt(2.0) * w * x;
    return cdiv(n, d);
}

static cx hp1(double f, double fh)
{
    double w = 2 * M_PI * fh, x = 2 * M_PI * f;
    cx n, d;
    n.re = 0; n.im = x; d.re = w; d.im = x;
    return cdiv(n, d);
}

/* matched 0.4 mm loop: exp(-gamma len), gamma = sqrt((R + jwL) jwC) */
static cx loop(double f, double km)
{
    const double R = 280, L = 0.65e-3, C = 50e-9;
    double w = 2 * M_PI * f;
    cx z, y, g, r;
    if (km <= 0) { r.re = 1; r.im = 0; return r; }
    z.re = R; z.im = w * L;
    y.re = 0; y.im = w * C;
    g = csqrt_(cmul(z, y));
    r.re = exp(-g.re * km) * cos(-g.im * km);
    r.im = exp(-g.re * km) * sin(-g.im * km);
    return r;
}

static cx resp(const dschan_cfg *cfg, double f)
{
    double fa = fabs(f);
    cx h = cheb8(fa, 3550);
    h = cmul(h, hp2(fa, 150));
    h = cmul(h, loop(fa, cfg->loop_km));
    h = cmul(h, hp1(fa, 50));
    h = cmul(h, cheb8(fa, 3550));
    if (f < 0) h.im = -h.im;
    return h;
}

static double norm_gain(const dschan_cfg *cfg)
{
    cx h = resp(cfg, 1000);
    return pow(10.0, cfg->level_db / 20) / sqrt(h.re * h.re + h.im * h.im);
}

double dschan_gain_db(const dschan_cfg *cfg, double f)
{
    cx h = resp(cfg, f);
    return 20 * log10(norm_gain(cfg) * sqrt(h.re * h.re + h.im * h.im) + 1e-300);
}

/* in place, inverse (e^{+j}), not normalised */
static void ifft(double *re, double *im, int n)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = 2 * M_PI / len;
        for (k = 0; k < len / 2; k++) {
            double wr = cos(a * k), wi = sin(a * k);
            for (i = k; i < n; i += len) {
                int m = i + len / 2;
                double xr = re[m] * wr - im[m] * wi, xi = re[m] * wi + im[m] * wr;
                re[m] = re[i] - xr; im[m] = im[i] - xi;
                re[i] += xr; im[i] += xi;
            }
        }
    }
}

/* the hybrid: a short dispersive leak at the DAA and a weaker reflection
 * from the line card about a millisecond later */
static const double near_taps[] = { 0.55, -0.9, 0.6, -0.35, 0.2, -0.12, 0.07, -0.04, 0.02, -0.01 };

void dschan_init(dschan_t *c, const dschan_cfg *cfg, dschan_src_fn src, void *ctx)
{
    double re[NFFT], im[NFFT], g, df = 8000.0 * DSCH_OS / NFFT;
    double er = 0, ei = 0, eg;
    int i, nt = (int)(sizeof near_taps / sizeof near_taps[0]);

    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    c->src = src; c->ctx = ctx;
    rng_seed(&c->rng, cfg->seed ? cfg->seed : 1);
    c->r = 1.0 / (1.0 + cfg->ppm * 1e-6);

    g = norm_gain(cfg);
    for (i = 0; i < NFFT; i++) {
        double f = (i < NFFT / 2 ? i : i - NFFT) * df;
        cx h = resp(cfg, f);
        re[i] = h.re * g; im[i] = h.im * g;
    }
    re[NFFT / 2] = im[NFFT / 2] = 0;
    ifft(re, im, NFFT);
    /* table index i is t = (i - 1) / OS - PRE samples */
    for (i = 0; i < DSCH_LEN * DSCH_OS + 4; i++) {
        int m = i - 1 - DSCH_PRE * DSCH_OS;
        c->pulse[i] = re[(m + NFFT) % NFFT] * df / 8000.0;
    }

    /* 0 dBm0: a sine of peak 8159 (mu-law, 14 bits) or 4096 (A-law, 13 bits)
     * at +3.17 / +3.14 dBm0 */
    c->tone_rms = (cfg->alaw ? 4096.0 * 8 / pow(10, 3.14 / 20) : 8159.0 * 4 / pow(10, 3.17 / 20))
                  / sqrt(2.0) * pow(10.0, cfg->level_db / 20);
    c->nrms = cfg->noise_dbm0 <= -200 ? 0 : c->tone_rms * pow(10.0, cfg->noise_dbm0 / 20);

    if (cfg->echo_db > -200) {
        for (i = 0; i < nt; i++) {
            c->echo[1 + i] += near_taps[i];
            c->echo[9 + (int)(2 * cfg->loop_km) + i] += 0.3 * near_taps[i];
        }
        for (i = 0; i < DSCH_ENTAP; i++) {
            er += c->echo[i] * cos(2 * M_PI * 1000.0 * i / 8000);
            ei -= c->echo[i] * sin(2 * M_PI * 1000.0 * i / 8000);
        }
        eg = pow(10.0, cfg->echo_db / 20) / sqrt(er * er + ei * ei);
        for (i = 0; i < DSCH_ENTAP; i++) c->echo[i] *= eg;
    }
}

double dschan_pulse(const dschan_t *c, double t)
{
    double x = (t + DSCH_PRE) * DSCH_OS + 1, mu;
    int i;
    const double *p;
    if (t < -DSCH_PRE || t >= DSCH_LEN - DSCH_PRE) return 0;
    i = (int)floor(x); mu = x - i; p = &c->pulse[i];
    return -mu * (mu - 1) * (mu - 2) / 6 * p[-1] + (mu + 1) * (mu - 1) * (mu - 2) / 2 * p[0]
           - (mu + 1) * mu * (mu - 2) / 2 * p[1] + (mu + 1) * mu * (mu - 1) / 6 * p[2];
}

static void take(dschan_t *c)
{
    const dschan_cfg *cfg = &c->cfg;
    uint8_t o = c->src(c->ctx);
    long n = c->n++;
    c->sent[n % DSCH_HIST] = o;
    if (cfg->pad_db != 0.0) {
        double v = v90_octet_linear(cfg->alaw, o) * pow(10.0, -cfg->pad_db / 20);
        o = pcm_encode(cfg->alaw, (int16_t)floor(v + 0.5));
    }
    if (cfg->rbs && n % 6 == cfg->rbs_phase) o |= 1;
    c->got[n % DSCH_HIST] = o;
    c->a[n & 255] = v90_octet_linear(cfg->alaw, o);
}

int16_t dschan_tick(dschan_t *c, int16_t up)
{
    double tk = c->cfg.phase + c->k * c->r, x0, mu, w[4], y = 0, e = 0;
    long nhi = (long)floor(tk + DSCH_PRE);
    int i, j, i0;

    while (c->n <= nhi) take(c);
    /* Lagrange weights are the same for every tap: the network samples are
     * a whole number of samples apart */
    x0 = (tk - nhi + DSCH_PRE) * DSCH_OS + 1;
    i0 = (int)floor(x0); mu = x0 - i0;
    w[0] = -mu * (mu - 1) * (mu - 2) / 6; w[1] = (mu + 1) * (mu - 1) * (mu - 2) / 2;
    w[2] = -(mu + 1) * mu * (mu - 2) / 2; w[3] = (mu + 1) * mu * (mu - 1) / 6;
    for (j = 0; j < DSCH_LEN && nhi - j >= 0; j++) {
        const double *p = &c->pulse[i0 + j * DSCH_OS];
        y += c->a[(nhi - j) & 255] * (w[0] * p[-1] + w[1] * p[0] + w[2] * p[1] + w[3] * p[2]);
    }
    c->k++;

    if (--c->upos < 0) c->upos = DSCH_ENTAP - 1;
    c->up[c->upos] = c->up[c->upos + DSCH_ENTAP] = up;
    for (i = 0; i < DSCH_ENTAP; i++) e += c->echo[i] * c->up[c->upos + i];

    y += e;
    c->echo_last = e;
    if (c->nrms > 0) y += c->nrms * rng_gauss(&c->rng);
    y = floor(y + 0.5);
    if (y > 32767) y = 32767;
    if (y < -32767) y = -32767;
    return (int16_t)y;
}

uint8_t dschan_sent(const dschan_t *c, long n) { return c->sent[n % DSCH_HIST]; }
uint8_t dschan_got(const dschan_t *c, long n) { return c->got[n % DSCH_HIST]; }
