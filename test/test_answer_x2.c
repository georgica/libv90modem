/* The answering modem (modem/answer) with x2 offered (ANS_MODE_X2), against
 * callers that are not x2 callers: what they get must be what they get
 * without it (docs/superpowers/specs/2026-11-20-x2-answering-design.md,
 * "Anti-goals").
 *   - A V.34 caller (sim/v34peer over test/line34.h, mu-law): JM is flipped
 *     after each complete JM and INFO0a carries max_sr_diff = 4 and cme
 *     (docs/history/x2-facts.md "Phase 1", "Phase 2 marker"); the call
 *     connects V.34 at the same rates, the data clean both ways, as without
 *     x2.
 *   - The same caller on an A-law line: not one transmit sample differs.
 *   - A caller whose CM has PCM (V.90): not one transmit sample differs.
 *   - A V.34 caller that sends the x2 marker in place of its L1 / L2: the
 *     call is x2 (ANS_X2) and, until the x2 start-up exists, ends with
 *     x2_failed. */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../modem/answer.h"
#include "../sim/v34peer.h"
#include "../sim/v8call.h"
#include "../sim/p2a.h"
#include "../sim/channel.h"
#include "../sim/g711.h"
#include "line34.h"

#define FS 8000

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

static answer_t A; static v34peer_t C; static line34_t L; static prbs_t UA, UC;

/* what our side sent, per run slot */
#define TXN (FS * 25)
static int16_t txs[2][TXN];
static long ntx;
static int slot;

/* the x2 client's marker: from the start of our L1 the caller's Phase 2 is
 * silent, and mk_at ms later the marker frame goes out on 1200 Hz */
static int mk_at; static long l1_at, mk_off; static dpsktx_t MK; static uint8_t mkbits[64]; static int nmk;

static int16_t a_tick(void *u, int16_t rx)
{
    int16_t t = answer_tick((answer_t *)u, rx);
    if (ntx < TXN) txs[slot][ntx++] = t;
    return t;
}
static int16_t c_tick(void *u, int16_t rx)
{
    int16_t t;
    if (mk_at && l1_at < 0 && A.phase == ANS_V34 && A.v34.state == V34A_PHASE2 && A.v34.p2.state == P2N_L1) {
        l1_at = L.n; C.p2.mute = 1;
    }
    t = v34peer_tick((v34peer_t *)u, rx);
    if (mk_at && l1_at >= 0) {
        int32_t v;
        if (L.n == l1_at + 8L * mk_at) { dpsktx_send(&MK, mkbits, nmk); mk_off = -1; }
        if (mk_off < 0 && !dpsktx_busy(&MK)) mk_off = L.n + 60;
        if (L.n == mk_off) dpsktx_off(&MK);
        v = t + dpsktx_tick(&MK);
        t = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return t;
}

static void start(unsigned modes, int law, int cm_access)
{
    answer_cfg ac; v34peer_cfg pc; line34_cfg lc;
    memset(&lc, 0, sizeof lc);
    lc.law = law; lc.d_ca = 24; lc.d_ac = 20; lc.gain = 0.5; lc.echo_a = 0.3; lc.echo_c = 0.3;
    memset(&UA, 0, sizeof UA); memset(&UC, 0, sizeof UC); UA.tx = 1; UC.tx = 77;
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = modes; ac.getbit = p_get; ac.putbit = p_put; ac.user = &UA;
    v34peer_default(&pc, 3000);
    pc.cm_access = cm_access;
    pc.dx.getbit = p_get; pc.dx.putbit = p_put; pc.dx.user = &UC;
    answer_init(&A, &ac);
    if (law == 2) { p2d_cfg p = A.p2cfg; p.alaw = 1; answer_v90_cfg(&A, &p); }
    answer_v90_line(&A, 0, 0, 0);
    v34peer_init(&C, &pc); line34_init(&L, &lc);
    ntx = 0; l1_at = -1; mk_off = 0;
}

static void run(long samples) { while (samples-- > 0) (void)line34_step(&L, a_tick, &A, c_tick, &C); }

static int to_data(int max_ms)
{
    long end = L.n + 8L * max_ms;
    while (L.n < end) {
        run(8);
        if (A.connected && C.state == VP_DATA) return 1;
        if (A.phase == ANS_FAILED || C.state == VP_ENDED) return 0;
    }
    return 0;
}

typedef struct { int ok, tx, rx, sym; long bad; int sr_diff, cme, x2; } v34_outcome;

static void v34_call(unsigned modes, int law, int cm_access, v34_outcome *o)
{
    memset(o, 0, sizeof *o);
    start(modes, law, cm_access);
    o->ok = to_data(25000);
    o->x2 = v8ans_x2(&A.v8);
    o->sr_diff = C.p2.info0a.max_sr_diff; o->cme = C.p2.info0a.cme;
    if (!o->ok) return;
    o->tx = A.bitrate; o->rx = A.rx_bitrate; o->sym = A.v34.dx.cfg.sym_rate;
    UA.n = UA.bad = UC.n = UC.bad = 0;
    run(8000L * 3);
    o->bad = UA.bad + UC.bad;
    if (UA.n < 2L * A.rx_bitrate || UC.n < 2L * A.bitrate) o->bad = -1;
}

static int same_tx(long n)
{
    long k;
    for (k = 0; k < n; k++) if (txs[0][k] != txs[1][k]) return 0;
    return 1;
}

/* a V.90 caller (V.8 with PCM, then V.90 Phase 2 as the analogue modem)
 * through the channel simulator for 8 s; what we sent goes to txs[slot] */
static void v90_call(unsigned modes)
{
    static v8call_t v8; static p2a_t p2;
    static const double one[1] = {1.0};
    channel_cfg cfg; channel_t ch;
    answer_cfg ac; v8_menu_t cm; p2a_cfg pc;
    int16_t atx = 0, ctx = 0;
    int n, in_p2 = 0;

    memset(&cfg, 0, sizeof cfg);
    cfg.ds_noise_dbfs = -60.0; cfg.us_noise_dbfs = -60.0;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.echo_gain = 0.1; cfg.echo_delay = 21; cfg.full_scale = 32124.0; cfg.seed = 5;
    channel_init(&ch, &cfg);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = modes; ac.getbit = p_get; ac.putbit = p_put; ac.user = &UA;
    answer_init(&A, &ac);
    memset(&cm, 0, sizeof cm);
    cm.call_function = V8_CALLF_DATA;
    cm.modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21; cm.n_mod_octets = 3;
    cm.has_pcm = 1; cm.pcm = V8_PCM_ANALOG; cm.has_access = 1;
    v8call_init(&v8, FS, 4000, &cm);
    memset(&pc, 0, sizeof pc);
    pc.level = 4000; pc.v90 = 1; pc.up_sr = -1; pc.uinfo = 91;
    pc.info0a.lo3000 = pc.info0a.hi3000 = 1; pc.info0a.lo3200 = pc.info0a.hi3200 = 1;
    pc.info0a.pwr_reduce = 1; pc.info0a.max_sr_diff = 5; pc.info0a.c1664 = 1;
    ntx = 0;
    for (n = 0; n < 8 * FS; n++) {
        double crx; uint8_t srv; int16_t t, x;
        channel_tick(&ch, linear_to_ulaw(atx), (double)ctx, &crx, &srv);
        t = answer_tick(&A, (int16_t)ulaw_to_linear(srv));
        if (ntx < TXN) txs[slot][ntx++] = t;
        x = (int16_t)lround(crx);
        if (!in_p2) {
            int16_t o = 0;
            if (v8call_tick(&v8, x, &o) == V8C_DONE) { in_p2 = 1; p2a_init(&p2, &pc); }
            ctx = o;
        } else {
            ctx = p2a_tick(&p2, x);
        }
        atx = t;
        if (A.phase == ANS_FAILED) break;
    }
}

int main(void)
{
    const unsigned base = ANS_MODE_V90 | V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21;
    v34_outcome o0, o1;
    long n0;

    /* 1. a V.34 caller, mu-law: x2 is offered to it and it does not take it */
    slot = 0; v34_call(base, 1, 0, &o0);
    slot = 1; v34_call(base | ANS_MODE_X2, 1, 0, &o1);
    printf("  V.34 caller, x2 offered: %d / %d bit/s at %d baud, %ld bad (without x2: %d / %d at %d, %ld bad)\n",
           o1.tx, o1.rx, o1.sym, o1.bad, o0.tx, o0.rx, o0.sym, o0.bad);
    CHECK_EQ_I(o0.ok, 1); CHECK_EQ_I(o1.ok, 1);
    CHECK_EQ_I(A.x2_offered, 1);
    CHECK_EQ_I(o0.x2, 0); CHECK_EQ_I(o1.x2, 1);
    CHECK_EQ_I(o0.sr_diff, 0); CHECK_EQ_I(o0.cme, 0);
    CHECK_EQ_I(o1.sr_diff, 4); CHECK_EQ_I(o1.cme, 1);
    CHECK_EQ_I(A.modulation, ANS_MOD_V34); CHECK_EQ_I(A.phase, ANS_V34);
    CHECK_EQ_I(o1.tx, o0.tx); CHECK_EQ_I(o1.rx, o0.rx); CHECK_EQ_I(o1.sym, o0.sym);
    CHECK_EQ_I(o0.bad, 0); CHECK_EQ_I(o1.bad, 0);
#ifndef TEST_SHORT     /* on ARM under qemu (test.h): cases 1, 3 and 4 */
    /* its CM carries the PSTN access octet: the JM gives it back with the
     * digital-network bit; still V.34 */
    slot = 1; v34_call(base | ANS_MODE_X2, 1, 1, &o1);
    CHECK_EQ_I(o1.ok, 1); CHECK_EQ_I(o1.x2, 1);
    CHECK(A.v8.jm.has_access); CHECK_EQ_I(A.v8.jm.access & V8_ACC_DIGITAL, V8_ACC_DIGITAL);
    CHECK(C.v8.got_jm && C.v8.jm.has_access);
    CHECK_EQ_I(o1.tx, o0.tx); CHECK_EQ_I(o1.rx, o0.rx); CHECK_EQ_I(o1.bad, 0);

    /* 2. the same caller on an A-law line: x2 is not offered at all */
    slot = 0; v34_call(base, 2, 1, &o0); n0 = ntx;
    slot = 1; v34_call(base | ANS_MODE_X2, 2, 1, &o1);
    CHECK_EQ_I(o0.ok, 1); CHECK_EQ_I(o1.ok, 1);
    CHECK_EQ_I(A.x2_offered, 0); CHECK_EQ_I(o1.x2, 0);
    CHECK_EQ_I(o1.sr_diff, 0); CHECK_EQ_I(o1.cme, 0);
    CHECK_EQ_I(ntx, n0);
    CHECK(same_tx(ntx < n0 ? ntx : n0));
    printf("  A-law V.34 caller, x2 offered: %ld samples sent, %s\n", ntx, same_tx(ntx < n0 ? ntx : n0) ? "all as without x2" : "SOME DIFFER");

#endif

    /* 3. a V.90 caller (its CM has PCM): V.90, and the same samples */
    slot = 0; v90_call(base); n0 = ntx;
    CHECK_EQ_I(A.v8.selected, V8_SEL_V90); CHECK(A.phase == ANS_P3 || A.phase == ANS_P2);
    slot = 1; v90_call(base | ANS_MODE_X2);
    CHECK_EQ_I(A.v8.selected, V8_SEL_V90); CHECK(A.phase == ANS_P3 || A.phase == ANS_P2);
    CHECK_EQ_I(A.x2_offered, 1); CHECK_EQ_I(v8ans_x2(&A.v8), 0);
    CHECK_EQ_I(ntx, n0);
    CHECK(same_tx(ntx < n0 ? ntx : n0));
    printf("  V.90 caller, x2 offered: %ld samples sent, %s\n", ntx, same_tx(ntx < n0 ? ntx : n0) ? "all as without x2" : "SOME DIFFER");

    /* 4. an x2 client's marker (4d) 170 ms after our L1 began: ANS_X2, and
     *    until the x2 start-up is there, the end, said as x2's */
    {
        long t, x2_at = -1;
        slot = 0; mk_at = 170;
        dpsktx_init(&MK, DPSK_FC_B, 3000, 0);
        nmk = p2ans_x2_info_frame(0x4d, mkbits);
        start(base | ANS_MODE_X2, 1, 1);
        for (t = 0; t < 8000L * 12 && A.phase != ANS_FAILED; t++) {
            run(1);
            if (A.phase == ANS_X2 && x2_at < 0) x2_at = L.n;
        }
        printf("  x2 marker: ANS_X2 %.0f ms after our L1 began; the call ended (x2_failed %d)\n",
               (x2_at - l1_at) / 8.0, A.x2_failed);
        CHECK(l1_at > 0); CHECK(x2_at > l1_at);
        CHECK_EQ_I(A.v34.p2.x2_marker, 0x4d);
        CHECK_EQ_I(A.phase, ANS_FAILED); CHECK_EQ_I(A.x2_failed, 1); CHECK_EQ_I(A.connected, 0);
        mk_at = 0;
    }
    T_REPORT("answer_x2");
}
