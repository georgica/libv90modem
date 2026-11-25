/* HST's back channel: the 8-DPSK transmitter and receiver (hst.h;
 * hst-facts.md, "Back channel").
 *
 * Each speed has a pulse train (hst_blane_t): unit points at 22.5 + 45 k
 * degrees, times the gain for the line level, shaped by the polyphase
 * root-raised-cosine pulse of hst_tabs (HST_BL symbols, HST_Bxxx_P phases a
 * symbol, HST_Bxxx_STEP phases a sample), then moved to its carrier. The
 * speed being sent clocks its train; the other one, after a change of
 * speed, takes zeros until its last symbols have left the pulse. Both
 * carriers run from the first tick, so each is a fixed function of time. */
#include <string.h>
#include "hst.h"
#include "sintab.h"
#include "fixmath.h"
#include "v34train.h"     /* v34_angle */

/* per lane: carrier, baud, pulse phases a symbol, phases a sample, the pulse */
static const struct {
    int fc, baud, P, step;
    const int16_t *pulse;
} lanes[2] = {
    { 375, 150, HST_B450_P, HST_B450_STEP, &hst_bpulse450[0][0] },
    { 350, 100, HST_B300_P, HST_B300_STEP, &hst_bpulse300[0][0] },
};
static const int rms_q12[2] = { HST_B450_RMS_Q12, HST_B300_RMS_Q12 };

static int lane_of(int bps) { return bps == 300 ? 1 : 0; }

/* Q15 sine of a 32-bit phase, interpolated between sintab's entries */
static int32_t sin_q15(uint32_t ph)
{
    int32_t s0 = sintab[(ph >> 22) & SIN_MASK], s1 = sintab[((ph >> 22) + 1) & SIN_MASK];
    return s0 + (((s1 - s0) * (int32_t)((ph >> 7) & 0x7FFF)) >> 15);
}

void hst_btx_init(hst_btx_t *t, int bps, int level, v32_getbit_fn getbit, void *user)
{
    int l, j;
    for (l = 0; l < 2; l++) {
        hst_blane_t *L = &t->lane[l];
        for (j = 0; j < HST_BL; j++) L->si[j] = L->sq[j] = 0;
        L->acc = lanes[l].P;
        L->left = 0;
        L->cph = 0;
        L->dph = (uint32_t)(((uint64_t)(uint32_t)lanes[l].fc << 32) / 8000u);
        L->gain = (int32_t)((int64_t)(level > 0 ? level : 0) * 4096 / rms_q12[l]);
    }
    t->bps = t->want = bps == 300 ? 300 : 450;
    t->mode = HST_BTX_SILENT;
    t->phase = 0;
    t->npre = 0;
    t->newsym = 0;
    scr_init(&t->scr);
    t->getbit = getbit; t->user = user;
}

/* from the next symbol of the active lane, the speed asked for */
static void take_speed(hst_btx_t *t)
{
    hst_blane_t *N;
    if (t->want == t->bps) return;
    t->bps = t->want;
    N = &t->lane[lane_of(t->bps)];
    if (N->left == 0) N->acc = lanes[lane_of(t->bps)].P;     /* idle: a symbol at once */
}

void hst_btx_preamble(hst_btx_t *t)
{
    hst_blane_t *L;
    take_speed(t);
    L = &t->lane[lane_of(t->bps)];
    if (L->left == 0) L->acc = lanes[lane_of(t->bps)].P;
    t->mode = HST_BTX_PREAMBLE;
    t->npre = HST_BTX_PRE_MS * lanes[lane_of(t->bps)].baud / 1000;
    t->phase = 0;
    scr_init(&t->scr);
}

void hst_btx_set_speed(hst_btx_t *t, int bps) { t->want = bps == 300 ? 300 : 450; }

void hst_btx_stop(hst_btx_t *t) { t->mode = HST_BTX_SILENT; }

/* the next symbol of the speed being sent: returns 0 (and a zero symbol)
 * when silent, 1 when a symbol is sent */
static int next_symbol(hst_btx_t *t, int32_t *si, int32_t *sq)
{
    int b, tri = 0, idx;
    int32_t g = t->lane[lane_of(t->bps)].gain;
    if (t->mode == HST_BTX_SILENT) { *si = *sq = 0; return 0; }
    for (b = 0; b < 3; b++) {                       /* the tribit's LSB first */
        int di = t->mode == HST_BTX_PREAMBLE ? 1 : t->getbit(t->user) & 1;
        tri |= scr17_scramble(&t->scr, di) << b;
    }
    if (t->mode == HST_BTX_PREAMBLE && --t->npre <= 0) t->mode = HST_BTX_DATA;
    t->phase = (t->phase + hst_bstep[tri]) & 7;
    idx = SIN_N / 16 + t->phase * (SIN_N / 8);      /* 22.5 + 45 k degrees */
    *si = (int32_t)(((int64_t)g * sintab[(idx + SIN_N / 4) & SIN_MASK]) >> 15);
    *sq = (int32_t)(((int64_t)g * sintab[idx & SIN_MASK]) >> 15);
    return 1;
}

int16_t hst_btx_tick(hst_btx_t *t)
{
    int l, j, act;
    int32_t v = 0;
    t->newsym = 0;
    /* a change of speed at the old speed's symbol boundary; the old lane drains below */
    if (t->lane[lane_of(t->bps)].acc >= lanes[lane_of(t->bps)].P) take_speed(t);
    act = lane_of(t->bps);
    for (l = 0; l < 2; l++) {
        hst_blane_t *L = &t->lane[l];
        if (l == act || L->left > 0) {
            if (L->acc >= lanes[l].P) {
                int32_t si = 0, sq = 0;
                L->acc -= lanes[l].P;
                for (j = HST_BL - 1; j > 0; j--) { L->si[j] = L->si[j - 1]; L->sq[j] = L->sq[j - 1]; }
                if (l == act) t->newsym = next_symbol(t, &si, &sq);
                L->si[0] = si; L->sq[0] = sq;
                if (si || sq) L->left = HST_BL; else if (L->left > 0) L->left--;
            }
            if (L->left > 0) {
                const int16_t *h = lanes[l].pulse + L->acc * HST_BL;
                int64_t bi = 0, bq = 0;
                int32_t c, s;
                for (j = 0; j < HST_BL; j++) { bi += (int64_t)L->si[j] * h[j]; bq += (int64_t)L->sq[j] * h[j]; }
                bi >>= 14; bq >>= 14;                    /* Q14 pulse */
                c = sin_q15(L->cph + (1u << 30));
                s = sin_q15(L->cph);
                v += (int32_t)((bi * c - bq * s) >> 15);
            }
            L->acc += lanes[l].step;
        }
        L->cph += L->dph;
    }
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    return (int16_t)v;
}

/* ======================== the receiver ======================== */

#define DEC       5                   /* 8000 Hz to 1600 */
#define BLK       16                  /* carrier detect and AGC: 10 ms blocks at 1600 Hz */
#define CD_ON_PW  9900                /* -44 dBm0 (0 dBm0 = 15770 RMS), a block's mean square */
#define CD_OFF_PW 4970                /* -47 dBm0 */
#define CD_BLKS   3                   /* blocks over the threshold for on */
#define CD_OFF_BLKS 6                 /* and under it for off: a lost 50 ms packet keeps the carrier */
#define AGC_RMS   4096                /* the band's RMS after the AGC */
#define GAIN0     ((int32_t)((AGC_RMS * 65536LL) / 1577))   /* until a carrier: -20 dBm0 */
#define DEG12     143165577u          /* 12 degrees, 2^32 a turn */
#define LOCK_WIN  0xFFFFu             /* lock: 15 of the last 16 decisions on the grid */
#define LOCK_MIN  15
#define KT_ACQ    16                  /* Gardner's gain, until locked (x P / 64 of the Q14 error) */
#define KT_TRK    4                   /* and once locked */
#define KDA       8                   /* the frequency loop: 1/8 of the decision error a symbol, until locked */
#define KDT       32                  /* and 1/32 once locked */
#define WTURN_MAX (1 << 30)           /* a quarter turn a symbol */
#define EST_AGAIN 24                  /* symbols after the carrier's estimate before acquiring again */
#define CHK_EVERY 32                  /* symbols between alias checks while locked */
#define RMS_SQ(d) (((d) * 65536 / 360) * ((d) * 65536 / 360) / 16 * HST_BRX_WIN)
/* faded symbols in a row that lose the lock, about 75 ms: 11 at 450, 8 at
 * 300 */
static const int32_t fade_lose[2] = { 11, 8 };
/* symbols the bits are held back while locked: past CONFIRM (no bit leaves
 * before the second alias check), and past a fade that loses the lock and
 * the 8 symbols before it, over which the matched filter ramps down */
static const int32_t hold[2] = { 19, 18 };
#define CONFIRM   10                  /* symbols after the lock: the second alias check, on a fresh window */
enum { ACQ_TIMING, ACQ_CARRIER, ACQ_LOCK };

/* pulse phases a 1600 Hz sample: 15 at 450 (160 a symbol), 5 at 300 (80) */
static const int32_t rx_k[2] = { HST_B450_P * 150 / 1600, HST_B300_P * 100 / 1600 };

/* a - b as a signed angle (2^32 a turn) */
static int32_t sangle(uint32_t a)
{
    return a <= (uint32_t)INT32_MAX ? (int32_t)a : -(int32_t)(UINT32_MAX - a) - 1;
}

static int ones(uint32_t v)
{
    int n = 0;
    for (; v; v >>= 1) n += (int)(v & 1);
    return n;
}

static int16_t sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void hst_brx_init(hst_brx_t *r, int bps)
{
    memset(r, 0, sizeof *r);
    r->lane = lane_of(bps);
    r->xn = HST_BBPF_N - 1;
    r->zn = HST_BRX_ZN;
    r->gain = GAIN0;
    r->dph0 = r->dph = (uint32_t)(((uint64_t)(uint32_t)lanes[r->lane].fc << 32) / 1600u);
    {
        /* the received pulse (the pulse through itself) at T/2 and 3T/2 */
        const int32_t P = lanes[r->lane].P, n = HST_BL * P;
        const int16_t *pulse = lanes[r->lane].pulse;
        int64_t r0 = 0, r1 = 0, r3 = 0;
        int32_t m;
        for (m = 0; m < n; m++) {
            int32_t a = pulse[(m % P) * HST_BL + m / P];
            r0 += (int64_t)a * a;
            if (m + P / 2 < n) r1 += (int64_t)a * pulse[((m + P / 2) % P) * HST_BL + (m + P / 2) / P];
            if (m + 3 * P / 2 < n) r3 += (int64_t)a * pulse[((m + 3 * P / 2) % P) * HST_BL + (m + 3 * P / 2) / P];
        }
        r->rho1 = (int32_t)(r1 * 16384 / r0);
        r->rho3 = (int32_t)(r3 * 16384 / r0);
    }
    scr_init(&r->scr);
}

int hst_brx_cd(const hst_brx_t *r) { return r->cd; }
int hst_brx_locked(const hst_brx_t *r) { return r->locked; }
int hst_brx_grid_ok(const hst_brx_t *r) { return r->grid_ok; }

int hst_brx_snr(const hst_brx_t *r, int64_t *num, int64_t *den)
{
    /* Es/N0 = 1 / var(differential phase error, radians) = 2^32 / (e2 4 pi^2) */
    if (!r->locked || r->e2 <= 0) return 0;
    *num = (int64_t)100 << 32;
    *den = r->e2 * 3948;                                    /* 4 pi^2 x 100 */
    return 1;
}

/* a carrier: acquisition from the start */
static void rx_acquire(hst_brx_t *r)
{
    r->dph = r->dph0; r->wturn = 0;
    r->tacc = 0; r->half = 0;
    r->acq = ACQ_TIMING; r->sidx = 0; r->nom = -1; r->omr = r->omi = 0;
    r->pref = 0; r->nsym = 0;
    r->okhist = 0; r->locked = 0; r->grid_ok = 0;
    r->e2 = 0;
    r->nest = 0; r->wait = 0; r->nchk = 0; r->nalias = 0;
    r->wpos = r->nwin = r->qbad = r->nfade = 0;
    r->ndly = r->pdly = 0; r->conf = 0;
    scr_init(&r->scr);
}

/* the band's power, a block at a time: carrier detect and AGC */
static void rx_block(hst_brx_t *r)
{
    int64_t p = r->bpw / BLK;
    r->bpw = 0;
    if (!r->cd) {
        r->cnt = p >= CD_ON_PW ? r->cnt + 1 : 0;
        if (r->cnt >= CD_BLKS) { r->cd = 1; r->cnt = 0; r->ps = p; rx_acquire(r); }
    } else {
        r->cnt = p < CD_OFF_PW ? r->cnt + 1 : 0;
        if (r->cnt >= CD_OFF_BLKS) { r->cd = 0; r->cnt = 0; r->locked = 0; r->grid_ok = 0; }
        else r->ps += (p - r->ps) / (r->locked ? 8 : 2);
    }
    if (r->cd) {
        int32_t rms = isqrt_s64(r->ps);
        r->gain = (int32_t)(((int64_t)AGC_RMS << 16) / (rms > 16 ? rms : 16));
    }
}

/* the matched filter at the strobe `frac` pulse phases (0 .. k-1) before the
 * newest baseband sample, 4 symbols back: the pulse of hst_tabs over +-4
 * symbols, read at the strobe's own phase */
static void rx_mf(const hst_brx_t *r, int32_t frac, int32_t *yr, int32_t *yi)
{
    const int32_t P = lanes[r->lane].P, k = rx_k[r->lane];
    const int16_t *pulse = lanes[r->lane].pulse;
    int32_t i0 = (frac + k - 1) / k, m = P + i0 * k - frac, ph = m % P, j = m / P;
    const int16_t *zr = r->zr + r->zn - 1 - i0, *zi = r->zi + r->zn - 1 - i0;   /* i0 samples back */
    int64_t ar = 0, ai = 0;
    for (; m <= 9 * P; m += k) {
        int32_t h = pulse[ph * HST_BL + j];
        ar += (int32_t)*zr-- * h;
        ai += (int32_t)*zi-- * h;
        ph += k;
        if (ph >= P) { ph -= P; j++; }
    }
    *yr = (int32_t)(ar / 16384);
    *yi = (int32_t)(ai / 16384);
}

/* (x + jy)(c + js), c and s Q14 */
static void rot14(int32_t x, int32_t y, int32_t c, int32_t s, int64_t *re, int64_t *im)
{
    *re += ((int64_t)x * c - (int64_t)y * s) / 16384;
    *im += ((int64_t)x * s + (int64_t)y * c) / 16384;
}

/* a differential angle d (the carrier's turn taken off): the step, its
 * error (returned), grid_ok and its history, the bits through the
 * descrambler */
static int rx_quality(const hst_brx_t *r, int rms);

/* a differential angle d: `count` 0 for a faded symbol, which is decided
 * (the descrambler runs on) but left out of the grid history and the
 * window, the fade rule alone judging it */
static int32_t rx_decide(hst_brx_t *r, uint32_t d, int count)
{
    int32_t step = (int32_t)(((d + (1u << 28)) >> 29) & 7);    /* the nearest multiple of 45 degrees */
    int32_t de = sangle(d - ((uint32_t)step << 29)), dq = de / 65536, tri, b, ok;
    ok = de <= (int32_t)DEG12 && de >= -(int32_t)DEG12;
    if (count) {
        r->okhist = (r->okhist << 1) | (uint32_t)ok;
        r->wstep[r->wpos] = (uint8_t)step;
        r->wsq[r->wpos] = dq * dq / 16;
        if (++r->wpos == HST_BRX_WIN) r->wpos = 0;
        if (r->nwin < HST_BRX_WIN) r->nwin++;
    }
    r->grid_ok = count && r->acq == ACQ_LOCK && ok && rx_quality(r, HST_BRX_RMS);
    for (tri = 0; tri < 8 && hst_bstep[tri] != step; tri++) ;
    for (b = 0; b < 3; b++) r->bits[b] = (uint8_t)scr17_descramble(&r->scr, (tri >> b) & 1);
    return de;
}

/* the last HST_BRX_WIN decisions: steps of variety (4 different at least,
 * none in more than 8) and an RMS error under rms degrees */
static int rx_quality(const hst_brx_t *r, int rms)
{
    int n[8] = { 0 }, i, distinct = 0, most = 0;
    int64_t sq = 0;
    if (r->nwin < HST_BRX_WIN) return 0;
    for (i = 0; i < HST_BRX_WIN; i++) { n[r->wstep[i]]++; sq += r->wsq[i]; }
    for (i = 0; i < 8; i++) { distinct += n[i] > 0; if (n[i] > most) most = n[i]; }
    return distinct >= 4 && most <= 8 && sq <= RMS_SQ(rms);
}

/* the kept midpoints against those their symbols predict at a turn of h a
 * symbol: each symbol is turned against the midpoint by h/4 at T/2, 3h/4
 * at 3T/2 (the matched filter's output for a carrier off by w turns at
 * w/2 across a pulse) */
static int64_t est_mismatch(const hst_brx_t *r, int32_t h)
{
    uint32_t q1 = (uint32_t)(h / 4), q3 = (uint32_t)(h / 4 * 3);
    int32_t c1 = sin_q15(q1 + (1u << 30)) / 2, s1 = sin_q15(q1) / 2;      /* Q14 */
    int32_t c3 = sin_q15(q3 + (1u << 30)) / 2, s3 = sin_q15(q3) / 2;
    int64_t sum = 0;
    int i;
    for (i = 2; i + 1 < HST_BRX_EST; i++) {
        int64_t ar = 0, ai = 0, br = 0, bi = 0, er, ei;
        rot14(r->esr[i - 1], r->esi[i - 1], c1, s1, &ar, &ai);    /* T/2 before: e^{+jh/4} */
        rot14(r->esr[i], r->esi[i], c1, -s1, &ar, &ai);           /* T/2 after: e^{-jh/4} */
        rot14(r->esr[i - 2], r->esi[i - 2], c3, s3, &br, &bi);
        rot14(r->esr[i + 1], r->esi[i + 1], c3, -s3, &br, &bi);
        er = (ar * r->rho1 + br * r->rho3) / 16384 - r->emr[i];
        ei = (ai * r->rho1 + bi * r->rho3) / 16384 - r->emi[i];
        sum += (er * er + ei * ei) / 256;
    }
    return sum;
}

/* the alias check on the kept strobes around the turn h in use: -1 or +1
 * when h - 45 or h + 45 predicts the midpoints better by 2:1, 0 when h
 * does better than both by num:den, 2 when neither is clear */
static int rx_alias(const hst_brx_t *r, int32_t h, int num, int den)
{
    int64_t e[3];
    int j;
    for (j = 0; j < 3; j++) e[j] = est_mismatch(r, h + (j - 1) * (int32_t)(1u << 29));
    if (e[0] * 2 < e[1] && e[0] <= e[2]) return -1;
    if (e[2] * 2 < e[1] && e[2] < e[0]) return 1;
    if (e[1] * num < e[0] * den && e[1] * num < e[2] * den) return 0;
    return 2;
}

/* the carrier's turn a symbol, from the kept strobes: modulo 45 degrees
 * from the eighth power of the differential strobes, then which of r,
 * r + 45 and r - 45 from the midpoints */
static void rx_estimate(hst_brx_t *r, int redecide)
{
    int32_t c = 0, s = 0, h, best = 0, j;
    int64_t e, emin = 0;
    uint32_t prev = v34_angle(r->esr[0], r->esi[0]);
    int i;
    for (i = 1; i < HST_BRX_EST; i++) {
        uint32_t a = v34_angle(r->esr[i], r->esi[i]), d8 = (a - prev) * 8u;
        c += sin_q15(d8 + (1u << 30));
        s += sin_q15(d8);
        prev = a;
    }
    h = sangle(v34_angle(c, s)) / 8;                        /* modulo 45 degrees */
    for (j = -1; j <= 1; j++) {
        e = est_mismatch(r, h + j * (int32_t)(1u << 29));
        if (j == -1 || e < emin) { emin = e; best = j; }
    }
    h += best * (int32_t)(1u << 29);
    /* taken off the differential angles from now on (the NCO gets it only
     * slowly, once locked: a step there would reach the strobes over the
     * matched filter's 8 symbols); and the kept symbols decided again with
     * it, for the lock */
    r->wturn = h;
    r->acq = ACQ_LOCK; r->wait = EST_AGAIN;
    r->okhist = 0; r->wpos = r->nwin = r->qbad = 0;
    scr_init(&r->scr);
    prev = v34_angle(r->esr[0], r->esi[0]);
    for (i = 1; i <= redecide; i++) {
        uint32_t a = v34_angle(r->esr[i], r->esi[i]);
        rx_decide(r, a - prev - (uint32_t)h, 1);
        prev = a;
    }
}

/* a strobe while acquiring the timing (four a symbol, sidx 0 .. 3): the
 * symbol-rate line of the strobes' power; after HST_BRX_OM symbols, the
 * next symbol strobe put at its peak */
static void rx_timing(hst_brx_t *r, int32_t frac, int32_t yr, int32_t yi)
{
    const int32_t P = lanes[r->lane].P, k = rx_k[r->lane];
    int64_t p = ((int64_t)yr * yr + (int64_t)yi * yi) / 256, mr, mi;
    int32_t tau, dist;
    if (r->nom >= 0)
        switch (r->sidx) {                                  /* x e^{-j 2 pi sidx / 4} */
        case 0: r->omr += p; break;
        case 1: r->omi -= p; break;
        case 2: r->omr -= p; break;
        default: r->omi += p; break;
        }
    if (++r->sidx < 4) return;
    r->sidx = 0;
    if (++r->nom < HST_BRX_OM) return;
    mr = r->omr; mi = r->omi;
    while (mr > INT32_MAX / 2 || mr < -(INT32_MAX / 2) || mi > INT32_MAX / 2 || mi < -(INT32_MAX / 2)) { mr /= 2; mi /= 2; }
    /* the power peaks tau pulse phases after the sidx 0 strobes */
    tau = (int32_t)(-((int64_t)sangle(v34_angle((int32_t)mr, (int32_t)mi)) * P / 65536 / 65536));
    /* this strobe (sidx 3) is 3P/4 after a sidx 0 one: the next peak is P/4 + tau away */
    dist = P / 4 + tau;
    r->half = 1;                                            /* the next strobe a symbol's */
    if (dist < k) { dist += P / 2; r->half = 0; }           /* too near: a midpoint first */
    r->tacc = (frac + P / 2 - dist) * 65536;
    r->acq = ACQ_CARRIER;
    r->nest = 0; r->nsym = 0;
}

/* a symbol strobe: timing, carrier, the decision */
static int rx_symbol(hst_brx_t *r, int32_t yr, int32_t yi)
{
    const int32_t P = lanes[r->lane].P, k = rx_k[r->lane], baud = lanes[r->lane].baud;
    int64_t pw = (int64_t)yr * yr + (int64_t)yi * yi, e, adj, lim;
    uint32_t a = v34_angle(yr, yi), d;
    int32_t de, dq;
    int fade;

    /* a fade: the symbol 9 dB under the mean. fade_lose in a row lose the
     * lock (a dropout under the carrier detect), and the bits held back
     * with them; fewer (a lost 20 ms packet) keep it, and their bits go
     * out with the rest */
    fade = r->locked && pw * 8 < r->pref;
    r->nfade = fade ? r->nfade + 1 : 0;
    r->pref = r->pref ? r->pref + (pw - r->pref) / 16 : pw;
    if (r->pref < 1) r->pref = 1;
    if (r->nsym > 0 && !fade) {
        /* Gardner: the midpoint against the change across it */
        e = ((int64_t)r->ypr - yr) * r->ymr + ((int64_t)r->ypi - yi) * r->ymi;
        e = e * 16384 / r->pref;                            /* Q14, about the timing error in symbols */
        adj = -e * P * (r->locked ? KT_TRK : KT_ACQ) / 64;  /* pulse phases Q16 */
        lim = (int64_t)k << 15;                             /* at most half a sample a symbol */
        r->tacc += (int32_t)(adj > lim ? lim : adj < -lim ? -lim : adj);
    }
    r->ypr = yr; r->ypi = yi;
    if (r->nsym > 0) {
        /* the last HST_BRX_EST symbol and midpoint strobes, the newest last */
        if (r->nest == HST_BRX_EST) {
            int j;
            for (j = 1; j < HST_BRX_EST; j++) {
                r->esr[j - 1] = r->esr[j]; r->esi[j - 1] = r->esi[j];
                r->emr[j - 1] = r->emr[j]; r->emi[j - 1] = r->emi[j];
            }
            r->nest--;
        }
        r->esr[r->nest] = yr; r->esi[r->nest] = yi;
        r->emr[r->nest] = r->ymr; r->emi[r->nest] = r->ymi;
        r->nest++;
    }
    if (r->acq == ACQ_CARRIER && r->nest == HST_BRX_EST) rx_estimate(r, HST_BRX_EST - 2);
    else if (r->acq == ACQ_LOCK && !r->locked && --r->wait <= 0) {
        rx_acquire(r);                                      /* no lock: from the start */
        r->sidx = 1;                                        /* this was a symbol's strobe */
        return 0;
    }

    d = a - r->aprev - (uint32_t)r->wturn;
    r->aprev = a;
    if (r->nsym++ == 0) return 0;

    de = rx_decide(r, d, !fade);
    if (r->acq == ACQ_LOCK) {
        /* frequency: the decisions' error is the turn a symbol left over */
        if (!fade) r->wturn += de / (r->locked ? KDT : KDA);
        if (r->wturn > WTURN_MAX) r->wturn = WTURN_MAX; else if (r->wturn < -WTURN_MAX) r->wturn = -WTURN_MAX;
        if (r->locked) {
            /* the turn onto the NCO, slowly */
            int32_t t = r->wturn / 64;
            r->wturn -= t;
            r->dph = (uint32_t)((int64_t)r->dph + (int64_t)t * baud / 1600);
        }
        /* quality: the strict ceiling to lock, a looser one to hold */
        r->qbad = rx_quality(r, r->locked ? HST_BRX_RMS_HOLD : HST_BRX_RMS) ? 0 : r->qbad + 1;
        if (!r->locked) {
            if (ones(r->okhist & LOCK_WIN) >= LOCK_MIN && r->qbad == 0) {
                int al = rx_alias(r, r->wturn, 2, 1);
                if (al == 0) { r->locked = 1; r->nchk = CHK_EVERY; r->nalias = 0; r->conf = CONFIRM; }
                else if (al != 2) rx_estimate(r, HST_BRX_EST - 1);     /* 45 degrees off: again */
            }
        } else if (ones(~r->okhist & 0xFFFFu) >= 8 || r->qbad >= HST_BRX_WIN || r->nfade >= fade_lose[r->lane]) {
            r->locked = 0; r->wait = 0;                     /* lost: acquire again */
        } else if (r->conf > 0) {
            /* the second check, before any bit is out, on a fresh window:
             * the turn in use better than both others by 3:2 again, or no
             * lock (again from the strobes, when they prefer r +- 45; an
             * unclear window costs a lock won again, never a bit) */
            if (--r->conf == 0) {
                int al = rx_alias(r, r->wturn, 3, 2);
                if (al != 0) {
                    r->locked = 0; r->okhist = 0;
                    if (al != 2) rx_estimate(r, HST_BRX_EST - 1);
                }
            }
        } else if (--r->nchk <= 0) {
            /* three checks in a row (on windows apart) for the same turn 45
             * degrees off: one alone can be the data's chance */
            int al = rx_alias(r, r->wturn, 3, 2);
            r->nchk = CHK_EVERY;
            r->nalias = (al == -1 || al == 1) && (r->nalias == 0 || (r->nalias > 0) == (al > 0))
                      ? r->nalias + al : 0;
            if (r->nalias >= 3 || r->nalias <= -3) { r->locked = 0; r->nalias = 0; rx_estimate(r, HST_BRX_EST - 1); }
        }
    }
    dq = de / 65536;                                        /* turns Q16 */
    r->e2 += ((int64_t)dq * dq - r->e2) / 32;
    if (!r->locked) { r->ndly = 0; return 0; }
    /* held back: a lock lost to a fade, or to the second alias check, takes
     * the bits not yet out with it */
    {
        const int32_t n = hold[r->lane];
        uint8_t in = (uint8_t)(r->bits[0] | r->bits[1] << 1 | r->bits[2] << 2), out = r->dly[r->pdly];
        r->dly[r->pdly] = in;
        if (++r->pdly == n) r->pdly = 0;
        if (r->ndly < n) { r->ndly++; return 0; }
        r->bits[0] = out & 1; r->bits[1] = (out >> 1) & 1; r->bits[2] = (out >> 2) & 1;
        return 3;
    }
}

int hst_brx_band(hst_brx_t *r, int32_t y)
{
    const int32_t P = lanes[r->lane].P, k = rx_k[r->lane];
    int64_t ya;
    int32_t c, s, frac, yr, yi, step;

    r->bpw += (int64_t)y * y;
    if (++r->nblk == BLK) { r->nblk = 0; rx_block(r); }

    /* AGC, then to baseband */
    ya = (int64_t)y * r->gain / 65536;
    c = sin_q15(r->ph + (1u << 30));
    s = sin_q15(r->ph);
    r->ph += r->dph;
    if (r->zn == HST_BRX_ZL) {
        memmove(r->zr, r->zr + HST_BRX_ZL - HST_BRX_ZN, HST_BRX_ZN * sizeof r->zr[0]);
        memmove(r->zi, r->zi + HST_BRX_ZL - HST_BRX_ZN, HST_BRX_ZN * sizeof r->zi[0]);
        r->zn = HST_BRX_ZN;
    }
    r->zr[r->zn] = sat16(ya * c / 32768);
    r->zi[r->zn++] = sat16(-ya * s / 32768);
    if (!r->cd) return 0;

    /* the strobes: two a symbol, four while acquiring the timing */
    step = r->acq == ACQ_TIMING ? P / 4 : P / 2;
    r->tacc += k * 65536;
    if (r->tacc < step * 65536) return 0;
    r->tacc -= step * 65536;
    frac = r->tacc < 0 ? 0 : r->tacc / 65536;
    if (frac >= k) frac = k - 1;
    rx_mf(r, frac, &yr, &yi);
    if (r->acq == ACQ_TIMING) { rx_timing(r, frac, yr, yi); return 0; }
    r->half ^= 1;
    if (r->half) { r->ymr = yr; r->ymi = yi; return 0; }
    return rx_symbol(r, yr, yi);
}

int hst_brx_tick(hst_brx_t *r, int16_t x)
{
    const int16_t *w;
    int64_t acc;
    int32_t i;

    if (r->xn == HST_BRX_XL) {
        memmove(r->xh, r->xh + HST_BRX_XL - (HST_BBPF_N - 1), (HST_BBPF_N - 1) * sizeof r->xh[0]);
        r->xn = HST_BBPF_N - 1;
    }
    r->xh[r->xn++] = x;
    if (++r->dec < DEC) return 0;
    r->dec = 0;

    /* the band-pass, folded (linear phase), at 1600 Hz */
    w = r->xh + r->xn - HST_BBPF_N;
    acc = (int64_t)hst_bbpf[HST_BBPF_N / 2] * w[HST_BBPF_N / 2];
    for (i = 0; i < HST_BBPF_N / 2; i++)
        acc += (int64_t)hst_bbpf[i] * ((int32_t)w[i] + w[HST_BBPF_N - 1 - i]);
    r->y = (int32_t)(acc / 32768);
    return hst_brx_band(r, r->y);
}
