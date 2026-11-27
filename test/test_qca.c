/* The simulated V.92 Quick Connect caller (sim/qca, and sim/v92call with
 * cfg.qc) against the V.92 text: Table 2's QC1a bit by bit, Table 13's QCA1d
 * received, 8.3.6's QTS and 8.3.1's ANSpcm codewords (Table 8 transcribed),
 * and a whole short Phase 1 (9.2.1.1, 9.2.1.3) against a stub digital modem
 * written here from the tables: ANSam, QCA1d, 75 ms, QTS, QTS\, ANSpcm, then
 * the caller's TONEq, 75 ms and INFO0a. */
#include <string.h>
#include "test.h"
#include "../sim/qca.h"
#include "../sim/v92call.h"
#include "../sim/g711.h"
#include "../modem/v21.h"
#include "../modem/dpsk.h"
#include "../modem/info.h"
#include "../sim/p2a.h"

#define FS 8000
#define PI 3.14159265358979323846

static void bits_of(const char *s, uint8_t *b, int *n)
{
    for (; *s; s++) if (*s == '0' || *s == '1') b[(*n)++] = (uint8_t)(*s - '0');
}

static int same_bits(const uint8_t *a, const char *s)
{
    uint8_t b[128]; int n = 0, i;
    bits_of(s, b, &n);
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void test_qc1a_bits(void)
{
    uint8_t b[64];
    int w, p, i;
    printf("qc1a_bits\n");
    CHECK_EQ_I(qca_qc1a_bits(0x5, 1, b), 60);
    CHECK(same_bits(b, "1111111111 0101010101 0001001011 1111111111 0101010101 0001001011"));
    (void)qca_qc1a_bits(0x7, 0, b);
    CHECK(same_bits(b + 20, "0000001111") && same_bits(b + 50, "0000001111"));
    {   /* that character's octet: its eight data bits, the first in time in bit 0 */
        int oct = 0;
        for (i = 0; i < 8; i++) oct |= b[21 + i] << i;
        CHECK_EQ_I(oct, 0xE0);
    }
    (void)qca_qc1a_bits(0xE, 1, b);
    CHECK(same_bits(b + 20, "0001101101") && same_bits(b + 50, "0001101101"));
    /* every (WXYZ, P) against "000PW0XYZ1", the framing around it fixed */
    for (w = 0; w < 16; w++)
        for (p = 0; p < 2; p++) {
            char want[11];
            int ok = 1;
            want[0] = '0'; want[1] = '0'; want[2] = '0'; want[3] = (char)('0' + p);
            want[4] = (char)('0' + ((w >> 3) & 1)); want[5] = '0';
            want[6] = (char)('0' + ((w >> 2) & 1)); want[7] = (char)('0' + ((w >> 1) & 1));
            want[8] = (char)('0' + (w & 1)); want[9] = '1'; want[10] = 0;
            ok &= qca_qc1a_bits(w, p, b) == 60;
            ok &= same_bits(b, "1111111111 0101010101") && same_bits(b + 30, "1111111111 0101010101");
            ok &= same_bits(b + 20, want) && same_bits(b + 50, want);
            CHECK(ok);
        }
}

/* the stub's V.21(H) sender: a bit string, at peak amplitude amp */
static int fsk_h(const char *s, int amp, int16_t *out, int max)
{
    uint8_t b[128]; int nb = 0, i = 0, n = 0, done;
    v21mod_t m;
    bits_of(s, b, &nb);
    v21mod_init(&m, V21H_MARK, V21H_SPACE, FS, 300, (int16_t)amp);
    while (i < nb && n < max) {
        out[n++] = v21mod_tick(&m, b[i], &done);
        if (done) i++;
    }
    return n;
}

#define QCA1D_87 "1111111111 0101010101 0111000011 1111111111 0101010101 0111000011 1111111111"
#define DBM0_12 5600            /* peak of a -12 dBm0 sine on the 16-bit scale (0 dBm0: 22300) */

static void test_qca1d_rx(void)
{
    static int16_t sig[4000];
    qca_rx_t r;
    int n, i, hits, at;
    printf("qca1d_rx\n");
    for (i = 0; i < 300; i++) sig[i] = 0;
    n = 300 + fsk_h(QCA1D_87, DBM0_12, sig + 300, 3000);
    for (i = n; i < n + 300; i++) sig[i] = 0;
    n += 300;
    qca_rx_init(&r);
    hits = 0; at = -1;
    for (i = 0; i < n; i++) {
        /* through the network's mu-law, as the caller hears it */
        int16_t x = (int16_t)ulaw_to_linear(linear_to_ulaw(sig[i]));
        if (qca_rx_tick(&r, x)) { hits++; if (at < 0) at = i; }
    }
    CHECK_EQ_I(hits, 1);
    CHECK_EQ_I(r.oct, 0x87);
    printf("  QCA1d read %d samples after its first bit\n", at - 300);
    /* bit 55 (in the second copy's character) flipped: never */
    {
        char bad[sizeof QCA1D_87];
        int k = 0, pos = 0;
        memcpy(bad, QCA1D_87, sizeof bad);
        for (i = 0; bad[i]; i++) if (bad[i] == '0' || bad[i] == '1') { if (pos == 55) k = i; pos++; }
        bad[k] = bad[k] == '0' ? '1' : '0';
        n = 300 + fsk_h(bad, DBM0_12, sig + 300, 3000);
        for (i = n; i < n + 300; i++) sig[i] = 0;
        n += 300;
        qca_rx_init(&r);
        hits = 0;
        for (i = 0; i < n; i++)
            if (qca_rx_tick(&r, (int16_t)ulaw_to_linear(linear_to_ulaw(sig[i])))) hits++;
        CHECK_EQ_I(hits, 0);
    }
}

static void test_qts(void)
{
    static const uint8_t mu0[6] = { 0xB9, 0xFF, 0xB9, 0x39, 0x7F, 0x39 };
    static const uint8_t a0[6] = { 0x93, 0xD5, 0x93, 0x13, 0x55, 0x13 };
    int i, ok;
    printf("qts\n");
    for (i = 0; i < 6; i++) {
        CHECK_EQ_I(qca_qts_octet(0, 70, i), mu0[i]);
        CHECK_EQ_I(qca_qts_octet(0, 70, 768 + i), mu0[(i + 3) % 6]);
        CHECK_EQ_I(qca_qts_octet(1, 70, i), a0[i]);
        CHECK_EQ_I(qca_qts_octet(1, 70, 768 + i), a0[(i + 3) % 6]);
    }
    /* 128 units of QTS, 8 of QTS\ */
    ok = 1;
    for (i = 0; i < 816; i++) ok &= qca_qts_octet(0, 70, i) == (i < 768 ? mu0[i % 6] : mu0[(i % 6 + 3) % 6]);
    CHECK(ok);
}

/* Table 8/V.92 (-12 dBm0), transcribed: k 0..5, mu-law and A-law, and k 300 */
static const uint8_t t8_mu[6] = { 0xA9, 0x5D, 0x29, 0xC9, 0xAA, 0x3D };
static const uint8_t t8_a[6] = { 0x83, 0x49, 0x00, 0xE1, 0x80, 0x15 };

static void test_anspcm(void)
{
    int i;
    printf("anspcm\n");
    for (i = 0; i < 6; i++) {
        CHECK_EQ_I(qca_anspcm_octet(0, 1, i), t8_mu[i]);
        CHECK_EQ_I(qca_anspcm_octet(1, 1, i), t8_a[i]);
    }
    CHECK_EQ_I(qca_anspcm_octet(0, 1, 300), 0x5E);
    CHECK_EQ_I(qca_anspcm_octet(0, 1, 3612), 0x29);      /* reversed */
    CHECK_EQ_I(qca_anspcm_octet(0, 1, 7224), 0xA9);
    CHECK_EQ_I(qca_anspcm_octet(0, 1, 301), 0xA9);       /* the cycle */
}

/* ---- the stub digital modem ---- */

enum { ST_SIL, ST_ANSAM, ST_QCA, ST_SIL75, ST_QTS, ST_ANSPCM, ST_AFTER };

typedef struct {
    int st; long s, t;          /* sample (from 0), samples in this state */
    long ansam_at;              /* first ANSam sample */
    /* upstream: QC1a and CM as received */
    v21demod_t dl; long q_on; uint8_t qbits[80]; int nq;
    int16_t qca[3000]; int nqca; long qca_end;
    long last_loud;             /* the last upstream sample with signal, before TONEq */
    long cm_stop;               /* last_loud when QCA1d ended and silence followed */
    long qts_at, anspcm_at, anspcm_end;
    /* TONEq: 80-sample blocks */
    double gi, gq, pw; int gn; long toneq_first, toneq_last; int toneq_run, toneq_blocks, impure, pend;
    long info0a_on; dpskrx_t rx; int got0a; uint8_t frame[INFO_MAX_BITS];
} stub_t;

static uint8_t stub_anspcm(long n)
{
    long k = n % 301;
    uint8_t o;
    if (k < 6) {
        o = t8_mu[k];
        if ((n / 3612) & 1) o ^= 0x80;
        return o;
    }
    return qca_anspcm_octet(0, 1, n);
}

static uint8_t stub_tick(stub_t *d, uint8_t us)
{
    int16_t x = (int16_t)ulaw_to_linear(us);
    int loud = x > 500 || x < -500;
    uint8_t o = 0xFF;
    long s = d->s;

    /* upstream: the QC1a and CM bits, read at the end of each bit */
    int lvl = v21demod_tick(&d->dl, x);
    if (d->q_on < 0 && loud && d->st == ST_ANSAM) d->q_on = s;
    if (d->q_on >= 0 && d->nq < 80) {
        long end = d->q_on + (long)((d->nq + 1) * 8000L / 300) - 1;
        if (s == end) d->qbits[d->nq++] = (uint8_t)lvl;
    }
    if (loud && d->toneq_first < 0) d->last_loud = s;

    switch (d->st) {
    case ST_SIL:
        if (s >= 1600) { d->st = ST_ANSAM; d->ansam_at = s; d->t = 0; }
        break;
    case ST_ANSAM:
        break;
    default:
        break;
    }
    if (d->st == ST_ANSAM) {
        /* ANSam (V.8 7.1): 2100 Hz, 15 Hz AM of 20 %, reversals every 450 ms */
        double t = (double)d->t / FS;
        double ph = 2 * PI * 2100 * t + ((d->t / 3600) & 1) * PI;
        double v = DBM0_12 * (1 + 0.2 * sin(2 * PI * 15 * t)) * cos(ph);
        o = linear_to_ulaw((int32_t)lround(v));
        d->t++;
        /* the QC1a is in once its 60 bits are: QCA1d at once */
        if (d->nq >= 60) { d->st = ST_QCA; d->t = 0; }
    }
    if (d->st == ST_QCA) {
        o = linear_to_ulaw(d->qca[d->t]);
        if (++d->t >= d->nqca) { d->st = ST_SIL75; d->t = 0; d->qca_end = s; }
    } else if (d->st == ST_SIL75) {
        if (d->t == 400) d->cm_stop = d->last_loud;
        if (++d->t > 600) { d->st = ST_QTS; d->t = 0; d->qts_at = s; }
    }
    if (d->st == ST_QTS) {
        /* 8.3.6 with UQTS 70: {+V,+0,+V,-V,-0,-V} x 128, then reversed x 8 */
        static const uint8_t unit[6] = { 0xB9, 0xFF, 0xB9, 0x39, 0x7F, 0x39 };
        o = d->t < 768 ? unit[d->t % 6] : unit[(d->t % 6 + 3) % 6];
        if (++d->t >= 816) { d->st = ST_ANSPCM; d->t = 0; }
    } else if (d->st == ST_ANSPCM) {
        if (d->t == 0) d->anspcm_at = s;
        o = stub_anspcm(d->t);
        d->t++;
        /* TONEq for 50 ms: ANSpcm stops (9.2.4.3) */
        if (d->toneq_run >= 5) { d->st = ST_AFTER; d->anspcm_end = s + 1; d->t = 0; }
        if (d->t > 3 * FS) { d->st = ST_AFTER; d->anspcm_end = s + 1; d->t = 0; }
    } else if (d->st == ST_AFTER) {
        d->t++;
    }

    /* TONEq: 980 Hz in 80-sample blocks, from ANSpcm on */
    if (d->anspcm_at >= 0 && d->info0a_on < 0) {
        double ph = 2 * PI * 980 * (double)s / FS;
        d->gi += x * cos(ph); d->gq += x * sin(ph); d->pw += (double)x * x;
        if (++d->gn == 80) {
            double p980 = 2 * (d->gi * d->gi + d->gq * d->gq) / 80;
            int tone = d->pw > 80 * 1e5 && p980 >= 0.9 * d->pw;
            /* a loud block that is not TONEq between two that are */
            if (d->pw > 80 * 1e5 && !tone && d->toneq_first >= 0) d->pend++;
            if (tone) {
                if (d->toneq_first < 0) d->toneq_first = s - 79;
                d->impure += d->pend; d->pend = 0;
                d->toneq_last = s; d->toneq_run++; d->toneq_blocks++;
            } else d->toneq_run = 0;
            d->gi = d->gq = d->pw = 0; d->gn = 0;
        }
    }
    /* INFO0a, after TONEq and its 75 ms */
    if (d->toneq_last >= 0 && d->info0a_on < 0 && loud && s > d->toneq_last + 80) d->info0a_on = s;
    if (d->toneq_last >= 0 && !d->got0a && (dpskrx_tick(&d->rx, x) & DPSK_EV_FRAME)) {
        d->got0a = 1;
        memcpy(d->frame, d->rx.frame, sizeof d->frame);
    }
    d->s++;
    return o;
}

static void stub_init(stub_t *d)
{
    memset(d, 0, sizeof *d);
    d->st = ST_SIL; d->q_on = -1; d->qts_at = d->anspcm_at = d->anspcm_end = d->toneq_first = d->toneq_last = -1;
    d->info0a_on = -1; d->cm_stop = -1; d->qca_end = -1; d->last_loud = -1;
    v21demod_init(&d->dl, V21L_MARK, V21L_SPACE, FS, 300);
    d->nqca = fsk_h(QCA1D_87, DBM0_12, d->qca, 3000);
    dpskrx_init(&d->rx, DPSK_FC_A);
    dpskrx_expect(&d->rx, INFO_PAYLOAD(INFO0A_BITS));
}

static void qc_cfg(v92call_cfg *cfg, int short_p2)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->v92 = 1; cfg->pcm_up = 1; cfg->lapm = 1;
    cfg->qc = 1; cfg->qc_wxyz = 5; cfg->qc_lapm = 1; cfg->short_p2 = short_p2;
}

static void caller_qc(int short_p2)
{
    static v92call_t c;
    static stub_t d;
    v92call_cfg cfg;
    uint8_t ds = 0xFF, us;
    long i, toneq_end;
    printf("caller_qc short_p2 %d\n", short_p2);
    qc_cfg(&cfg, short_p2);
    v92call_init(&c, &cfg);
    stub_init(&d);
    for (i = 0; i < 6 * FS && !d.got0a; i++) {
        us = v92call_tick(&c, ds);      /* sample i is v92call's c.n = i + 1 */
        ds = stub_tick(&d, us);
    }
    /* QC1a 1.0 s after ANSam began, then CM's ten ONEs and its sync */
    printf("  QC1a began %ld samples after ANSam\n", d.q_on - d.ansam_at);
    CHECK(d.q_on >= 0 && labs(d.q_on - d.ansam_at - FS) <= 160);
    CHECK_EQ_I(d.nq, 80);
    CHECK(same_bits(d.qbits, "1111111111 0101010101 0001001011 1111111111 0101010101 0001001011"));
    CHECK(same_bits(d.qbits + 60, "1111111111 0000001111"));
    /* CM stops within a character of QCA1d's end, and not before the middle
     * of its last information bit (bit 59) was on the line */
    printf("  CM stopped %ld samples from QCA1d's end\n", d.cm_stop - d.qca_end);
    CHECK(d.cm_stop >= 0 && d.cm_stop - d.qca_end >= -280 && d.cm_stop - d.qca_end <= 267);
    CHECK_EQ_I(c.qca1d, 0x87);
    CHECK_EQ_I(c.qts_at, d.qts_at + 2);     /* the stub's octet of sample s is v92call's n = s + 2 */
    CHECK_EQ_I(c.qts_ok, 1);
    CHECK_EQ_I(c.anspcm_ok, 1);
    /* TONEq: within 100 ms of ANSpcm, pure, until ANSpcm stops */
    printf("  TONEq from %ld to %ld samples after ANSpcm began; ANSpcm %ld samples\n",
           d.toneq_first - d.anspcm_at, d.toneq_last - d.anspcm_at, d.anspcm_end - d.anspcm_at);
    CHECK(d.toneq_first >= d.anspcm_at && d.toneq_first - d.anspcm_at <= 800);
    CHECK_EQ_I(d.impure, 0);
    CHECK(d.toneq_last >= d.anspcm_end - 80 && d.toneq_last <= d.anspcm_end + 80);
    CHECK(c.toneq_on > 0 && c.toneq_off >= c.toneq_on);
    CHECK(labs(c.toneq_off - (d.anspcm_end + 2)) <= 1);
    /* 75 ms of silence, then INFO0a: V.92 (bit 26), short Phase 2 (bit 27) */
    toneq_end = c.toneq_off - 1;        /* as the stub's sample number */
    printf("  silence %ld samples, then INFO0a\n", d.info0a_on - toneq_end);
    CHECK(d.info0a_on >= 0 && labs(d.info0a_on - toneq_end - 600) <= 40);
    CHECK_EQ_I(d.got0a, 1);
    CHECK_EQ_I(d.frame[26 - INFO_HDR_BITS], 1);
    CHECK_EQ_I(d.frame[27 - INFO_HDR_BITS], short_p2);
    CHECK_EQ_I(c.stage, VCL_P2);
}

/* the fault qc_deaf: QCA1d read, and CM goes on as if it had not come */
static void caller_deaf(void)
{
    static v92call_t c;
    static stub_t d;
    v92call_cfg cfg;
    uint8_t ds = 0xFF, us;
    long i;
    printf("caller_deaf\n");
    qc_cfg(&cfg, 1);
    cfg.qc_deaf = 1;
    v92call_init(&c, &cfg);
    stub_init(&d);
    for (i = 0; i < 4 * FS && (d.qca_end < 0 || d.s < d.qca_end + 1000); i++) {
        us = v92call_tick(&c, ds);
        ds = stub_tick(&d, us);
    }
    CHECK_EQ_I(c.qca1d, 0x87);
    CHECK_EQ_I(c.stage, VCL_V8);
    CHECK(d.qca_end > 0 && d.last_loud >= d.qca_end + 990);
    CHECK_EQ_I(c.qts_at, -1);
    CHECK_EQ_I(c.toneq_on, -1);
}

/* Table 19's INFO1a as sim/p2a writes it, against the table */
static void t19(void)
{
    uint8_t b[INFO_MAX_BITS];
    int k, ok = 1;
    printf("t19\n");
    CHECK_EQ_I(p2a_t19_build(5, 100, 1, 4, -512, b), INFO1A_BITS);
    CHECK(same_bits(b, "1111 01110010"));                       /* 0:11 */
    for (k = 12; k <= 17; k++) ok &= b[k] == 0;                 /* reserved */
    for (k = 0; k < 7; k++) ok &= b[18 + k] == ((5 >> k) & 1);  /* MD, LSB first */
    for (k = 0; k < 7; k++) ok &= b[25 + k] == ((100 >> k) & 1);/* UINFO */
    ok &= b[32] == 0 && b[33] == 1;
    ok &= b[34] == 0 && b[35] == 0 && b[36] == 1;               /* 4 */
    ok &= b[37] == 0 && b[38] == 1 && b[39] == 1;               /* 6 */
    for (k = 40; k <= 48; k++) ok &= b[k] == 0;                 /* -512: 10 0000 0000 */
    ok &= b[49] == 1;
    CHECK(ok);
    CHECK(info_check(b + INFO_HDR_BITS, INFO_PAYLOAD(INFO1A_BITS)));
    CHECK(same_bits(b + 66, "1111"));
}

int main(void)
{
    test_qc1a_bits();
    test_qca1d_rx();
    test_qts();
    test_anspcm();
    caller_qc(1);
    caller_qc(0);
    caller_deaf();
    t19();
    T_REPORT("test_qca");
}
