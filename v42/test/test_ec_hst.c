/* The HST profile in the endpoint (spec "HST profile"): our answerer, MNP
 * on, linked to a stub pump (a hst_link_t owned by the test, state ON),
 * against sim/mnppeer in HST mode as the caller. The line: our bits at the
 * forward rate to the peer, the peer's at the back-channel rate to us
 * through a delay (the back receiver's hold-back), 1 ms ticks. What is
 * checked: our 0xC0 offer and the merge; the streamlined send rule and the
 * tolerant receiver; the mailbox (agreement, the caller's LA, LD);
 * attention codes as forward holder and as requester; LTs held while an
 * attention is pending; frames of an unknown id; the reversal policy; the
 * max_up cap; T401 with the back channel's latency; throughput at
 * 14400/450 and 9600/300 (figures under HST_VERBOSE). One line per case. */
#include <stdint.h>
#include "v42.h"
#include "hst_link.h"
#include "../../sim/mnppeer.h"
#include "test.h"

/* ---- the stub pump ---- */
typedef struct {
    hst_link_t L;
    int state;
} stub_t;

static hst_link_t *stub_get(void *ctx, int *state)
{
    stub_t *s = (stub_t *)ctx;
    *state = s->state;
    return s->state == HST_LINK_ON ? &s->L : NULL;
}

static stub_t S;
static v42ec_t E;
static v42ec_cfg_t C;
static mnppeer_t P;

/* ---- the line ---- */
static int fwd_bps, back_bps, acc_f, acc_b, clock_on;
static uint8_t dq[256];                /* the back channel's delay, dq_len bits */
static int dq_len, dq_pos;
static uint32_t rng;
static int fwd_err_ppm;                /* forward bit errors per million bits */
static long now;
static uint8_t ob[40000];              /* our first bits */
static int ob_n;
/* the LNA granting a reversal: its closing flag out before the peer has it? */
static int watch_rev, rev_early, rev_seen;

static int flip(void)
{
    if (fwd_err_ppm <= 0) return 0;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng % 1000000u < (uint32_t)fwd_err_ppm;
}

/* one millisecond: the ticks, then the bits of that millisecond */
static void step(void)
{
    now++;
    mnppeer_tick(&P, 1);
    if (clock_on) {
        acc_f += fwd_bps;
        while (acc_f >= 1000) {
            int y = v42ec_tx_bit(&E);
            acc_f -= 1000;
            if (ob_n < (int)sizeof ob) ob[ob_n++] = (uint8_t)y;
            if (flip()) y ^= 1;
            mnppeer_rx_bit(&P, y);
            if (watch_rev && S.L.rev_lna_sent && !rev_seen) {
                rev_seen = 1;
                rev_early = P.ln_code != -1;    /* the peer has not got that LNA */
            }
        }
        acc_b += back_bps;
        while (acc_b >= 1000) {
            int x = mnppeer_tx_bit(&P), d = x;
            acc_b -= 1000;
            if (dq_len > 0) { d = dq[dq_pos]; dq[dq_pos] = (uint8_t)x; dq_pos = (dq_pos + 1) % dq_len; }
            v42ec_rx_bit(&E, d);
        }
    }
    v42ec_tick(&E, 1);
}

static void run(int ms) { while (ms-- > 0) step(); }

static v42ec_status_t st(void)
{
    v42ec_status_t s;
    v42ec_get_status(&E, &s);
    return s;
}

/* ---- the call ---- */
/* BinModem's shape [BM]: parameter 8 = 3, 0xC0 flags/FC */
static mnp_peer_cfg peer_bm(int flags)
{
    mnp_peer_cfg c;
    memset(&c, 0, sizeof c);
    c.originator = 1; c.framing = 3; c.k = 8; c.n401 = 64;
    c.send_opt = 1; c.opt = 3;
    c.c0 = 1; c.c0_flags = flags; c.c0_rates = 0xFC;
    c.seed = 5u;
    return c;
}

/* the Courier's template [FW]: no parameter 8; streamlined all the same
 * (the patent), so which layout a Courier sends is open (question 5) */
static mnp_peer_cfg peer_courier(void)
{
    mnp_peer_cfg c = peer_bm(0x01);
    c.send_opt = 0; c.opt = 0;
    c.stream_always = 1;
    c.layout_rx_any = 1;
    return c;
}

/* our answerer linked to the stub (ON, our training out, holding the
 * forward channel at fb, the back at bb), the peer, the line with a hold
 * of hold_ms on the back channel */
static void hst_setup(mnp_peer_cfg pc, int fb, int bb, int hold_ms, uint8_t offer, uint8_t cap)
{
    int i;
    v42ec_cfg_default(&C, 0);
    C.mnp.enable = 1;
    v42ec_init(&E, &C);
    memset(&S, 0, sizeof S);
    S.state = HST_LINK_ON;
    S.L.offer_rates = offer;
    S.L.cap_up_code = cap;
    S.L.trained = 1;
    S.L.role = HST_ROLE_FWD;
    S.L.tx_bps = fb; S.L.rx_bps = bb; S.L.fwd_code = fb / 2400;
    v42ec_set_link(&E, stub_get, &S);
    if (pc.tx_bps == 0 && pc.t401_ms == 0) { pc.tx_bps = bb; pc.rx_bps = fb; }
    mnppeer_init(&P, &pc);
    fwd_bps = fb; back_bps = bb; acc_f = acc_b = 0; clock_on = 1;
    dq_len = hold_ms * bb / 1000;
    if (dq_len > (int)sizeof dq) dq_len = (int)sizeof dq;
    for (i = 0; i < dq_len; i++) dq[i] = 1;
    dq_pos = 0;
    rng = 0x9E3779B9u; fwd_err_ppm = 0;
    now = 0; ob_n = 0;
    watch_rev = rev_early = rev_seen = 0;
}

/* both ends connected within max_ms: the ms it took, -1 not */
static int connect(int max_ms)
{
    long t0 = now;
    while (now - t0 < max_ms) {
        step();
        if (v42ec_state(&E) == V42EC_CONNECTED && mnppeer_state(&P) == MNPP_CONNECTED) return (int)(now - t0);
    }
    return -1;
}

/* ---- data: a pattern each way, checked on arrival ---- */
static unsigned long up_made, up_got, up_bad, dn_made, dn_got, dn_bad;

static uint8_t pat_up(unsigned long i) { return (uint8_t)(i * 151u + (i >> 7) + 0x35u); }
static uint8_t pat_dn(unsigned long i) { return (uint8_t)(i * 89u + (i >> 5) + 0x5Au); }

static void data_reset(void) { up_made = up_got = up_bad = dn_made = dn_got = dn_bad = 0; }

/* write what fits up to the limits, read and check what came */
static void data_step(unsigned long up_lim, unsigned long dn_lim)
{
    uint8_t b[256];
    size_t n, i, w;
    n = up_lim - up_made < sizeof b ? (size_t)(up_lim - up_made) : sizeof b;
    for (i = 0; i < n; i++) b[i] = pat_up(up_made + i);
    w = n ? v42ec_write(&E, b, n) : 0;
    up_made += w;
    n = dn_lim - dn_made < sizeof b ? (size_t)(dn_lim - dn_made) : sizeof b;
    for (i = 0; i < n; i++) b[i] = pat_dn(dn_made + i);
    w = n ? mnppeer_write(&P, b, n) : 0;
    dn_made += w;
    while ((n = mnppeer_read(&P, b, sizeof b)) > 0)
        for (i = 0; i < n; i++, up_got++) if (b[i] != pat_up(up_got)) up_bad++;
    while ((n = v42ec_read(&E, b, sizeof b)) > 0)
        for (i = 0; i < n; i++, dn_got++) if (b[i] != pat_dn(dn_got)) dn_bad++;
}

/* data both ways until both limits arrived or max_ms: 1 if they did */
static int data_run(unsigned long up_lim, unsigned long dn_lim, int max_ms)
{
    long t0 = now;
    while (now - t0 < max_ms) {
        step();
        data_step(up_lim, dn_lim);
        if (up_got >= up_lim && dn_got >= dn_lim) return 1;
    }
    return 0;
}

/* ---- our bits: the first HDLC frame (copied from test_ec_link) ---- */
static int is_flag(const uint8_t *b, int i, int n)
{
    int k;
    if (i + 8 > n) return 0;
    for (k = 0; k < 8; k++) if (b[i + k] != ((0x7E >> k) & 1)) return 0;
    return 1;
}

static int first_frame(const uint8_t *b, int n, uint8_t *f, int max)
{
    int i = 0, j, len = 0, nb = 0, ones = 0;
    unsigned cur = 0;
    while (i < n && !is_flag(b, i, n)) i++;
    if (i >= n) return 0;
    while (is_flag(b, i, n)) i += 8;
    for (j = i; j < n && !is_flag(b, j, n); j++) {
        if (ones == 5) { ones = 0; if (b[j] == 0) continue; }
        ones = b[j] ? ones + 1 : 0;
        cur |= (unsigned)b[j] << nb;
        if (++nb == 8) {
            if (len < max) f[len++] = (uint8_t)cur;
            cur = 0; nb = 0;
        }
    }
    return j >= n ? 0 : len;
}

/* ---- the cases ---- */

/* our offer: the codes 2400n <= max_down_bps (offer_rates); the reply's
 * 0xC0 the merge of it and the caller's */
static void offer(void)
{
    uint8_t f[300];
    int n, c;
    v42ec_status_t s;
    hst_setup(peer_bm(0x01), 14400, 450, 0, 0x3C, 6);
    c = connect(5000);
    CHECK(c > 0);
    n = first_frame(ob, ob_n, f, (int)sizeof f) - 2;           /* less the FCS */
    CHECK(n > 4 && f[1] == 0x01);
    CHECK(n > 4 && f[n - 4] == 0xC0 && f[n - 3] == 0x02 && f[n - 2] == 0x01 && f[n - 1] == 0x3C);
    s = st();
    CHECK_EQ_I(s.hst, 1);
    CHECK_EQ_I(s.hst_rates, 0x3C);
    CHECK_EQ_I(s.hst_back300, 0);
    CHECK_EQ_I(S.L.agreed_rates, 0x3C);
    CHECK_EQ_I(P.hst, 1);
    CHECK_EQ_I(P.hst_rates, 0x3C);
    printf("offer: offer_rates 3C, the caller 01/FC: our LR ends %02X %02X %02X %02X; hst %d, rates %02X\n",
           f[n - 4], f[n - 3], f[n - 2], f[n - 1], s.hst, s.hst_rates);
}

/* streamlined when both LRs carry a non-zero parameter 8; the Courier's
 * template without it: standard from us, and its streamlined frames read */
static void streamlined_rule(void)
{
    int ok_a, ok_b, sa, sb, la, lb;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    data_reset();
    ok_a = data_run(6000, 300, 30000);
    CHECK(ok_a);
    CHECK(up_bad == 0 && dn_bad == 0);
    sa = st().mnp_streamlined;
    la = P.last_layout_rx;
    CHECK_EQ_I(sa, 1);
    CHECK_EQ_I(la, MNPP_LAYOUT_STREAM);

    hst_setup(peer_courier(), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    data_reset();
    ok_b = data_run(6000, 300, 30000);
    CHECK(ok_b);
    CHECK(up_bad == 0 && dn_bad == 0);
    sb = st().mnp_streamlined;
    lb = P.last_layout_rx;
    CHECK_EQ_I(sb, 0);
    CHECK_EQ_I(st().hst, 1);
    CHECK_EQ_I(lb, MNPP_LAYOUT_STD);
    CHECK_EQ_I(P.layout, MNPP_LAYOUT_STREAM);
    CHECK_EQ_I(st().rx_frames_bad, 0);
    printf("streamlined_rule: parameter 8 both: streamlined %d (the peer heard %d), data %s; "
           "the template: streamlined %d (the peer heard %d, sent streamlined), data %s\n",
           sa, la, ok_a ? "intact" : "LOST", sb, lb, ok_b ? "intact" : "LOST");
}

/* the mailbox: the agreement once the LRs are exchanged, the caller's LA */
static void mailbox(void)
{
    int ag = 0, la_then = -1, c;
    hst_setup(peer_bm(0x09), 14400, 450, 127, 0x7C, 6);
    while (now < 3000 && !S.L.agreed) step();
    ag = S.L.agreed;
    la_then = S.L.la_rx;
    CHECK_EQ_I(ag, 1);
    CHECK_EQ_I(la_then, 0);
    CHECK_EQ_I(S.L.agreed_rates, 0x7C);
    CHECK_EQ_I(S.L.back300, 1);
    c = connect(5000);
    CHECK(c >= 0);
    run(50);
    CHECK_EQ_I(S.L.la_rx, 1);
    CHECK_EQ_I(st().hst_back300, 1);
    printf("mailbox: agreed %d, rates %02X, back300 %d; la_rx %d at the agreement, %d after the caller's LA\n",
           ag, S.L.agreed_rates, S.L.back300, la_then, S.L.la_rx);
}

/* the LTs we send (new and again) */
static unsigned long lts(void) { return E.l.mnp.tx_lt + E.l.mnp.tx_retx; }

/* the peer's LN code, its LNA back (the peer's ln_code back to -1) */
static int peer_ln(int code, int max_ms)
{
    long t0 = now;
    mnppeer_send_ln(&P, code);
    while (now - t0 < max_ms) {
        step();
        data_step(up_made + 64, dn_made);
        if (P.ln_code == -1 && P.lnq_n == 0 && now - t0 > 1) return 1;
    }
    return 0;
}

/* attention from the caller while we hold the forward channel */
static void attn_in(void)
{
    unsigned long n0, viol;
    int acked, held, early, r23, r35;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    data_reset();
    run(100);
    /* 0x14: a reversal at 9600; the LNA, then rev_lna_sent once its
     * closing flag has gone, LTs held until the epoch */
    watch_rev = 1;
    acked = peer_ln(0x14, 3000);
    CHECK(acked);
    early = rev_early;
    CHECK(rev_seen);
    CHECK_EQ_I(early, 0);
    CHECK_EQ_I(S.L.rev_lna_sent, 1);
    CHECK_EQ_I(S.L.rev_code, 4);
    CHECK_EQ_I(st().attn_rx, 0x14);
    n0 = lts();
    run(1000);
    data_step(up_made + 200, dn_made);
    run(1000);
    held = lts() == n0;
    CHECK(held);
    CHECK(E.txr.len > 0);
    S.L.epoch++;                         /* the pump: the reversal is done */
    run(300);
    CHECK(lts() > n0);
    /* 0x2R and 0x3R with R agreed: the order for the holder */
    CHECK(peer_ln(0x23, 3000));
    r23 = S.L.rate_order;
    CHECK_EQ_I(r23, 0x23);
    S.L.rate_order = 0; S.L.epoch++;
    CHECK(peer_ln(0x35, 3000));
    r35 = S.L.rate_order;
    CHECK_EQ_I(r35, 0x35);
    S.L.rate_order = 0; S.L.epoch++;
    /* 16800 (never agreed) and an unknown class: an LNA, a count, nothing */
    viol = E.l.mnp.violations;
    CHECK(peer_ln(0x37, 3000));
    CHECK(peer_ln(0x45, 3000));
    CHECK_EQ_I(E.l.mnp.violations, viol + 2);
    CHECK_EQ_I(S.L.rate_order, 0);
    n0 = lts();
    data_step(up_made + 200, dn_made);
    run(500);
    CHECK(lts() > n0);                   /* no hold for them */
    printf("attn_in: LN 14: LNA, rev_lna_sent after its closing flag (early %d), rev_code %d, LTs held %d until the epoch; "
           "LN 23, 35: rate_order %02X, %02X; LN 37, 45: violations +%lu, no order\n",
           early, S.L.rev_code, held, r23, r35, E.l.mnp.violations - viol);
}

/* attention the pump asks for: our LN, LTs held until the epoch, the
 * control frames still going */
static void attn_out(void)
{
    unsigned long n0, la0;
    int code = -1, t, held, lna_ok;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    data_reset();
    run(100);
    S.L.ln_req = 0x16;
    for (t = 0; t < 3000 && code < 0; t++) {
        step();
        code = mnppeer_get_ln(&P);
    }
    CHECK_EQ_I(code, 0x16);
    CHECK_EQ_I(S.L.ln_req, 0);
    CHECK_EQ_I(st().attn_tx, 0x16);
    run(1000);                           /* its LNA comes back */
    CHECK_EQ_I(S.L.rev_lna_rx, 1);
    CHECK_EQ_I(S.L.rev_code, 6);
    n0 = lts();
    la0 = E.l.mnp.tx_la;
    for (t = 0; t < 2000; t++) {         /* data both ways: LAs go, no LT */
        step();
        data_step(up_made + 16, dn_made + 16);
    }
    held = lts() == n0;
    CHECK(held);
    CHECK(E.l.mnp.tx_la > la0);
    lna_ok = peer_ln(0x24, 3000);        /* an LNA goes */
    CHECK(lna_ok);
    S.L.rate_order = 0;
    S.L.epoch++;
    run(300);
    CHECK(lts() > n0);
    printf("attn_out: ln_req 16: LN %02X sent, attn_tx %02X, rev_lna_rx %d; LTs held %d, LAs %lu, the peer's LN answered %d; "
           "LTs after the epoch\n", code, st().attn_tx, S.L.rev_lna_rx, held, E.l.mnp.tx_la - la0, lna_ok);
}

/* a pump that never reports the new state: the hold on our LTs ends after
 * MNP_HST_HOLD_MAX_MS all the same (every wait bounded) */
static void hold_bound(void)
{
    unsigned long n0;
    int t, t_free = -1;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    data_reset();
    run(100);
    S.L.ln_req = 0x16;
    run(500);
    CHECK_EQ_I(E.l.mnp.attn_hold, 1);
    n0 = lts();
    for (t = 500; t < 14000 && t_free < 0; t++) {
        step();
        data_step(up_made + 16, dn_made);
        if (lts() != n0) t_free = t;
    }
    CHECK(t_free >= MNP_HST_HOLD_MAX_MS && t_free <= MNP_HST_HOLD_MAX_MS + 100);
    CHECK_EQ_I(v42ec_state(&E), V42EC_CONNECTED);
    printf("hold_bound: no epoch: LTs again %d ms after the LN was asked for\n", t_free);
}

/* a frame of an unknown id: counted, dropped, unacknowledged; its data
 * comes one T401 later in the ordinary LT */
static void unknown(void)
{
    mnp_peer_cfg pc = peer_bm(0x01);
    uint8_t b[16];
    long t_unk = -1, t_data = -1;
    unsigned long la0 = 0;
    int got = 0;
    v42ec_status_t s;
    pc.unknown_ids = 1;
    pc.t401_ms = 3000;
    hst_setup(pc, 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    run(200);
    mnppeer_write(&P, (const uint8_t *)"hello", 5);
    while (now < 20000 && t_data < 0) {
        step();
        if (t_unk < 0 && st().rx_unknown > 0) { t_unk = now; la0 = E.l.mnp.tx_la; }
        if (t_unk >= 0 && t_data < 0 && (got = (int)v42ec_read(&E, b, sizeof b)) > 0) t_data = now;
    }
    s = st();
    CHECK_EQ_I(s.rx_unknown, 1);
    CHECK_EQ_I(s.rx_unknown_id, 0x3F);
    CHECK_EQ_I(got, 5);
    CHECK(got == 5 && memcmp(b, "hello", 5) == 0);
    CHECK(t_unk > 0 && t_data > t_unk);
    CHECK_EQ_I(E.l.mnp.tx_la, la0);      /* none until the LT came again */
    CHECK_NEAR(t_data - t_unk, 3000, 150);
    printf("unknown: rx_unknown %lu, id %02X, no LA for it; its data %ld ms later (the peer's T401 3000 ms)\n",
           s.rx_unknown, s.rx_unknown_id, t_data - t_unk);
}

/* holding the back channel: ask for the forward one when the backlog is
 * over 64 octets and the far end has held it 2000 ms */
static int policy_run(int octets, int ms)
{
    uint8_t b[128];
    int i;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    run(200);
    clock_on = 0;                        /* nothing goes or comes: the backlog stays */
    for (i = 0; i < octets; i++) b[i] = (uint8_t)i;
    CHECK_EQ_I(v42ec_write(&E, b, (size_t)octets), octets);
    S.L.role = HST_ROLE_BACK;            /* the caller has taken the forward channel */
    S.L.fwd_code = 5;
    S.L.epoch++;
    run(1);                              /* the tick that sees it */
    run(ms);
    return E.l.mnp.ln_code;
}

static void reversal_policy(void)
{
    int a = policy_run(65, 2000), b, c, code = -1, t;
    CHECK_EQ_I(a, 0x15);
    clock_on = 1;
    for (t = 0; t < 3000 && code < 0; t++) { step(); code = mnppeer_get_ln(&P); }
    CHECK_EQ_I(code, 0x15);
    b = policy_run(64, 3000);
    CHECK_EQ_I(b, -1);
    c = policy_run(65, 1999);
    CHECK_EQ_I(c, -1);
    printf("reversal_policy: 65 octets after 2000 ms: LN %02X (the peer got %02X); 64 octets after 3000 ms: %d; "
           "65 after 1999 ms: %d\n", a, code, b, c);
}

/* max_up_bps: a reversal above cap_up_code is granted, then our 0x2R at
 * the cap once the caller's training is in; its 0x3R above it, an LNA, a
 * count and our 0x2R again */
static void max_up(void)
{
    int c1 = -1, c2 = -1, t;
    unsigned long viol;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 4);
    CHECK(connect(5000) > 0);
    run(100);
    CHECK(peer_ln(0x16, 3000));
    CHECK_EQ_I(S.L.rev_lna_sent, 1);
    run(500);
    CHECK_EQ_I(mnppeer_get_ln(&P), -1);  /* nothing before the switch */
    S.L.role = HST_ROLE_BACK;            /* the caller trained forward at 14400 */
    S.L.fwd_code = 6;
    S.L.epoch++;
    for (t = 0; t < 3000 && c1 < 0; t++) { step(); c1 = mnppeer_get_ln(&P); }
    CHECK_EQ_I(c1, 0x24);
    run(1000);
    S.L.epoch++;                         /* the caller retrained at 9600 */
    viol = E.l.mnp.violations;
    CHECK(peer_ln(0x36, 3000));
    CHECK_EQ_I(E.l.mnp.violations, viol + 1);
    CHECK_EQ_I(S.L.rate_order, 0);
    for (t = 0; t < 3000 && c2 < 0; t++) { step(); c2 = mnppeer_get_ln(&P); }
    CHECK_EQ_I(c2, 0x24);
    printf("max_up: cap code 4: LN 16 granted, then our %02X after the switch; its LN 36: a count, our %02X\n",
           c1, c2);
}

/* the far end's LD into the mailbox */
static void far_ld(void)
{
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    run(100);
    mnppeer_release(&P);
    run(500);
    CHECK_EQ_I(S.L.far_ld, 1);
    CHECK_EQ_I(st().peer_ld_reason, 255);
    CHECK_EQ_I(v42ec_state(&E), V42EC_DISCONNECTED);
    printf("far_ld: far_ld %d, peer_ld_reason %d\n", S.L.far_ld, st().peer_ld_reason);
}

/* T401 over HST: Annex A's formula plus the far end's one LT queued ahead
 * of its LA and the back receiver's hold-back (127 ms at 450, 180 at 300);
 * a rate change without an epoch recomputes it too (F18) */
static void t401(void)
{
    int a, b, c, d;
    hst_setup(peer_bm(0x01), 14400, 450, 127, 0x7C, 6);
    CHECK(connect(5000) > 0);
    a = E.l.mnp.t401_ms;
    S.L.tx_bps = 9600;                   /* a retrain at 9600, no epoch */
    run(1);
    b = E.l.mnp.t401_ms;
    S.L.tx_bps = 450; S.L.rx_bps = 9600; S.L.role = HST_ROLE_BACK; S.L.epoch++;
    run(1);
    c = E.l.mnp.t401_ms;
    hst_setup(peer_bm(0x09), 9600, 300, 180, 0x7C, 6);
    CHECK(connect(8000) > 0);
    d = E.l.mnp.t401_ms;
    /* 1560 + 1245 + 127; 1716 + 1245 + 127; 2 (4 x 8 x 70 / 450 + 8 x 7 /
     * 9600) s + 1000 = 10968 + 59 queued at 9600; 1840 + 1867 + 180 */
    CHECK_EQ_I(a, 2932);
    CHECK_EQ_I(b, 3088);
    CHECK_EQ_I(c, 11027);
    CHECK_EQ_I(d, 3887);
    printf("t401: 14400/450 %d ms, 9600/450 %d ms, 450/9600 %d ms, 9600/300 %d ms\n", a, b, c, d);
}

/* throughput, our LTs at the forward rate, the caller's LAs on the back
 * channel with its hold-back: the share of the line's rate delivered, and
 * LTs sent again per LT lost */
static void tput_run(int fb, int bb, int hold, int flags, int err_ppm, int secs, long *pct, long *rpl)
{
    long t0;
    unsigned long g0, r0, b0, lost;
    hst_setup(peer_bm(flags), fb, bb, hold, 0x7C, 6);
    CHECK(connect(8000) > 0);
    data_reset();
    fwd_err_ppm = err_ppm;
    t0 = now;
    while (now - t0 < 2000) { step(); data_step(up_made + 512, 0); }
    g0 = up_got; r0 = E.l.mnp.tx_retx; b0 = (unsigned long)P.rx_bad;
    t0 = now;
    while (now - t0 < secs * 1000L) { step(); data_step(up_made + 512, 0); }
    CHECK_EQ_I(up_bad, 0);
    CHECK_EQ_I(v42ec_state(&E), V42EC_CONNECTED);
    *pct = (long)((up_got - g0) * 8u * 1000u / ((unsigned long)fb * (unsigned long)secs)) ;
    lost = (unsigned long)P.rx_bad - b0;
    *rpl = lost ? (long)((E.l.mnp.tx_retx - r0) * 100u / lost) : 0;
}

static void throughput(void)
{
#ifdef TEST_SHORT
    int secs = 10;
#else
    int secs = 30;
#endif
    long p1, p2, p3, p4, r1, r2, r3, r4;
    tput_run(14400, 450, 127, 0x01, 0, secs, &p1, &r1);
    tput_run(9600, 300, 180, 0x09, 0, secs, &p2, &r2);
    tput_run(14400, 450, 127, 0x01, 20, secs, &p3, &r3);
    tput_run(9600, 300, 180, 0x09, 20, secs, &p4, &r4);
    /* the bounds: k 8 frames of 64 octets per round trip (the caller's LA
     * at 450 or 300 bit/s and its hold-back) is about 77 % of the line;
     * go-back-N sends again what was in flight behind a loss, at most a
     * window (A.7.3.2.2: the receiver keeps nothing out of sequence).
     * Annex A's literal go-back on every repeated N(R) gave 66 % clean
     * and 21 to 24 sent again per loss here (task 8 report). */
    CHECK(p1 >= 700);
    CHECK(p2 >= 700);
    CHECK(p3 >= 550);
    CHECK(p4 >= 550);
    CHECK(r3 > 0 && r3 <= 800);
    CHECK(r4 > 0 && r4 <= 800);
#ifdef HST_VERBOSE
    printf("throughput (per mille of the line, retx per 100 lost): 14400/450 %ld; 9600/300 %ld; "
           "20 ppm: %ld (%ld), %ld (%ld)\n", p1, p2, p3, r3, p4, r4);
#endif
    printf("throughput: clean >= 70 %% at 14400/450 and 9600/300: %s; bit errors: >= 55 %%, "
           "at most k sent again per LT lost: %s\n",
           p1 >= 700 && p2 >= 700 ? "yes" : "NO",
           p3 >= 550 && p4 >= 550 && r3 > 0 && r3 <= 800 && r4 > 0 && r4 <= 800 ? "yes" : "NO");
}

int main(void)
{
    offer();
    streamlined_rule();
    mailbox();
    attn_in();
    attn_out();
    hold_bound();
    unknown();
    reversal_policy();
    max_up();
    far_ld();
    t401();
    throughput();
    T_REPORT("test_ec_hst");
}
