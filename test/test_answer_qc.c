/* V.92 Quick Connect on the answering side: short Phase 1 (9.2.4.1,
 * 9.2.4.3) in the answering modem (modem/answer, ANS_QC: modem/qcd), at its
 * G.711 interface, against the simulated QC caller (sim/v92call with cfg.qc:
 * QC1a ahead of its CM, then sim/qca's QCA1d reader and its own QTS and
 * ANSpcm checks, TONEq, Phase 2), PRBS data both ways:
 *   - qc_full: QCA1d (P 1, LM 01: 0x87), 75 ms of silence, QTS and QTS\,
 *     ANSpcm until TONEq, 75 ms, Phase 2 (full), V.92 data; the 6-symbol
 *     data frame grid from QTS's first symbol (8.3.6) and V.42's ODP/ADP
 *     bypassed by the two P bits (9.2.5);
 *   - lapm_off (P 0), v90_choice (V.34 upstream: V.90's Phase 3 on the
 *     grid), retrain (the grid kept), alaw;
 *   - Review Focus 2: no TONEq, a caller deaf to QCA1d (its CM's 980 Hz
 *     marks are not TONEq), TONEq late (1.90 s: taken) and too late (2.10
 *     s): ANSam again 2 s after QCA1d, V.8, V.92 data;
 *   - v92_off and ANS_MODE_NO_QC: the QC1a heard, not taken, JM;
 *   - k56_clears_qc: an agreed K56flex client is never offered QC.
 * Times are samples at our port (our tick index); the caller's (C.*_at)
 * are its own, 2 later for what we send (PORT). */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../modem/answer.h"
#include "../modem/qc.h"
#include "../modem/qcd.h"
#include "../v90/v90_pcm.h"
#include "../sim/v92call.h"
#include "../sim/k56call.h"
#include "../sim/g711.h"

/* x^7 + x^6 + 1 sent; the receiver predicts each bit from the seven before */
typedef struct { unsigned tx, rx; long n, bad; } prbs_t;
static int p_get(void *u)
{
    prbs_t *p = (prbs_t *)u;
    int b = ((p->tx >> 6) ^ (p->tx >> 5)) & 1;
    p->tx = ((p->tx << 1) | (unsigned)b) & 127;
    return b;
}
static void p_put(void *u, int bit)
{
    prbs_t *p = (prbs_t *)u;
    int want = ((p->rx >> 6) ^ (p->rx >> 5)) & 1;
    if (p->n >= 7 && want != (bit & 1)) p->bad++;
    p->rx = ((p->rx << 1) | (unsigned)(bit & 1)) & 127;
    p->n++;
}

#define BASE (ANS_MODE_V90 | V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21)
#define V92  (BASE | ANS_MODE_V92)
#define RECN (40 * 8000)
#define PORT 2              /* our octet of tick i is the caller's sample i + 2 */
#define ALLF (V8A_QC_HEARD | V8A_QC_ACCEPTED | V8A_QC_TONEQ | V8A_QC_GRID | V8A_QC_BYPASS)
#ifdef TEST_SHORT
#define DATA_S 1
#else
#define DATA_S 3
#endif

static answer_t A; static v92call_t C; static prbs_t UA, UC;
static uint8_t atx;
static long now;
static uint8_t rec[RECN];   /* what we sent, by tick */

typedef struct {
    unsigned modes; int alaw;
    int lapm, pcm_up, deaf, no_toneq, toneq_delay_ms;
} qscen;

/* what the run saw, by tick (-1 not seen) */
static struct {
    long pre_end;           /* the caller's QC1a (its prefix) fully sent */
    long hand;              /* V.8 handed over to ANS_QC */
    long qca_last;          /* QCA1d's last sample */
    long qts;               /* QTS's first */
    long back;              /* ANS_QC gave up: ANSam from the next tick */
    long toneq;             /* ANS_QC took TONEq */
    int  odp_at_back;       /* answer_odp_bypass on the tick of the fallback */
} W;

static void scen(qscen *s, unsigned modes)
{
    memset(s, 0, sizeof *s);
    s->modes = modes; s->lapm = 1; s->pcm_up = 1;
}

static void start(const qscen *s)
{
    answer_cfg ac; v92call_cfg cc;
    memset(&UA, 0, sizeof UA); memset(&UC, 0, sizeof UC); UA.tx = 1; UC.tx = 77;
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = s->modes; ac.getbit = p_get; ac.putbit = p_put; ac.user = &UA;
    answer_init(&A, &ac);
    if (s->alaw) { p2d_cfg pc = A.p2cfg; pc.alaw = 1; answer_v90_cfg(&A, &pc); }
    answer_v90_line(&A, -1, 0, 0);
    memset(&cc, 0, sizeof cc);
    cc.alaw = s->alaw; cc.v92 = 1; cc.pcm_up = s->pcm_up;
    cc.lapm = s->lapm; cc.qc_lapm = s->lapm;        /* one P for the call (F5) */
    cc.qc = 1; cc.qc_wxyz = 5; cc.short_p2 = 0;
    cc.qc_deaf = s->deaf; cc.qc_no_toneq = s->no_toneq; cc.toneq_delay_ms = s->toneq_delay_ms;
    cc.echo_gain = 0.1; cc.echo_delay = 21;
    cc.getbit = p_get; cc.putbit = p_put; cc.user = &UC;
    v92call_init(&C, &cc);
    atx = v90_pcm_octet(s->alaw, 0, 1); now = 0;
    memset(&W, 0xff, sizeof W);
}

static void run(long samples)
{
    while (samples-- > 0) {
        ans_phase was = A.phase;
        int before = C.v8.ppos;
        uint8_t from_c = v92call_tick(&C, atx);
        if (before < C.v8.npre && C.v8.ppos == C.v8.npre && W.pre_end < 0) W.pre_end = now;
        atx = answer_tick_pcm(&A, from_c);
        if (now < RECN) rec[now] = atx;
        if (A.phase == ANS_QC && was != ANS_QC) W.hand = now;
        if (A.phase == ANS_QC || was == ANS_QC) {
            if (A.u.qc.state != QCD_QCA && W.qca_last < 0) W.qca_last = now;
            if (A.u.qc.qts_first) W.qts = now;
        }
        if (was == ANS_QC && A.phase == ANS_V8) { W.back = now; W.odp_at_back = answer_odp_bypass(&A); }
        if (was == ANS_QC && A.phase == ANS_P2) W.toneq = now;
        now++;
    }
}

static int moving(void)
{
    if (!A.connected || A.retraining || !v92call_data(&C)) return 0;
    if (A.phase == ANS_V92) return A.u.v92.state == V92D_DATA;
    return A.phase == ANS_V90 && A.p34.state == P34_DATA;
}

static int to_data(int max_ms)
{
    long end = now + 8L * max_ms;
    while (now < end) {
        run(8);
        if (moving()) return 1;
        if (A.phase == ANS_FAILED || C.stage == VCL_FAILED || C.hung) return 0;
    }
    return 0;
}

static void data_ok(const char *what, int mod)
{
    run(400);
    UA.n = UA.bad = UC.n = UC.bad = 0;
    run(8000L * DATA_S);
    CHECK_EQ_I(A.connected, 1); CHECK_EQ_I(A.retraining, 0);
    CHECK_EQ_I(A.modulation, mod);
    CHECK(UA.n > (DATA_S - 1) * (long)A.rx_bitrate + A.rx_bitrate / 2);
    CHECK(UC.n > (DATA_S - 1) * (long)A.bitrate + A.bitrate / 2);
    CHECK_EQ_I(UA.bad, 0); CHECK_EQ_I(UC.bad, 0);
    printf("  %-30s %s: %d bit/s down, %d bit/s up; %ld / %ld bits, %ld / %ld bad\n",
           what, mod == ANS_MOD_V92 ? "V.92" : "V.90", A.bitrate, A.rx_bitrate, UC.n, UA.n, UC.bad, UA.bad);
}

static int tq_ms(void) { return A.v8.qc.tq_lo | A.v8.qc.tq_hi << 8; }

/* the grid: Sd of each Phase 3 on an interval 0 of QTS's */
static void grid_ok(int n)
{
    int i;
    CHECK_EQ_I(C.n_sd, n);
    for (i = 0; i < n && i < 4; i++) {
        CHECK(C.sd_at[i] > C.qts_at);
        CHECK_EQ_I((C.sd_at[i] - C.qts_at) % 6, 0);
    }
}

/* short Phase 1 as it went: QCA1d after QC1a, its 600 samples of silence,
 * QTS and ANSpcm the caller's own, TONEq, 75 ms, INFO0d */
static void phase1_ok(const char *what, int alaw, int lapm)
{
    uint8_t sil = v90_pcm_octet(alaw, 0, 1);
    long k, quiet = 0;
    CHECK(W.pre_end > 0 && W.hand > W.pre_end);
    CHECK(W.hand + 1 - W.pre_end <= 250);
    CHECK_EQ_I(C.qca1d, lapm ? 0x87 : 0x83);
    CHECK_EQ_I(C.qca1d, qca1d_octet(1, lapm));
    CHECK(W.qca_last > W.hand && W.qts > W.qca_last);
    CHECK_EQ_I(W.qts - W.qca_last - 1, 600);
    for (k = W.qca_last + 1; k < W.qts; k++) quiet += rec[k] == sil;
    CHECK_EQ_I(quiet, 600);
    CHECK_EQ_I(C.qts_at, W.qts + PORT);
    CHECK_EQ_I(C.qts_ok, 1); CHECK_EQ_I(C.anspcm_ok, 1);
    CHECK(C.toneq_off > 0 && C.info0d_at > C.toneq_off);
    CHECK(C.info0d_at - C.toneq_off >= 560 && C.info0d_at - C.toneq_off <= 640);
    CHECK_EQ_I(C.d_v92, 1);
    CHECK(tq_ms() > 0 && tq_ms() <= 2000);
    CHECK_EQ_I(C.qc_fallbacks, 0);
    printf("  %-30s QCA1d 0x%02X %ld samples after QC1a, 600 silent, QTS at %.3f s, %ld ANSpcm codewords, "
           "TONEq at %d ms, INFO0d %ld samples after ANSpcm\n",
           what, C.qca1d, W.hand + 1 - W.pre_end, W.qts / 8000.0, C.anspcm_n, tq_ms(), C.info0d_at - C.toneq_off);
}

static void qc_full(int alaw)
{
    qscen s;
    const char *name = alaw ? "alaw" : "qc_full";
    printf("%s\n", name);
    scen(&s, V92); s.alaw = alaw;
    start(&s);
    CHECK(A.v8.qc.aux & V8A_QCX_OFFER);
    CHECK_EQ_I(to_data(30000), 1);
    phase1_ok(name, alaw, 1);
    CHECK_EQ_I(A.v8.qc.flags, ALLF);
    CHECK_EQ_I((A.v8.qc.aux >> V8A_QCX_LM_SHIFT) & 3, 1);
    CHECK_EQ_I(A.v8.selected, V8_SEL_V90); CHECK_EQ_I(A.used_v8, 1);
    CHECK_EQ_I(A.phase, ANS_V92); CHECK_EQ_I(A.modulation, ANS_MOD_V92);
    CHECK_EQ_I(answer_odp_bypass(&A), 1);
    CHECK_EQ_I(answer_hst_decided(&A), 1);
    grid_ok(1);
    data_ok(name, ANS_MOD_V92);
}

static void lapm_off(void)
{
    qscen s;
    printf("lapm_off\n");
    scen(&s, V92); s.lapm = 0;
    start(&s);
    CHECK_EQ_I(to_data(30000), 1);
    phase1_ok("lapm_off", 0, 0);
    CHECK_EQ_I(A.v8.qc.flags, ALLF & ~V8A_QC_BYPASS);
    CHECK_EQ_I(answer_odp_bypass(&A), 0);
    grid_ok(1);
    data_ok("lapm_off", ANS_MOD_V92);
}

static void v90_choice(void)
{
    qscen s;
    printf("v90_choice\n");
    scen(&s, V92); s.pcm_up = 0;
    start(&s);
    CHECK_EQ_I(to_data(30000), 1);
    phase1_ok("v90_choice", 0, 1);
    CHECK_EQ_I(A.phase, ANS_V90);
    CHECK_EQ_I(C.v90_starts, 1); CHECK_EQ_I(C.pcm_starts, 0);
    grid_ok(1);
    data_ok("v90_choice", ANS_MOD_V90);
}

static void retrain(void)
{
    qscen s;
    printf("retrain\n");
    scen(&s, V92);
    start(&s);
    CHECK_EQ_I(to_data(30000), 1);
    data_ok("before the caller's retrain", ANS_MOD_V92);
    v92call_retrain(&C);
    CHECK_EQ_I(to_data(30000), 1);
    CHECK_EQ_I(A.retrains, 1);
    CHECK(A.v8.qc.flags & V8A_QC_GRID);
    grid_ok(2);
    printf("  Sd of both Phase 3s %ld and %ld samples after QTS (both multiples of 6)\n",
           C.sd_at[0] - C.qts_at, C.sd_at[1] - C.qts_at);
    data_ok("after it", ANS_MOD_V92);
}

/* Review Focus 2: ANSam again 2 s after QCA1d, then V.8 and data */
static void fallback(const char *name, int deaf, int no_toneq, int delay_ms)
{
    qscen s;
    printf("%s\n", name);
    scen(&s, V92); s.deaf = deaf; s.no_toneq = no_toneq; s.toneq_delay_ms = delay_ms;
    start(&s);
    CHECK_EQ_I(to_data(40000), 1);
    CHECK(W.qca_last > 0);
    CHECK_EQ_I(W.back + 1 - (W.qca_last + 1), 16000);
    CHECK_EQ_I(W.toneq, -1);
    CHECK(A.v8.qc.flags & V8A_QC_FALLBACK);
    CHECK(A.v8.qc.flags & V8A_QC_ACCEPTED);
    CHECK_EQ_I(A.v8.qc.flags & V8A_QC_GRID, 0);
    CHECK_EQ_I(A.v8.qc.aux & V8A_QCX_OFFER, 0);
    CHECK_EQ_I(W.odp_at_back, 0);               /* the P bits' bypass is gone with QC */
    if (deaf) CHECK_EQ_I(A.v8.qc.flags & V8A_QC_TONEQ, 0);
    CHECK_EQ_I(A.v8.result, V8A_RES_OK);        /* JM to its CM */
    CHECK_EQ_I(C.qc_fallbacks, deaf ? 0 : 1);
    CHECK_EQ_I(A.v8.selected, V8_SEL_V90);
    CHECK_EQ_I(answer_hst_decided(&A), 1);
    /* V.8's own: both LAPM, V.92 at both ends (9.3.1/V.92), as 0.7.3 */
    CHECK_EQ_I(answer_odp_bypass(&A), 1);
    printf("  ANSam again %ld samples after QCA1d's end; JM, Phase 2; flags 0x%02X\n",
           W.back + 1 - (W.qca_last + 1), A.v8.qc.flags);
    data_ok(name, ANS_MOD_V92);
}

static void late_toneq(void)
{
    qscen s;
    printf("late_toneq\n");
    /* ANSpcm starts 1416 samples after QCA1d's end at our port */
    scen(&s, V92); s.toneq_delay_ms = 1900 - 1416 / 8;
    start(&s);
    CHECK_EQ_I(to_data(30000), 1);
    CHECK(W.toneq > W.qca_last + 16000);       /* taken past the 2 s: a run begun before it */
    CHECK_EQ_I(W.back, -1);
    CHECK_EQ_I(A.v8.qc.flags, ALLF);
    CHECK(tq_ms() >= 1895 && tq_ms() <= 1915);
    printf("  TONEq's run from %d ms after QCA1d, taken %ld samples after QCA1d's end\n",
           tq_ms(), W.toneq - W.qca_last);
    grid_ok(1);
    data_ok("late_toneq", ANS_MOD_V92);
}

/* not taken: the QC1a heard, the CM's JM */
static void not_taken(const char *name, unsigned modes, int mod)
{
    qscen s;
    printf("%s\n", name);
    scen(&s, modes);
    start(&s);
    CHECK_EQ_I(A.v8.qc.aux & V8A_QCX_OFFER, 0);
    CHECK_EQ_I(to_data(30000), 1);
    CHECK_EQ_I(W.hand, -1);
    CHECK(A.v8.qc.flags & V8A_QC_HEARD);
    CHECK_EQ_I(A.v8.qc.flags & ~V8A_QC_HEARD, 0);
    CHECK_EQ_I(A.v8.qc.oct, 0xA4);
    CHECK_EQ_I(A.v8.result, V8A_RES_OK);
    CHECK_EQ_I(C.qca1d, 0);
    CHECK_EQ_I(C.qts_at, -1);
    data_ok(name, mod);
}

/* K56flex agreed: QC is not offered in the call (an agreed client is not a
 * V.92 caller); not agreed (no V.8bis answer): still offered */
static void k56_clears_qc(void)
{
    static k56call_t K;
    int v8bis;
    printf("k56_clears_qc\n");
    for (v8bis = 0; v8bis < 2; v8bis++) {
        answer_cfg ac; k56call_cfg kc;
        uint8_t arx = 0xff;
        long n;
        int offered0;
        memset(&ac, 0, sizeof ac);
        ac.level = 3000; ac.modes = V92 | ANS_MODE_K56; ac.getbit = p_get; ac.putbit = p_put; ac.user = &UA;
        answer_init(&A, &ac);
        offered0 = (A.v8.qc.aux & V8A_QCX_OFFER) != 0;
        k56call_default(&kc, 3000);
        kc.v8bis = v8bis ? K56C_V8BIS_NONE : K56C_V8BIS_FULL;
        kc.getbit = p_get; kc.putbit = p_put; kc.user = &UC;
        k56call_init(&K, &kc);
        for (n = 0; n < 8000L * 10 && A.phase == ANS_V8BIS; n++)
            arx = linear_to_ulaw(k56call_tick(&K, answer_tick_pcm(&A, arx)) / 2);
        CHECK_EQ_I(offered0, 1);
        CHECK_EQ_I(A.phase, ANS_V8);
        CHECK_EQ_I(ANS_K56_AGREED_OF(&A), !v8bis);
        CHECK_EQ_I((A.v8.qc.aux & V8A_QCX_OFFER) != 0, v8bis);
        printf("  K56flex %s: QC %s\n", v8bis ? "not agreed (no V.8bis answer)" : "agreed (SM56's MS)",
               (A.v8.qc.aux & V8A_QCX_OFFER) ? "still offered" : "no longer offered");
    }
}

int main(void)
{
    qc_full(0);
    lapm_off();
    v90_choice();
    retrain();
    fallback("no_toneq", 0, 1, 0);
    fallback("deaf", 1, 0, 0);
    late_toneq();
    fallback("too_late", 0, 0, 2100 - 1416 / 8);
    qc_full(1);
    not_taken("v92_off", BASE, ANS_MOD_V90);
    not_taken("no_qc_option", V92 | ANS_MODE_NO_QC, ANS_MOD_V92);
    k56_clears_qc();
    T_REPORT("test_answer_qc");
}
