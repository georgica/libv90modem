/* V.90 analogue modem: PCM downstream receiver. See v90arx.h. */
#include <string.h>
#include "v90arx.h"
#include "v34_tabs.h"           /* the fractional delay bank, shared with qamrx */
#include "v90_pcm.h"

#define ONE32    ((int64_t)1 << 32)
#define AGC_RMS  4096           /* Sd at the equaliser input */
#define SD_FLOOR 16             /* A/D units: weaker Sd is not looked at */
#define SD_SKIP  12             /* periods of Sd before its clock is measured */
#define TBLK     64             /* symbols per timing measurement */
#define ECLIP    (1 << 15)      /* error used for adaptation, Q YF */
#define FASTN    8000           /* equaliser symbols with the larger steps (1 s) */
/* The far end is not trusted: whatever the line carries, the clock ratio
 * stays within this of one (Q32; 1000 ppm, ten times what V.90's clocks
 * may be off), the phase slew within half of that, and the
 * interpolator makes at most this many outputs per input sample. */
#define STEP_MAX ((int64_t)4294967)
#define CLK_TRUST ((int64_t)21475)  /* 5 ppm: Sd overrules a clock set with v90arx_clock() */
#define SLEW_MAX (STEP_MAX / 2)
#define PRODUCE_MAX 3

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static int32_t asr64(int64_t v, int s) { return (int32_t)((v + ((int64_t)1 << (s - 1))) >> s); }

static int32_t isqrt64(int64_t v)
{
    int64_t r = 0, b = (int64_t)1 << 62;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int32_t)r;
}

static int16_t sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v); }

void v90arx_cons_mask(v90arx_cons_t *c, int alaw, int slot, const uint8_t mask[16])
{
    int u, n = 0;
    for (u = 0; u < 128; u++)
        if ((mask[u >> 3] >> (u & 7)) & 1) {
            c->u[slot][n] = (uint8_t)u;
            c->lv[slot][n] = v90_ucode_linear(alaw, (unsigned)u) << V90ARX_YF;
            n++;
        }
    c->n[slot] = (uint8_t)n;
}

void v90arx_init(v90arx_t *r, const v90arx_cfg_t *cfg)
{
    int i;
    memset(r, 0, sizeof *r);
    r->cfg = *cfg;
    if (r->cfg.ntrain <= 0) r->cfg.ntrain = V90ARX_NTRAIN;
    ec_init(&r->ec);
    r->ec_mu = -1;
    r->step = ONE32;
    r->tnext = ONE32;
    r->gain = 4096;
    v92_p6_init(&r->sd);
    r->L = (int16_t)v90_ucode_linear(cfg->alaw, (unsigned)cfg->uinfo);
    if (r->L < 1) r->L = 1;             /* mu-law Ucode 0: it divides */
    /* until told otherwise: the two points of UINFO (TRN1d, Jd, Jd') */
    for (i = 0; i < 6; i++) {
        r->cons.n[i] = 1; r->cons.u[i][0] = (uint8_t)cfg->uinfo;
        r->cons.lv[i][0] = (int32_t)r->L << V90ARX_YF;
    }
    r->state = V90ARX_WAIT;
}

void v90arx_ec(v90arx_t *r, int mu_shift) { r->ec_mu = mu_shift; }
void v90arx_hold(v90arx_t *r, int on)
{
    r->hold = on != 0;
    if (r->hold) r->slew = 0;   /* the timing loop's phase correction is per block: none while it waits */
}

void v90arx_set_cons(v90arx_t *r, const v90arx_cons_t *c, int32_t m)
{
    r->next = *c; r->next_m = m; r->have_next = 1;
}

int32_t v90arx_ppm_q8(const v90arx_t *r)
{
    return (int32_t)(((r->step - ONE32) * 256000000LL) >> 32);
}

/* ---- training ------------------------------------------------------------ */

/* NLMS step of the channel estimate on sample k: q(k) = sum h[j] s(k + LEAD - j) / L,
 * s the symbols' levels in Table 1 units (known, or decided), pd their mean
 * square; mu = 2^-g */
static void estimate(v90arx_t *r, int32_t k, int64_t pd, int g)
{
    int64_t acc = 0, es;
    int32_t e, m0 = k + V90ARX_LEAD;
    int j;
    for (j = 0; j < V90ARX_NCH; j++) acc += (int64_t)r->h[j] * r->sb[(m0 - j) & 255];
    e = (int32_t)r->qb[k & 255] - asr64(acc / r->L, 16);
    if (e > 8191) e = 8191;
    if (e < -8191) e = -8191;
    /* once converged, an error beyond 4 sigma is an impairment of one symbol
     * (a robbed bit in TRN1d): not learnt from */
    if (r->nest > 600 && (int64_t)e * e > 16 * r->res) { r->res += r->res >> 10; r->nest++; return; }
    /* h += mu e s L / (NCH pd), h in Q16, applied as (es s) >> 8. The
     * decided levels' power can decay to nothing (a far end sending Ucode
     * 0 alone): not below (L / 8)^2 */
    if (pd < ((int64_t)r->L * r->L >> 6)) pd = (int64_t)r->L * r->L >> 6;
    if (pd < 1) pd = 1;
    es = (int64_t)e * r->L * ((int64_t)1 << 24) / ((int64_t)V90ARX_NCH * pd) >> g;
    for (j = 0; j < V90ARX_NCH; j++)
        r->h[j] += (int32_t)((es * r->sb[(m0 - j) & 255]) >> 8);
    r->res += (((int64_t)e * e) - r->res) / 64;
    r->nest++;
}

/* How much later (Q16 samples) the response v is than u: the phase step
 * of their DFTs at w = pi/4 and pi/2 (1 and 2 kHz, where the line is
 * good), arg(V conj U) = -w d for v(j) = u(j - d), weighted by |U|^2.
 * The values are taken >> sh. */
static int32_t later_q16(const int32_t *v, const int32_t *u, int n, int sh)
{
    static const int32_t C8[8] = { 32767, 23170, 0, -23170, -32767, -23170, 0, 23170 };
    int64_t num = 0, den = 0;
    int w, j;
    for (w = 1; w <= 2; w++) {
        int64_t xr = 0, xi = 0, ur = 0, ui = 0, pr, pi, m, t, d, wt;
        int ss = 0;
        for (j = 0; j < n; j++) {
            int64_t a = (int64_t)v[j] >> sh, b = (int64_t)u[j] >> sh;
            int32_t c = C8[(j * w) & 7], si = C8[(j * w - 2) & 7];
            xr += a * c; xi -= a * si;
            ur += b * c; ui -= b * si;
        }
        m = 0;
        if ((xr < 0 ? -xr : xr) > m) m = xr < 0 ? -xr : xr;
        if ((xi < 0 ? -xi : xi) > m) m = xi < 0 ? -xi : xi;
        if ((ur < 0 ? -ur : ur) > m) m = ur < 0 ? -ur : ur;
        if ((ui < 0 ? -ui : ui) > m) m = ui < 0 ? -ui : ui;
        while ((m >> ss) >= ((int64_t)1 << 30)) ss++;
        xr >>= ss; xi >>= ss; ur >>= ss; ui >>= ss;
        pr = (xr * ur + xi * ui) >> 30;
        pi = (xi * ur - xr * ui) >> 30;
        if (pr <= 0) continue;
        t = pi * 65536 / pr;                       /* tan, Q16 */
        if (t > 32768) t = 32768;
        if (t < -32768) t = -32768;
        t -= (((t * t) >> 16) * t >> 16) / 3;      /* atan, to third order */
        /* d = -t / w with w = pi/4 or pi/2: * 4/pi or * 2/pi */
        d = -(t * (w == 1 ? 83443 : 41722)) >> 16;  /* 4/pi, 2/pi in Q16 */
        wt = (ur * ur + ui * ui) >> 30;
        num += d * wt; den += wt;
    }
    if (den <= 0) return 0;
    num /= den;
    return (int32_t)(num > 65536 ? 65536 : num < -65536 ? -65536 : num);
}

/* steps normalised by the input and decision powers: k and s such that
 * ((e k) >> 16) x >> s is e x 2^P / np, k < 2^15 */
static void norm_step(int P, int64_t np, int32_t *k, int *sh)
{
    int s = 24;
    int64_t v;
    if (np < 1) np = 1;
    v = ((int64_t)1 << (P + 16 + s)) / np;
    while (v >= 32768 && s > 1) { v >>= 1; s--; }
    *k = (int32_t)v; *sh = s;
}

static void renorm(v90arx_t *r)
{
    /* forward: taps Q20 to an output in Q YF with e in Q YF, so a step of
     * mu e x 2^20 / (NF Pq); feedback: taps Q16 on Q0 levels to Q YF, so
     * mu e d 2^(16 - YF) / (NB Pd); mu = 2^-g is applied by the caller */
    norm_step(20, (int64_t)V90ARX_NF * (r->pq >> 8), &r->kf, &r->shf);
    norm_step(16 - V90ARX_YF, (int64_t)V90ARX_NB * (r->pd >> 8), &r->kb, &r->shb);
}

static int find_cursor(v90arx_t *r)
{
    int j, c = 8;
    int64_t best = -1, hp;
    for (j = 8; j <= V90ARX_NCH - 40; j++) {
        int64_t a = r->h[j] < 0 ? -(int64_t)r->h[j] : r->h[j];
        if (a > best) { best = a; c = j; }
    }
    hp = r->h[c] / (1 << 16);
    if (hp > -16 && hp < 16) return -1;
    r->cpk = (int16_t)c;
    return 0;
}

/* The MMSE decision feedback equaliser for the estimated channel, with the
 * feedback taken as cancelling every earlier symbol. Forward line x[i] =
 * q(k - i) holds symbol m + l through h[a - i - l], a = DFF + cursor; so
 *   R f = p,  R_ij = sum_{l >= 0} h[a-i-l] h[a-j-l] + s2 delta_ij,  p_i = h[a-i]
 *   b_j = sum_i f_i h[a - i + 1 + j]
 * solved by Cholesky in fixed point: R scaled to (2^26, 2^28], its factor
 * G in Q14, z in Q30, f in Q32 (see below). s2 is the estimator's residual,
 * but not less than 2^-18 of the largest diagonal (it bounds the condition).
 * About 18000 64-bit multiplies and 1200 divisions. */
#define NT ((V90ARX_NF * (V90ARX_NF + 1)) / 2)
#define TRI(i, j) ((i) * ((i) + 1) / 2 + (j))
static int dfe_design(v90arx_t *r)
{
    int64_t A[NT], z[V90ARX_NF], fs[V90ARX_NF], mx = 0, dmax = 0, s2, bias = 0, t;
    int32_t hh[V90ARX_NCH];
    int a = V90ARX_DFF + r->cpk, i, j, k, hs = 0, rs, e1;

    for (j = 0; j < V90ARX_NCH; j++) { int64_t v = r->h[j] < 0 ? -(int64_t)r->h[j] : r->h[j]; if (v > mx) mx = v; }
    while ((mx >> hs) >= (1 << 20)) hs++;
    /* rounded, not truncated: the channel has a null at DC, and the many
     * small taps of its tail, each a half step low, add up to a false DC
     * response the solution is very sensitive to (seen at 6 km) */
    for (j = 0; j < V90ARX_NCH; j++) hh[j] = hs ? (int32_t)(((int64_t)r->h[j] + (1 << (hs - 1))) >> hs) : r->h[j];
#define HH(u) ((u) >= 0 && (u) < V90ARX_NCH ? (int64_t)hh[u] : 0)
    /* first column, then R(i+1, j+1) = R(i, j) - h[a-i] h[a-j] */
    for (i = 0; i < V90ARX_NF; i++) {
        int64_t acc = 0;
        int u;
        for (u = 0; u <= a - i && u + i < V90ARX_NCH; u++) acc += HH(u) * HH(u + i);
        A[TRI(i, 0)] = acc;
    }
    for (j = 0; j + 1 < V90ARX_NF; j++)
        for (i = j; i + 1 < V90ARX_NF; i++)
            A[TRI(i + 1, j + 1)] = A[TRI(i, j)] - HH(a - i) * HH(a - j);
    /* the residual, in hh^2: h is Q16, hh = h >> hs (up to 2^20, so that
     * the small taps keep their precision: R up to 2^47) */
    s2 = r->res;
    s2 = 16 - hs >= 0 ? s2 << (2 * (16 - hs)) : s2 >> (2 * (hs - 16));
    for (i = 0; i < V90ARX_NF; i++) if (A[TRI(i, i)] > dmax) dmax = A[TRI(i, i)];
    if (s2 < (dmax >> 18)) s2 = dmax >> 18;
    if (s2 < 1) s2 = 1;
    for (i = 0; i < V90ARX_NF; i++) A[TRI(i, i)] += s2;
    dmax += s2;
    /* scale: the largest diagonal to (2^26, 2^28], by an even shift */
    rs = 0;
    while ((dmax >> rs) > ((int64_t)1 << 28)) rs += 2;
    while (rs > -28 && (rs >= 0 ? dmax >> rs : dmax << -rs) <= ((int64_t)1 << 26)) rs -= 2;
    for (i = 0; i < NT; i++) A[i] = rs > 0 ? (A[i] + ((int64_t)1 << (rs - 1))) >> rs : A[i] * ((int64_t)1 << -rs);
    /* Cholesky, in place: G in Q14 */
    for (j = 0; j < V90ARX_NF; j++) {
        int64_t d = A[TRI(j, j)] * ((int64_t)1 << 28);
        for (k = 0; k < j; k++) d -= A[TRI(j, k)] * A[TRI(j, k)];
        if (d <= 0) return -1;
        A[TRI(j, j)] = isqrt64(d);
        for (i = j + 1; i < V90ARX_NF; i++) {
            t = A[TRI(i, j)] * ((int64_t)1 << 28);
            for (k = 0; k < j; k++) t -= A[TRI(i, k)] * A[TRI(j, k)];
            A[TRI(i, j)] = t / A[TRI(j, j)];
        }
    }
    /* G z = p, G' f = z; p in hh >> rs/2, with 8 more fraction bits (the
     * window's oldest positions see nothing of the current symbol but the
     * precursor taps, which are tiny, and their p, divided by the load
     * alone, would be all rounding). z'z = f'p, the MMSE gain, is at
     * most 1, so z in Q30 cannot overflow. |f| <= |z| / sqrt(least
     * eigenvalue) <= 2^-4 (the load is at least 2^8) and a column of G is
     * at most sqrt(trace) <= 2^16.8, so with f in Q32 a product in the back
     * substitution stays under 2^59 */
    for (i = 0; i < V90ARX_NF; i++) {
        int64_t p = HH(a - i);
        p = rs > 16 ? (p + (1 << (rs / 2 - 9))) >> (rs / 2 - 8) : p * ((int64_t)1 << (8 - rs / 2));
        t = p * ((int64_t)1 << 36);
        for (k = 0; k < i; k++) t -= A[TRI(i, k)] * z[k];
        z[i] = t / A[TRI(i, i)];
    }
    for (i = V90ARX_NF - 1; i >= 0; i--) {
        t = z[i] * 65536;
        for (k = i + 1; k < V90ARX_NF; k++) t -= A[TRI(k, i)] * fs[k];
        fs[i] = t / A[TRI(i, i)];
    }
    /* f p, the gain the MMSE solution leaves on the symbol (just under 1),
     * Q32: divided out, so the output is unbiased */
    for (i = 0; i < V90ARX_NF; i++) {
        int64_t p = HH(a - i);
        p = rs > 16 ? (p + (1 << (rs / 2 - 9))) >> (rs / 2 - 8) : p * ((int64_t)1 << (8 - rs / 2));
        bias += (fs[i] * p) >> 8;
    }
    bias >>= 12;                                         /* Q20 */
    if (bias <= 0) return -1;
    /* f_true = fs 2^e1, e1 = 16 - hs - rs/2: taps Q20 for an output of L in Q YF */
    e1 = 16 - hs - rs / 2;
    for (i = 0; i < V90ARX_NF; i++) {
        int64_t v = fs[i] * r->L / bias;                 /* fs L / bias, Q12 */
        int sh = e1 + 20 + V90ARX_YF - 12;
        v = sh >= 0 ? v * ((int64_t)1 << sh) : v >> -sh;
        if (v > 0x7fffffff) v = 0x7fffffff;
        if (v < -0x7fffffff) v = -0x7fffffff;
        r->f[i] = (int32_t)v;
    }
    /* b_j = sum f_true h_true / bias = sum fs hh 2^(-rs/2) / bias, Q16 */
    for (j = 0; j < V90ARX_NB; j++) {
        int64_t acc = 0;
        for (i = 0; i < V90ARX_NF; i++) acc += fs[i] * HH(a - i + 1 + j);
        acc = rs > 0 ? (acc + (1 << (rs / 2 - 1))) >> (rs / 2) : acc * ((int64_t)1 << (-rs / 2));
        acc = acc * 16 / bias;                           /* Q32 / Q20 -> Q16 */
        r->b[j] = (int32_t)(acc > 0x7fffffff ? 0x7fffffff : acc < -0x7fffffff ? -0x7fffffff : acc);
    }
#undef HH
    return 0;
}

/* ---- the slicer ------------------------------------------------------------ */

static int32_t slice(const v90arx_cons_t *c, int slot, int32_t y, int *ucode, int *pos)
{
    const int32_t *lv = c->lv[slot];
    int n = c->n[slot], lo = 0, hi = n - 1;
    int32_t a = y < 0 ? -y : y;
    *pos = y >= 0;
    if (n == 0) { *ucode = 0; return 0; }
    while (lo < hi) {                       /* the largest level <= a, or the first */
        int mid = (lo + hi + 1) >> 1;
        if (lv[mid] <= a) lo = mid; else hi = mid - 1;
    }
    if (lo + 1 < n && lv[lo + 1] - a < a - lv[lo]) lo++;
    *ucode = c->u[slot][lo];
    return *pos ? lv[lo] : -lv[lo];
}

/* ---- timing ------------------------------------------------------------------ */

/* How far the channel estimate has moved later since the equaliser was
 * first computed. A second order loop per block of TBLK symbols: the phase
 * correction is spread over the next block (slew), the frequency
 * integrates. The estimate follows the channel within about 1000 symbols
 * (NCH / mu); the loop is some four times slower. */
static void timing(v90arx_t *r)
{
    int64_t d = later_q16(r->h, r->href, V90ARX_NCH, 0);
    r->delta = (int32_t)d;
    r->step = clamp64(r->step + d * 65536 / (4096 * TBLK), ONE32 - STEP_MAX, ONE32 + STEP_MAX);
    r->slew = clamp64(d * 65536 / (32 * TBLK), -SLEW_MAX, SLEW_MAX);
}

/* ---- the equaliser ---------------------------------------------------------- */

static void equalise(v90arx_t *r, int32_t k, int16_t q)
{
    const int16_t *x, *dl;
    int64_t acc = 0, acc2 = 0;
    int32_t m, y, lev, e, ec, es;
    int i, uc = 0, pos = 1, known = 0, slot, g, gate;
    v90arx_dec_t *o;

    if (--r->xpos < 0) r->xpos = V90ARX_NF - 1;
    r->x[r->xpos] = r->x[r->xpos + V90ARX_NF] = q;
    r->pq += (((int64_t)q * q << 8) - r->pq) >> 8;
    m = k - V90ARX_DFF - (r->cpk - V90ARX_LEAD);
    if (m < 0) return;
    x = &r->x[r->xpos]; dl = &r->d[r->dpos];

    for (i = 0; i < V90ARX_NF; i++) acc += (int64_t)r->f[i] * x[i];
    for (i = 0; i < V90ARX_NB; i++) acc2 += (int64_t)r->b[i] * dl[i];
    y = asr64(acc, 20) - asr64(acc2, 16 - V90ARX_YF);

    if (r->have_next && m >= r->next_m) { r->cons = r->next; r->have_next = 0; }
    slot = (int)(m % 6);
    if (m < r->cfg.ntrain && m < r->mref) {
        lev = (int32_t)r->sb[m & 255] * (1 << V90ARX_YF);
        uc = r->cfg.uinfo; pos = lev >= 0; known = 1;
    } else if (r->cfg.known_lv && r->cfg.known_lv(r->cfg.known_ctx, m, &uc, &pos, &lev)) {
        known = 1;
    } else if (r->cfg.known && r->cfg.known(r->cfg.known_ctx, m, &uc, &pos)) {
        lev = v90_ucode_linear(r->cfg.alaw, (unsigned)uc) << V90ARX_YF;
        if (!pos) lev = -lev;
        known = 1;
    } else {
        lev = slice(&r->cons, slot, y, &uc, &pos);
    }
    if (r->state == V90ARX_TRAIN && m >= r->cfg.ntrain) r->state = V90ARX_DD;
    e = y - lev;

    /* Symbols far off their point (more than 4 sigma of what has been
     * accepted) teach nothing but the impairment: a robbed bit or a pad
     * moving a point by a step, or a wrong decision. They are left out of
     * the adaptation; the gate opens slowly if they persist. */
    {
        int64_t e2 = (int64_t)e * e;
        gate = !known && e2 > 16 * r->gmse;
        if (gate) r->gmse += r->gmse >> 10;
        else r->gmse += (e2 - r->gmse) >> 8;
    }
    /* A slot whose error stands 9 dB above the best slot's is impaired as
     * a whole (robbed bits): its symbols are not adapted on either. */
    if (!known && r->state == V90ARX_DD) {
        uint64_t mn = r->mse_slot[0];
        int s;
        for (s = 1; s < 6; s++) if (r->mse_slot[s] < mn) mn = r->mse_slot[s];
        if (r->mse_slot[slot] > 8 * mn) gate = 1;
    }
    if (!r->hold && !gate) {
        ec = e > ECLIP ? ECLIP : e < -ECLIP ? -ECLIP : e;
        /* mu 1/8 for the first second, then 1/32 */
        g = r->neq < FASTN ? 3 : 5;
        es = (int32_t)(((int64_t)ec * r->kf) >> 16);
        {
            int sh = r->shf + g;
            int32_t half = (int32_t)1 << (sh - 1);
            /* 32 bits, as always; with a wild error (es far out) the
             * product overflows and wraps, as it always did, now defined:
             * unsigned arithmetic, two's complement back */
            for (i = 0; i < V90ARX_NF; i++) r->f[i] -= (int32_t)((uint32_t)es * (uint32_t)x[i] + (uint32_t)half) >> sh;
        }
        es = (int32_t)(((int64_t)ec * r->kb) >> 16);
        {
            int sh = r->shb + g;
            int32_t half = (int32_t)1 << (sh - 1);
            for (i = 0; i < V90ARX_NB; i++) r->b[i] += (es * dl[i] + half) >> sh;
        }
    }
    {
        int16_t dv = (int16_t)(lev >> V90ARX_YF);
        int64_t e2 = (int64_t)e * e;           /* Q YF squared = Q8 */
        int64_t ms = (int64_t)r->mse, mss = (int64_t)r->mse_slot[slot];
        if (--r->dpos < 0) r->dpos = V90ARX_NB - 1;
        r->d[r->dpos] = r->d[r->dpos + V90ARX_NB] = dv;
        r->pd += (((int64_t)dv * dv << 8) - r->pd) >> 8;
        r->mse = (uint64_t)(ms + ((e2 - ms) >> 8));
        r->mse_slot[slot] = (uint64_t)(mss + ((e2 - mss) >> 6));
    }
    r->neq++;
    if ((r->neq & 63) == 0) renorm(r);

    /* the channel estimate, one symbol behind (every symbol of its span is
     * known or decided), the timing loop, and the equaliser again from time
     * to time */
    if (!r->hold) {
        /* what aliases at the band edge turns with our sampling phase, one
         * turn per sample of drift: compute the equaliser more often the
         * faster the clocks slip (every 0.26, 0.13, 0.06 s up to 30 ppm,
         * 61 ppm, above) */
        int64_t off = r->step - ONE32;
        int rc = V90ARX_RECALC;
        r->sb[m & 255] = (int16_t)(lev >> V90ARX_YF);
        if (off < 0) off = -off;
        if (off > (ONE32 >> 15)) rc >>= 1;          /* 30.5 ppm */
        if (off > (ONE32 >> 14)) rc >>= 1;          /* 61 ppm */
        if (!gate) estimate(r, m - V90ARX_LEAD, r->state == V90ARX_TRAIN ? (int64_t)r->L * r->L : r->pd >> 8, 2);
        if (++r->tblk >= TBLK) { r->tblk = 0; timing(r); }
        if (r->state == V90ARX_DD && r->neq % rc == 0) (void)dfe_design(r);
    }

    if (r->nout >= 2) return;
    o = &r->out[r->nout++];
    o->m = m; o->slot = (uint8_t)slot; o->ucode = (uint8_t)uc; o->sign = (uint8_t)pos;
    o->known = (uint8_t)known; o->y = y; o->e = e;
}

/* the equaliser starts: its lines filled from what is known so far */
static void eq_start(v90arx_t *r, int32_t k)
{
    int i;
    int32_t m = k - V90ARX_DFF - (r->cpk - V90ARX_LEAD);
    for (i = 0; i < V90ARX_NF; i++) r->x[i] = r->x[i + V90ARX_NF] = r->qb[(k - i) & 255];
    r->xpos = 0;
    for (i = 0; i < V90ARX_NB; i++) {
        int32_t mm = m - i;
        int16_t v = mm >= 0 ? r->sb[mm & 255] : 0;
        r->d[i] = r->d[i + V90ARX_NB] = v;
    }
    r->dpos = 0;
}

/* ---- one network-rate sample ----------------------------------------------- */

static void process(v90arx_t *r, int16_t q)
{
    int32_t n = r->n++, k;

    if (r->state == V90ARX_WAIT) {
        int st;
        if (r->deaf) return;
        st = v92_p6_tick(&r->sd, q, SD_FLOOR);
        if (st == V92_P6_ON) {
            /* the 1333 Hz line of each period of 6 */
            static const int16_t C6[6] = { 32767, 16384, -16384, -32767, -16384, 16384 };
            static const int16_t S6[6] = { 0, 28378, 28378, 0, -28378, -28378 };
            r->sd_pow += (int64_t)q * q; r->sd_cnt++;
            r->sd_br += (q * C6[r->sd_i]) >> 8;
            r->sd_bi -= (q * S6[r->sd_i]) >> 8;
            if (++r->sd_i == 6) {
                /* each period against the one SDLAG before it, from the
                 * SD_SKIP-th (the line's tail takes about 10 ms to settle
                 * into Sd's period after silence), and two periods late
                 * (the last ones before the reversal may hold Sd-bar) */
                int w = V90ARX_SDLAG + 3, p = r->sd_np++;
                r->sd_x[p % w][0] = r->sd_br; r->sd_x[p % w][1] = r->sd_bi;
                if (p - 2 - V90ARX_SDLAG >= SD_SKIP) {
                    const int32_t *a = r->sd_x[(p - 2) % w], *b = r->sd_x[(p - 2 - V90ARX_SDLAG) % w];
                    r->sd_yr += (int64_t)a[0] * b[0] + (int64_t)a[1] * b[1];
                    r->sd_yi += (int64_t)a[1] * b[0] - (int64_t)a[0] * b[1];
                }
                r->sd_br = r->sd_bi = 0; r->sd_i = 0;
            }
        } else if (st == V92_P6_REV && r->sd_cnt >= 48) {
            int32_t rms = isqrt64(r->sd_pow / r->sd_cnt);
            int64_t g;
            r->level = rms;
            if (rms < 1) rms = 1;
            g = ((int64_t)AGC_RMS << 12) / rms;
            r->gain = (int32_t)(g > (1 << 22) ? (1 << 22) : g);
            /* the line turned by -2 pi offset per period: from the tangent
             * (Q32) of SDLAG periods' turn, a step of 1 + offset (input
             * samples per network sample) */
            if (r->sd_np >= SD_SKIP + V90ARX_SDLAG + 10 && r->sd_yr > 0) {
                int64_t t = r->sd_yi, y = r->sd_yr;
                while (t > ((int64_t)1 << 30) || t < -((int64_t)1 << 30)) { t >>= 1; y >>= 1; }
                if (y > 0) {
                    /* tan, Q32; beyond 1 (a turn of 45 degrees, 7800 ppm)
                     * it is no codec clock, and the product would overflow */
                    t = clamp64(t * ONE32 / y, -ONE32, ONE32);
                    /* (relative to the step Sd came through: one sample, or
                     * what v90arx_clock() set; that one, from a longer
                     * measurement, stands unless Sd finds it 5 ppm off) */
                    int64_t corr = ((t * 683565276) >> 32) / V90ARX_SDLAG;  /* 1 / (2 pi), Q32 */
                    if (!r->clk_set || corr > CLK_TRUST || corr < -CLK_TRUST) r->step = r->step - corr;
                    r->step = clamp64(r->step, ONE32 - STEP_MAX, ONE32 + STEP_MAX);
                }
            }
            /* Sd-bar is 48 symbols, then TRN1d */
            r->n0 = n - V92_P6_REV_LAG + 48;
            v90_scr_init(&r->scr);
            r->mref = 0; r->nest = 0;
            memset(r->h, 0, sizeof r->h);
            r->state = V90ARX_EST;
        } else if (st == 0) {
            r->sd_pow = 0; r->sd_cnt = 0;
            r->sd_br = r->sd_bi = 0; r->sd_i = 0; r->sd_np = 0; r->sd_yr = r->sd_yi = 0;
        }
        return;
    }
    if (r->state == V90ARX_FAIL) return;

    k = n - r->n0;
    r->qb[k & 255] = q;
    /* TRN1d reference: the scrambler fed with ones, from zero (8.4.5); the
     * estimate needs it up to its end, V90ARX_EST0 steps from sample NCH,
     * whatever is taken as known (TRN1d is at least 2040 T) */
    while (r->mref <= k + V90ARX_LEAD && (r->mref < r->cfg.ntrain || r->state == V90ARX_EST)) {
        r->sb[r->mref & 255] = (int16_t)(v90_scramble_gpc(&r->scr, 1) ? r->L : -r->L);
        r->mref++;
    }
    if (r->state == V90ARX_EST) {
        if (k < V90ARX_NCH) return;
        /* mu 1 throughout: TRN1d's signs start far from white (the scrambler
         * leaves its zero state through long runs: lag-1 correlation 0.55
         * over the first 300 symbols, 0.13 by 600), so a smaller step would
         * still be converging when the equaliser is computed */
        estimate(r, k, (int64_t)r->L * r->L, 0);
        if (r->nest == V90ARX_EST0) {
            int i;
            if (find_cursor(r) != 0 || dfe_design(r) != 0) { r->state = V90ARX_FAIL; return; }
            r->pq = 0;
            for (i = 1; i <= 64; i++) { int32_t v = r->qb[(k - i) & 255]; r->pq += ((int64_t)v * v << 8) / 64; }
            r->pd = (int64_t)r->L * r->L << 8;
            renorm(r);
            eq_start(r, k);
            r->neq = 0; r->mse = 0;
            r->gmse = (int64_t)r->L * r->L << 4;        /* open: (L / 4)^2, Q8 */
            memcpy(r->href, r->h, sizeof r->h);
            r->tblk = 0;
            r->state = V90ARX_TRAIN;
        }
        return;
    }
    equalise(r, k, q);
}

/* ---- interpolator ----------------------------------------------------------- */

static void produce(v90arx_t *r)
{
    uint32_t f = (uint32_t)(-r->tnext);         /* behind the evaluable instant */
    int p = (int)(f >> 27);
    int32_t a = (int32_t)((f >> 12) & 0x7fff);  /* between branches p and p + 1, Q15 */
    const int16_t *h0 = v34_interp[p], *h1 = v34_interp[p + 1];
    int64_t acc0 = 0, acc1 = 0, v;
    int i, idx = r->rpos - 1;
    for (i = 0; i < V34_IN; i++) {
        int32_t s = r->ring[(idx - i) & 63];
        acc0 += s * h0[i];
        acc1 += s * h1[i];
    }
    v = acc0 + (((acc1 - acc0) * a) >> 15);     /* Q14 */
    process(r, sat16((v * r->gain) >> 26));
}

void v90arx_clock(v90arx_t *r, int32_t ppm_q8)
{
    r->step = clamp64(ONE32 + (int64_t)ppm_q8 * ONE32 / 256000000, ONE32 - STEP_MAX, ONE32 + STEP_MAX);
    r->clk_set = 1;
}

void v90arx_deaf(v90arx_t *r, int on)
{
    r->deaf = on != 0;
    v92_p6_init(&r->sd);
    r->sd_pow = 0; r->sd_cnt = 0;
    r->sd_br = r->sd_bi = 0; r->sd_i = 0; r->sd_np = 0; r->sd_yr = r->sd_yi = 0;
}

int v90arx_tick(v90arx_t *r, int16_t rx, int16_t tx)
{
    int16_t x = ec_tick(&r->ec, tx, rx, r->ec_mu);
    int i;
    r->eout = x;
    r->nout = 0;
    r->ring[r->rpos] = x;
    r->rpos = (r->rpos + 1) & 63;
    r->tnext -= ONE32;
    for (i = 0; r->tnext <= 0 && i < PRODUCE_MAX; i++) {
        produce(r);
        r->tnext += r->step + r->slew;
    }
    return r->nout;
}
