/* V.92's QC1a heard during ANSam (modem/v8rx v8rx_tick_qc, modem/v8ans):
 * our answer side against sim/v8call with a prefix (sim/qca's QC1a, or other
 * bits) ahead of its CM, through the PSTN channel simulator (mu-law both
 * ways). Not acted on beyond V8A_RES_QC: nothing in modem/answer.c offers QC
 * yet.
 *
 * Review Focus 1 of the plan (docs/superpowers/plans/2026-11-26-v92-qc.md):
 * a caller that sends no QC1a must not be taken for one. For each such case
 * the run with QC offered and the run without send the same samples, sample
 * for sample, to V8A_DONE, with the same result and V8A_QC_HEARD clear. */
#include <string.h>
#include "test.h"
#include "../modem/v8ans.h"
#include "../modem/qc.h"
#include "../sim/v8call.h"
#include "../sim/qca.h"
#include "../sim/channel.h"
#include "../sim/g711.h"

#define FS 8000
#define AMP 5600                /* about -12 dBm0 */
#define MAXN (12 * FS)

typedef struct {
    const v8_menu_t *cm;        /* the caller's CM; NULL: no caller */
    int16_t amp;                /* the caller's level (peak) */
    const uint8_t *pre; int npre;   /* v8call_prefix */
    int late;                   /* samples the prefix goes out later than 1 s after ANSam */
    int offer;                  /* v8ans_offer_qc */
    double noise_dbm0;          /* both ways */
    double echo; int echo_delay;
    uint32_t seed;
    int max_s;
} runcfg;

typedef struct {
    int result, done_at, pre_end, ntx;
    v8a_qc_t qc;
    v8_menu_t jm;
    uint8_t jmoct[V8_MAX_OCTETS]; int njmoct;
    int njm;
} outcome;

static int16_t txbuf[2][MAXN];

static void defcfg(runcfg *c, const v8_menu_t *cm)
{
    memset(c, 0, sizeof *c);
    c->cm = cm; c->amp = AMP; c->noise_dbm0 = -60.0; c->echo = 0.2; c->echo_delay = 37;
    c->seed = 4242; c->max_s = 15;
}

static void run(const runcfg *rc, const v8_menu_t *local, int slot, outcome *o)
{
    static const double one[1] = {1.0};
    channel_cfg cfg; channel_t ch;
    v8ans_t ans; v8call_t call;
    int16_t atx = 0, ctx = 0, arx, crx;
    int n;

    memset(&cfg, 0, sizeof cfg);
    /* dBm0 to the simulator's dBFS: full scale 32124 is +3.14 dBm0 */
    cfg.ds_pad_db = 0; cfg.ds_noise_dbfs = rc->noise_dbm0 - 3.14; cfg.us_noise_dbfs = rc->noise_dbm0 - 3.14;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.echo_gain = rc->echo; cfg.echo_delay = rc->echo_delay; cfg.full_scale = 32124.0; cfg.seed = rc->seed;
    channel_init(&ch, &cfg);
    v8ans_init(&ans, FS, AMP, local);
    if (rc->offer) v8ans_offer_qc(&ans, 1);
    if (rc->cm) {
        v8call_init(&call, FS, rc->amp, rc->cm);
        if (rc->npre) { v8call_prefix(&call, rc->pre, rc->npre); call.te += rc->late; }
    }
    memset(o, 0, sizeof *o);
    o->done_at = o->pre_end = -1;
    for (n = 0; n < rc->max_s * FS; n++) {
        double client_rx; uint8_t srv;
        channel_tick(&ch, linear_to_ulaw(atx), (double)ctx, &client_rx, &srv);
        arx = (int16_t)ulaw_to_linear(srv);
        crx = (int16_t)lround(client_rx);
        if (rc->cm) {
            int before = call.ppos;
            v8call_tick(&call, crx, &ctx);
            if (rc->npre && before < call.npre && call.ppos == call.npre) o->pre_end = n;
        }
        if (v8ans_tick(&ans, arx, &atx) == V8A_DONE) { o->done_at = n; break; }
        if (n < MAXN) txbuf[slot][n] = atx;
    }
    o->ntx = n < MAXN ? n : MAXN;
    o->result = ans.result; o->qc = ans.qc; o->jm = ans.jm; o->njm = ans.njm;
    o->njmoct = ans.result == V8A_RES_OK ? v8_encode(&ans.jm, o->jmoct, V8_MAX_OCTETS) : 0;
}

static int same_jm(const outcome *a, const outcome *b)
{
    return a->njmoct > 0 && a->njmoct == b->njmoct && !memcmp(a->jmoct, b->jmoct, (size_t)a->njmoct)
           && a->njm == b->njm;
}

/* Review Focus 1: offered and not, the same samples to V8A_DONE, the same
 * result, nothing heard. Returns the result. */
static int same_both_ways(runcfg *rc, const v8_menu_t *local)
{
    outcome a, b;
    int same;
    rc->offer = 0; run(rc, local, 0, &a);
    rc->offer = 1; run(rc, local, 1, &b);
    same = a.done_at >= 0 && a.done_at == b.done_at && a.ntx == b.ntx
           && !memcmp(txbuf[0], txbuf[1], (size_t)a.ntx * sizeof txbuf[0][0]);
    CHECK(same);
    CHECK_EQ_I(b.result, a.result);
    CHECK_EQ_I(a.qc.flags & V8A_QC_HEARD, 0);
    CHECK_EQ_I(b.qc.flags & V8A_QC_HEARD, 0);
    CHECK_EQ_I(b.qc.aux & V8A_QCX_ONHOLD, 0);
    if (a.result == V8A_RES_OK) CHECK(same_jm(&a, &b));
    return a.result;
}

static void menus(v8_menu_t *local, v8_menu_t *cm)
{
    memset(local, 0, sizeof *local);
    local->call_function = V8_CALLF_DATA;
    local->modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21;
    local->pcm = V8_PCM_DIGITAL;
    local->prot = V8_PROT_LAPM;
    local->access = V8_ACC_DIGITAL;
    memset(cm, 0, sizeof *cm);
    cm->call_function = V8_CALLF_DATA;
    cm->modes = V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21;
    cm->n_mod_octets = 3;
    cm->has_pcm = 1; cm->pcm = V8_PCM_ANALOG;
    cm->has_access = 1;
    cm->has_prot = 1; cm->prot = V8_PROT_LAPM;
}

/* the octet Table 2 gives (Task 2's rule): P bit 2, W 3, X 5, Y 6, Z 7 */
static int table2_octet(int wxyz, int p)
{
    return p << 2 | ((wxyz >> 3) & 1) << 3 | ((wxyz >> 2) & 1) << 5 | ((wxyz >> 1) & 1) << 6 | (wxyz & 1) << 7;
}

/* a start-stop character, bit 0 first: start 0, eight bits, stop 1 */
static int put_char(uint8_t *b, int n, int c)
{
    int i;
    b[n++] = 0;
    for (i = 0; i < 8; i++) b[n++] = (uint8_t)((c >> i) & 1);
    b[n++] = 1;
    return n;
}
static int put_marks(uint8_t *b, int n, int k) { while (k-- > 0) b[n++] = 1; return n; }

static void test_qc_ok(const v8_menu_t *local, const v8_menu_t *cm)
{
    uint8_t q[64]; runcfg rc; outcome o, plain;
    int late;
    printf("qc_ok\n");
    defcfg(&rc, cm); rc.echo = 0; run(&rc, local, 0, &plain);
    for (late = 0; late <= 2000; late += 2000) {     /* 1.0 s (V.92 9.2.1.1) and 1.25 s (BinModem) */
        defcfg(&rc, cm);
        rc.pre = q; rc.npre = qca_qc1a_bits(5, 1, q); rc.late = late; rc.offer = 1; rc.echo = 0;
        run(&rc, local, 0, &o);
        CHECK_EQ_I(o.result, V8A_RES_QC);
        CHECK_EQ_I(o.qc.oct, 0xA4);
        CHECK(o.qc.flags & V8A_QC_HEARD);
        CHECK_EQ_I(o.qc.flags & ~V8A_QC_HEARD, 0);
        CHECK(o.pre_end > 0 && o.done_at > o.pre_end && o.done_at - o.pre_end <= 240);
        printf("  QC1a %d ms after ANSam: V8A_DONE %d samples after its last stop bit was sent\n",
               1000 + late / 8, o.done_at - o.pre_end);
        /* not offered: heard, and the CM behind it decides */
        rc.offer = 0;
        run(&rc, local, 0, &o);
        CHECK_EQ_I(o.result, V8A_RES_OK);
        CHECK(o.qc.flags & V8A_QC_HEARD);
        CHECK_EQ_I(o.qc.oct, 0xA4);
        CHECK(same_jm(&o, &plain));
    }
}

/* WXYZ 0111 P 0: the octet 0xE0, V.8's CM/JM sync octet; not offered:
 * heard, and the CM decides */
static void test_e0_not_offered(const v8_menu_t *local, const v8_menu_t *cm)
{
    uint8_t q[64]; runcfg rc; outcome o, plain;
    printf("e0_not_offered\n");
    defcfg(&rc, cm); run(&rc, local, 0, &plain);
    defcfg(&rc, cm);
    rc.pre = q; rc.npre = qca_qc1a_bits(7, 0, q); rc.offer = 0;
    run(&rc, local, 0, &o);
    CHECK_EQ_I(o.result, V8A_RES_OK);
    CHECK(o.qc.flags & V8A_QC_HEARD);
    CHECK_EQ_I(o.qc.flags & ~V8A_QC_HEARD, 0);
    CHECK_EQ_I(o.qc.oct, 0xE0);
    CHECK(same_jm(&o, &plain));
    printf("  QC1a 0xE0 not offered: heard, the CM's JM\n");
}

/* v8ans_restart_ansam after V8A_RES_QC (9.2.4.3): ANSam from the next
 * sample, the QC1a no longer taken, the CM's JM as for any caller */
static void test_restart(const v8_menu_t *local, const v8_menu_t *cm)
{
    static const double one[1] = {1.0};
    uint8_t q[64];
    channel_cfg cfg; channel_t ch;
    v8ans_t ans; v8call_t call;
    int16_t atx = 0, ctx = 0;
    int n, qc_at = -1, back = 0, loud = 0;
    printf("restart\n");
    memset(&cfg, 0, sizeof cfg);
    cfg.ds_noise_dbfs = cfg.us_noise_dbfs = -63.14;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.full_scale = 32124.0; cfg.seed = 4242;
    channel_init(&ch, &cfg);
    v8ans_init(&ans, FS, AMP, local);
    v8ans_offer_qc(&ans, 1);
    v8call_init(&call, FS, AMP, cm);
    v8call_prefix(&call, q, qca_qc1a_bits(5, 1, q));
    for (n = 0; n < 15 * FS; n++) {
        double crx; uint8_t srv;
        channel_tick(&ch, linear_to_ulaw(atx), (double)ctx, &crx, &srv);
        v8call_tick(&call, (int16_t)lround(crx), &ctx);
        if (v8ans_tick(&ans, (int16_t)ulaw_to_linear(srv), &atx) == V8A_DONE) {
            if (qc_at >= 0) break;
            CHECK_EQ_I(ans.result, V8A_RES_QC);
            qc_at = n;
            v8ans_restart_ansam(&ans);
            CHECK_EQ_I(ans.state, V8A_ANSAM);
            CHECK(ans.qc.flags & V8A_QC_FALLBACK);
            CHECK_EQ_I(ans.qc.aux & (V8A_QCX_OFFER | V8A_QCX_TRACK), 0);
            CHECK_EQ_I(ans.result, V8A_RES_NONE);
            back = 1;
        } else if (back && n < qc_at + FS / 10 && (atx > 1000 || atx < -1000)) loud = 1;
    }
    CHECK(qc_at > 0);
    CHECK(loud);                                    /* ANSam again */
    CHECK_EQ_I(ans.result, V8A_RES_OK);
    CHECK_EQ_I(ans.selected, V8_SEL_V90);
    CHECK_EQ_I(ans.qc.flags, V8A_QC_HEARD | V8A_QC_FALLBACK);
    printf("  V8A_RES_QC at %.3f s, ANSam again, then the CM's JM: V.8 done at %.3f s\n", qc_at / 8000.0, n / 8000.0);
}

static void test_every_uqts(const v8_menu_t *local, const v8_menu_t *cm)
{
    uint8_t q[64]; runcfg rc; outcome o, plain;
    int w, p, ok = 0;
    printf("every_uqts\n");
    defcfg(&rc, cm); run(&rc, local, 0, &plain);
    CHECK_EQ_I(plain.result, V8A_RES_OK);
    for (w = 0; w < 16; w++)
        for (p = 0; p < 2; p++) {
            int want = table2_octet(w, p), wx = -1, lp = -1;
            defcfg(&rc, cm);
            rc.pre = q; rc.npre = qca_qc1a_bits(w, p, q); rc.offer = 1;
            run(&rc, local, 0, &o);
            CHECK_EQ_I(qc1a_parse((uint8_t)want, &wx, &lp), 0);
            CHECK_EQ_I(wx, w);
            CHECK(o.qc.flags & V8A_QC_HEARD);
            CHECK_EQ_I(o.qc.oct, want);
            if (w < 15) {
                CHECK_EQ_I(o.result, V8A_RES_QC);
                CHECK_EQ_I(o.qc.aux & V8A_QCX_ONHOLD, 0);
                ok += o.result == V8A_RES_QC && o.qc.oct == want;
            } else {                                    /* cleardown from on-hold: the CM decides */
                CHECK(o.qc.aux & V8A_QCX_ONHOLD);
                CHECK_EQ_I(o.result, V8A_RES_OK);
                CHECK(same_jm(&o, &plain));
                printf("  WXYZ 1111 P %d (octet 0x%02X): on-hold, ignored; the CM's JM\n", p, want);
            }
        }
    printf("  WXYZ 0..14 x P 0, 1: %d of 30 V8A_RES_QC with Table 2's octet (0xE0, the CM sync, among them)\n", ok);
}

static void test_cm_only(const v8_menu_t *local)
{
    static const int lev[4] = { -9, -26, -38, -43 };
    v8_menu_t m[8];
    runcfg rc;
    int i, k, okn = 0, taken = 0;
    printf("cm_only\n");
    for (i = 0; i < 8; i++) {
        memset(&m[i], 0, sizeof m[i]);
        m[i].call_function = V8_CALLF_DATA;
    }
    m[0].modes = V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21; m[0].n_mod_octets = 3;
    m[0].has_pcm = 1; m[0].pcm = V8_PCM_ANALOG; m[0].has_access = 1; m[0].has_prot = 1; m[0].prot = V8_PROT_LAPM;
    m[1] = m[0]; m[1].has_access = 0;
    m[2].modes = V8_MOD_V34; m[2].n_mod_octets = 1; m[2].has_prot = 1; m[2].prot = V8_PROT_LAPM;
    m[3] = m[2]; m[3].has_access = 1; m[3].access = V8_ACC_CALL_CELL;
    m[4].modes = V8_MOD_V32 | V8_MOD_V22; m[4].n_mod_octets = 2;
    m[5].modes = V8_MOD_V22; m[5].n_mod_octets = 2; m[5].has_prot = 1; m[5].prot = V8_PROT_LAPM;
    m[6].modes = V8_MOD_V21; m[6].n_mod_octets = 3;
    m[7].modes = 0x1FFE; m[7].n_mod_octets = 3; m[7].has_pcm = 1; m[7].pcm = V8_PCM_ANALOG | V8_PCM_V91;
    m[7].has_prot = 1; m[7].prot = V8_PROT_LAPM; m[7].has_access = 1; m[7].access = V8_ACC_CALL_CELL;
    for (i = 0; i < 8; i++)
        for (k = 0; k < 4; k++) {
            defcfg(&rc, &m[i]);
            rc.amp = (int16_t)lround(22300.0 * pow(10.0, lev[k] / 20.0));
            {
                int r = same_both_ways(&rc, local);
                okn += r == V8A_RES_OK;
                taken += r != V8A_RES_NO_CM;
            }
        }
    /* at -43 dBm0 the CM is not taken (V8A_RES_NO_CM) either way: the V.21
     * squelch's edge; at -38 the CM is taken (JM sent; under our ANSam's
     * echo the caller does not take our JM: V8A_RES_NO_CJ), so the QC1a
     * tracker has had its bits */
    CHECK_EQ_I(taken, 24);
    printf("  32 callers (8 CMs at -9, -26, -38, -43 dBm0): the same samples either way, %d CMs taken, %d V8A_RES_OK\n",
           taken, okn);
}

static void test_echo(const v8_menu_t *local, const v8_menu_t *cm)
{
    runcfg rc;
    printf("echo\n");
    defcfg(&rc, cm); rc.echo = 0.5; rc.echo_delay = 40;     /* -6 dB, 5 ms */
    CHECK_EQ_I(same_both_ways(&rc, local), V8A_RES_OK);
    printf("  ANSam echoed at -6 dB, 5 ms, under a CM: the same samples either way\n");
}

static void test_noise(const v8_menu_t *local)
{
    static const double lev[3] = { -30.0, -38.0, -46.0 };
    runcfg rc;
    int s, k, nocm = 0;
    printf("noise\n");
    for (k = 0; k < 3; k++)
        for (s = 0; s < 20; s++) {
            defcfg(&rc, NULL);
            rc.noise_dbm0 = lev[k]; rc.echo = 0; rc.seed = (uint32_t)(1000 + 17 * s); rc.max_s = 7;
            nocm += same_both_ways(&rc, local) == V8A_RES_NO_CM;
        }
    CHECK_EQ_I(nocm, 60);
    printf("  noise at -30, -38 and -46 dBm0, 20 seeds: the same samples either way, %d V8A_RES_NO_CM\n", nocm);
}

/* a prefix that is not a QC1a, ahead of the CM: no QC, the CM decides */
static void not_qc(const char *name, const uint8_t *b, int n, const v8_menu_t *local, const v8_menu_t *cm)
{
    runcfg rc;
    printf("%s\n", name);
    defcfg(&rc, cm); rc.pre = b; rc.npre = n;
    CHECK_EQ_I(same_both_ways(&rc, local), V8A_RES_OK);
    printf("  %d bits before the CM: the same samples either way, the CM's JM\n", n);
}

static void test_not_qc(const v8_menu_t *local, const v8_menu_t *cm)
{
    uint8_t b[64];
    int n, i;
    /* start-stop text UDUDUD, one stop bit between the characters */
    n = put_marks(b, 0, 4);
    for (i = 0; i < 6; i++) n = put_char(b, n, i & 1 ? 'D' : 'U');
    not_qc("typed", b, n, local, cm);
    /* QC1d: bit 21, a digital modem, set in both copies */
    n = qca_qc1a_bits(5, 1, b); b[21] = 1; b[51] = 1;
    not_qc("qc1d", b, n, local, cm);
    n = qca_qc1a_bits(5, 1, b);
    not_qc("one_copy", b, 30, local, cm);
    {   uint8_t c[64];
        (void)qca_qc1a_bits(6, 1, c);
        memcpy(b + 30, c + 30, 30);
        not_qc("two_differ", b, 60, local, cm); }
    /* an extra character after the first information character (the
     * copy's leading ONEs cut to 4: the silence before reads as mark) */
    n = put_marks(b, 0, 4);
    n = put_char(b, n, QC_SYNC); n = put_char(b, n, 0xA4); n = put_char(b, n, 0xA4);
    n = put_marks(b, n, 10); n = put_char(b, n, QC_SYNC); n = put_char(b, n, 0xA4);
    not_qc("extra_char", b, n, local, cm);
}

/* the one text that is a QC1a by Table 2: U, D, ten marks, U, D */
static void test_ud_pattern(const v8_menu_t *local, const v8_menu_t *cm)
{
    uint8_t b[64]; runcfg rc; outcome o;
    int n;
    printf("ud_pattern\n");
    n = put_marks(b, 0, 10);
    n = put_char(b, n, 'U'); n = put_char(b, n, 'D');
    n = put_marks(b, n, 10);
    n = put_char(b, n, 'U'); n = put_char(b, n, 'D');
    defcfg(&rc, cm); rc.pre = b; rc.npre = n; rc.offer = 1;
    run(&rc, local, 0, &o);
    CHECK_EQ_I(o.result, V8A_RES_QC);
    CHECK(o.qc.flags & V8A_QC_HEARD);
    CHECK_EQ_I(o.qc.oct, 'D');
    printf("  U D, ten marks, U D: heard as QC1a, octet 0x%02X (the residue, docs/limits.md)\n", o.qc.oct);
}

static void test_jm_fits(void)
{
    v8_menu_t local, cm, jm;
    uint8_t oct[V8_MAX_OCTETS];
    runcfg rc; outcome o;
    int n;
    printf("jm_fits\n");
    memset(&local, 0, sizeof local);
    local.call_function = V8_CALLF_DATA;
    local.modes = 0x1FFE;
    local.pcm = V8_PCM_ANALOG | V8_PCM_DIGITAL | V8_PCM_V91;
    local.prot = V8_PROT_LAPM;
    local.access = V8_ACC_CALL_CELL | V8_ACC_ANS_CELL | V8_ACC_DIGITAL;
    memset(&cm, 0, sizeof cm);
    cm.call_function = V8_CALLF_DATA;
    cm.modes = 0x1FFE; cm.n_mod_octets = 3;
    cm.has_pcm = 1; cm.pcm = V8_PCM_ANALOG | V8_PCM_DIGITAL | V8_PCM_V91;
    cm.has_prot = 1; cm.prot = V8_PROT_LAPM;
    cm.has_access = 1; cm.access = V8_ACC_CALL_CELL | V8_ACC_ANS_CELL | V8_ACC_DIGITAL;
    v8_build_jm(&cm, &local, &jm);
    n = v8_encode(&jm, oct, V8_MAX_OCTETS);
    CHECK(n > 0 && n <= 15);
    defcfg(&rc, &cm);
    run(&rc, &local, 0, &o);
    CHECK_EQ_I(o.result, V8A_RES_OK);
    CHECK(o.njm > 0 && o.njm <= V8A_MAXBITS);
    CHECK_EQ_I(o.njm, 20 + 10 * n);
    printf("  everything offered, everything in the menu: a JM of %d octets, %d bits (at most %d)\n",
           n, o.njm, V8A_MAXBITS);
}

/* a JM that does not fit (v8_frame returns 0): V.8 ends with V8A_RES_NO_CM,
 * no empty JM sent */
static void test_jm_too_long(const v8_menu_t *local)
{
    uint8_t oct[V8_MAX_OCTETS];
    v8ans_t v;
    int16_t tx;
    int i, quiet = 1;
    printf("jm_too_long\n");
    for (i = 0; i < V8_MAX_OCTETS; i++) oct[i] = 0x41;
    v8ans_init(&v, FS, AMP, local);
    for (i = 0; i < FS / 2; i++) (void)v8ans_tick(&v, 0, &tx);
    CHECK_EQ_I(v.state, V8A_ANSAM);
    CHECK_EQ_I(v8ans_send_jm(&v, oct, 15), 170);
    CHECK_EQ_I(v.state, V8A_JM);
    v8ans_init(&v, FS, AMP, local);
    for (i = 0; i < FS / 2; i++) (void)v8ans_tick(&v, 0, &tx);
    CHECK_EQ_I(v8ans_send_jm(&v, oct, 16), 0);
    CHECK_EQ_I(v.result, V8A_RES_NO_CM);
    CHECK_EQ_I(v.state, V8A_POST);
    for (i = 0; i < FS && v8ans_tick(&v, 0, &tx) != V8A_DONE; i++) quiet &= tx == 0;
    CHECK(quiet);
    CHECK_EQ_I(v.state, V8A_DONE);
    printf("  15 octets: 170 bits sent; 16: V8A_RES_NO_CM, silence, done\n");
}

int main(void)
{
    v8_menu_t local, cm;
    menus(&local, &cm);
    test_qc_ok(&local, &cm);
    test_every_uqts(&local, &cm);
    test_e0_not_offered(&local, &cm);
    test_restart(&local, &cm);
    test_cm_only(&local);
    test_echo(&local, &cm);
    test_noise(&local);
    test_not_qc(&local, &cm);
    test_ud_pattern(&local, &cm);
    test_jm_fits();
    test_jm_too_long(&local);
    T_REPORT("test_qc1a");
}
