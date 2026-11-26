/* modem/v8bis: K56flex's V.8bis exchange as the initiating station, against
 * a scripted far end (docs/history/k56-facts.md "V.8bis signals", rulings
 * C1 and C6):
 *   - 400 ms of silence, CRe: 290 ms of 1375 + 2002 Hz, 100 ms of 400 Hz,
 *     each tone within 1 %, 14.6 dB below our CL;
 *   - CRd 175 ms after CRe: our CL 120 ms (+-10) after CRd ends, decoding
 *     to v8bis_cl_octets(V8B_NS_DEFAULT, V8B_VERS_BOTH) (version octet
 *     0x43); SM56's MS (its echo of our version octet, 0x03) 10 ms after
 *     it: ACK(1) 180 ms (+-10) after the MS, V8B_RES_AGREED;
 *   - no CRd: V8B_RES_NO_CRD, done 1000 ms after CRe; a CRd that starts at
 *     990 ms is received to its end; one at 1050 ms is not;
 *   - CRd, no MS: V8B_RES_NO_MS 1500 ms after our CL; an MS without the
 *     block: V8B_RES_NO_MS, nothing sent; an MS with a bad FCS:
 *     V8B_RES_NO_MS, NAK(1), no ACK(1);
 *   - versions: an MS selecting version 1 (0x01, 0x21): NAK(3) 180 ms
 *     after it, V8B_RES_DECLINED; with accept_v1: ACK(1); 0x02 and 0x40
 *     (no version bit): ACK(1); version 2 alone offered (0x42, 0.7.1's
 *     CL): ACK(1) to 0x21 and to 0x01, as in 0.7.1;
 *   - the MS with its clock 0.1 % off and noise 40 dB down: received.
 * And the CRd detector alone: the pair held 150 ms, its 1900 Hz end. */
#include <string.h>
#include "test.h"
#include "../modem/v8bis.h"
#include "../sim/noise.h"

#define FS 8000
#define NMAX (FS * 8)
#define TWO_PI 6.283185307179586

static v8bis_t B;
static int16_t txs[NMAX];
static uint8_t sts[NMAX];

/* SM56's MS (k56-facts.md "V.8bis messages") */
static const uint8_t sm56_ms[13] = { 0x11, 0xc9, 0x80, 0x80, 0x80, 0x07, 0xb5, 0x02, 0x00, 0x94, 0x81, 0x83, 0x21 };
static const uint8_t plain_ms[5] = { 0x11, 0xc9, 0x80, 0x80, 0x80 };     /* an MS without NS */

enum { MS_NONE, MS_SM56, MS_PLAIN, MS_BADFCS };

typedef struct {
    int crd_ms;             /* CRd's start after CRe's end; -1 none */
    int ms;                 /* MS_* */
    int ms_gap_ms;          /* after our CL */
    int fs_far;             /* the far end's clock: FS, or 0.1 % off */
    double noise;           /* RMS */
    int vers;               /* the versions we offer (V8B_VERS_*); 0: V8B_VERS_BOTH */
    int accept_v1;
    int ms_ver;             /* the version octet of its MS; 0: SM56's echo of our CL's
                               (ours less 0x40) */
} script_t;

typedef struct {
    long cre_end, crd_end, cl_start, cl_end, ms_end, ack_start, ack_end, done;
    int result;
} events_t;

static void run(const script_t *s, events_t *e)
{
    static uint8_t msbits[V8B_MAXBITS];
    v21mod_t fm;
    rng_t rng;
    long n, crd_at = -1, ms_at = -1;
    int nms = 0, mpos = 0, ms_on = 0, prev = -1;
    double ph1 = 0, ph2 = 0, ph3 = 0;

    memset(e, 0, sizeof *e);
    e->cre_end = e->crd_end = e->cl_start = e->cl_end = e->ms_end = e->ack_start = e->ack_end = e->done = -1;
    memset(txs, 0, sizeof txs);
    rng_seed(&rng, 7);
    v21mod_init(&fm, V21H_MARK, V21H_SPACE, s->fs_far ? s->fs_far : FS, 300, 3000);
    int vers = s->vers ? s->vers : V8B_VERS_BOTH;
    uint8_t ms[13];
    memcpy(ms, sm56_ms, 13);
    ms[12] = (uint8_t)(s->ms_ver ? s->ms_ver : vers);
    if (s->ms == MS_SM56 || s->ms == MS_BADFCS) nms = v8bis_frame(ms, 13, msbits);
    if (s->ms == MS_PLAIN) nms = v8bis_frame(plain_ms, 5, msbits);
    /* a bad FCS: octet 0's first bit (a one) sent as a zero, nothing else changed */
    if (s->ms == MS_BADFCS) msbits[32 + 24] = 0;
    v8bis_init(&B, 4243, V8B_NS_DEFAULT, vers, s->accept_v1);
    for (n = 0; n < NMAX; n++) {
        double far = 0;
        int16_t rx, tx;
        v8b_state st;
        if (crd_at >= 0 && n >= crd_at) {
            long k = n - crd_at;
            if (k < 8L * 400) {
                far = 2000 * sin(ph1) + 2000 * sin(ph2);
                ph1 += TWO_PI * 1529 / FS; ph2 += TWO_PI * 2225 / FS;
            } else if (k < 8L * 500) {
                far = 2800 * sin(ph3); ph3 += TWO_PI * 1900 / FS;
            } else if (e->crd_end < 0) e->crd_end = n;
        }
        if (ms_on) {
            int done;
            far += v21mod_tick(&fm, msbits[mpos], &done);
            if (done && ++mpos >= nms) { ms_on = 0; e->ms_end = n + 1; }
        } else if (ms_at >= 0 && n == ms_at) {
            int done;
            ms_on = 1;
            far += v21mod_tick(&fm, msbits[mpos], &done);
            if (done) mpos++;
        }
        far += s->noise * rng_gauss(&rng);
        rx = (int16_t)(far > 32767 ? 32767 : far < -32768 ? -32768 : lround(far));
        st = v8bis_tick(&B, rx, &tx);
        txs[n] = tx; sts[n] = (uint8_t)st;
        /* the state after sample n is the next sample's: an event at n + 1 */
        if ((int)st != prev) {
            long b = n + 1;
            if (prev == V8B_CRE) {
                e->cre_end = b;
                if (s->crd_ms >= 0) crd_at = b + 8L * s->crd_ms;
            }
            if (st == V8B_CL) e->cl_start = b;
            if (prev == V8B_CL) {
                e->cl_end = b;
                if (nms) ms_at = b + 8L * s->ms_gap_ms;
            }
            if (st == V8B_ACK) e->ack_start = b;
            if (prev == V8B_ACK) e->ack_end = b;
            if (st == V8B_DONE) { e->done = b; break; }
            prev = (int)st;
        }
    }
    e->result = v8bis_result(&B);
}

/* the frames in what we sent from a to b, on V.21 channel 1, bit-aligned with
 * a (our modulator's first bit begins there) */
static int decode(long a, long b, uint8_t frames[][V8B_MAXOCT], int *lens, int maxf)
{
    static uint8_t bits[4096];
    v21demod_t d;
    v8bis_rx_t r;
    int nb, k, nf = 0;
    if (a < 0 || b <= a) return 0;
    v21demod_init(&d, V21L_MARK, V21L_SPACE, FS, 300);
    v8bis_rx_init(&r);
    nb = v21demod_run(&d, txs + a, (int)(b - a), bits, 4096);
    for (k = 0; k < nb && nf < maxf; k++) {
        int n = v8bis_rx_bit(&r, bits[k], frames[nf]);
        if (n > 0) lens[nf++] = n;
    }
    return nf;
}

/* the power at f, |sum x e^-jwn|^2 / N^2 (a tone of peak A gives A^2 / 4) */
static double gpow(long a, long n, double f)
{
    double re = 0, im = 0;
    long k;
    for (k = 0; k < n; k++) { re += txs[a + k] * cos(TWO_PI * f * k / FS); im -= txs[a + k] * sin(TWO_PI * f * k / FS); }
    return (re * re + im * im) / ((double)n * n);
}
/* the frequency within +-5 % of f where the power peaks, in steps of 0.1 % */
static double peak(long a, long n, double f)
{
    double best = 0, bf = f;
    int k;
    for (k = -50; k <= 50; k++) {
        double g = f * (1 + k / 1000.0), p = gpow(a, n, g);
        if (p > best) { best = p; bf = g; }
    }
    return bf;
}
static double rms(long a, long n)
{
    double s = 0;
    long k;
    for (k = 0; k < n; k++) s += (double)txs[a + k] * txs[a + k];
    return sqrt(s / n);
}

static void test_detector(void)
{
    v8bis_crd_t d;
    double p1 = 0, p2 = 0, p3 = 0;
    long n, tone_at = -1, end_at = -1;
    int r;
    /* 400 ms of the pair, 100 ms of 1900 Hz, silence */
    v8bis_crd_init(&d);
    for (n = 0; n < 8000; n++) {
        double x = 0;
        if (n < 3200) { x = 1500 * sin(p1) + 1500 * sin(p2); p1 += TWO_PI * 1529 / FS; p2 += TWO_PI * 2225 / FS; }
        else if (n < 4000) { x = 2000 * sin(p3); p3 += TWO_PI * 1900 / FS; }
        r = v8bis_crd_tick(&d, (int16_t)lround(x));
        if (r == V8B_CRD_TONE) { CHECK(tone_at < 0); tone_at = n; }
        if (r == V8B_CRD_END) { CHECK(end_at < 0); end_at = n; }
    }
    CHECK_NEAR(tone_at, 1200, 80);              /* held 150 ms */
    CHECK_NEAR(end_at - 80, 4000, 40);          /* one block after the end */
    /* the pair 100 ms only: no CRd; 1900 Hz alone: none; CRe's own tones: none */
    v8bis_crd_init(&d); p1 = p2 = 0;
    for (n = 0, r = 0; n < 4000; n++) {
        double x = n < 800 ? 1500 * sin(p1) + 1500 * sin(p2) : 0;
        p1 += TWO_PI * 1529 / FS; p2 += TWO_PI * 2225 / FS;
        r |= v8bis_crd_tick(&d, (int16_t)lround(x));
    }
    CHECK_EQ_I(r, 0);
    v8bis_crd_init(&d); p1 = p2 = 0;
    for (n = 0, r = 0; n < 8000; n++) {
        double x = 1500 * sin(p1) + 1500 * sin(p2) + (n < 4000 ? 0 : 2000 * sin(p3));
        p1 += TWO_PI * 1375 / FS; p2 += TWO_PI * 2002 / FS; p3 += TWO_PI * 1900 / FS;
        r |= v8bis_crd_tick(&d, (int16_t)lround(x));
    }
    CHECK_EQ_I(r, 0);
}

int main(void)
{
    script_t s;
    events_t e;
    uint8_t fr[4][V8B_MAXOCT], cl[V8B_MAXOCT];
    int len[4], nf, ncl, ok;
    long n;

    test_detector();

    /* the whole exchange: CRd 175 ms after CRe, SM56's MS 10 ms after our CL */
    memset(&s, 0, sizeof s);
    s.crd_ms = 175; s.ms = MS_SM56; s.ms_gap_ms = 10;
    run(&s, &e);
    /* silence, then CRe */
    for (n = 0, ok = 1; n < 8L * V8B_SIL_MS; n++) if (txs[n] != 0) ok = 0;
    CHECK(ok);
    CHECK(txs[8L * V8B_SIL_MS + 1] != 0);
    CHECK_EQ_I(e.cre_end, 8L * (V8B_SIL_MS + 290 + 100));
    {
        long a = 8L * V8B_SIL_MS, b = a + 8L * 290;
        CHECK_NEAR(peak(a, 8L * 290, 1375) / 1375, 1, 0.01);
        CHECK_NEAR(peak(a, 8L * 290, 2002) / 2002, 1, 0.01);
        CHECK_NEAR(peak(b, 8L * 100, 400) / 400, 1, 0.01);
        CHECK(gpow(b, 8L * 100, 1375) < gpow(b, 8L * 100, 400) * 1e-3);    /* the segments do not overlap */
        CHECK(gpow(a, 8L * 290, 400) < gpow(a, 8L * 290, 1375) * 1e-3);
        /* 14.6 dB below the messages, both segments */
        CHECK_NEAR(20 * log10(rms(e.cl_start + 8, 8L * 100) / rms(a, 8L * 290)), 14.6, 0.3);
        CHECK_NEAR(20 * log10(rms(e.cl_start + 8, 8L * 100) / rms(b, 8L * 100)), 14.6, 0.3);
        printf("  CRe: %.1f + %.1f Hz, %.1f Hz; %.1f / %.1f dB below the CL\n", peak(a, 8L * 290, 1375),
               peak(a, 8L * 290, 2002), peak(b, 8L * 100, 400),
               20 * log10(rms(e.cl_start + 8, 800) / rms(a, 2320)), 20 * log10(rms(e.cl_start + 8, 800) / rms(b, 800)));
    }
    CHECK_NEAR(e.cl_start - e.crd_end, 8 * V8B_CL_GAP_MS, 80);
    printf("  CL begins %.1f ms after CRd ends; ACK(1) %.1f ms after the MS ends\n", (e.cl_start - e.crd_end) / 8.0,
           (e.ack_start - e.ms_end) / 8.0);
    ncl = v8bis_cl_octets(V8B_NS_DEFAULT, V8B_VERS_BOTH, cl);
    CHECK_EQ_I(cl[13], 0x43);               /* versions 1 and 2 */
    CHECK_EQ_I(B.ms_ver, 0x03); CHECK_EQ_I(B.cl_sent, 1);
    nf = decode(e.cl_start, e.cl_end, fr, len, 4);
    CHECK_EQ_I(nf, 1);
    CHECK_EQ_I(len[0], ncl);
    CHECK(nf == 1 && memcmp(fr[0], cl, (size_t)ncl) == 0);
    CHECK_NEAR(e.ack_start - e.ms_end, 8 * V8B_ACK_GAP_MS, 80);
    nf = decode(e.ack_start, e.ack_end, fr, len, 4);
    CHECK_EQ_I(nf, 1);
    CHECK(nf == 1 && len[0] == 1 && fr[0][0] == 0x14);
    CHECK_EQ_I(e.result, V8B_RES_AGREED);
    CHECK_EQ_I(e.done, e.ack_end);
    /* our transmitter is quiet between the CL and the ACK(1) */
    for (n = e.cl_end, ok = 1; n < e.ack_start; n++) if (txs[n] != 0) ok = 0;
    CHECK(ok);

    /* no CRd: done 1000 ms after CRe, nothing sent after it */
    s.crd_ms = -1;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_NO_CRD);
    CHECK_EQ_I(e.done - e.cre_end, 8L * V8B_CRD_DEADLINE_MS);
    CHECK_EQ_I(e.cl_start, -1);
    for (n = e.cre_end, ok = 1; n <= e.done; n++) if (txs[n] != 0) ok = 0;
    CHECK(ok);
    /* a CRd that begins 990 ms after CRe is received to its end; at 1050 ms, not */
    s.crd_ms = 990;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_AGREED);
    CHECK_NEAR(e.cl_start - e.crd_end, 8 * V8B_CL_GAP_MS, 80);
    s.crd_ms = 1050;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_NO_CRD);
    CHECK_EQ_I(e.done - e.cre_end, 8L * V8B_CRD_DEADLINE_MS);

    /* CRd, no MS: done 1500 ms after our CL, nothing sent after it */
    s.crd_ms = 175; s.ms = MS_NONE;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_NO_MS);
    CHECK_EQ_I(e.done - e.cl_end, 8L * V8B_MS_WAIT_MS);
    for (n = e.cl_end, ok = 1; n <= e.done; n++) if (txs[n] != 0) ok = 0;
    CHECK(ok);
    /* an MS without the block: no ACK(1), done when it ends */
    s.ms = MS_PLAIN;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_NO_MS);
    CHECK_EQ_I(e.ack_start, -1);
    CHECK(e.done >= e.ms_end && e.done - e.ms_end < 8 * 20);
    for (n = e.cl_end, ok = 1; n <= e.done; n++) if (txs[n] != 0) ok = 0;
    CHECK(ok);
    /* an MS with a bad FCS: NAK(1), not ACK(1) (7.2.9, 9.8; ruling C6) */
    s.ms = MS_BADFCS;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_NO_MS);
    CHECK_NEAR(e.ack_start - e.ms_end, 8 * V8B_ACK_GAP_MS, 80);
    nf = decode(e.ack_start, e.ack_end, fr, len, 4);
    CHECK_EQ_I(nf, 1);
    CHECK(nf == 1 && len[0] == 1 && fr[0][0] == 0x18);

    /* K56flex versions (docs/history/k56-facts.md "Versions"). Its MS
     * selecting version 1 alone (0x01; 0x21 with the client's flag, as the
     * WebTV Plus would send it): NAK(3), ACK_GAP after it, then done */
    {
        static const int v1[2] = { 0x01, 0x21 };
        int j;
        s.ms = MS_SM56; s.ms_gap_ms = 10;
        for (j = 0; j < 2; j++) {
            s.ms_ver = v1[j];
            run(&s, &e);
            CHECK_EQ_I(e.result, V8B_RES_DECLINED); CHECK_EQ_I(B.ms_ver, v1[j]);
            CHECK_NEAR(e.ack_start - e.ms_end, 8 * V8B_ACK_GAP_MS, 80);
            nf = decode(e.ack_start, e.ack_end, fr, len, 4);
            CHECK_EQ_I(nf, 1);
            CHECK(nf == 1 && len[0] == 1 && fr[0][0] == 0x1a);
            CHECK_EQ_I(e.done, e.ack_end);
        }
        printf("  version 1 selected: NAK(3) %.1f ms after the MS ends\n", (e.ack_start - e.ms_end) / 8.0);
        /* accept_v1: ACK(1) */
        s.ms_ver = 0x01; s.accept_v1 = 1;
        run(&s, &e);
        CHECK_EQ_I(e.result, V8B_RES_AGREED);
        nf = decode(e.ack_start, e.ack_end, fr, len, 4);
        CHECK(nf == 1 && len[0] == 1 && fr[0][0] == 0x14);
        s.accept_v1 = 0;
        /* version 2 (0x02, the real client's), no version bit (0x00 as 0x40
         * alone): ACK(1), as before 0.7.2 */
        s.ms_ver = 0x02;
        run(&s, &e);
        CHECK_EQ_I(e.result, V8B_RES_AGREED);
        s.ms_ver = 0x40;
        run(&s, &e);
        CHECK_EQ_I(e.result, V8B_RES_AGREED); CHECK_EQ_I(v8bis_version_of(B.ms_ver), 0);
        /* version 2 alone offered (0.7.1's CL, 0x42): SM56's own MS (0x21)
         * acknowledged as it was */
        s.vers = V8B_VERS_2; s.ms_ver = 0x21;
        run(&s, &e);
        CHECK_EQ_I(e.result, V8B_RES_AGREED);
        nf = decode(e.cl_start, e.cl_end, fr, len, 4);
        CHECK(nf == 1 && len[0] == 16 && fr[0][13] == 0x42);
        nf = decode(e.ack_start, e.ack_end, fr, len, 4);
        CHECK(nf == 1 && len[0] == 1 && fr[0][0] == 0x14);
        s.ms_ver = 0x01;
        run(&s, &e);
        CHECK_EQ_I(e.result, V8B_RES_AGREED);
        s.vers = 0; s.ms_ver = 0;
    }

    /* the MS on a clock 0.1 % off, noise 40 dB below it */
    s.ms = MS_SM56; s.fs_far = FS * 1000 / 1001; s.noise = 3000 / sqrt(2.0) / 100;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_AGREED);
    s.fs_far = FS * 1001 / 1000;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_AGREED);
    /* the real client's gap: its MS 220 ms after our CL */
    s.fs_far = 0; s.noise = 0; s.ms_gap_ms = 220;
    run(&s, &e);
    CHECK_EQ_I(e.result, V8B_RES_AGREED);

    T_REPORT("test_v8bis");
}
