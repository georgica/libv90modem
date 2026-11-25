/* Test peer: a USR HST calling modem in double. See hstcall.h; the values
 * are docs/history/hst-facts.md's, by section. */
#include <math.h>
#include <string.h>
#include "hstcall.h"
#include "v32spec.h"

#define PI 3.14159265358979323846
#define FS 8000.0

/* 0 dBm0 is 3.17 dB below a full-scale sine of 32124 (G.711) */
static double rms_of(double dbm0) { return 32124.0 / sqrt(2.0) * pow(10.0, (dbm0 - 3.17) / 20.0); }

/* ======================= shapes ======================= */

/* root-raised-cosine, t in symbols */
static double rrc(double t, double b)
{
    double x = 4 * b * t;
    if (fabs(t) < 1e-9) return 1 - b + 4 * b / PI;
    if (fabs(fabs(x) - 1) < 1e-9)
        return b / sqrt(2.0) * ((1 + 2 / PI) * sin(PI / (4 * b)) + (1 - 2 / PI) * cos(PI / (4 * b)));
    return (sin(PI * t * (1 - b)) + x * cos(PI * t * (1 + b))) / (PI * t * (1 - x * x));
}

static double bessel_i0(double x)
{
    double s = 1, t = 1;
    int k;
    for (k = 1; k < 50; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
    return s;
}

/* u in -1..1 */
static double kaiser(double u, double beta)
{
    return fabs(u) <= 1 ? bessel_i0(beta * sqrt(1 - u * u)) / bessel_i0(beta) : 0;
}

/* ======================= V.32bis coding (v32spec.h) ======================= */

/* the sets at a common scale: 4800, 7200 and 9600 are printed at half
 * 12000's and 14400's (v32spec.h) */
static void load_sets(double pts[5][128][2])
{
    int r;
    for (r = 0; r < 5; r++) {
        const v32spec_row *row = r == 0 ? v32spec_4800 : r == 1 ? v32spec_7200 : r == 2 ? v32spec_9600
                               : r == 3 ? v32spec_12000 : v32spec_14400;
        int nb = r == 0 ? 2 : r + 3, scale = r >= 3 ? 1 : 2, i;
        for (; row->labels; row++) {
            const char *p = row->labels;
            int re = row->re0;
            while (*p) {
                int lab = 0;
                for (i = 0; i < nb; i++) lab = lab << 1 | (*p++ - '0');
                if (*p == ' ') p++;
                pts[r][lab][0] = re * scale;
                pts[r][lab][1] = row->im * scale;
                re += row->step;
            }
        }
    }
}

/* the synchronising states A, B, C, D (0..3) are the 4800 labels 00, 01, 11, 10 */
static const int st_lab[4] = { 0, 1, 3, 2 };

/* Table 1 or 2: Q1 Q2 and the last Y1 Y2 to the new Y1 Y2 */
static int diff_enc(const char *const *tab, int q, int y)
{
    int i;
    for (i = 0; i < 16; i++) {
        const char *r = tab[i];
        if (((r[0] - '0') << 1 | (r[1] - '0')) == q && ((r[2] - '0') << 1 | (r[3] - '0')) == y)
            return (r[5] - '0') << 1 | (r[6] - '0');
    }
    return 0;
}

/* and back: the Q1 Q2 that took y0 to y */
static int diff_dec(const char *const *tab, int y0, int y)
{
    int i;
    for (i = 0; i < 16; i++) {
        const char *r = tab[i];
        if (((r[2] - '0') << 1 | (r[3] - '0')) == y0 && ((r[5] - '0') << 1 | (r[6] - '0')) == y)
            return (r[0] - '0') << 1 | (r[1] - '0');
    }
    return 0;
}

/* V.32's convolutional encoder (Figure 1), three delay elements a, b, c in
 * a state s = a << 2 | b << 1 | c; Y0 is the last one's content. With
 * inputs Y1 Y2 the next state is a' = Y0, b' = a ^ Y1 ^ Y2 ^ (Y0 & (b ^ Y2)),
 * c' = b ^ Y2 ^ (Y1 & Y0). */
static int conv_next(int s, int y1, int y2, int *y0)
{
    int a = s >> 2 & 1, b = s >> 1 & 1, c = s & 1;
    int nb = a ^ y1 ^ y2 ^ (c & (b ^ y2));
    int nc = b ^ y2 ^ (y1 & c);
    *y0 = c;
    return c << 2 | nb << 1 | nc;
}

/* GPC, 1 + x^-18 + x^-23: h holds the line bits, the newest in bit 0 */
static int gpc_scr(uint32_t *h, int in)
{
    int o = (in ^ (int)(*h >> 17) ^ (int)(*h >> 22)) & 1;
    *h = *h << 1 | (uint32_t)o;
    return o;
}
static int gpc_dscr(uint32_t *h, int in)
{
    int o = (in ^ (int)(*h >> 17) ^ (int)(*h >> 22)) & 1;
    *h = *h << 1 | (uint32_t)(in & 1);
    return o;
}

/* ======================= the back transmitter ======================= */

static const uint32_t steps_word = 0x43705261u;   /* `Back channel` [FW] */

static int bt3(int lane) { return lane ? HC_B300_T3 : HC_B450_T3; }
static double bfc(int lane) { return lane ? 350.0 : 375.0; }

void hc_btx_init(hc_btx *t, int bps, double level, hc_getbit_fn getbit, void *user)
{
    int lane, i;
    memset(t, 0, sizeof *t);
    for (lane = 0; lane < 2; lane++) {
        int T3 = bt3(lane), h3 = HC_BSPAN * T3;
        double *p = lane ? t->p300 : t->p450, e = 0;
        for (i = 0; i <= 2 * h3; i++) {
            double u = (double)(i - h3) / T3;
            p[i] = rrc(u, 0.35) * (0.5 + 0.5 * cos(PI * u / HC_BSPAN));
        }
        for (i = h3 % 3; i <= 2 * h3; i += 3) e += p[i] * p[i];
        /* power of unit symbols: sum of the pulse's squares over a symbol's samples, halved */
        t->gain[lane] = level / sqrt(e / (2.0 * T3 / 3.0));
    }
    t->lane = bps == 300;
    t->getbit = getbit; t->user = user;
}

void hc_btx_start(hc_btx *t)
{
    t->on = 1;
    t->phase = 0;
    t->scr = 0;
    t->npre = HC_BPRE;
    t->next3 = 3 * t->n + HC_BSPAN * bt3(t->lane);
}

void hc_btx_speed(hc_btx *t, int bps) { t->lane = bps == 300; }
void hc_btx_stop(hc_btx *t) { t->on = 0; }

static void btx_symbol(hc_btx *t)
{
    int i, tri = 0, step;
    double a;
    hc_bsym *s;
    for (i = 0; i < 3; i++) {
        int b = t->npre > 0 ? 1 : (t->getbit(t->user) & 1);
        int o = (b ^ (int)(t->scr >> 13) ^ (int)(t->scr >> 16)) & 1;
        t->scr = t->scr << 1 | (uint32_t)o;
        tri |= o << i;
    }
    if (t->npre > 0) t->npre--;
    step = (int)(steps_word >> (4 * tri)) & 0xF;
    t->phase = (t->phase + step) & 7;
    a = PI / 8 + PI / 4 * t->phase;
    s = &t->ring[t->rpos];
    t->rpos = (t->rpos + 1) % HC_BRING;
    if (t->rn < HC_BRING) t->rn++;
    s->re = cos(a); s->im = sin(a);
    s->t3 = t->next3;
    s->lane = t->lane;
    t->next3 += bt3(t->lane);
    t->nsym++;
}

double hc_btx_tick(hc_btx *t)
{
    long t3 = 3 * t->n;
    double out = 0, c[2], s[2];
    int k, lane;
    while (t->on && t->next3 - HC_BSPAN * bt3(t->lane) <= t3) btx_symbol(t);
    for (lane = 0; lane < 2; lane++) {
        double w = 2 * PI * bfc(lane) / FS * (double)(t->n % 320);   /* 375 and 350 Hz repeat in 320 samples */
        c[lane] = cos(w); s[lane] = sin(w);
    }
    for (k = 0; k < t->rn; k++) {
        const hc_bsym *y = &t->ring[k];
        long h3 = HC_BSPAN * bt3(y->lane), tau = t3 - y->t3;
        if (tau < -h3 || tau > h3) continue;
        out += t->gain[y->lane] * (y->lane ? t->p300 : t->p450)[tau + h3] * (y->re * c[y->lane] - y->im * s[y->lane]);
    }
    t->n++;
    return out;
}

/* ======================= the back receiver ======================= */

void hc_brx_init(hc_brx *r, int bps)
{
    int i, n = HC_BLP, m = (n - 1) / 2;
    double baud = bps == 300 ? 100 : 150, fc = 190.0 / FS, sum = 0;
    memset(r, 0, sizeof *r);
    r->bps = bps;
    r->T = 1600.0 / baud;
    r->w = 2 * PI * (bps == 300 ? 350.0 : 375.0) / FS;
    for (i = 0; i < n; i++) {
        double k = i - m, h = fabs(k) < 1e-9 ? 2 * fc : sin(2 * PI * fc * k) / (PI * k);
        r->lp[i] = h * kaiser(k / m, 5.65);
        sum += r->lp[i];
    }
    for (i = 0; i < n; i++) r->lp[i] /= sum;
    r->mfh = (int)ceil(HC_BSPAN * r->T);
    for (i = 0; i <= 2 * r->mfh * HC_BMF_R; i++) {
        double u = ((double)i / HC_BMF_R - r->mfh) / r->T;
        r->mf[i] = fabs(u) <= HC_BSPAN ? rrc(u, 0.35) * (0.5 + 0.5 * cos(PI * u / HC_BSPAN)) : 0;
    }
    /* a real signal of power P is P / 2 at baseband */
    r->thr_on = rms_of(-43) * rms_of(-43) / 2;
    r->thr_off = rms_of(-48) * rms_of(-48) / 2;
}

/* the matched pulse at time t, in 1600 Hz samples */
static void brx_mf(const hc_brx *r, double t, double *yr, double *yi)
{
    long m0 = (long)ceil(t - r->mfh), m1 = (long)floor(t + r->mfh), m;
    double ar = 0, ai = 0;
    for (m = m0; m <= m1; m++) {
        int i = (int)floor((t - m + r->mfh) * HC_BMF_R + 0.5);
        double h = r->mf[i];
        ar += r->zr[m % HC_BZN] * h; ai += r->zi[m % HC_BZN] * h;
    }
    *yr = ar; *yi = ai;
}

static int brx_symbol(hc_brx *r, double yr, double yi)
{
    double dr = yr * r->pr + yi * r->pi_, di = yi * r->pr - yr * r->pi_;
    double d = atan2(di, dr), err, p = yr * yr + yi * yi;
    int q, tri, i, step;
    r->pw += (p - r->pw) / 32;
    if (r->nsym++ > 0) {
        /* Gardner: early strobes give a midpoint like the earlier symbol */
        double e = (r->mr * (r->pr - yr) + r->mi * (r->pi_ - yi)) / (r->pw > 1e-9 ? r->pw : 1e-9);
        r->ts += (r->nsym < 100 ? 0.05 : 0.01) * e * r->T;
    }
    r->pr = yr; r->pi_ = yi;
    if (r->acq < 2) return 0;
    q = (int)floor(d / (PI / 4) + 0.5);
    err = d - q * PI / 4;
    r->fo += 0.05 * err / r->T;
    r->e2 += (err * err - r->e2) / 64;
    step = q & 7;
    for (tri = 0; tri < 8; tri++) if ((int)(steps_word >> (4 * tri) & 0xF) == step) break;
    for (i = 0; i < 3; i++) {
        int s = tri >> i & 1;
        r->bits[i] = (uint8_t)((s ^ (int)(r->hist >> 13) ^ (int)(r->hist >> 16)) & 1);
        r->hist = r->hist << 1 | (uint32_t)s;
    }
    if (r->settle > 0) { r->settle--; return 0; }
    return 3;
}

int hc_brx_tick(hc_brx *r, double x)
{
    int nb = 0, i;
    double c = cos(r->w * (double)(r->n % 320)), s = sin(r->w * (double)(r->n % 320));
    r->xr[r->xp] = r->xr[r->xp + HC_BLP] = x * c;
    r->xi[r->xp] = r->xi[r->xp + HC_BLP] = -x * s;
    r->xp = (r->xp + 1) % HC_BLP;
    r->n++;
    if (++r->dec < 5) return 0;
    r->dec = 0;
    {
        double zr = 0, zi = 0, cr, ci, p;
        long m = r->zn;
        for (i = 0; i < HC_BLP; i++) {
            zr += r->lp[i] * r->xr[r->xp + i];
            zi += r->lp[i] * r->xi[r->xp + i];
        }
        p = zr * zr + zi * zi;
        /* the carrier's offset off */
        cr = cos(r->ph); ci = sin(r->ph);
        r->zr[m % HC_BZN] = zr * cr + zi * ci;
        r->zi[m % HC_BZN] = zi * cr - zr * ci;
        r->ph += r->fo;
        if (r->ph > PI) r->ph -= 2 * PI; else if (r->ph < -PI) r->ph += 2 * PI;
        r->zn++;
        /* carrier detect, 10 ms blocks */
        r->bp += p;
        if (++r->nb == 16) {
            double bp = r->bp / 16;
            r->bp = 0; r->nb = 0;
            if (!r->cd) {
                r->on_n = bp > r->thr_on ? r->on_n + 1 : 0;
                if (r->on_n >= 3) {
                    r->cd = 1; r->off_n = 0;
                    r->acq = 1; r->ar = r->ai = 0; r->an = 0; r->fo = 0; r->ph = 0;
                    r->ts = (double)r->zn + r->mfh; r->half = 0; r->nsym = 0; r->settle = 8;
                    r->pw = bp; r->e2 = 0.1;
                }
            } else {
                r->off_n = bp < r->thr_off ? r->off_n + 1 : 0;
                if (r->off_n >= 6) { r->cd = 0; r->acq = 0; r->on_n = 0; }
            }
        }
        if (r->acq == 1 && m > 0) {
            long q = (m - 1) % HC_BZN;
            /* before the offset is set the ring holds the plain baseband */
            r->ar += r->zr[m % HC_BZN] * r->zr[q] + r->zi[m % HC_BZN] * r->zi[q];
            r->ai += r->zi[m % HC_BZN] * r->zr[q] - r->zr[m % HC_BZN] * r->zi[q];
            if (++r->an >= 160) {
                r->fo = atan2(r->ai, r->ar);
                r->acq = 2;
            }
        }
        /* strobes */
        while (r->acq > 0 && r->ts + r->mfh <= (double)(r->zn - 1)) {
            double yr, yi;
            brx_mf(r, r->ts, &yr, &yi);
            if (r->half) { r->mr = yr; r->mi = yi; }
            else nb = brx_symbol(r, yr, yi);
            r->half ^= 1;
            r->ts += r->T / 2;
        }
    }
    r->nbits = nb;
    return nb;
}

double hc_brx_snr(const hc_brx *r)
{
    return r->cd && r->acq == 2 && r->e2 > 0 ? 10 * log10(1 / r->e2) : 0;
}

/* ======================= the forward transmitter ======================= */

void hc_ftx_init(hc_ftx *t, double level, hc_getbit_fn getbit, void *user)
{
    int i;
    double e = 0;
    memset(t, 0, sizeof *t);
    for (i = 0; i <= HC_FL * 10; i++) {
        double u = (i - HC_FSPAN * 10) / 10.0;
        t->p[i] = rrc(u, 1.0 / 12) * kaiser(u / HC_FSPAN, 3.0);
        e += t->p[i] * t->p[i];
    }
    /* mean |a|^2 = 40 (the 4 points; the sets are near it): power = 40 g^2 (e / 3) / (2 x 10 / 3) */
    t->gain = level / sqrt(40.0 * (e / 3.0) / (2 * 10.0 / 3.0));
    load_sets(t->pts);
    t->getbit = getbit; t->user = user;
    t->code = t->next_code = 4;
    t->seg = HC_FS_OFF;
    t->acc = 10;
}

static void ftx_seg(hc_ftx *t, int seg)
{
    t->seg = seg;
    t->left = seg >= HC_FS_S1 && seg <= HC_FS_S4 ? t->lens[seg - HC_FS_S1] : seg == HC_FS_DATA || seg == HC_FS_OFF ? 0 : 36;
    if (seg == HC_FS_S3 && !t->v17) t->scr = 0;
    if (seg == HC_FS_S2 && t->v17) t->scr = 0x2ECDD5u;    /* `V.17 long train` */
}

void hc_ftx_train(hc_ftx *t, int code, int long_form, int v17)
{
    t->v17 = v17;
    t->code = t->next_code = code < 2 ? 2 : code > 6 ? 6 : code;
    if (v17) { t->lens[0] = 256; t->lens[1] = 2976; t->lens[2] = 64; t->lens[3] = 48; }
    else { t->lens[0] = 180; t->lens[1] = 72; t->lens[2] = long_form ? 3096 : 200; t->lens[3] = 0; }
    t->bridge = 0;
    t->on = 1;
    ftx_seg(t, HC_FS_S1);
}

void hc_ftx_marks(hc_ftx *t, int new_code)
{
    t->next_code = new_code < 2 ? 2 : new_code > 6 ? 6 : new_code;
    ftx_seg(t, HC_FS_MARK_OLD);
}

void hc_ftx_stop(hc_ftx *t) { t->on = 0; t->seg = HC_FS_OFF; }
int  hc_ftx_in_data(const hc_ftx *t) { return t->seg == HC_FS_DATA; }

/* one symbol of data or ones at t->code: the point */
static void ftx_data(hc_ftx *t, int ones, double *x, double *y)
{
    int q[6], i, n = t->code, y12 = t->y1 << 1 | t->y2, lab;
    for (i = 0; i < n; i++) q[i] = gpc_scr(&t->scr, ones ? 1 : (t->getbit(t->user) & 1));
    if (n == 2) {
        y12 = diff_enc(v32spec_table2, q[0] << 1 | q[1], y12);
        lab = y12;
    } else {
        int y0, s = t->ta << 2 | t->tb << 1 | t->tc;
        y12 = diff_enc(v32spec_table1, q[0] << 1 | q[1], y12);
        s = conv_next(s, y12 >> 1, y12 & 1, &y0);
        t->ta = s >> 2 & 1; t->tb = s >> 1 & 1; t->tc = s & 1;
        lab = (y0 << 2 | y12);
        for (i = 2; i < n; i++) lab = lab << 1 | q[i];
    }
    t->y1 = y12 >> 1; t->y2 = y12 & 1;
    *x = t->pts[n - 2][lab][0]; *y = t->pts[n - 2][lab][1];
}

static void ftx_symbol(hc_ftx *t, double *x, double *y)
{
    int st = -1, d1, d2;
    *x = *y = 0;
    if (!t->on) return;
    while (t->seg >= HC_FS_S1 && t->seg <= HC_FS_S4 && t->left == 0)
        ftx_seg(t, t->seg == HC_FS_S4 || (t->seg == HC_FS_S3 && !t->v17) ? HC_FS_DATA : t->seg + 1);
    if (t->seg == HC_FS_MARK_OLD && t->left == 0) { t->code = t->next_code; ftx_seg(t, HC_FS_MARK_NEW); }
    if (t->seg == HC_FS_MARK_NEW && t->left == 0) ftx_seg(t, HC_FS_DATA);
    switch (t->seg) {
    case HC_FS_S1:
        st = (t->lens[0] - t->left) & 1 ? 1 : 0;          /* A B A B */
        break;
    case HC_FS_S2:
        if (!t->v17) { st = (t->lens[1] - t->left) & 1 ? 3 : 2; break; }   /* C D C D */
        d1 = gpc_scr(&t->scr, 1); d2 = gpc_scr(&t->scr, 1);
        st = d1 ? (d2 ? 0 : 1) : (d2 ? 3 : 2);             /* 00 C, 01 D, 11 A, 10 B */
        break;
    case HC_FS_S3:
        if (!t->v17) {
            d1 = gpc_scr(&t->scr, 1); d2 = gpc_scr(&t->scr, 1);
            st = d1 ? (d2 ? 2 : 3) : (d2 ? 1 : 0);         /* Table 4: 00 A, 01 B, 11 C, 10 D */
            break;
        } else {
            static const int rot[4] = { 1, 0, 2, 3 };     /* 00 +90, 01 0, 10 180, 11 -90 */
            int i0 = t->bridge++ % 16, b0 = i0 == 7 || i0 == 11 || i0 == 15, b1;
            i0 = t->bridge++ % 16; b1 = i0 == 7 || i0 == 11 || i0 == 15;
            d1 = gpc_scr(&t->scr, b0); d2 = gpc_scr(&t->scr, b1);
            st = (t->last + rot[d1 << 1 | d2]) & 3;
        }
        break;
    case HC_FS_S4: case HC_FS_MARK_OLD: case HC_FS_MARK_NEW:
        ftx_data(t, 1, x, y);
        break;
    case HC_FS_DATA:
        ftx_data(t, 0, x, y);
        break;
    default:
        break;
    }
    if (st >= 0) {
        *x = t->pts[0][st_lab[st]][0]; *y = t->pts[0][st_lab[st]][1];
        t->last = st;
        t->y1 = st_lab[st] >> 1; t->y2 = st_lab[st] & 1;
        t->ta = t->tb = t->tc = 0;
    }
    if (t->left > 0) t->left--;
}

double hc_ftx_tick(hc_ftx *t)
{
    double out = 0, c, s, br = 0, bi = 0;
    int j;
    if (t->acc >= 10) {
        double x, y;
        t->acc -= 10;
        for (j = HC_FL; j > 0; j--) { t->ar[j] = t->ar[j - 1]; t->ai[j] = t->ai[j - 1]; }
        ftx_symbol(t, &x, &y);
        t->ar[0] = x; t->ai[0] = y;
    }
    for (j = 0; j <= HC_FL; j++) {
        int i = t->acc + 10 * j;
        if (i > HC_FL * 10) break;
        br += t->ar[j] * t->p[i]; bi += t->ai[j] * t->p[i];
    }
    c = cos(2 * PI * 9 * (double)(t->n % 40) / 40); s = sin(2 * PI * 9 * (double)(t->n % 40) / 40);
    out = t->gain * (br * c - bi * s);
    t->acc += 3;
    t->n++;
    return out;
}

/* ======================= the forward receiver ======================= */

static void dec_reset(hc_dec *d, int code)
{
    int s;
    d->code = code;
    for (s = 0; s < 8; s++) d->v.pm[s] = 0;
    d->v.pos = 0; d->v.n = 0;
    d->py1 = d->py2 = 0;
    d->ones = 0;
}

void hc_frx_init(hc_frx *r, int code)
{
    int i, m = (HC_GN - 1) / 2, s, y;
    double fc = 525.0 / FS;
    memset(r, 0, sizeof *r);
    for (i = 0; i < HC_GN; i++) {
        double k = i - m, h = fabs(k) < 1e-9 ? 2 * fc : sin(2 * PI * fc * k) / (PI * k);
        r->g[i] = -h * kaiser(k / m, 5.0);
    }
    r->g[m] += 1;
    for (i = 0; i <= 2 * HC_FMFH * HC_FMF_R; i++) {
        double u = (double)i / HC_FMF_R - HC_FMFH;
        r->mf[i] = rrc(u * 0.3, 1.0 / 12) * kaiser(u / HC_FMFH, 4.0);
    }
    load_sets(r->pts);
    for (s = 0; s < 8; s++)
        for (y = 0; y < 4; y++) r->vnext[s][y] = conv_next(s, y >> 1, y & 1, &r->vy0[s]);
    r->thr_on = rms_of(-43) * rms_of(-43);
    r->thr_off = rms_of(-48) * rms_of(-48);
    r->state = HC_FRX_IDLE;
    r->next_code = code < 2 ? 2 : code > 6 ? 6 : code;
    dec_reset(&r->d, r->next_code);
}

void hc_frx_set_code(hc_frx *r, int code) { r->next_code = code < 2 ? 2 : code > 6 ? 6 : code; }

void hc_frx_arm(hc_frx *r, int code)
{
    r->next_code = code < 2 ? 2 : code > 6 ? 6 : code;
    if (r->state != HC_FRX_DATA) return;
    dec_reset(&r->d2, r->next_code);
    r->armed = 1; r->armed_n = 0;
}

int hc_frx_state(const hc_frx *r) { return r->state; }

void hc_frx_keep(hc_frx *r)
{
    if (r->state != HC_FRX_DATA) return;
    memcpy(r->sr, r->wr, sizeof r->sr); memcpy(r->si, r->wi, sizeof r->si);
    r->sfreq = r->freq; r->sdrift = r->tdrift; r->sadj = r->tadj;
    r->stored = 1;
}

double hc_frx_snr(const hc_frx *r)
{
    return r->state >= HC_FRX_TRAIN && r->mse > 0 ? 10 * log10(r->sig / r->mse) : 0;
}

static double frx_guard(hc_frx *r, double x)
{
    double y = 0;
    int i;
    r->gx[r->gp] = r->gx[r->gp + HC_GN] = x;
    for (i = 0; i < HC_GN; i++) y += r->g[i] * r->gx[r->gp + HC_GN - i];
    r->gp = (r->gp + 1) % HC_GN;
    return y;
}

static void frx_mf(const hc_frx *r, double t, double *yr, double *yi)
{
    long m0 = (long)ceil(t - HC_FMFH), m1 = (long)floor(t + HC_FMFH), m;
    double ar = 0, ai = 0;
    for (m = m0; m <= m1; m++) {
        int i = (int)floor((t - m + HC_FMFH) * HC_FMF_R + 0.5);
        ar += r->bbr[m % HC_FBB] * r->mf[i]; ai += r->bbi[m % HC_FBB] * r->mf[i];
    }
    *yr = ar; *yi = ai;
}

/* the Viterbi decoder: the label decided HC_VD symbols ago, or -1; *hard
 * the nearest point's label */
static int vit_step(hc_frx *r, hc_dec *d, double zr, double zi, int *hard)
{
    int c = d->code, set = c - 2, npts = 1 << (c + 1), i, s, y, best = 0, bl[8];
    double bm[8], nm[8], min;
    hc_vit *v = &d->v;
    for (i = 0; i < 8; i++) { bm[i] = 1e30; bl[i] = 0; }
    for (i = 0; i < npts; i++) {
        double dx = zr - r->pts[set][i][0], dy = zi - r->pts[set][i][1], e = dx * dx + dy * dy;
        int k = i >> (c - 2);
        if (e < bm[k]) { bm[k] = e; bl[k] = i; }
    }
    for (i = 1; i < 8; i++) if (bm[i] < bm[best]) best = i;
    *hard = bl[best];
    for (s = 0; s < 8; s++) nm[s] = 1e30;
    for (s = 0; s < 8; s++)
        for (y = 0; y < 4; y++) {
            int ns = r->vnext[s][y], sub = r->vy0[s] << 2 | y;
            double m = v->pm[s] + bm[sub];
            if (m < nm[ns]) { nm[ns] = m; v->hist[v->pos][ns] = bl[sub] << 3 | s; }
        }
    min = nm[0]; best = 0;
    for (s = 1; s < 8; s++) if (nm[s] < min) { min = nm[s]; best = s; }
    for (s = 0; s < 8; s++) v->pm[s] = nm[s] - min;
    v->pos = (v->pos + 1) & 31;
    if (++v->n < HC_VD) return -1;
    s = best;
    for (i = 1; i < HC_VD; i++) s = v->hist[(v->pos - i) & 31][s] & 7;
    return v->hist[(v->pos - HC_VD) & 31][s] >> 3;
}

/* a decoded label: its bits, descrambled, into out[]; returns how many */
static int dec_bits(hc_dec *d, int lab, uint8_t *out)
{
    int c = d->code, y12, q, i, n = 0, all = 1;
    if (c == 2) y12 = lab & 3;
    else y12 = (lab >> (c - 2)) & 3;
    q = diff_dec(c == 2 ? v32spec_table2 : v32spec_table1, d->py1 << 1 | d->py2, y12);
    d->py1 = y12 >> 1; d->py2 = y12 & 1;
    for (i = 0; i < c; i++) {
        int b = i == 0 ? q >> 1 : i == 1 ? q & 1 : (lab >> (c - 1 - i)) & 1;
        b = gpc_dscr(&d->dh, b);
        if (!b) all = 0;
        if (out) out[n] = (uint8_t)b;
        n++;
    }
    d->ones = all ? d->ones + 1 : 0;
    return n;
}

/* the nearest of A..D: its index, and the squared distance */
static int near4(const hc_frx *r, double zr, double zi, double *dist)
{
    int s, best = 0;
    double bd = 1e30;
    for (s = 0; s < 4; s++) {
        double dx = zr - r->pts[0][st_lab[s]][0], dy = zi - r->pts[0][st_lab[s]][1], e = dx * dx + dy * dy;
        if (e < bd) { bd = e; best = s; }
    }
    *dist = bd;
    return best;
}

static int popc(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* the last k decisions alternate between two neighbours */
static int alternating(uint32_t sh, int k)
{
    int i;
    for (i = 0; i + 2 < k; i++) {
        int a = sh >> (2 * i) & 3, b = sh >> (2 * i + 2) & 3, c = sh >> (2 * i + 4) & 3;
        if (a != c || a == b || ((a - b) & 1) == 0) return 0;
    }
    return 1;
}

static void frx_symbol(hc_frx *r)
{
    double zr = 0, zi = 0, cs, sn, qr, qi, dr, di, er, ei, fr, fi, perr, mu, kp, kf, d4, px = 0;
    int k, s4, hard = -1, lab = -1;
    if (r->state == HC_FRX_GAIN && r->restoring) {
        /* the kept equaliser: the carrier's phase from 8 symbols' fourth
         * power, against A's (B, C and D have the same), once the delay
         * line holds the symbols the taps were kept for */
        double z2r, z2i;
        for (k = 0; k < HC_EQN; k++) {
            zr += r->wr[k] * r->xr[k] - r->wi[k] * r->xi[k];
            zi += r->wr[k] * r->xi[k] + r->wi[k] * r->xr[k];
        }
        /* once the delay line is full (16 symbols), 8 symbols */
        z2r = zr * zr - zi * zi; z2i = 2 * zr * zi;
        if (r->gn >= HC_EQN / 2) { r->z4r += z2r * z2r - z2i * z2i; r->z4i += 2 * z2r * z2i; }
        if (++r->gn == HC_EQN / 2 + 8) {
            r->theta = (atan2(r->z4i, r->z4r) - 4 * atan2(-2.0, -6.0)) / 4;
            r->state = HC_FRX_TRAIN; r->trsym = 0; r->restoring = 0;
        }
        return;
    }
    if (r->state == HC_FRX_GAIN) {
        r->gacc += r->xr[0] * r->xr[0] + r->xi[0] * r->xi[0];
        if (++r->gn == 16) {
            r->wr[HC_EQN / 2] = sqrt(40.0 / (r->gacc / 16 > 1e-9 ? r->gacc / 16 : 1e-9));
            r->state = HC_FRX_TRAIN; r->trsym = 0;
        }
        return;
    }
    for (k = 0; k < HC_EQN; k++) {
        zr += r->wr[k] * r->xr[k] - r->wi[k] * r->xi[k];
        zi += r->wr[k] * r->xi[k] + r->wi[k] * r->xr[k];
        px += r->xr[k] * r->xr[k] + r->xi[k] * r->xi[k];
    }
    cs = cos(r->theta); sn = sin(r->theta);
    qr = zr * cs + zi * sn; qi = zi * cs - zr * sn;
    s4 = near4(r, qr, qi, &d4);
    r->sh = r->sh << 2 | (uint32_t)s4;
    r->offw = r->offw << 1 | (d4 > 4.0);
    r->nsym++;
    r->snb = 0;
    if (r->state == HC_FRX_TRAIN) {
        r->trsym++;
        dr = r->pts[0][st_lab[s4]][0]; di = r->pts[0][st_lab[s4]][1];
        /* fast for 300 symbols, 100 from a kept equaliser */
        if (r->trsym < 300 && !r->warm) { mu = 0.1; kp = 0.1; kf = 2e-3; }
        else { mu = 0.03; kp = 0.05; kf = 5e-4; }
        if (d4 > 4.0) mu = kp = 0;                /* off the 4 points: data coming */
        if (alternating(r->sh, 8) && d4 < 4.0) { r->seen_alt = 1; r->rand_n = 0; }
        else r->rand_n++;
        /* data only once trained on the random 4 points (TRN, V.17's
         * conditioning), not on the alternations alone: 48 symbols past
         * them, the error before the last 16 under -14 dB */
        if (r->seen_alt && ((r->d.code >= 3 && r->rand_n > 48 && r->sig > 25 * r->msh[(r->mshp + 1) & 15]
                             && popc(r->offw & 0xFF) > 3)
                            || (r->d.code == 2 && r->rand_n > 8))) {
            r->state = HC_FRX_DATA;
            dec_reset(&r->d, r->d.code);
        }
    } else {
        if (r->d.code == 2) {
            dr = r->pts[0][st_lab[s4]][0]; di = r->pts[0][st_lab[s4]][1];
            r->snb = dec_bits(&r->d, st_lab[s4], r->sbits);
        } else {
            lab = vit_step(r, &r->d, qr, qi, &hard);
            dr = r->pts[r->d.code - 2][hard][0]; di = r->pts[r->d.code - 2][hard][1];
            if (lab >= 0) r->snb = dec_bits(&r->d, lab, r->sbits);
        }
        mu = 0.01; kp = 0.03; kf = 1e-4;
        if (r->armed) {
            int h2, l2;
            uint8_t tmp[8];
            r->armed_n++;
            if (r->d2.code == 2) l2 = st_lab[s4];
            else l2 = vit_step(r, &r->d2, qr, qi, &h2);
            if (l2 >= 0) dec_bits(&r->d2, l2, tmp);
            mu = 0;
            if (r->d2.ones >= 16) {
                r->d = r->d2;
                r->armed = 0;
            } else if (r->armed_n > 4800) r->armed = 0;
        }
        /* a new training: AB (or CD) on the 4 points */
        if (r->armed == 0 && d4 < 4.0 && r->nsym > 24 && alternating(r->sh, 16) && popc(r->offw & 0xFFFF) == 0) {
            int nc = r->next_code;
            r->state = HC_FRX_TRAIN; r->trsym = 0; r->seen_alt = 1; r->offw = 0; r->mse0 = 40; for (k = 0; k < 16; k++) r->msh[k] = 40;
            dec_reset(&r->d, nc);
            r->snb = 0;
        }
    }
    /* the signal fading (3 dB under its level): nothing adapted (the
     * normalised steps on a fading input would wreck the taps) */
    if (r->fading) { mu = 0; kp = 0; kf = 0; }
    er = qr - dr; ei = qi - di;
    r->mse += (er * er + ei * ei - r->mse) / 64;
    /* training: the error over every point, 16 symbols, kept for 16 */
    if (r->state == HC_FRX_TRAIN) {
        r->mse0 += (er * er + ei * ei - r->mse0) / 16;
        r->mshp = (r->mshp + 1) & 15;
        r->msh[r->mshp] = r->mse0;
    }
    r->sig += (dr * dr + di * di - r->sig) / 64;
    perr = (qi * dr - qr * di) / (dr * dr + di * di + 1e-9);
    if (perr > 0.5) perr = 0.5; else if (perr < -0.5) perr = -0.5;
    r->freq += kf * perr;
    r->theta += kp * perr + r->freq;
    if (r->theta > PI) r->theta -= 2 * PI; else if (r->theta < -PI) r->theta += 2 * PI;
    fr = er * cs - ei * sn; fi = ei * cs + er * sn;
    mu /= (px > 1e-9 ? px : 1e-9);
    for (k = 0; k < HC_EQN; k++) {
        r->wr[k] -= mu * (fr * r->xr[k] + fi * r->xi[k]);
        r->wi[k] -= mu * (fi * r->xr[k] - fr * r->xi[k]);
    }
    /* the timing: the error's gradient against the strobes' time, from the
     * taps one strobe later and earlier (dz/dt), normalised by its mean
     * square; and the equaliser's centre of energy held, weakly, at its
     * middle (an FSE and a timing loop can otherwise drift together) */
    if (r->state == HC_FRX_DATA || r->trsym > 300) {
        double pr_ = 0, pi2 = 0, mr = 0, mi = 0, gr, gi, num = 0, den = 0;
        for (k = 1; k < HC_EQN; k++) {
            pr_ += r->wr[k] * r->xr[k - 1] - r->wi[k] * r->xi[k - 1];
            pi2 += r->wr[k] * r->xi[k - 1] + r->wi[k] * r->xr[k - 1];
            mr += r->wr[k - 1] * r->xr[k] - r->wi[k - 1] * r->xi[k];
            mi += r->wr[k - 1] * r->xi[k] + r->wi[k - 1] * r->xr[k];
        }
        gr = (pr_ - mr) / (2 * r->tstep); gi = (pi2 - mi) / (2 * r->tstep);
        {
            double dqr = gr * cs + gi * sn, dqi = gi * cs - gr * sn;
            r->dq2 += (dqr * dqr + dqi * dqi - r->dq2) / 256;
            if (r->dq2 > 1e-12 && mu > 0) {
                double u = (er * dqr + ei * dqi) / r->dq2;
                if (u > 1) u = 1; else if (u < -1) u = -1;
                r->ts -= 0.01 * u; r->tadj -= 0.01 * u;
                r->tdrift -= 2.5e-5 * u;           /* a clock offset: the step itself */
            }
        }
        for (k = 0; k < HC_EQN; k++) {
            double e = r->wr[k] * r->wr[k] + r->wi[k] * r->wi[k];
            num += k * e; den += e;
        }
        if (den > 0) { r->ts += 1e-4 * (HC_EQN / 2 - num / den); r->tadj += 1e-4 * (HC_EQN / 2 - num / den); }
    }
}

static void frx_listen(hc_frx *r)
{
    r->state = HC_FRX_IDLE;
    r->abrun = 0; r->low = 0; r->fading = 0;
    r->r6r = r->r6i = r->r30r = r->r30i = 0;
    r->armed = 0;
}

int hc_frx_tick(hc_frx *r, double x)
{
    double g = frx_guard(r, x), c, s;
    long n = r->n;
    int nbits = 0;
    c = cos(2 * PI * 9 * (double)(n % 40) / 40); s = sin(2 * PI * 9 * (double)(n % 40) / 40);
    r->bbr[n % HC_FBB] = g * c; r->bbi[n % HC_FBB] = -g * s;
    r->pw += (g * g - r->pw) / 40;
    r->n++;
    if (r->state == HC_FRX_IDLE) {
        double a6 = 2 * PI * 600 / FS * (double)(n % 40), a30 = 2 * PI * 3000 / FS * (double)(n % 8), l = 1.0 / 40, p6, p30;
        r->r6r += (g * cos(a6) - r->r6r) * l; r->r6i += (-g * sin(a6) - r->r6i) * l;
        r->r30r += (g * cos(a30) - r->r30r) * l; r->r30i += (-g * sin(a30) - r->r30i) * l;
        p6 = 2 * (r->r6r * r->r6r + r->r6i * r->r6i);
        p30 = 2 * (r->r30r * r->r30r + r->r30i * r->r30i);
        if (r->pw > r->thr_on && p6 > r->pw / 20 && p30 > r->pw / 20) r->abrun++;
        else r->abrun = 0;
        if (r->abrun >= 320) {
            /* the symbol instants from the tones' phases: t0 = -T (phase(3000) - phase(600)) / 2 pi */
            double T = FS / 2400, d = atan2(r->r30i * r->r6r - r->r30r * r->r6i, r->r30r * r->r6r + r->r30i * r->r6i);
            double t0 = -d / (2 * PI) * T, k = ceil(((double)n - 60 - t0) / T);
            /* the resonators' phases are against n = 0, and a period of 40 */
            r->ts = t0 + k * T;
            r->tstep = T / 2;
            r->tdrift = 0; r->tadj = 0;
            r->pref = r->pw;
            r->half = 0;
            memset(r->xr, 0, sizeof r->xr); memset(r->xi, 0, sizeof r->xi);
            r->theta = 0;
            r->sh = 0; r->offw = 0; r->seen_alt = 0; r->low = 0; r->nsym = 0;
            r->mse = 0; r->sig = 0; r->mse0 = 40; { int j; for (j = 0; j < 16; j++) r->msh[j] = 40; }
            dec_reset(&r->d, r->next_code);
            if (r->stored) {
                memcpy(r->wr, r->sr, sizeof r->wr); memcpy(r->wi, r->si, sizeof r->wi);
                r->freq = r->sfreq; r->tdrift = r->sdrift;
                /* the strobes where the timing loop had them against AB's estimate */
                r->ts += r->sadj; r->tadj = r->sadj;
                r->z4r = r->z4i = 0; r->gn = 0; r->restoring = 1; r->warm = 1;
                r->state = HC_FRX_GAIN;
            } else {
                memset(r->wr, 0, sizeof r->wr); memset(r->wi, 0, sizeof r->wi);
                r->freq = 0;
                r->gacc = 0; r->gn = 0; r->restoring = 0; r->warm = 0;
                r->state = HC_FRX_GAIN;
            }
        }
        r->nbits = 0;
        return 0;
    }
    /* carrier lost: under -48 dBm0, or 10 dB under the signal's level (its
     * power in training and data, followed slowly while within 6 dB) */
    if (r->pw > r->pref / 4) r->pref += (r->pw - r->pref) / 4096;
    r->low = r->pw < r->thr_off || r->pw < r->pref / 10 ? r->low + 1 : 0;
    /* fading: the equaliser kept as it was before (once, as it begins) */
    if (r->pw < r->pref / 2) { if (!r->fading) hc_frx_keep(r); r->fading = 1; }
    else r->fading = 0;
    if (r->low > 800) {
        frx_listen(r);
        r->nbits = 0;
        return 0;
    }
    while (r->ts + HC_FMFH <= (double)n) {
        double yr, yi;
        frx_mf(r, r->ts, &yr, &yi);
        memmove(r->xr + 1, r->xr, (HC_EQN - 1) * sizeof r->xr[0]);
        memmove(r->xi + 1, r->xi, (HC_EQN - 1) * sizeof r->xi[0]);
        r->xr[0] = yr; r->xi[0] = yi;
        if (!r->half) {
            frx_symbol(r);
            if (r->snb > 0 && nbits + r->snb <= (int)sizeof r->bits) {
                memcpy(r->bits + nbits, r->sbits, (size_t)r->snb);
                nbits += r->snb;
            }
        }
        r->half ^= 1;
        r->ts += r->tstep + (r->half ? 0 : r->tdrift);
    }
    r->nbits = nbits;
    return nbits;
}

/* ======================= the calling modem ======================= */

void hstcall_defaults(hstcall_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->back_bps = 450;
    c->start_bps = 450;
    c->rates = 0x7C;
    c->flags = 0x01;
    c->level_dbm0 = -12;
}

/* MNP's bit for whichever transmitter runs; our LNA watched */
static int hc_bit(void *u)
{
    hstcall_t *c = (hstcall_t *)u;
    mnppeer_t *m = &c->mnp;
    int due, conf, b;
    /* after an 0x2R or 0x3R's LNA: the rest of this symbol ones, then the
     * marks or the training, which take no bits until their data */
    if (c->hold) {
        if (c->ftx.seg != HC_FS_DATA) return 1;
        c->hold = 0;
    }
    /* an 0x2R or 0x3R taken: once our LNA has gone, between frames (the
     * frame in flight finishes [PUB]; the LNA, owed, goes before any LT).
     * The marks straight after it; a training 20 ms after it, as our
     * reversal waits [OURS]: a receiver that starts again on a training
     * loses what its decoder still holds (24 symbols at 3..6), the LNA's
     * end */
    if (c->rate_rx >= 0 && c->role == HC_ROLE_FWD && m->line_framing == 0 && !m->lna_due && !c->lna_going
        && c->lna_done >= 0 && (((c->rate_rx & 0xF0) == 0x30 && !c->cfg.bm_training) || c->ms >= c->lna_done + 20)) {
        int r = c->rate_rx & 0x0F;
        if ((c->rate_rx & 0xF0) == 0x30 && !c->cfg.bm_training) hc_ftx_marks(&c->ftx, r);
        else hc_ftx_train(&c->ftx, r, 0, c->cfg.bm_training);
        c->fwd_code = r;
        c->rate_rx = -1;
        c->hold = 1;
        return 1;
    }
    due = m->lna_due; conf = m->conf_la;
    b = mnppeer_tx_bit(m);
    if (due && !m->lna_due) { c->lna_going = 1; c->lna_done = -1; }
    if (c->lna_going && m->line_framing == 0) { c->lna_going = 0; c->lna_done = c->ms; }
    /* the confirming LA: its end, to the bit */
    if (conf && !m->conf_la) c->conf_going = 1;
    if (c->conf_going && m->line_framing == 0) { c->conf_going = 0; c->conf_at = c->ms; }
    return b;
}

static int open_code(const hstcall_t *c) { return c->cfg.bm_training ? 3 : 4; }

void hstcall_init(hstcall_t *c, const hstcall_cfg *cfg)
{
    mnp_peer_cfg m;
    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    if (c->cfg.back_bps != 300) c->cfg.back_bps = 450;
    if (c->cfg.start_bps != 300) c->cfg.start_bps = 450;
    if (c->cfg.level_dbm0 == 0) c->cfg.level_dbm0 = -12;
    if (c->cfg.hst96) { c->cfg.rates = 0x10; c->cfg.flags = 0x09; c->cfg.back_bps = 300; }
    if (c->cfg.back_bps == 300 && (c->cfg.flags || c->cfg.rates)) c->cfg.flags |= 0x08;
    c->level = rms_of(c->cfg.level_dbm0);
    c->phase = HC_TONE;
    c->role = HC_ROLE_BACK;
    c->fwd_code = open_code(c);
    c->back_bps = c->cfg.start_bps;
    c->conf_at = -1; c->ln_pend = -1; c->ln_rx = -1; c->lna_done = -1; c->rate_rx = -1;
    c->tq1 = c->tq2 = 0;
    hc_btx_init(&c->btx, c->back_bps, c->level, hc_bit, c);
    hc_ftx_init(&c->ftx, c->level, hc_bit, c);
    hc_frx_init(&c->frx, c->fwd_code);
    hc_brx_init(&c->brx, c->cfg.back_bps);
    memset(&m, 0, sizeof m);
    m.originator = 1; m.framing = 3;
    m.send_opt = c->cfg.send_opt; m.opt = 2;
    m.c0 = c->cfg.flags || c->cfg.rates; m.c0_flags = c->cfg.flags; m.c0_rates = c->cfg.rates;
    m.layout_rx_any = 1;
    m.never_lr = c->cfg.no_lr;
    m.lr_delay_ms = c->cfg.lr_delay_ms > 0 ? c->cfg.lr_delay_ms : 0;   /* mnppeer's clock starts with the back channel */
    m.t401_ms = 3000; m.n400 = 15;           /* BinModem's HST session [BM] */
    m.confirm_la_standard = 1;                /* [BM] */
    m.seed = 0x48535443u;
    mnppeer_init(&c->mnp, &m);
}

size_t hstcall_write(hstcall_t *c, const uint8_t *d, size_t n) { return mnppeer_write(&c->mnp, d, n); }
size_t hstcall_read(hstcall_t *c, uint8_t *d, size_t max) { return mnppeer_read(&c->mnp, d, max); }
int    hstcall_state(const hstcall_t *c) { return c->phase; }

long hstcall_backlog(const hstcall_t *c)
{
    const mnppeer_t *p = &c->mnp;
    long n = (long)p->txq_n;
    uint8_t ns;
    for (ns = (uint8_t)(p->ack + 1); ns != p->vs; ns++) n += p->sto_n[ns % MNPP_MAX_K];
    return n;
}

/* the tone: 10 ms blocks, 2100 Hz (Goertzel) at least half the block's
 * power, over -43 dBm0 */
static int tone_block(hstcall_t *c, double x)
{
    double coef = 2 * cos(2 * PI * 2100 / FS), q0 = coef * c->tq1 - c->tq2 + x;
    c->tq2 = c->tq1; c->tq1 = q0;
    c->te += x * x;
    if (++c->tk < 80) return 0;
    {
        double mag = c->tq1 * c->tq1 + c->tq2 * c->tq2 - coef * c->tq1 * c->tq2;
        double tone = 2 * mag / (80.0 * 80.0), pw = c->te / 80;
        int on = pw > rms_of(-43) * rms_of(-43) && tone > 0.5 * pw;
        c->tq1 = c->tq2 = c->te = 0; c->tk = 0;
        if (on) { c->t_on++; c->t_off = 0; return 0; }
        if (++c->t_off >= 3) {
            if (c->t_on >= 40) return 1;      /* 400 ms, then 30 ms absent */
            c->t_on = 0;                      /* a shorter burst */
        }
    }
    return 0;
}

static void start_back(hstcall_t *c, int code)
{
    hc_btx_init(&c->btx, c->back_bps, c->level, hc_bit, c);
    hc_btx_start(&c->btx);
    hc_frx_set_code(&c->frx, code);
    frx_listen(&c->frx);
    c->role = HC_ROLE_BACK;
    c->phase = HC_BACK;
    c->t_switch = c->ms;
}

static int best_code(int rates)
{
    int k;
    for (k = 6; k >= 2; k--) if (rates & (1 << k)) return k;
    return 0;
}

/* our LN's LNA is in */
static void ln_done(hstcall_t *c, int code)
{
    int r = code & 0x0F, kind = code & 0xF0;
    if (kind == 0x10 && c->role == HC_ROLE_BACK) {
        /* the reversal: our back transmitter stops at once; 200 ms; our
         * training. The forward equaliser is kept for when we listen again */
        hc_frx_keep(&c->frx);
        hc_btx_stop(&c->btx);
        c->phase = HC_GAP;
        c->t_wait = c->ms + 200;
        c->wait_code = r;
    } else if (kind == 0x20 && c->role == HC_ROLE_BACK) {
        hc_frx_set_code(&c->frx, r);
        c->fwd_code = r;
    } else if (kind == 0x30 && c->role == HC_ROLE_BACK) {
        if (c->cfg.bm_training) hc_frx_set_code(&c->frx, r);
        else hc_frx_arm(&c->frx, r);
        c->fwd_code = r;
    }
}

/* an LN from the answerer */
static void ln_take(hstcall_t *c, int code)
{
    int r = code & 0x0F, kind = code & 0xF0;
    if (c->role != HC_ROLE_FWD || r < 2 || r > 6) return;
    if (c->lns_ignored < c->cfg.ignore_ln) { c->lns_ignored++; return; }
    if (kind == 0x10) {
        c->ln_rx = code;
        if (c->mnp.lna_due || c->lna_going) c->lna_done = -1;
        c->phase = HC_TURN;
    } else if (kind == 0x20 || kind == 0x30) {
        c->rate_rx = code;                    /* done in hc_bit, after our LNA */
        if (c->mnp.lna_due || c->lna_going) c->lna_done = -1;
    }
}

static void every_ms(hstcall_t *c)
{
    mnppeer_t *m = &c->mnp;
    int code;
    c->ms++;
    if (c->phase == HC_DROPPED) return;
    if (c->cfg.drop_at_ms > 0 && c->ms >= c->cfg.drop_at_ms) {
        hc_btx_stop(&c->btx); hc_ftx_stop(&c->ftx);
        c->phase = HC_DROPPED;
        return;
    }
    if (c->phase == HC_TONE) return;
    mnppeer_tick(m, 1);
    if (c->cfg.ld_at_ms > 0 && c->ms >= c->cfg.ld_at_ms && !c->ld_sent) {
        mnppeer_release(m);
        c->ld_sent = 1; c->ld_off = -1;
    }
    if (c->ld_sent) {
        if (c->ld_off < 0 && !m->ld_due && m->line_framing == 0)
            c->ld_off = c->ms + (c->cfg.ld_hold_ms > 0 ? c->cfg.ld_hold_ms : 1000);   /* [BM] */
        if (c->ld_off >= 0 && c->ms >= c->ld_off) {
            hc_btx_stop(&c->btx); hc_ftx_stop(&c->ftx);
            c->phase = HC_DROPPED;
            return;
        }
    }
    if (!c->linked && mnppeer_state(m) == MNPP_CONNECTED) {
        c->linked = 1;
        if (m->hst && best_code(m->hst_rates & 0x7C)) {
            c->fwd_code = best_code(m->hst_rates & 0x7C);
            hc_frx_set_code(&c->frx, c->fwd_code);      /* the answerer's next training */
        }
    }
    /* 300 bit/s agreed: after the confirming LA, plus 250 ms [BM] */
    if (c->linked && m->hst && m->back300 && c->back_bps == 450 && c->role == HC_ROLE_BACK && c->phase == HC_BACK
        && c->conf_at >= 0 && c->ms >= c->conf_at + 250) {
        hc_btx_speed(&c->btx, 300);
        c->back_bps = 300;
    }
    while ((code = mnppeer_get_ln(m)) >= 0) ln_take(c, code);
    /* our LN: out, then its LNA */
    if (c->ln_pend >= 0) {
        if (m->ln_code == c->ln_pend) c->ln_out = 1;
        else if (c->ln_out && m->ln_code < 0) {
            int was = c->ln_pend;
            c->ln_pend = -1; c->ln_out = 0;
            if (c->lnas_ignored < c->cfg.ignore_lna) c->lnas_ignored++;
            else ln_done(c, was);
        }
    }
    if (c->linked && c->ln_pend < 0 && c->nforce < 4) {
        int at, kind, rc;
        if (c->nforce == 0) { at = c->cfg.force_rate_at_ms; kind = c->cfg.force_kind; rc = c->cfg.force_rate_code; }
        else {
            at = c->cfg.force_more[c->nforce - 1].at_ms; kind = c->cfg.force_more[c->nforce - 1].kind;
            rc = c->cfg.force_more[c->nforce - 1].code;
        }
        if (at <= 0 || kind == 0) c->nforce = 4;          /* no more */
        else if (c->ms >= at) {
            c->nforce++;
            c->ln_pend = (kind & 0xF0) | (rc & 0x0F);
            mnppeer_send_ln(m, c->ln_pend);
        }
    }
    if (c->linked && c->cfg.rev_backlog > 0 && c->phase == HC_BACK && c->ln_pend < 0
        && hstcall_backlog(c) > c->cfg.rev_backlog && c->ms - c->t_switch >= 2000) {
        c->ln_pend = 0x10 | c->fwd_code;
        mnppeer_send_ln(m, c->ln_pend);
    }
    if (c->phase == HC_GAP && c->ms >= c->t_wait) {
        hc_ftx_train(&c->ftx, c->wait_code, !c->trained, c->cfg.bm_training);
        c->trained = 1;
        c->fwd_code = c->wait_code;
        hc_brx_init(&c->brx, c->back_bps);
        c->role = HC_ROLE_FWD;
        c->phase = HC_FWD;
        c->reversals++;
        c->t_switch = c->ms;
    }
    if (c->phase == HC_TURN && c->lna_done >= 0 && c->ms >= c->lna_done + 20) {
        int r = c->ln_rx & 0x0F;
        hc_ftx_stop(&c->ftx);
        c->ln_rx = -1;
        c->fwd_code = r;
        c->reversals++;
        start_back(c, r);
    }
}

int16_t hstcall_tick(hstcall_t *c, int16_t rx)
{
    double out = 0, x = rx;
    int k, nb;
    if (c->phase == HC_TONE) {
        if (tone_block(c, x)) {
            c->phase = HC_BACK;
            c->back_bps = c->cfg.start_bps;
            start_back(c, open_code(c));
        }
    } else if (c->phase != HC_DROPPED) {
        if (c->role == HC_ROLE_BACK && c->phase == HC_BACK) {
            nb = hc_frx_tick(&c->frx, x);
            for (k = 0; k < nb; k++) { mnppeer_rx_bit(&c->mnp, c->frx.bits[k]); c->rx_bits++; }
        } else if (c->role == HC_ROLE_FWD) {
            nb = hc_brx_tick(&c->brx, x);
            for (k = 0; k < nb; k++) { mnppeer_rx_bit(&c->mnp, c->brx.bits[k]); c->rx_bits++; }
        }
    }
    out = hc_btx_tick(&c->btx) + hc_ftx_tick(&c->ftx);
    c->n++;
    if (c->n % 8 == 0) every_ms(c);
    out = floor(out + 0.5);
    return (int16_t)(out > 32767 ? 32767 : out < -32768 ? -32768 : out);
}
