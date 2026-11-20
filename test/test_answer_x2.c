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
 *   - A V.34 caller that sends the x2 marker in place of its L1 / L2 and
 *     then nothing: the call is x2 (ANS_X2) and ends with x2_failed.
 * And x2 itself, against the simulated client (sim/x2call) over a digital
 * link (our octets through the network's pad to it, its samples back
 * through a mu-law codec with our own echo and noise):
 *   - behind no pad, 3 dB and 6 dB: x2 at 32000 down and 24000 up, the
 *     client's checks of our script clean, 100000 random bits each way
 *     equal end to end;
 *   - an MP asking for an index we have no bank for, or reporting a pad we
 *     have no levels for: the call ends, x2_failed (plan, Review Focus 4);
 *   - the client silent after its marker, after its J, after its MP, in
 *     data: the call ends within 10 s, with an end reason (Review Focus 5). */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "../modem/answer.h"
#include "../sim/v34peer.h"
#include "../sim/v8call.h"
#include "../sim/p2a.h"
#include "../sim/channel.h"
#include "../sim/g711.h"
#include "../sim/x2call.h"
#include "../sim/noise.h"
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

/* ---- x2 against the simulated client ---- */

/* random bits, the same sequence at the far end: xorshift32 */
typedef struct { uint32_t tx, rx; long n, bad; } rbits_t;
static int rnext(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return (int)(*s & 1); }
static void r_check(rbits_t *r, int bit)
{
    if (rnext(&r->rx) != (bit & 1)) r->bad++;
    r->n++;
}

#define XD 4096
static x2call_t X; static rbits_t DN, UP;
/* downstream: our getbit to the client's putbit; upstream the other way */
static int s_get(void *u) { (void)u; return rnext(&DN.tx); }
static void s_put(void *u, int bit) { (void)u; r_check(&UP, bit); }
static int c_get(void *u) { (void)u; return rnext(&UP.tx); }
static void c_put(void *u, int bit) { (void)u; r_check(&DN, bit); }
typedef struct {
    int ok;                     /* both in data mode */
    int down, up, mod;
    long x2_at, quiet_at, end_at;   /* samples: ANS_X2, the client's silence, ANS_FAILED; -1 none */
    int error, x2_failed, connected_once;
} x2_outcome;

/* One x2 call: the client with pad (0, 1, 2; 3 reported only), the index it
 * asks for, its upstream mask and where it falls silent; for up to secs of
 * line, or data_bits each way once both are in data mode. */
static void x2_call(int pad, int index, uint16_t up_mask, int silent, int echo_delay, int secs, long data_bits, x2_outcome *o)
{
    static uint8_t down[XD]; static int16_t up[XD]; static double dl[XD];
    const int d_ac = 37, d_ca = 41;     /* one-way delays, samples */
    /* our echo off the line's hybrid comes back this late: behind a driver's
     * buffers when we are not told (echo_delay -1: x2d measures it) */
    const int e = echo_delay < 0 ? 300 : echo_delay;
    answer_cfg ac; x2call_cfg xc; rng_t rng;
    uint8_t arx = 0xff;
    long n;
    memset(o, 0, sizeof *o);
    o->x2_at = o->quiet_at = o->end_at = -1;
    memset(down, 0x7f, sizeof down); memset(up, 0, sizeof up); memset(dl, 0, sizeof dl);
    memset(&DN, 0, sizeof DN); memset(&UP, 0, sizeof UP);
    DN.tx = DN.rx = 0x2468ace1u; UP.tx = UP.rx = 0x13579bdfu;
    rng_seed(&rng, 4242);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = ANS_MODE_V90 | V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21 | ANS_MODE_X2;
    ac.getbit = s_get; ac.putbit = s_put; ac.user = NULL;
    answer_init(&A, &ac);
    answer_v90_line(&A, echo_delay, 0, 0);
    memset(&xc, 0, sizeof xc);
    xc.level = 3000; xc.pad_mode = pad; xc.max_index = index; xc.up_mask = up_mask; xc.silent = silent;
    xc.getbit = c_get; xc.putbit = c_put; xc.user = NULL;
    x2call_init(&X, &xc);
    for (n = 0; n < 8000L * secs; n++) {
        int i = (int)(n & (XD - 1));
        double v;
        uint8_t atx = answer_tick_pcm(&A, arx);
        down[i] = atx;
        dl[i] = (double)ulaw_to_linear(atx);
        up[i] = x2call_tick(&X, down[(n - d_ac) & (XD - 1)]);
        /* what reaches us: the client, our own echo off the line's hybrid, noise */
        v = 0.5 * up[(n - d_ca) & (XD - 1)]
            + 0.12 * dl[(n - e - 2) & (XD - 1)] + 0.08 * dl[(n - e - 4) & (XD - 1)] - 0.05 * dl[(n - e - 9) & (XD - 1)]
            + 2.0 * rng_gauss(&rng);
        arx = linear_to_ulaw((int32_t)lround(v));
        if (A.phase == ANS_X2 && o->x2_at < 0) o->x2_at = n;
        if (X.quiet && o->quiet_at < 0) o->quiet_at = n;
        if (A.connected && A.modulation == ANS_MOD_X2) o->connected_once = 1;
        if (A.phase == ANS_FAILED) { o->end_at = n; break; }
        if (data_bits && A.connected && x2call_state(&X) == X2C_DATA && DN.n >= data_bits && UP.n >= data_bits) break;
    }
    o->ok = A.connected && A.phase == ANS_X2 && x2call_state(&X) == X2C_DATA;
    o->down = A.bitrate; o->up = A.rx_bitrate; o->mod = A.modulation;
    o->error = A.u.x2.error; o->x2_failed = A.x2_failed;
}

static void x2_report(const char *what, const x2_outcome *o)
{
    const x2call_seen *s = &X.seen;
    printf("  x2 %s: %s %d / %d bit/s; down %ld bits %ld bad, up %ld bits %ld bad\n", what,
           o->ok ? "data" : "NO DATA", o->down, o->up, DN.n, DN.bad, UP.n, UP.bad);
    printf("     pad mode %d in the MP and the mapper; echo delay %d; upstream SNR %.1f dB\n",
           A.u.x2.pad_mode, A.u.x2.ecd.delay, answer_snr_db10(&A) / 10.0);
    printf("     client saw: preamble %ld bad, TRN %ld bad, J words %d + %d, ones %ld bad, alignment %ld bad,\n"
           "     sync %ld frames (%ld bad), records %d (%d bad), start %ld (%ld bad), data frames %ld (%ld lost)\n",
           s->pre_bad, s->trn_bad, s->jw_good, s->jw_last, s->ones_bad, s->align_bad, s->sync_frames, s->sync_bad,
           s->rec_good, s->rec_bad, s->start_ones, s->start_bad, s->data_frames, s->data_lost);
}

/* the client's script checks, clean */
static void x2_seen_clean(void)
{
    const x2call_seen *s = &X.seen;
    CHECK_EQ_I(s->not_x2, 0);
    CHECK_EQ_I(s->pre_bad, 0); CHECK_EQ_I(s->trn_bad, 0);
    CHECK_EQ_I(s->jw_good, 99); CHECK_EQ_I(s->jw_last, 1);
    CHECK_EQ_I(s->ones_bad, 0); CHECK_EQ_I(s->align_bad, 0);
    CHECK(s->sync_frames >= 1667); CHECK_EQ_I(s->sync_bad, 0);
    CHECK(s->rec_good >= 1); CHECK_EQ_I(s->rec_bad, 0);
    CHECK_EQ_I(s->start_ones, 680); CHECK_EQ_I(s->start_bad, 0);
    CHECK_EQ_I(s->data_lost, 0);
}

static void x2_tests(void)
{
    static const char *const pad_name[3] = { "no pad", "3 dB pad", "6 dB pad" };
    x2_outcome o;
    int pad;

    /* 1. x2 at 32000 down, 24000 up (the Courier's mask, 03FE), behind each pad */
    for (pad = 0; pad < 3; pad++) {
        x2_call(pad, 1, 0x03FE, X2C_SIL_NONE, pad == 0 ? -1 : 0, 40, 100000, &o);
        x2_report(pad_name[pad], &o);
        CHECK(o.ok);
        CHECK_EQ_I(o.mod, ANS_MOD_X2); CHECK_EQ_I(o.mod, 10 /* V90M_CONN_X2 */);
        CHECK_EQ_I(o.down, 32000); CHECK_EQ_I(o.up, 24000);
        CHECK_EQ_I(o.x2_failed, 0);
        CHECK_EQ_I(A.u.x2.pad_mode, pad); CHECK_EQ_I(A.u.x2.tx.map.pad_mode, pad);
        if (pad == 0) CHECK(A.u.x2.ecd.delay > 300 - 24 && A.u.x2.ecd.delay <= 300 + 2);   /* measured */
        x2_seen_clean();
        CHECK(DN.n >= 100000); CHECK_EQ_I(DN.bad, 0);
        CHECK(UP.n >= 100000); CHECK_EQ_I(UP.bad, 0);
        CHECK(answer_snr_db10(&A) > 200);
    }

    /* 2. the MP asks for index 5 (we have a bank for 1 only: 0x0002), or
     *    reports a pad of mode 3: the call ends, as x2's, soon after it */
    {
        static const struct { int pad, index, err; const char *what; } no[] = {
            { 0, 5, X2D_ERR_RATE, "MP asks for index 5" }, { 3, 1, X2D_ERR_PAD, "MP reports pad mode 3" } };
        int j;
        for (j = 0; j < 2; j++) {
            x2_call(no[j].pad, no[j].index, 0x03FE, X2C_SIL_NONE, 0, 30, 0, &o);
            printf("  x2 %s: ended %.2f s after the marker, error %d, x2_failed %d, %d MPs sent\n", no[j].what,
                   (o.end_at - o.x2_at) / 8000.0, o.error, o.x2_failed, X.mp_count);
            CHECK(o.x2_at > 0); CHECK(o.end_at > o.x2_at);
            CHECK_EQ_I(A.phase, ANS_FAILED); CHECK_EQ_I(o.x2_failed, 1); CHECK_EQ_I(o.connected_once, 0);
            CHECK_EQ_I(o.error, no[j].err);
            CHECK(X.mp_count >= 2 && X.mp_count <= 4);     /* at its second MP */
            CHECK_EQ_I(X.seen.rec_good, 0);
        }
    }

    /* 3. the client falls silent after its marker, after its J, after its
     *    MP, in data mode: the call ends within 10 s, with a reason */
    {
        static const struct { int sil, err; const char *what; } q[] = {
            { X2C_SIL_MARKER, X2D_ERR_NO_J, "after its marker" }, { X2C_SIL_J, X2D_ERR_NO_S4, "after its J" },
            { X2C_SIL_MP, X2D_ERR_NO_E, "after its MP" }, { X2C_SIL_DATA, X2D_ERR_CARRIER, "in data mode" } };
        int j;
        for (j = 0; j < 4; j++) {
            x2_call(0, 1, 0x03FE, q[j].sil, 0, 40, 0, &o);
            printf("  x2 client silent %s: the call ended %.2f s later, error %d, x2_failed %d\n", q[j].what,
                   (o.end_at - o.quiet_at) / 8000.0, o.error, o.x2_failed);
            CHECK(o.quiet_at > 0); CHECK(o.end_at > o.quiet_at);
            CHECK(o.end_at - o.quiet_at <= 8000L * 10);
            CHECK_EQ_I(A.phase, ANS_FAILED);
            CHECK_EQ_I(o.error, q[j].err);
            /* the end reason: x2's before data mode, the connection lost in it */
            CHECK_EQ_I(o.x2_failed, q[j].sil != X2C_SIL_DATA);
            CHECK_EQ_I(o.connected_once, q[j].sil == X2C_SIL_DATA);
        }
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

    /* 4. an x2 client's marker (4d) 170 ms after our L1 began, and then
     *    nothing: ANS_X2, and the end, said as x2's */
    {
        long t, x2_at = -1;
        slot = 0; mk_at = 170;
        dpsktx_init(&MK, DPSK_FC_B, 3000, 0);
        nmk = p2ans_x2_info_frame(0x4d, mkbits);
        start(base | ANS_MODE_X2, 1, 1);
        for (t = 0; t < 8000L * 14 && A.phase != ANS_FAILED; t++) {
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

    x2_tests();
    T_REPORT("answer_x2");
}
