/* v90m_get_qc (0.7.4): what V.92's Quick Connect did in a call, from the
 * outside: the answering modem from libv90modem.a as it is installed (the
 * defaults and V90M_MOD_V92, v90m_process_g711 in blocks of 160 octets, as
 * a host drives it), against the simulated V.92 caller (sim/v92call, from
 * the dev archive) sending its QC1a (WXYZ 0101: UQTS 70; P 1) ahead of its
 * CM:
 *   - accepted: QCA1d, QTS, ANSpcm (LM 01, -12 dBm0), the caller's TONEq,
 *     short Phase 2 (Table 18, DIL N = 0), V.92 data; V.42's ODP/ADP
 *     bypassed by the two P bits (9.2.5), as v90m_state says;
 *   - fallback: no TONEq: ANSam again, V.8, Phase 2, V.92 data; the bypass
 *     reported is the one the call used, V.8's (9.3.1: LAPM in CM and JM,
 *     V.92 at both ends): 1, and 0 with a caller whose P bits and CM ask
 *     for no LAPM; the QC1a's octet is the one acted on (V.8 listens for no
 *     second one: v8ans_tick);
 *   - default_mods: the defaults without V.92: the QC1a heard, not taken,
 *     V.90;
 *   - no_qc: V90M_OPT_NO_QC: heard, not taken, V.92 from V.8;
 *   - k56_ms_v8: V90M_MOD_K56 and BinModem's +A8E MS (21 89 80 80 81 c2:
 *     the modulation left to V.8, ACK(1) asked for): v8bis_ack 1 (and
 *     v90m_k56.exchanged 0: its MS carried no K56flex block), then QC;
 *   - plain: a V.92 caller with no QC1a: all 0 but the bypass of V.8 in
 *     v90m_state alone;
 *   - null_and_reset: NULL m, NULL q, after v90m_reset, a calling modem.
 * The diagnostics' phase through the accepted call: v8, phase2 (the short
 * Phase 2), ..., data. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "v90modem.h"
#include "test.h"
#include "v92call.h"

#define BLK 160

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

typedef struct {
    unsigned mods;              /* added to the defaults */
    int no_qc1a, lapm, no_toneq, short_p2;
    const uint8_t *bis; int nbis;
} scen_t;

static v92call_t C;
static prbs_t UA, UC;
static uint8_t tx[BLK];
static int phases[8], nph;      /* v90m_diag.phase as it went, each once */
static long nblk, conn_blk;      /* blocks run; the first with data moving */

static v90m_t *start(const scen_t *s)
{
    v90m_cfg c; v92call_cfg cc; int err, i;
    memset(&UA, 0, sizeof UA); memset(&UC, 0, sizeof UC); UA.tx = 1; UC.tx = 77;
    v90m_cfg_default(&c);
    c.modulations |= s->mods;
    c.getbit = p_get; c.putbit = p_put; c.user = &UA;
    memset(&cc, 0, sizeof cc);
    cc.v92 = 1; cc.pcm_up = 1;
    cc.lapm = s->lapm; cc.qc_lapm = s->lapm;        /* one P for the call */
    cc.qc = !s->no_qc1a; cc.qc_wxyz = 5; cc.short_p2 = s->short_p2; cc.dil_n = 0;
    cc.qc_no_toneq = s->no_toneq;
    cc.echo_gain = 0.1; cc.echo_delay = 21;
    cc.v8bis_ms = s->bis; cc.v8bis_ms_len = s->nbis;
    cc.getbit = p_get; cc.putbit = p_put; cc.user = &UC;
    v92call_init(&C, &cc);
    for (i = 0; i < BLK; i++) tx[i] = 0xFF;
    nph = 0; nblk = 0; conn_blk = -1;
    return v90m_create(&c, &err);
}

/* one block: the caller hears the last block we sent, we hear its */
static void block(v90m_t *m)
{
    uint8_t rx[BLK];
    v90m_diag d;
    int i;
    for (i = 0; i < BLK; i++) rx[i] = v92call_tick(&C, tx[i]);
    CHECK_EQ_I(v90m_process_g711(m, rx, tx, BLK), V90M_OK);
    nblk++;
    v90m_get_diag(m, &d);
    if ((nph == 0 || phases[nph - 1] != (int)d.phase) && nph < 8) phases[nph++] = (int)d.phase;
}

/* to data moving both ways (at most 30 s), then 2 s of it */
static int to_data(v90m_t *m, v90m_state *st)
{
    long k;
    for (k = 0; k < 30L * 8000 / BLK; k++) {
        block(m);
        v90m_get_state(m, st);
        if (st->end != V90M_END_NONE) return 0;
        if (st->moving && v92call_data(&C)) { conn_blk = nblk; break; }
    }
    if (k == 30L * 8000 / BLK) return 0;
    UA.n = UA.bad = UC.n = UC.bad = 0;
    for (k = 0; k < 2L * 8000 / BLK; k++) block(m);
    v90m_get_state(m, st);
    CHECK_EQ_I(st->moving, 1);
    CHECK(UA.n > 0 && UC.n > 0);
    CHECK_EQ_I(UA.bad, 0); CHECK_EQ_I(UC.bad, 0);
    printf("    data both ways from %.2f s; then 2 s: %ld bits up, %ld down, 0 bad\n", conn_blk * BLK / 8000.0, UA.n, UC.n);
    return 1;
}

static void show(const char *name, const v90m_qc *q, const v90m_state *st)
{
    printf("  %-14s heard %d uqts %d lapm %d accepted %d level %d toneq %s fallback %d bypass %d short %d v8bis_ack %d;"
           " %s, odp_bypass %d\n", name, q->heard, q->uqts, q->lapm, q->accepted, q->level_db10,
           q->toneq_ms > 0 && q->toneq_ms <= 2000 ? "(0, 2000]" : q->toneq_ms ? "?" : "0",
           q->fallback, q->bypass, q->short_p2, q->v8bis_ack, v90m_conn_name(st->modulation), st->odp_bypass);
}

static void zero_ok(const v90m_qc *q)
{
    CHECK_EQ_I(q->heard, 0); CHECK_EQ_I(q->uqts, 0); CHECK_EQ_I(q->lapm, 0); CHECK_EQ_I(q->accepted, 0);
    CHECK_EQ_I(q->level_db10, 0); CHECK_EQ_I(q->toneq_ms, 0); CHECK_EQ_I(q->fallback, 0);
    CHECK_EQ_I(q->bypass, 0); CHECK_EQ_I(q->short_p2, 0); CHECK_EQ_I(q->v8bis_ack, 0);
}

static void accepted(void)
{
    scen_t s; v90m_t *m; v90m_state st; v90m_qc q; int i, have_v8 = 0, have_p2 = 0;
    printf("accepted\n");
    memset(&s, 0, sizeof s); s.mods = V90M_MOD_V92; s.lapm = 1; s.short_p2 = 1;
    m = start(&s);
    CHECK(m != NULL);
    if (!m) return;
    /* before the answer tone: nothing */
    block(m); v90m_get_qc(m, &q); zero_ok(&q);
    CHECK_EQ_I(to_data(m, &st), 1);
    CHECK_EQ_I(st.connected, 1); CHECK_EQ_I((int)st.modulation, (int)V90M_CONN_V92);
    v90m_get_qc(m, &q);
    show("accepted", &q, &st);
    CHECK_EQ_I(q.heard, 1); CHECK_EQ_I(q.uqts, 70); CHECK_EQ_I(q.lapm, 1); CHECK_EQ_I(q.accepted, 1);
    CHECK_EQ_I(q.level_db10, -120);
    CHECK(q.toneq_ms > 0 && q.toneq_ms <= 2000);
    CHECK_EQ_I(q.fallback, 0); CHECK_EQ_I(q.bypass, 1); CHECK_EQ_I(q.short_p2, 1); CHECK_EQ_I(q.v8bis_ack, 0);
    CHECK_EQ_I(st.odp_bypass, 1); CHECK_EQ_I(q.bypass, st.odp_bypass);
    /* v8, phase2, ..., data, never back */
    for (i = 0; i < nph; i++) {
        if (phases[i] == (int)V90M_PHASE_V8) have_v8 = 1;
        if (phases[i] == (int)V90M_PHASE_PHASE2) { CHECK_EQ_I(have_v8, 1); have_p2 = 1; }
        if (i) CHECK(phases[i] > phases[i - 1]);
    }
    CHECK_EQ_I(have_v8, 1); CHECK_EQ_I(have_p2, 1);
    CHECK(nph > 0 && phases[nph - 1] == (int)V90M_PHASE_DATA);
    printf("  phases:");
    for (i = 0; i < nph; i++) printf(" %s", v90m_phase_name((v90m_phase)phases[i]));
    printf("\n");
    /* kept after the end */
    CHECK_EQ_I(v90m_cleardown(m), V90M_OK);
    for (i = 0; i < 4 * 8000 / BLK; i++) { block(m); v90m_get_state(m, &st); if (st.end != V90M_END_NONE) break; }
    CHECK(st.end != V90M_END_NONE);
    {
        v90m_qc e; v90m_get_qc(m, &e);
        CHECK(memcmp(&e, &q, sizeof e) == 0);
    }
    v90m_destroy(m);
}

static void fallback(const char *name, int lapm)
{
    scen_t s; v90m_t *m; v90m_state st; v90m_qc q;
    printf("%s\n", name);
    memset(&s, 0, sizeof s); s.mods = V90M_MOD_V92; s.lapm = lapm; s.no_toneq = 1;
    m = start(&s);
    CHECK(m != NULL);
    if (!m) return;
    CHECK_EQ_I(to_data(m, &st), 1);
    CHECK_EQ_I((int)st.modulation, (int)V90M_CONN_V92);
    v90m_get_qc(m, &q);
    show(name, &q, &st);
    CHECK_EQ_I(q.heard, 1); CHECK_EQ_I(q.uqts, 70); CHECK_EQ_I(q.lapm, lapm);
    CHECK_EQ_I(q.accepted, 1); CHECK_EQ_I(q.level_db10, -120);
    CHECK_EQ_I(q.toneq_ms, 0); CHECK_EQ_I(q.fallback, 1);
    CHECK_EQ_I(q.short_p2, 0); CHECK_EQ_I(q.v8bis_ack, 0);
    /* the bypass the call used: V.8's after the fallback (9.3.1) */
    CHECK_EQ_I(q.bypass, lapm); CHECK_EQ_I(st.odp_bypass, lapm);
    v90m_destroy(m);
}

/* the QC1a heard, not taken */
static void not_taken(const char *name, unsigned mods, v90m_conn conn)
{
    scen_t s; v90m_t *m; v90m_state st; v90m_qc q;
    printf("%s\n", name);
    memset(&s, 0, sizeof s); s.mods = mods; s.lapm = 1;
    m = start(&s);
    CHECK(m != NULL);
    if (!m) return;
    CHECK_EQ_I(to_data(m, &st), 1);
    CHECK_EQ_I((int)st.modulation, (int)conn);
    v90m_get_qc(m, &q);
    show(name, &q, &st);
    CHECK_EQ_I(q.heard, 1); CHECK_EQ_I(q.uqts, 70); CHECK_EQ_I(q.lapm, 1);
    CHECK_EQ_I(q.accepted, 0); CHECK_EQ_I(q.level_db10, 0); CHECK_EQ_I(q.toneq_ms, 0);
    CHECK_EQ_I(q.fallback, 0); CHECK_EQ_I(q.short_p2, 0); CHECK_EQ_I(q.v8bis_ack, 0);
    CHECK_EQ_I(q.bypass, st.odp_bypass);
    v90m_destroy(m);
}

static void k56_ms_v8(void)
{
    static const uint8_t bm[6] = { 0x21, 0x89, 0x80, 0x80, 0x81, 0xc2 };
    scen_t s; v90m_t *m; v90m_state st; v90m_qc q; v90m_k56 k;
    printf("k56_ms_v8\n");
    memset(&s, 0, sizeof s); s.mods = V90M_MOD_V92 | V90M_MOD_K56; s.lapm = 1; s.bis = bm; s.nbis = 6;
    m = start(&s);
    CHECK(m != NULL);
    if (!m) return;
    CHECK_EQ_I(to_data(m, &st), 1);
    CHECK_EQ_I((int)st.modulation, (int)V90M_CONN_V92);
    CHECK_EQ_I(C.v8bis_got, 0x14);              /* ACK(1) */
    v90m_get_qc(m, &q); v90m_get_k56(m, &k);
    show("k56_ms_v8", &q, &st);
    printf("  %-14s k56 offered %d exchanged %d chosen %d declined %d agreed %d\n", "", k.offered, k.exchanged,
           k.chosen, k.declined, k.agreed);
    CHECK_EQ_I(q.v8bis_ack, 1);
    CHECK_EQ_I(q.heard, 1); CHECK_EQ_I(q.accepted, 1); CHECK_EQ_I(q.fallback, 0); CHECK_EQ_I(q.bypass, 1);
    CHECK_EQ_I(k.offered, 3); CHECK_EQ_I(k.exchanged, 0); CHECK_EQ_I(k.agreed, 0); CHECK_EQ_I(k.declined, 0);
    v90m_destroy(m);
}

static void plain(void)
{
    scen_t s; v90m_t *m; v90m_state st; v90m_qc q;
    printf("plain\n");
    memset(&s, 0, sizeof s); s.mods = V90M_MOD_V92; s.lapm = 1; s.no_qc1a = 1;
    m = start(&s);
    CHECK(m != NULL);
    if (!m) return;
    CHECK_EQ_I(to_data(m, &st), 1);
    CHECK_EQ_I((int)st.modulation, (int)V90M_CONN_V92);
    v90m_get_qc(m, &q);
    show("plain", &q, &st);
    zero_ok(&q);
    CHECK_EQ_I(st.odp_bypass, 1);               /* V.8's (9.3.1), not QC's */
    v90m_destroy(m);
}

static void null_and_reset(void)
{
    scen_t s; v90m_t *m; v90m_state st; v90m_qc q; v90m_cfg c; int err;
    printf("null_and_reset\n");
    memset(&q, 0x55, sizeof q);
    v90m_get_qc(NULL, &q);
    CHECK_EQ_I(q.heard, 0x55555555); CHECK_EQ_I(q.v8bis_ack, 0x55555555);
    memset(&s, 0, sizeof s); s.mods = V90M_MOD_V92; s.lapm = 1;
    m = start(&s);
    CHECK(m != NULL);
    if (!m) return;
    v90m_get_qc(m, NULL);
    CHECK_EQ_I(to_data(m, &st), 1);
    v90m_get_qc(m, &q);
    CHECK_EQ_I(q.heard, 1);
    v90m_cfg_default(&c);
    c.modulations |= V90M_MOD_V92;
    CHECK_EQ_I(v90m_reset(m, &c), V90M_OK);
    memset(&q, 0x55, sizeof q);
    v90m_get_qc(m, &q);
    zero_ok(&q);
    v90m_destroy(m);
    /* calling */
    v90m_cfg_default(&c);
    c.role = V90M_ROLE_ORIGINATE; c.modulations |= V90M_MOD_V92;
    m = v90m_create(&c, &err);
    CHECK(m != NULL);
    if (!m) return;
    memset(&q, 0x55, sizeof q);
    v90m_get_qc(m, &q);
    zero_ok(&q);
    v90m_destroy(m);
    printf("  NULL: untouched; after v90m_reset and calling: all 0\n");
}

int main(void)
{
    printf("qc: v90m_get_qc, V.92 Quick Connect answering\n");
    CHECK_EQ_I((int)sizeof(v90m_qc), (int)(10 * sizeof(int)));
    accepted();
    fallback("fallback", 1);
    fallback("fallback_nolapm", 0);
    not_taken("default_mods", 0, V90M_CONN_V90);
    not_taken("no_qc", V90M_MOD_V92 | V90M_OPT_NO_QC, V90M_CONN_V92);
    k56_ms_v8();
    plain();
    null_and_reset();
    T_REPORT("test_qc");
}
