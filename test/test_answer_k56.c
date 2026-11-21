/* The answering modem (modem/answer) with K56flex offered (ANS_MODE_K56):
 * Phase 1, V.8bis before V.8 (modem/v8bis), and what every caller gets
 * from it (docs/superpowers/plans/2026-11-21-k56flex-answering.md, Task 5;
 * Review Focus 1 and 2). K56flex, V.34 and V.90 offered unless said.
 *   - the simulated client (sim/k56call) over a digital link, the whole
 *     V.8bis exchange, its CM V.34 only: agreed, JM selects V.34, with x2
 *     offered too no JM flip and no x2 INFO0a bits, and (until the K56flex
 *     start-up exists) V.34 at the rates of the same call without K56flex;
 *   - the same client with CRd and no MS (a V.8bis caller without K56flex,
 *     Review Focus 1): our ANSam 1700 ms (+-20) after our CL ended (V.8bis
 *     done at 1500 ms, then v8ans' own 200 ms of silence), not agreed,
 *     V.34 as without K56flex; its MS with a bad FCS: NAK(1), not agreed;
 *   - the whole exchange with PCM in its CM (Review Focus 2): agreed, but
 *     JM selects V.90 and Phase 2 is V.90's;
 *   - fall-through, each as without K56flex (the same modulation and rates,
 *     100000 bits each way equal end to end): sim/v34peer (no V.8bis), the
 *     library's own calling modem in V.90 and in V.92 (test/v90link.h; there
 *     the reference answerer starts as late as V.8 does behind V.8bis, since
 *     V.92's rates move a step with the line's noise, and from there on it
 *     sends the same octets), the x2 client (sim/x2call) with x2 offered
 *     too;
 *   - an A-law line: not one transmit sample differs (no CRe).
 * The delays K56flex adds before our ANSam are printed (docs/limits.md). */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "test.h"
#include "../modem/answer.h"
#include "../sim/k56call.h"
#include "../sim/x2call.h"
#include "../sim/g711.h"
#include "../sim/noise.h"
#include "line34.h"
#ifndef TEST_SHORT          /* on ARM under qemu (test.h): not the calling modem, not x2 */
#include "v90link.h"
#endif

#define FS 8000
#define MS(x) ((int64_t)(x) * 8)
#define BASE (ANS_MODE_V90 | V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21)
#define BITS 100000L

static answer_t A;

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
static prbs_t UA, UC;     /* what we receive, what the caller receives */

/* what our side sent, per run slot */
#define TXN (FS * 20)
static int16_t txs[2][TXN];
static long ntx;
static int slot;

/* Phase 1 as our answerer went through it: samples, -1 none */
typedef struct { long cl_end, ansam_at, p2_at; int prev; int flipped; } p1_t;
static p1_t P1;
static void p1_reset(void) { P1.cl_end = P1.ansam_at = P1.p2_at = -1; P1.prev = -1; P1.flipped = 0; }
/* after our sample n (the next sample is n + 1) */
static void p1_watch(long n)
{
    if (A.phase == ANS_V8BIS) {
        if (P1.prev == V8B_CL && A.u.v8b.state != V8B_CL) P1.cl_end = n + 1;
        P1.prev = A.u.v8b.state;
    }
    if (P1.ansam_at < 0 && A.phase == ANS_V8 && A.v8.state == V8A_ANSAM) P1.ansam_at = n + 1;
    if (P1.p2_at < 0 && A.phase == ANS_P2) P1.p2_at = n + 1;
    if (A.v8.jm_sign < 0) P1.flipped = 1;
}

/* ---- the K56flex client over a digital link (as test_answer_x2's x2 client) ---- */

#define XD 4096
static k56call_t K;

typedef struct {
    int ok, mod, down, up;
    int agreed, sel, x2, sr_diff, cme;
    long bad;
} k_outcome;

static void k_call(unsigned modes, int v8bis, int cm_pcm, int secs, k_outcome *o)
{
    static uint8_t down[XD]; static int16_t up[XD]; static double dl[XD];
    const int d_ac = 37, d_ca = 41;
    answer_cfg ac; k56call_cfg kc; rng_t rng;
    uint8_t arx = 0xff;
    long n, end = -1;
    memset(o, 0, sizeof *o);
    memset(down, 0xff, sizeof down); memset(up, 0, sizeof up); memset(dl, 0, sizeof dl);
    memset(&UA, 0, sizeof UA); memset(&UC, 0, sizeof UC); UA.tx = 1; UC.tx = 77;
    rng_seed(&rng, 4242);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = modes; ac.getbit = p_get; ac.putbit = p_put; ac.user = &UA;
    answer_init(&A, &ac);
    answer_v90_line(&A, 0, 0, 0);
    memset(&kc, 0, sizeof kc);
    kc.level = 3000; kc.v8bis = v8bis; kc.cm_pcm = cm_pcm;
    kc.getbit = p_get; kc.putbit = p_put; kc.user = &UC;
    k56call_init(&K, &kc);
    p1_reset();
    for (n = 0; n < 8000L * secs; n++) {
        int i = (int)(n & (XD - 1));
        double v;
        uint8_t atx = answer_tick_pcm(&A, arx);
        p1_watch(n);
        down[i] = atx;
        dl[i] = (double)ulaw_to_linear(atx);
        up[i] = k56call_tick(&K, down[(n - d_ac) & (XD - 1)]);
        /* what reaches us: the client, our own echo off the line's hybrid, noise */
        v = 0.5 * up[(n - d_ca) & (XD - 1)]
            + 0.12 * dl[(n - 2) & (XD - 1)] + 0.08 * dl[(n - 4) & (XD - 1)] - 0.05 * dl[(n - 9) & (XD - 1)]
            + 2.0 * rng_gauss(&rng);
        arx = linear_to_ulaw((int32_t)lround(v));
        if (A.phase == ANS_FAILED || K.state == K56C_ENDED) break;
        if (cm_pcm && A.phase == ANS_P2) break;
        /* in data: BITS each way, from 1 s into it */
        if (end < 0 && A.connected && K.state == K56C_V34 && K.peer.state == VP_DATA) {
            end = n + 8000; UA.n = UA.bad = UC.n = UC.bad = 0;
        }
        if (end >= 0 && n == end) { UA.n = UA.bad = UC.n = UC.bad = 0; }
        if (end >= 0 && n > end && UA.n >= BITS && UC.n >= BITS) break;
    }
    o->agreed = A.k56_agreed; o->sel = A.v8.selected; o->x2 = v8ans_x2(&A.v8);
    o->sr_diff = K.peer.p2.info0a.max_sr_diff; o->cme = K.peer.p2.info0a.cme;
    o->ok = A.connected && A.modulation == ANS_MOD_V34 && K.state == K56C_V34 && K.peer.state == VP_DATA;
    o->mod = A.modulation; o->down = A.bitrate; o->up = A.rx_bitrate;
    o->bad = UA.bad + UC.bad;
    if (UA.n < BITS || UC.n < BITS) o->bad = -1;
}

static void k_report(const char *what, const k_outcome *o)
{
    printf("  %s: %s %d / %d bit/s, %ld bad; agreed %d, JM %d, x2 %d (flipped %d), CL %d (ok %d), MS %d, ACK %d, NAK %d\n",
           what, o->ok ? "V.34" : "NO DATA", o->down, o->up, o->bad, o->agreed, o->sel, o->x2, P1.flipped,
           K.got_cl, K.cl_ok, K.sent_ms, K.got_ack, K.got_nak);
}

static void k56_client_tests(void)
{
    k_outcome o0, o1, o2;
    long ansam0;

    /* without K56flex: the reference (the client never hears CRe) */
    k_call(BASE, K56C_V8BIS_FULL, 0, 40, &o0);
    ansam0 = P1.ansam_at;
    k_report("K56flex client, K56flex not offered", &o0);
    CHECK(o0.ok); CHECK_EQ_I(o0.bad, 0); CHECK_EQ_I(o0.agreed, 0);
    CHECK_EQ_I(K.got_cre, 0); CHECK_EQ_I(K.got_cl, 0);

    /* the whole exchange, x2 offered too: agreed, x2 not offered in the call */
    k_call(BASE | ANS_MODE_K56 | ANS_MODE_X2, K56C_V8BIS_FULL, 0, 40, &o1);
    k_report("K56flex client, K56flex and x2 offered", &o1);
    CHECK_EQ_I(A.k56_offered, 1);
    CHECK_EQ_I(K.got_cre, 1); CHECK_EQ_I(K.got_cl, 1); CHECK_EQ_I(K.cl_ok, 1);
    CHECK_EQ_I(K.sent_ms, 1); CHECK_EQ_I(K.got_ack, 1); CHECK_EQ_I(K.got_nak, 0);
    CHECK_EQ_I(o1.agreed, 1);
    CHECK_EQ_I(o1.sel, V8_SEL_V34); CHECK_EQ_I(K.v8_sel, V8_SEL_V34);
    CHECK_EQ_I(o1.x2, 0); CHECK_EQ_I(P1.flipped, 0);
    CHECK_EQ_I(o1.sr_diff, o0.sr_diff); CHECK_EQ_I(o1.cme, o0.cme);
    CHECK_EQ_I(A.v34.cfg.x2, 0); CHECK_EQ_I(A.v34.cfg.k56, 1);
    CHECK(o1.ok); CHECK_EQ_I(o1.mod, ANS_MOD_V34);
    CHECK_EQ_I(o1.down, o0.down); CHECK_EQ_I(o1.up, o0.up); CHECK_EQ_I(o1.bad, 0);
    printf("     our ANSam %.3f s after answering (%.3f s without K56flex)\n", P1.ansam_at / 8000.0, ansam0 / 8000.0);

    /* Review Focus 1: CRd, then no MS (a V.92 modem) */
    k_call(BASE | ANS_MODE_K56, K56C_V8BIS_NO_MS, 0, 40, &o2);
    k_report("V.8bis caller without K56flex", &o2);
    printf("     our ANSam %.1f ms after our CL ended; %.3f s later than without K56flex\n",
           (P1.ansam_at - P1.cl_end) / 8.0, (P1.ansam_at - ansam0) / 8000.0);
    CHECK_EQ_I(K.got_cl, 1); CHECK_EQ_I(K.sent_ms, 0); CHECK_EQ_I(K.got_ack, 0); CHECK_EQ_I(K.got_nak, 0);
    CHECK_NEAR(P1.ansam_at - P1.cl_end, 8 * (V8B_MS_WAIT_MS + V8A_SILENCE_MS), 8 * 20);
    CHECK_EQ_I(o2.agreed, 0); CHECK_EQ_I(A.v34.cfg.k56, 0);
    CHECK(o2.ok); CHECK_EQ_I(o2.down, o0.down); CHECK_EQ_I(o2.up, o0.up); CHECK_EQ_I(o2.bad, 0);

    /* a caller without V.8bis over the same link */
    k_call(BASE | ANS_MODE_K56, K56C_V8BIS_NONE, 0, 40, &o2);
    k_report("caller without V.8bis", &o2);
    printf("     our ANSam %.3f s later than without K56flex\n", (P1.ansam_at - ansam0) / 8000.0);
    CHECK_EQ_I(P1.ansam_at - ansam0, 8L * (V8B_SIL_MS + V8B_CRE_DUAL_MS + V8B_CRE_TONE_MS + V8B_CRD_DEADLINE_MS));
    CHECK_EQ_I(o2.agreed, 0);
    CHECK(o2.ok); CHECK_EQ_I(o2.down, o0.down); CHECK_EQ_I(o2.up, o0.up); CHECK_EQ_I(o2.bad, 0);

#ifndef TEST_SHORT
    /* its MS with a bad FCS: NAK(1), not agreed, V.34 */
    k_call(BASE | ANS_MODE_K56, K56C_V8BIS_BAD_FCS, 0, 40, &o2);
    k_report("K56flex client, its MS with a bad FCS", &o2);
    CHECK_EQ_I(K.sent_ms, 1); CHECK_EQ_I(K.got_ack, 0); CHECK_EQ_I(K.got_nak, 1);
    CHECK_EQ_I(o2.agreed, 0);
    CHECK(o2.ok); CHECK_EQ_I(o2.down, o0.down); CHECK_EQ_I(o2.up, o0.up); CHECK_EQ_I(o2.bad, 0);
#endif

    /* Review Focus 2: agreed, but its CM offers PCM while we offer V.90 */
    k_call(BASE | ANS_MODE_K56, K56C_V8BIS_FULL, 1, 20, &o2);
    printf("  K56flex client with PCM in its CM: agreed %d, JM %d, V.90 Phase 2 %.3f s after answering\n",
           o2.agreed, o2.sel, P1.p2_at / 8000.0);
    CHECK_EQ_I(o2.agreed, 1); CHECK_EQ_I(K.got_ack, 1);
    CHECK_EQ_I(o2.sel, V8_SEL_V90); CHECK_EQ_I(K.v8_sel, V8_SEL_V90);
    CHECK(P1.p2_at > 0); CHECK_EQ_I(A.phase, ANS_P2);
    CHECK_EQ_I(o2.x2, 0); CHECK_EQ_I(P1.flipped, 0);
}

/* ---- a caller without V.8bis: sim/v34peer over test/line34.h ---- */

static v34peer_t C; static line34_t L;
static int16_t a_tick(void *u, int16_t rx)
{
    int16_t t = answer_tick((answer_t *)u, rx);
    p1_watch(L.n);
    if (ntx < TXN) txs[slot][ntx++] = t;
    return t;
}
static int16_t c_tick(void *u, int16_t rx) { return v34peer_tick((v34peer_t *)u, rx); }

typedef struct { int ok, tx, rx, sym; long bad, ansam_at; } v34_outcome;

static void v34_call(unsigned modes, int law, long data_bits, v34_outcome *o)
{
    answer_cfg ac; v34peer_cfg pc; line34_cfg lc;
    long end;
    memset(o, 0, sizeof *o);
    memset(&lc, 0, sizeof lc);
    lc.law = law; lc.d_ca = 24; lc.d_ac = 20; lc.gain = 0.5; lc.echo_a = 0.3; lc.echo_c = 0.3;
    memset(&UA, 0, sizeof UA); memset(&UC, 0, sizeof UC); UA.tx = 1; UC.tx = 77;
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = modes; ac.getbit = p_get; ac.putbit = p_put; ac.user = &UA;
    v34peer_default(&pc, 3000);
    pc.dx.getbit = p_get; pc.dx.putbit = p_put; pc.dx.user = &UC;
    answer_init(&A, &ac);
    if (law == 2) { p2d_cfg p = A.p2cfg; p.alaw = 1; answer_v90_cfg(&A, &p); }
    answer_v90_line(&A, 0, 0, 0);
    v34peer_init(&C, &pc); line34_init(&L, &lc);
    ntx = 0; p1_reset();
    end = 8000L * 25;
    while (L.n < end) {
        (void)line34_step(&L, a_tick, &A, c_tick, &C);
        if (A.connected && C.state == VP_DATA) break;
        if (A.phase == ANS_FAILED || C.state == VP_ENDED) break;
    }
    o->ansam_at = P1.ansam_at;
    o->ok = A.connected && C.state == VP_DATA;
    if (!o->ok || !data_bits) return;
    o->tx = A.bitrate; o->rx = A.rx_bitrate; o->sym = A.v34.dx.cfg.sym_rate;
    end = L.n + 8000;
    while (L.n < end) (void)line34_step(&L, a_tick, &A, c_tick, &C);
    UA.n = UA.bad = UC.n = UC.bad = 0;
    end = L.n + 8000L * 10;
    while (L.n < end && (UA.n < data_bits || UC.n < data_bits)) (void)line34_step(&L, a_tick, &A, c_tick, &C);
    o->bad = UA.bad + UC.bad;
    if (UA.n < data_bits || UC.n < data_bits) o->bad = -1;
}

static int same_tx(long n)
{
    long k;
    for (k = 0; k < n; k++) if (txs[0][k] != txs[1][k]) return 0;
    return 1;
}

static void v34peer_tests(void)
{
    v34_outcome o0, o1;
    long n0;
    slot = 0; v34_call(BASE, 1, BITS, &o0);
    slot = 1; v34_call(BASE | ANS_MODE_K56, 1, BITS, &o1);
    printf("  V.34 caller without V.8bis, K56flex offered: %d / %d bit/s at %d baud, %ld bad (without: %d / %d at %d, %ld bad);"
           " ANSam %.3f s later\n", o1.tx, o1.rx, o1.sym, o1.bad, o0.tx, o0.rx, o0.sym, o0.bad, (o1.ansam_at - o0.ansam_at) / 8000.0);
    CHECK(o0.ok); CHECK(o1.ok);
    CHECK_EQ_I(A.k56_offered, 1); CHECK_EQ_I(A.k56_agreed, 0);
    CHECK_EQ_I(A.modulation, ANS_MOD_V34);
    CHECK_EQ_I(o1.tx, o0.tx); CHECK_EQ_I(o1.rx, o0.rx); CHECK_EQ_I(o1.sym, o0.sym);
    CHECK_EQ_I(o0.bad, 0); CHECK_EQ_I(o1.bad, 0);
    CHECK_EQ_I(o1.ansam_at - o0.ansam_at, 8L * (V8B_SIL_MS + V8B_CRE_DUAL_MS + V8B_CRE_TONE_MS + V8B_CRD_DEADLINE_MS));

    /* an A-law line: K56flex is not offered at all, not a sample differs */
    slot = 0; v34_call(BASE, 2, 0, &o0); n0 = ntx;
    slot = 1; v34_call(BASE | ANS_MODE_K56, 2, 0, &o1);
    printf("  A-law V.34 caller, K56flex offered: %ld samples sent, %s\n", ntx,
           ntx == n0 && same_tx(ntx) ? "all as without K56flex" : "SOME DIFFER");
    CHECK(o0.ok); CHECK(o1.ok);
    CHECK_EQ_I(A.k56_offered, 0);
    CHECK_EQ_I(ntx, n0);
    CHECK(same_tx(ntx < n0 ? ntx : n0));
}

#ifndef TEST_SHORT
/* ---- the library's calling modem in V.90 and V.92 (test/v90link.h) ---- */

static vl_t VL;

/* The reference is the answerer without K56flex started as late as V.8
 * starts when K56flex is offered to a caller without V.8bis (silent until
 * then), so that it meets the same line from there on: then what it sends
 * from there on is the same, octet for octet (hashed over 12 s). */
#define LATE (8L * (V8B_SIL_MS + V8B_CRE_DUAL_MS + V8B_CRE_TONE_MS + V8B_CRD_DEADLINE_MS))
static long dig_n; static int dig_late; static uint32_t dig_hash;
static uint8_t dig_answer(void *u, uint8_t in)
{
    vl_t *l = (vl_t *)u;
    long n = dig_n++;
    uint8_t out = dig_late && n < LATE ? 0xff : answer_tick_pcm(&l->ans, in);
    if (n >= LATE && n < LATE + 8000L * 12) dig_hash = (dig_hash ^ out) * 16777619u;
    return out;
}

typedef struct { int ok, mod, down, up; int64_t bd, bu, fd, fu; uint32_t hash; } vl_outcome;

static void orig_call(int v92, int k56, vl_outcome *o)
{
    vl_cfg c;
    vl_t *l = &VL;
    int64_t d0, u0, f0d, f0u, e;
    memset(o, 0, sizeof *o);
    memset(&c, 0, sizeof c);
    c.loop_km = 2; c.noise_dbm0 = -75; c.echo_db = -16;
    c.up_loss_db = 6; c.up_noise_dbm0 = -70; c.up_line = 1; c.full = 1; c.caller_orig = 1; c.seed = 20;
    c.v92 = v92; c.caller_v92 = v92;
    vl_init(l, &c);
    if (k56) {
        /* vl_init's answerer, with K56flex offered too */
        answer_cfg ac;
        ac = l->ans.cfg;
        ac.modes |= ANS_MODE_K56;
        answer_init(&l->ans, &ac);
        answer_v90_line(&l->ans, -1, 0, 0);
        CHECK_EQ_I(l->ans.phase, ANS_V8BIS);
    }
    dig_n = 0; dig_late = !k56; dig_hash = 2166136261u;
    l->dig = dig_answer; l->dig_user = l;
    while (l->k < MS(40000) && !orig_moving(&l->org) && l->org.phase != ORIG_FAILED) vl_tick(l);
    if (!orig_moving(&l->org)) return;
    o->mod = l->org.modulation; o->down = l->org.rx_bitrate; o->up = l->org.bitrate;
    while (l->k < MS(42000) && !answer_moving(&l->ans)) vl_tick(l);
    e = l->k + MS(300); while (l->k < e) vl_tick(l);
    d0 = l->down.got; u0 = l->up.got; f0d = l->down.fails; f0u = l->up.fails;
    e = l->k + MS(10000);
    while (l->k < e && l->org.phase != ORIG_FAILED && (l->down.got - d0 < BITS || l->up.got - u0 < BITS)) vl_tick(l);
    o->bd = l->down.got - d0; o->bu = l->up.got - u0;
    o->fd = l->down.fails - f0d; o->fu = l->up.fails - f0u;
    o->ok = orig_moving(&l->org) && answer_moving(&l->ans);
    o->hash = dig_hash;
}

static void orig_tests(void)
{
    int v92;
    for (v92 = 0; v92 < 2; v92++) {
        vl_outcome o0, o1;
        orig_call(v92, 0, &o0);
        orig_call(v92, 1, &o1);
        printf("  our calling modem in %s, K56flex offered: mod %d, %d / %d bit/s, %" PRId64 " / %" PRId64 " bits,"
               " %" PRId64 " / %" PRId64 " fail (without: mod %d, %d / %d)\n", v92 ? "V.92" : "V.90", o1.mod, o1.down, o1.up,
               o1.bd, o1.bu, o1.fd, o1.fu, o0.mod, o0.down, o0.up);
        CHECK(o0.ok); CHECK(o1.ok);
        CHECK_EQ_I(VL.ans.k56_offered, 1); CHECK_EQ_I(VL.ans.k56_agreed, 0);
        CHECK_EQ_I(o1.mod, v92 ? ORIG_MOD_V92 : ORIG_MOD_V90); CHECK_EQ_I(o1.mod, o0.mod);
        CHECK_EQ_I(o1.down, o0.down); CHECK_EQ_I(o1.up, o0.up);
        CHECK_EQ_I(o1.hash, o0.hash);
        CHECK(o1.bd >= BITS); CHECK(o1.bu >= BITS);
        CHECK_EQ_I(o1.fd, 0); CHECK_EQ_I(o1.fu, 0);
    }
}

/* ---- the x2 client with x2 and K56flex offered ---- */

typedef struct { uint32_t tx, rx; long n, bad; } rbits_t;
static int rnext(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return (int)(*s & 1); }
static void r_check(rbits_t *r, int bit) { if (rnext(&r->rx) != (bit & 1)) r->bad++; r->n++; }
static x2call_t X; static rbits_t DN, UP;
static int s_get(void *u) { (void)u; return rnext(&DN.tx); }
static void s_put(void *u, int bit) { (void)u; r_check(&UP, bit); }
static int c_get(void *u) { (void)u; return rnext(&UP.tx); }
static void c_put(void *u, int bit) { (void)u; r_check(&DN, bit); }

typedef struct { int ok, mod, down, up; } x2_outcome;

static void x2_call(unsigned modes, x2_outcome *o)
{
    static uint8_t down[XD]; static int16_t up[XD]; static double dl[XD];
    const int d_ac = 37, d_ca = 41;
    answer_cfg ac; x2call_cfg xc; rng_t rng;
    uint8_t arx = 0xff;
    long n;
    memset(o, 0, sizeof *o);
    memset(down, 0x7f, sizeof down); memset(up, 0, sizeof up); memset(dl, 0, sizeof dl);
    memset(&DN, 0, sizeof DN); memset(&UP, 0, sizeof UP);
    DN.tx = DN.rx = 0x2468ace1u; UP.tx = UP.rx = 0x13579bdfu;
    rng_seed(&rng, 4242);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = modes; ac.getbit = s_get; ac.putbit = s_put; ac.user = NULL;
    answer_init(&A, &ac);
    answer_v90_line(&A, 0, 0, 0);
    memset(&xc, 0, sizeof xc);
    xc.level = 3000; xc.max_index = 1; xc.up_mask = 0x03FE;
    xc.getbit = c_get; xc.putbit = c_put; xc.user = NULL;
    x2call_init(&X, &xc);
    for (n = 0; n < 8000L * 45; n++) {
        int i = (int)(n & (XD - 1));
        double v;
        uint8_t atx = answer_tick_pcm(&A, arx);
        down[i] = atx;
        dl[i] = (double)ulaw_to_linear(atx);
        up[i] = x2call_tick(&X, down[(n - d_ac) & (XD - 1)]);
        v = 0.5 * up[(n - d_ca) & (XD - 1)]
            + 0.12 * dl[(n - 2) & (XD - 1)] + 0.08 * dl[(n - 4) & (XD - 1)] - 0.05 * dl[(n - 9) & (XD - 1)]
            + 2.0 * rng_gauss(&rng);
        arx = linear_to_ulaw((int32_t)lround(v));
        if (A.phase == ANS_FAILED) break;
        if (A.connected && x2call_state(&X) == X2C_DATA && DN.n >= BITS && UP.n >= BITS) break;
    }
    o->ok = A.connected && A.phase == ANS_X2 && x2call_state(&X) == X2C_DATA;
    o->mod = A.modulation; o->down = A.bitrate; o->up = A.rx_bitrate;
}

static void x2_tests(void)
{
    x2_outcome o0, o1;
    long b0;
    x2_call(BASE | ANS_MODE_X2, &o0);
    b0 = DN.bad + UP.bad;
    x2_call(BASE | ANS_MODE_X2 | ANS_MODE_K56, &o1);
    printf("  x2 client, x2 and K56flex offered: %s %d / %d bit/s, down %ld bits %ld bad, up %ld bits %ld bad"
           " (without K56flex: %d / %d)\n", o1.ok ? "x2" : "NO DATA", o1.down, o1.up, DN.n, DN.bad, UP.n, UP.bad, o0.down, o0.up);
    CHECK(o0.ok); CHECK(o1.ok); CHECK_EQ_I(b0, 0);
    CHECK_EQ_I(A.k56_offered, 1); CHECK_EQ_I(A.k56_agreed, 0);
    CHECK_EQ_I(o1.mod, ANS_MOD_X2); CHECK_EQ_I(o1.down, o0.down); CHECK_EQ_I(o1.up, o0.up);
    CHECK(DN.n >= BITS); CHECK(UP.n >= BITS); CHECK_EQ_I(DN.bad, 0); CHECK_EQ_I(UP.bad, 0);
}

#endif

int main(void)
{
    k56_client_tests();
    v34peer_tests();
#ifndef TEST_SHORT          /* on ARM under qemu (test.h): the K56flex client and v34peer only */
    orig_tests();
    x2_tests();
#endif
    T_REPORT("answer_k56");
}
