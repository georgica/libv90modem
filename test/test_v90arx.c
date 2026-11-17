/* The V.90 analogue modem's PCM downstream receiver (modem/v90arx) against
 * the library's own digital transmitter (v90tx: Sd, Sd-bar, TRN1d, then B1d
 * and data through a map of the test's choosing), through the downstream
 * channel model (sim/dschan: pad, robbed bits, codec and loop filters,
 * noise, clock offset, our own echo). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "v90arx.h"
#include "v90_tx.h"
#include "v90_pcm.h"
#include "v34mod.h"
#include "dschan.h"
#include "noise.h"
#include "pcmenc.h"

/* ---- constellations ------------------------------------------------------ */

/* 0 dBm0 sine power in Table 1 units */
static double p0dbm0(int alaw)
{
    double pk = alaw ? 4096.0 * 8 / pow(10, 3.14 / 20) : 8159.0 * 4 / pow(10, 3.17 / 20);
    return pk * pk / 2;
}

/* The points of a data rate (Sr = 0, so D = rate * 6 / 8000 and K = D - 6):
 * the smallest M with M^6 >= 2^K, spaced as widely as a mean power of
 * -12 dBm0 allows, built up from Ucode 0. Returns dmin (Table 1 units). */
static int design(int alaw, int rate, uint8_t mask[16], int *Mout, int *Kout)
{
    int D = rate * 6 / 8000, K = D - 6, M = 1, d, lo = 1, hi = 32768, best = 0;
    double pmax = p0dbm0(alaw) * pow(10, -1.2);
    while (pow(M, 6) < pow(2, K)) M++;
    while (lo <= hi) {
        int n = 0, u, last = -1;
        double ps = 0;
        d = (lo + hi) / 2;
        for (u = 0; u < 128 && n < M; u++) {
            int32_t l = v90_ucode_linear(alaw, (unsigned)u);
            /* the first point's two signs must be dmin apart too (A-law's
             * +-Ucode 0 are 16 apart) */
            if (last < 0 ? 2 * l >= d || l == 0 : l - last >= d) { last = l; n++; ps += (double)l * l; }
        }
        if (n == M && ps / M <= pmax) { best = d; lo = d + 1; } else hi = d - 1;
    }
    memset(mask, 0, 16);
    {
        int n = 0, u, last = -1;
        for (u = 0; u < 128 && n < M; u++) {
            int32_t l = v90_ucode_linear(alaw, (unsigned)u);
            if (last < 0 ? 2 * l >= best || l == 0 : l - last >= best) { last = l; n++; mask[u >> 3] |= (uint8_t)(1 << (u & 7)); }
        }
    }
    *Mout = M; *Kout = K;
    return best;
}

/* ---- the link -------------------------------------------------------------- */

typedef struct {
    /* channel */
    double loop_km, noise, ppm, phase, pad, echo_db;
    int rbs, rbs_phase, alaw;
    uint32_t seed;
    /* what is sent */
    int rate;                   /* data rate, 0: no data (TRN1d to the end) */
    long ec_train;              /* A/D samples of upstream alone before Sd, EC adapting */
    int up_level;               /* RMS of our upstream, 0 = none */
    long trn_len;               /* TRN1d symbols */
    long data_len;              /* B1d + data symbols counted */
    int ntrain;
    int uinfo;
    int dil;                    /* give the receiver the levels the pad and robbed bit make
                                   (as a DIL analysis would find them; here computed) */
} lcfg_t;

typedef struct {
    int got_dd;                 /* the receiver reached decision directed mode */
    double mse_trn_db;          /* mse at the end of TRN1d, dB re UINFO's level squared */
    double mse_trn_rms;         /* the same as an RMS error, Table 1 units */
    double mse_db, mse_rms;     /* at the end of the run */
    double ppm_est;
    long ndec, nerr, nerr_got;  /* data decisions, errors against the octets as sent and as received */
    long trn_err;               /* decision errors on TRN1d after the known part */
    long slot_n[6], slot_err[6];
    double slot_off[6], slot_off2[6];   /* signed offset |y| - |level sent| (units), and squared */
    int dmin, M;
    double erle_db, echo_rel_db;
} lres_t;

typedef struct {
    v90tx_t tx;
    dschan_t ch;
    v90arx_t rx;
    v34mod_t up;
    rng_t rng;
    long n;                     /* network samples taken */
    long n_sd, n_trn, n_data;   /* first symbol of Sd, TRN1d, B1d */
    long trn_len;
    int sel_data;
    int rate;
    double up_rms;
} link_t;

static int getbit(void *ctx) { link_t *l = (link_t *)ctx; return rng_uniform(&l->rng) < 0.5; }

static uint8_t src(void *ctx)
{
    link_t *l = (link_t *)ctx;
    uint8_t o;
    long n = l->n++;
    if (n == l->n_sd) v90tx_select(&l->tx, V90TX_SD);
    o = v90tx_tick(&l->tx);
    if (v90tx_signal(&l->tx) == V90TX_TRN1D) {
        if (l->n_trn < 0) l->n_trn = n;
        if (l->rate && !l->sel_data && (long)v90tx_symbols(&l->tx) >= l->trn_len - 3) {
            v90tx_select(&l->tx, V90TX_B1D);
            l->sel_data = 1;
        }
    }
    if (l->n_data < 0 && v90tx_signal(&l->tx) == V90TX_B1D) l->n_data = n;
    return o;
}

static link_t L;                /* large: kept off the stack */

/* the level Ucode u arrives at through the pad and, in the robbed slot, the
 * robbed bit (Table 1 units) */
static int32_t arrives(const lcfg_t *c, unsigned u, int robbed)
{
    uint8_t o = v90_pcm_octet(c->alaw, u, 1);
    if (c->pad != 0.0)
        o = pcm_encode(c->alaw, (int16_t)floor(v90_octet_linear(c->alaw, o) * pow(10, -c->pad / 20) + 0.5));
    if (robbed) o |= 1;
    return v90_octet_linear(c->alaw, o);
}

/* What a DIL analysis would tell the receiver: each point's level as it
 * arrives, on the scale the equaliser was trained to (UINFO's level). */
static void given_levels(const lcfg_t *c, v90arx_cons_t *cons, int rslot)
{
    double k = (double)v90_ucode_linear(c->alaw, (unsigned)c->uinfo) / arrives(c, (unsigned)c->uinfo, 0);
    int s, i;
    for (s = 0; s < 6; s++)
        for (i = 0; i < cons->n[s]; i++)
            cons->lv[s][i] = (int32_t)floor(arrives(c, cons->u[s][i], c->rbs && s == rslot) * k * (1 << V90ARX_YF) + 0.5);
}

/* our upstream: white noise while the canceller learns (standing for a
 * wideband training signal: NLMS on V.34's band-limited spectrum converges
 * slowly at its band edges, 34 dB after 0.75 s, 48 dB after 30 s, measured),
 * then V.34, 16 random points at 3000 baud */
static int16_t up_tick(link_t *l, int on, int white)
{
    if (!on) return 0;
    if (white) {
        double v = l->up_rms * rng_gauss(&l->rng);
        return (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : floor(v + 0.5));
    }
    if (v34mod_due(&l->up)) {
        /* 16 points, random */
        int a = (int)(rng_uniform(&l->rng) * 4), b = (int)(rng_uniform(&l->rng) * 4);
        v34mod_put(&l->up, (2 * a - 3) * V34MOD_REF / 2, (2 * b - 3) * V34MOD_REF / 2);
    }
    return v34mod_tick(&l->up);
}

static void run(const lcfg_t *c, lres_t *res)
{
    link_t *l = &L;
    dschan_cfg cc;
    v90arx_cfg_t rc;
    v90arx_cons_t cons;
    v90_map_t map;
    uint8_t mask[16];
    long k, kend, m_end_trn;
    int i, K = 0, M = 0, have_cons = 0, ec_mu = -1;
    double pe = 0, pr = 0, pd = 0, pec = 0; long npe = 0, npd = 0;

    memset(res, 0, sizeof *res);
    memset(l, 0, sizeof *l);
    rng_seed(&l->rng, c->seed + 7);
    l->n_sd = c->ec_train + 400; l->n_trn = l->n_data = -1;
    l->trn_len = c->trn_len; l->rate = c->rate;
    v90tx_init(&l->tx, c->alaw, c->uinfo);
    v90tx_set_getbit(&l->tx, getbit, l);
    if (c->rate) {
        res->dmin = design(c->alaw, c->rate, mask, &M, &K);
        res->M = M;
        memset(&map, 0, sizeof map);
        map.K = (uint8_t)K;
        for (i = 0; i < 6; i++) v90_map_set_points(&map, i, mask);
        CHECK_EQ_I(v90tx_set_map(&l->tx, V90TX_MAP_DATA, &map), 0);
        for (i = 0; i < 6; i++) v90arx_cons_mask(&cons, c->alaw, i, mask);
    }
    if (c->up_level) v34mod_init(&l->up, 3000, 3, 5, 0, c->up_level, 0);
    l->up_rms = c->up_level;

    memset(&cc, 0, sizeof cc);
    cc.alaw = c->alaw; cc.pad_db = c->pad; cc.rbs = c->rbs; cc.rbs_phase = c->rbs_phase;
    cc.loop_km = c->loop_km; cc.level_db = -12; cc.noise_dbm0 = c->noise; cc.ppm = c->ppm;
    cc.phase = c->phase; cc.echo_db = c->up_level ? c->echo_db : -300; cc.seed = c->seed;
    dschan_init(&l->ch, &cc, src, l);

    memset(&rc, 0, sizeof rc);
    rc.alaw = c->alaw; rc.uinfo = c->uinfo; rc.ntrain = c->ntrain;
    v90arx_init(&l->rx, &rc);

    m_end_trn = c->trn_len;
    kend = l->n_sd + 432 + c->trn_len + (c->rate ? c->data_len : 0) + 400;
    for (k = 0; k < kend; k++) {
        int16_t up, rx;
        int nd;
        /* the echo canceller learns while the downstream is silent */
        if (c->up_level && k < c->ec_train) {
            int mu = k < c->ec_train / 4 ? 0 : k < c->ec_train / 2 ? 2 : 4;
            if (mu != ec_mu) { ec_mu = mu; v90arx_ec(&l->rx, mu); }
        } else if (ec_mu != -1) { ec_mu = -1; v90arx_ec(&l->rx, -1); }
        up = up_tick(l, c->up_level != 0, k < c->ec_train);
        rx = dschan_tick(&l->ch, up);
        nd = v90arx_tick(&l->rx, rx, up);
        if (c->up_level && k >= c->ec_train * 3 / 4 && k < c->ec_train) {
            pr += (double)rx * rx; pe += (double)l->rx.eout * l->rx.eout; npe++;
        }
        if (l->n_data >= 0 && l->n > l->n_data + 2000) {
            pd += (rx - l->ch.echo_last) * (rx - l->ch.echo_last); pec += l->ch.echo_last * l->ch.echo_last; npd++;
        }
        if (!have_cons && c->rate && l->n_data >= 0) {
            if (c->dil) given_levels(c, &cons, (int)(((c->rbs_phase - l->n_trn) % 6 + 6) % 6));
            v90arx_set_cons(&l->rx, &cons, (int32_t)(l->n_data - l->n_trn));
            have_cons = 1;
        }
        for (i = 0; i < nd; i++) {
            const v90arx_dec_t *d = &l->rx.out[i];
            long n = l->n_trn + d->m;
            if (d->m == m_end_trn - 1) {
                res->mse_trn_db = 10 * log10(v90arx_mse(&l->rx) / 256.0 / pow(v90_ucode_linear(c->alaw, (unsigned)c->uinfo), 2) + 1e-30);
                res->mse_trn_rms = sqrt(v90arx_mse(&l->rx) / 256.0);
            }
            if (l->n_trn < 0) continue;
            if (!d->known && d->m < m_end_trn && d->m >= 0) {
                if (d->sign != v90_octet_sign(dschan_sent(&l->ch, n))) res->trn_err++;
            }
            if (c->rate && l->n_data >= 0 && n >= l->n_data && n < l->n_data + c->data_len) {
                uint8_t os = dschan_sent(&l->ch, n), og = dschan_got(&l->ch, n);
                unsigned us = v90_octet_ucode(c->alaw, os), ug = v90_octet_ucode(c->alaw, og);
                int ss = v90_octet_sign(os), sg = v90_octet_sign(og);
                int zs = !c->alaw && us == 0, zg = !c->alaw && ug == 0;   /* mu-law +0 = -0 */
                int s = d->slot;
                double off = (ss ? 1.0 : -1.0) * d->y / (double)(1 << V90ARX_YF) - v90_ucode_linear(c->alaw, us);
                res->ndec++;
                if (d->ucode != us || (!zs && d->sign != ss)) { res->nerr++; res->slot_err[s]++; }
                if (d->ucode != ug || (!zg && d->sign != sg)) res->nerr_got++;
                res->slot_n[s]++;
                res->slot_off[s] += off; res->slot_off2[s] += off * off;
            }
        }
    }
    res->got_dd = l->rx.state == V90ARX_DD;
    res->mse_db = 10 * log10(v90arx_mse(&l->rx) / 256.0 / pow(v90_ucode_linear(c->alaw, (unsigned)c->uinfo), 2) + 1e-30);
    res->mse_rms = sqrt(v90arx_mse(&l->rx) / 256.0);
    res->ppm_est = v90arx_ppm_q8(&l->rx) / 256.0;
    if (npe) res->erle_db = 10 * log10((pr + 1) / (pe + 1));
    if (npd && pec > 0) res->echo_rel_db = 10 * log10(pec / pd);
    for (i = 0; i < 6; i++) if (res->slot_n[i]) {
        res->slot_off[i] /= res->slot_n[i];
        res->slot_off2[i] = sqrt(res->slot_off2[i] / res->slot_n[i]);
    }
}

static lcfg_t base(void)
{
    lcfg_t c;
    memset(&c, 0, sizeof c);
    c.loop_km = 2; c.noise = -75; c.ppm = 0; c.phase = 0.3; c.echo_db = -300;
    c.trn_len = 4800; c.data_len = 16000; c.seed = 1;
    /* even: its codeword's LSB is 1 already, so a robbed bit (forced to 1)
     * leaves Sd and TRN1d alone (the analogue modem chooses UINFO) */
    c.uinfo = 90;
    return c;
}

static double ser(const lres_t *r) { return r->ndec ? (double)r->nerr / r->ndec : 1.0; }

/* ---- the channel model itself ---------------------------------------------- */

static long tn;
static uint8_t tone_src(void *ctx)
{
    long n = tn++;
    (void)ctx;
    return v90_pcm_octet(0, n % 6 == 0 ? 100 : 61, (n / 3) & 1);   /* 61: LSB 0 */
}

static void test_channel(void)
{
    static dschan_t ch;
    dschan_cfg c;
    long k;
    printf("the channel model\n");
    memset(&c, 0, sizeof c);
    c.level_db = -12; c.noise_dbm0 = -300; c.echo_db = -300; c.loop_km = 0;
    CHECK_NEAR(dschan_gain_db(&c, 1000), -12, 0.01);
    CHECK(dschan_gain_db(&c, 4000) < -12 - 20);
    CHECK(dschan_gain_db(&c, 300) > -12 - 1.5 && dschan_gain_db(&c, 3400) > -12 - 1);
    c.loop_km = 6;
    printf("  6 km: %.1f dB at 300 Hz, %.1f at 3400 Hz (1 kHz: -12)\n", dschan_gain_db(&c, 300), dschan_gain_db(&c, 3400));
    CHECK(dschan_gain_db(&c, 3400) < dschan_gain_db(&c, 1000) - 6);
    /* clock: +100 ppm takes 100 fewer octets per million A/D samples */
    c.ppm = 100; c.rbs = 1; c.rbs_phase = 2; c.pad_db = 6;
    tn = 0; dschan_init(&ch, &c, tone_src, 0);
    for (k = 0; k < 200000; k++) (void)dschan_tick(&ch, 0);
    printf("  +100 ppm: %ld octets for 200000 A/D samples\n", ch.n);
    CHECK_NEAR(ch.n - DSCH_PRE - 200000, -20, 1.5);
    /* robbed bit in one sample of six; the pad lowers Ucode 100 by 6 dB */
    {
        int robbed = 0, other = 0;
        for (k = ch.n - 600; k < ch.n; k++) {
            uint8_t s = dschan_sent(&ch, k), g = dschan_got(&ch, k);
            int32_t ls = v90_octet_linear(0, s), lg = v90_octet_linear(0, g);
            uint8_t padded = pcm_encode(0, (int16_t)floor(ls * pow(10, -6 / 20.0) + 0.5));
            if (k % 6 == 2) robbed += (g & 1) == 1 && (padded & 1) == 0;
            else other += g == padded;
            if (k % 6 == 0) CHECK_NEAR(20 * log10((double)lg / ls), -6, 0.6);
        }
        CHECK_EQ_I(robbed, 100);
        CHECK_EQ_I(other, 500);
    }
}

/* ---- tests -------------------------------------------------------------- */

int main(void)
{
    static const double PPM[] = { -100, -50, 0, 50, 100 };
    static const double PH[] = { 0.0, 0.37, 0.73 };
    static const double LOOP[] = { 0, 2, 4, 6 };
    static const int RATE[] = { 28000, 32000, 36000, 40000, 44000, 48000, 52000, 56000 };
    lcfg_t c;
    lres_t r;
    int i, j;

    test_channel();

    printf("training: Sd, TRN1d (4800 T), 2 km, noise -75 dBm0\n");
    c = base();
    run(&c, &r);
    printf("  state DD %d, mse after TRN1d %.1f dB re UINFO^2 (rms %.2f units), TRN1d decided wrong %ld\n",
           r.got_dd, r.mse_trn_db, r.mse_trn_rms, r.trn_err);
    CHECK(r.got_dd);
    CHECK(r.mse_trn_db < -40);
    CHECK_EQ_I(r.trn_err, 0);
    c.trn_len = 2040; c.ntrain = 1800;
    run(&c, &r);
    printf("  TRN1d at its shortest (2040 T, 1800 taken as known): mse %.1f dB\n", r.mse_trn_db);
    CHECK(r.got_dd);
    CHECK(r.mse_trn_db < -30);

    printf("timing: 40000 bit/s, 2 km, noise -75 dBm0, A/D clock offset and phase\n");
    for (i = 0; i < 5; i++) for (j = 0; j < 3; j++) {
        c = base(); c.ppm = PPM[i]; c.phase = PH[j]; c.rate = 40000; c.seed = (uint32_t)(10 + i * 3 + j);
        run(&c, &r);
        printf("  %+4.0f ppm phase %.2f: measured %+7.2f ppm, mse after TRN1d %.1f dB, at the end %.1f dB, errors %ld of %ld\n",
               PPM[i], PH[j], r.ppm_est, r.mse_trn_db, r.mse_db, r.nerr, r.ndec);
        CHECK(r.got_dd);
        CHECK_NEAR(r.ppm_est, PPM[i], 2);
        CHECK(r.ndec >= c.data_len - 10);
        CHECK(ser(&r) < 1e-3);
    }

    printf("loops: 40000 bit/s, noise -75 dBm0, +60 ppm\n");
    for (i = 0; i < 4; i++) {
        c = base(); c.loop_km = LOOP[i]; c.ppm = 60; c.rate = 40000; c.seed = (uint32_t)(40 + i);
        run(&c, &r);
        printf("  %.0f km: mse after TRN1d %.1f dB, end %.1f dB (rms %.2f units, dmin %d), errors %ld of %ld\n",
               LOOP[i], r.mse_trn_db, r.mse_db, r.mse_rms, r.dmin, r.nerr, r.ndec);
        CHECK(r.got_dd);
        CHECK(ser(&r) < 1e-3);
    }

    printf("echo of our own upstream (V.34, 3000 baud, 16 points; the canceller trained on noise first), 40000 bit/s, 4 km, -20 ppm\n");
    for (i = 0; i < 3; i++) {
        static const double EDB[] = { -300, -10, 0 };
        c = base(); c.loop_km = 4; c.ppm = -20; c.rate = 40000; c.seed = (uint32_t)(50 + i);
        c.ec_train = 6000; c.up_level = 2000; c.echo_db = EDB[i];
        if (i == 0) c.up_level = 0;
        run(&c, &r);
        if (i == 0) printf("  no echo: mse end %.1f dB, errors %ld of %ld\n", r.mse_db, r.nerr, r.ndec);
        else printf("  echo %+.1f dB re the downstream, ERLE %.1f dB: mse end %.1f dB, errors %ld of %ld\n",
                    r.echo_rel_db, r.erle_db, r.mse_db, r.nerr, r.ndec);
        CHECK(r.got_dd);
        CHECK(ser(&r) < 1e-3);
        if (i == 2) CHECK(r.echo_rel_db > 15);
        if (i > 0) CHECK(r.erle_db > 60);
    }

    printf("data: Ucode decision error rate (against what the digital modem sent), 16000 symbols each\n");
    {
        static const struct { const char *name; double km, noise, ppm, pad; int rbs, alaw, dil; } CH[] = {
            { "2 km, -75 dBm0,    0 ppm", 2, -75,    0, 0, 0, 0, 0 },
            { "0 km, -75 dBm0,  +80 ppm", 0, -75,   80, 0, 0, 0, 0 },
            { "6 km, -75 dBm0,  -80 ppm", 6, -75,  -80, 0, 0, 0, 0 },
            { "2 km, -65 dBm0,  +30 ppm", 2, -65,   30, 0, 0, 0, 0 },
            { "4 km, -60 dBm0,  -30 ppm", 4, -60,  -30, 0, 0, 0, 0 },
            { "2 km, -75 dBm0,  +30 ppm", 2, -75,   30, 0, 0, 0, 0 },
            { "  as row 6, 3 dB pad", 2, -75, 30, 3, 0, 0, 0 },
            { "  ... levels given (DIL)", 2, -75, 30, 3, 0, 0, 1 },
            { "  as row 6, 6 dB pad", 2, -75, 30, 6, 0, 0, 0 },
            { "  ... levels given (DIL)", 2, -75, 30, 6, 0, 0, 1 },
            { "  as row 6, RBS", 2, -75, 30, 0, 1, 0, 0 },
            { "  ... levels given (DIL)", 2, -75, 30, 0, 1, 0, 1 },
            { "  as row 6, A-law", 2, -75, 30, 0, 0, 1, 0 },
        };
        enum { NCH = sizeof CH / sizeof CH[0], NR = sizeof RATE / sizeof RATE[0] };
        static lres_t R[NCH][NR];
        static int rsl[NCH];
        printf("  %-30s", "channel \\ bit/s");
        for (j = 0; j < NR; j++) printf(" %8d", RATE[j]);
        printf("\n  %-30s", "  dmin, mu-law (units)");
        for (j = 0; j < NR; j++) {
            uint8_t mk[16]; int M, K;
            printf(" %8d", design(0, RATE[j], mk, &M, &K));
        }
        printf("\n");
        for (i = 0; i < NCH; i++) {
            printf("  %-30s", CH[i].name);
            for (j = 0; j < NR; j++) {
                c = base(); c.loop_km = CH[i].km; c.noise = CH[i].noise; c.ppm = CH[i].ppm; c.pad = CH[i].pad;
                c.rbs = CH[i].rbs; c.rbs_phase = 4; c.alaw = CH[i].alaw; c.dil = CH[i].dil; c.rate = RATE[j];
                c.seed = (uint32_t)(100 + i * 10 + j);
                run(&c, &R[i][j]);
                rsl[i] = (int)(((4 - L.n_trn) % 6 + 6) % 6);
                if (R[i][j].nerr == 0) printf(" %8s", "0");
                else printf(" %8.1e", ser(&R[i][j]));
            }
            printf("\n");
        }
        printf("  mse (dB re UINFO^2) and rms error (units) at 28000 / 40000 bit/s; rms of |y| - |sent| at 36000\n");
        for (i = 0; i < NCH; i++)
            printf("  %-30s %6.1f / %6.1f   %7.2f / %7.2f   %8.2f\n", CH[i].name, R[i][0].mse_db, R[i][3].mse_db,
                   R[i][0].mse_rms, R[i][3].mse_rms,
                   sqrt((R[i][2].slot_off2[0] * R[i][2].slot_off2[0] + R[i][2].slot_off2[1] * R[i][2].slot_off2[1]
                       + R[i][2].slot_off2[2] * R[i][2].slot_off2[2] + R[i][2].slot_off2[3] * R[i][2].slot_off2[3]
                       + R[i][2].slot_off2[4] * R[i][2].slot_off2[4] + R[i][2].slot_off2[5] * R[i][2].slot_off2[5]) / 6));
        for (i = 0; i < NCH; i++) if (CH[i].rbs) {
            int s, worst = 0;
            printf("  %s: robbed slot %d; per slot at 36000: errors, mean and rms of |y| - |sent| (units)\n   ", CH[i].name, rsl[i]);
            for (s = 0; s < 6; s++) {
                printf(" [%d] %ld %+.1f %.1f", s, R[i][2].slot_err[s], R[i][2].slot_off[s], R[i][2].slot_off2[s]);
                if (fabs(R[i][2].slot_off[s]) > fabs(R[i][2].slot_off[worst])) worst = s;
            }
            printf("\n");
            if (!CH[i].dil) CHECK_EQ_I(worst, rsl[i]);
        }
        /* what the table must show: no error up to these rates (beyond them,
         * the noise, or points that a pad or robbed bit moves to within
         * dmin / 2 of a neighbour, which only a constellation designed for
         * the slot avoids) */
        for (j = 0; j < NR; j++) {
            static const int UPTO[NCH] = { 48000, 48000, 48000, 44000, 40000, 48000,
                                           32000, 44000, 40000, 48000, 32000, 40000, 48000 };
            for (i = 0; i < NCH; i++) {
                if (RATE[j] <= UPTO[i]) CHECK_EQ_I(R[i][j].nerr, 0);
                CHECK(R[i][j].got_dd);
            }
        }
        /* pads and the robbed bit show as errors against Table 1's levels
         * (rows 7, 9, 11), and go when the receiver is given the levels */
        for (i = 6; i <= 10; i += 2) {
            CHECK(R[i][2].mse_rms > 3 * R[5][2].mse_rms);
            CHECK(R[i + 1][2].mse_rms < R[i][2].mse_rms / 3);
        }
    }

    T_REPORT("v90arx");
}
