/* The endpoint (v42ec_t, answerer, MNP on) linked to an HST pump through
 * the mailbox of hst_link.h, with a stub pump here: a hst_link_t owned by
 * the test and a get function that returns it with the state the test
 * sets. What is checked: T400 held while the modem has not decided; the
 * pre-connect ring drained into the receive path (in v42ec_tick and at
 * the top of v42ec_tx_bit and v42ec_rx_bit), so an LR sent during our
 * training is answered at the first transmitted bit; the HST window (our
 * training's end plus 5 s, NO_EC, no start-stop) in T400's place;
 * v42ec_reset keeping the link and a captured LR; plain MNP over the link.
 * The caller's frames are built here (own HDLC bit layer and CRC) or come
 * from sim/mnppeer. 1 ms ticks. One line per case. */
#include <stdint.h>
#include "v42.h"
#include "hst_link.h"
#include "../../sim/mnppeer.h"
#include "test.h"

/* ---- the stub pump ---- */
typedef struct {
    hst_link_t L;
    int state;                 /* HST_LINK_* */
    unsigned long calls;       /* get calls */
} stub_t;

static hst_link_t *stub_get(void *ctx, int *state)
{
    stub_t *s = (stub_t *)ctx;
    s->calls++;
    *state = s->state;
    return s->state == HST_LINK_ON ? &s->L : NULL;
}

static stub_t S;
static v42ec_t E;
static v42ec_cfg_t C;

/* our answerer, MNP on, linked (link 1) or not, the stub in state st */
static void ep_init(int link, int st)
{
    v42ec_cfg_default(&C, 0);
    C.mnp.enable = 1;
    v42ec_init(&E, &C);
    memset(&S, 0, sizeof S);
    S.state = st;
    if (link) v42ec_set_link(&E, stub_get, &S);
}

static int ep_proto(void)
{
    v42ec_status_t s;
    v42ec_get_status(&E, &s);
    return s.protocol;
}

static int ep_reason(void)
{
    v42ec_status_t s;
    v42ec_get_status(&E, &s);
    return s.reason;
}

/* ---- the caller's bits, built here: HDLC with its own CRC ---- */
static uint8_t cb[8000];
static int cb_n, cb_i;

static void cbit(int b) { if (cb_n < (int)sizeof cb) cb[cb_n++] = (uint8_t)b; }

static void cflag(void)
{
    int i;
    for (i = 0; i < 8; i++) cbit((0x7E >> i) & 1);
}

/* x^16 + x^12 + x^5 + 1, reflected, bit by bit */
static uint16_t crc16(const uint8_t *d, int n)
{
    uint16_t crc = 0xFFFFu;
    int i, b;
    for (i = 0; i < n; i++)
        for (b = 0; b < 8; b++) {
            unsigned x = (crc ^ (unsigned)(d[i] >> b)) & 1u;
            crc = (uint16_t)(x ? (crc >> 1) ^ 0x8408u : crc >> 1);
        }
    return (uint16_t)(crc ^ 0xFFFFu);
}

/* flag, body and FCS with zeros inserted, flag */
static void cframe(const uint8_t *body, int n)
{
    uint8_t f[300];
    uint16_t c = crc16(body, n);
    int i, b, ones = 0;
    memcpy(f, body, (size_t)n);
    f[n] = (uint8_t)(c & 0xFF);
    f[n + 1] = (uint8_t)(c >> 8);
    cflag();
    for (i = 0; i < n + 2; i++)
        for (b = 0; b < 8; b++) {
            int x = (f[i] >> b) & 1;
            cbit(x);
            ones = x ? ones + 1 : 0;
            if (ones == 5) { cbit(0); ones = 0; }
        }
    cflag();
}

/* the Courier's LR (hst-facts.md `LR and 0xC0`): framing 3, k 8, N401 64,
 * no parameter 8, no 0xC0 */
static const uint8_t lr_courier[] = {
    0x14, 0x01, 0x02, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x02, 0x01, 0x03, 0x03, 0x01, 0x08, 0x04, 0x02, 0x40, 0x00 };

/* the caller during our training: ten flags, its LR, then flags */
static void caller_lr(int tail_flags)
{
    int i;
    cb_n = cb_i = 0;
    for (i = 0; i < 10; i++) cflag();
    cframe(lr_courier, (int)sizeof lr_courier);
    for (i = 0; i < tail_flags; i++) cflag();
}

/* the pump: the caller's next n bits (mark once they are out) into the ring */
static void pump_bits(int n)
{
    while (n-- > 0) (void)hst_pre_put(&S.L, cb_i < cb_n ? cb[cb_i++] : 1);
}

/* ---- our bits: the first HDLC frame among them ---- */
static uint8_t ob[4000];

static void our_bits(int n)
{
    int i;
    for (i = 0; i < n && i < (int)sizeof ob; i++) ob[i] = (uint8_t)v42ec_tx_bit(&E);
}

static int is_flag(const uint8_t *b, int i, int n)
{
    int k;
    if (i + 8 > n) return 0;
    for (k = 0; k < 8; k++) if (b[i + k] != ((0x7E >> k) & 1)) return 0;
    return 1;
}

/* the first frame after a flag in b[0..n): body and FCS into f (zeros
 * removed), its length (0 none); *at the index of its first bit */
static int first_frame(const uint8_t *b, int n, uint8_t *f, int max, int *at)
{
    int i = 0, j, len = 0, nb = 0, ones = 0;
    unsigned cur = 0;
    while (i < n && !is_flag(b, i, n)) i++;
    if (i >= n) return 0;
    while (is_flag(b, i, n)) i += 8;
    *at = i;
    for (j = i; j < n && !is_flag(b, j, n); j++) {
        if (ones == 5) { ones = 0; if (b[j] == 0) continue; }
        ones = b[j] ? ones + 1 : 0;
        cur |= (unsigned)b[j] << nb;
        if (++nb == 8) {
            if (len < max) f[len++] = (uint8_t)cur;
            cur = 0; nb = 0;
        }
    }
    if (j >= n) return 0;
    return len;
}

/* our first frame is an LR with a good FCS: 1, and where it began */
static int our_lr_first(int *at)
{
    uint8_t f[300];
    int n;
    *at = -1;
    our_bits(600);
    n = first_frame(ob, 600, f, (int)sizeof f, at);
    if (n < 5) return 0;
    if (crc16(f, n - 2) != (uint16_t)(f[n - 2] | (f[n - 1] << 8))) return 0;
    return f[1] == 0x01 && f[2] == 0x02 && f[0] + 3 == n;
}

/* ---- the cases ---- */

/* Review Focus 2's unit form: UNDECIDED holds T400; from NONE on, 750 ms */
static void undecided_holds_t400(void)
{
    int t, acc = 0, st3, unl;
    ep_init(1, HST_LINK_UNDECIDED);
    for (t = 0; t < 3000; t++) v42ec_tick(&E, 1);
    st3 = v42ec_state(&E);
    CHECK_EQ_I(st3, V42EC_DETECT);
    CHECK(S.calls >= 3000);
    S.state = HST_LINK_NONE;
    for (t = 0; t < 5000 && v42ec_state(&E) == V42EC_DETECT; ) {
        v42ec_tick(&E, 1);
        t++;
        acc += 2400;
        while (acc >= 1000) { acc -= 1000; v42ec_rx_bit(&E, 1); (void)v42ec_tx_bit(&E); }
    }
    CHECK_EQ_I(t, 750);
    CHECK_EQ_I(v42ec_state(&E), V42EC_ASYNC);
    /* the same endpoint unlinked: T400 from its start */
    ep_init(0, HST_LINK_UNDECIDED);
    for (unl = 0; unl < 3000 && v42ec_state(&E) == V42EC_DETECT; unl++) v42ec_tick(&E, 1);
    CHECK_EQ_I(unl, 750);
    CHECK_EQ_I(S.calls, 0);
    printf("undecided_holds_t400: UNDECIDED 3000 ms: state %d; NONE: start-stop %d ms later; unlinked: at %d ms\n",
           st3, t, unl);
}

/* an LR in the ring during our training: captured, answered at the first
 * transmitted bit; through the ticks and with no tick at all */
static void lr_from_ring(void)
{
    int t, at_a, at_b, ok_a, ok_b, proto_before;
    ep_init(1, HST_LINK_ON);
    caller_lr(200);
    for (t = 0; t < 1400; t++) {        /* our training: no getbit, no putbit */
        if (t % 20 < 9) pump_bits(1);   /* 450 bit/s */
        v42ec_tick(&E, 1);
    }
    CHECK(cb_i > cb_n - 200 * 8);       /* the LR is in */
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_MNP);
    CHECK_EQ_I(v42ec_state(&E), V42EC_ESTABLISH);
    CHECK_EQ_I(S.L.pre_r, S.L.pre_w);   /* drained */
    ok_a = our_lr_first(&at_a);
    CHECK(ok_a);
    CHECK_EQ_I(at_a, 16);               /* two flags, then our LR */

    ep_init(1, HST_LINK_ON);
    caller_lr(4);
    pump_bits(cb_n);                    /* no tick: nothing drained yet */
    proto_before = ep_proto();
    CHECK_EQ_I(proto_before, V42EC_PROTO_NONE);
    CHECK_EQ_I(S.L.pre_w - S.L.pre_r, cb_n);
    ok_b = our_lr_first(&at_b);
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_MNP);
    CHECK(ok_b);
    CHECK_EQ_I(at_b, 16);
    CHECK_EQ_I(S.L.pre_lost, 0);
    printf("lr_from_ring: ticks: protocol MNP, our LR %s at bit %d; no tick: protocol %d, then our LR %s at bit %d\n",
           ok_a ? "out" : "MISSING", at_a, proto_before, ok_b ? "out" : "MISSING", at_b);
}

/* no LR on an HST call: NO_EC at our training's end plus 5 s, never
 * start-stop; with mark (detection) and with flags (LAPM establishment) */
static int window_run(int flags, int *at750)
{
    int t, end = -1, async = 0;
    ep_init(1, HST_LINK_ON);
    cb_n = cb_i = 0;
    if (flags) while (cb_n + 8 <= (int)sizeof cb) cflag();
    for (t = 1; t <= 8000 && end < 0; t++) {
        if (t % 20 < 9) pump_bits(1);
        if (t == 1400) S.L.trained = 1;
        v42ec_tick(&E, 1);
        if (t == 751) *at750 = v42ec_state(&E);
        if (v42ec_state(&E) == V42EC_ASYNC) async = 1;
        if (v42ec_state(&E) == V42EC_DISCONNECTED) end = t;
    }
    CHECK(!async);
    CHECK_EQ_I(ep_reason(), V42EC_R_NO_EC);
    return end;
}

static void window(void)
{
    int s1 = -1, s2 = -1, e1, e2;
    e1 = window_run(0, &s1);
    CHECK_EQ_I(s1, V42EC_DETECT);
    CHECK_NEAR(e1, 1400 + 5000, 1);
    e2 = window_run(1, &s2);
    CHECK_EQ_I(s2, V42EC_ESTABLISH);
    CHECK_NEAR(e2, 1400 + 5000, 1);
    printf("window: trained at 1400 ms; mark: state %d at 751 ms, NO_EC at %d ms; flags: state %d at 751 ms, NO_EC at %d ms\n",
           s1, e1, s2, e2);
}

/* v42ec_reset keeps the link and a captured LR (or one still in the
 * ring); with mnp.enable 0 it unlinks; v42ec_init starts clean */
static void reset_keeps(void)
{
    int t, at_a, at_b, ok_a, ok_b, linked_a, linked_c, t400;
    unsigned long calls;
    v42ec_cfg_t off;

    ep_init(1, HST_LINK_ON);
    caller_lr(4);
    for (t = 0; t < 1400; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_MNP);
    v42ec_reset(&E, &C);                /* the host's reset at CONNECTED */
    linked_a = v42ec_linked(&E);
    CHECK_EQ_I(linked_a, 1);
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_MNP);
    ok_a = our_lr_first(&at_a);
    CHECK(ok_a);
    CHECK_EQ_I(at_a, 16);

    ep_init(1, HST_LINK_ON);
    caller_lr(4);
    pump_bits(cb_n);                    /* in the ring, not drained */
    v42ec_reset(&E, &C);
    CHECK_EQ_I(v42ec_linked(&E), 1);
    CHECK_EQ_I(S.L.pre_w - S.L.pre_r, cb_n);
    ok_b = our_lr_first(&at_b);
    CHECK(ok_b);
    CHECK_EQ_I(at_b, 16);

    off = C;
    off.mnp.enable = 0;
    v42ec_reset(&E, &off);
    linked_c = v42ec_linked(&E);
    CHECK_EQ_I(linked_c, 0);
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_NONE);
    CHECK_EQ_I(v42ec_state(&E), V42EC_DETECT);
    calls = S.calls;
    for (t400 = 0; t400 < 3000 && v42ec_state(&E) == V42EC_DETECT; t400++) v42ec_tick(&E, 1);
    CHECK_EQ_I(S.calls, calls);
    CHECK_EQ_I(t400, 750);

    ep_init(1, HST_LINK_ON);
    v42ec_init(&E, &C);                 /* a clean start: no link */
    CHECK_EQ_I(v42ec_linked(&E), 0);
    printf("reset_keeps: captured LR: linked %d, our LR %s at bit %d; LR in the ring: %s at bit %d; "
           "mnp.enable 0: linked %d, start-stop at %d ms\n",
           linked_a, ok_a ? "out" : "MISSING", at_a, ok_b ? "out" : "MISSING", at_b, linked_c, t400);
}

/* an MNP caller without 0xC0 over the link: its LR during our training
 * through the ring, then the line at 2400: plain MNP, data both ways */
static mnppeer_t P;

static void plain_mnp_over_link(void)
{
    mnp_peer_cfg pc;
    int t, acc = 0, t401_2400;
    size_t up_made = 0, up_got = 0, dn_made = 0, dn_got = 0;
    uint32_t hu_in = 2166136261u, hu_out = 2166136261u, hd_in = 2166136261u, hd_out = 2166136261u;
    uint8_t buf[256];
    int i, st6500 = -1;

    memset(&pc, 0, sizeof pc);
    pc.originator = 1; pc.framing = 3; pc.k = 8; pc.n401 = 64; pc.seed = 7u;
    mnppeer_init(&P, &pc);
    ep_init(1, HST_LINK_ON);
    for (t = 1; t <= 30000; t++) {
        mnppeer_tick(&P, 1);
        if (t == 1400) S.L.trained = 1;
        acc += 2400;
        while (acc >= 1000) {
            int x = mnppeer_tx_bit(&P);
            acc -= 1000;
            if (t <= 1400) {            /* our training: the pump hands its bits over */
                (void)hst_pre_put(&S.L, x);
                mnppeer_rx_bit(&P, 1);
            } else {                    /* CONNECTED: getbit and putbit */
                int y = v42ec_tx_bit(&E);
                v42ec_rx_bit(&E, x);
                mnppeer_rx_bit(&P, y);
            }
        }
        v42ec_tick(&E, 1);
        if (t == 6500) st6500 = v42ec_state(&E);
        if (v42ec_state(&E) == V42EC_CONNECTED) {
            uint8_t d[64];
            size_t n, w;
            for (i = 0; i < 64; i++) d[i] = (uint8_t)(up_made + (size_t)i);
            n = up_made + 64 <= 3000 ? 64 : 3000 - up_made;
            w = v42ec_write(&E, d, n);
            for (i = 0; i < (int)w; i++) hu_in = (hu_in ^ d[i]) * 16777619u;
            up_made += w;
            for (i = 0; i < 64; i++) d[i] = (uint8_t)(dn_made * 7u + (size_t)i);
            n = dn_made + 64 <= 3000 ? 64 : 3000 - dn_made;
            w = mnppeer_write(&P, d, n);
            for (i = 0; i < (int)w; i++) hd_in = (hd_in ^ d[i]) * 16777619u;
            dn_made += w;
        }
        while ((i = (int)mnppeer_read(&P, buf, sizeof buf)) > 0) {
            int k;
            for (k = 0; k < i; k++) hu_out = (hu_out ^ buf[k]) * 16777619u;
            up_got += (size_t)i;
        }
        while ((i = (int)v42ec_read(&E, buf, sizeof buf)) > 0) {
            int k;
            for (k = 0; k < i; k++) hd_out = (hd_out ^ buf[k]) * 16777619u;
            dn_got += (size_t)i;
        }
        if (t > 6500 && up_got >= up_made && dn_got >= dn_made && up_made >= 3000 && dn_made >= 3000) break;
    }
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_MNP);
    CHECK_EQ_I(v42ec_state(&E), V42EC_CONNECTED);
    CHECK_EQ_I(st6500, V42EC_CONNECTED);
    CHECK_EQ_I(S.L.agreed, 1);
    CHECK_EQ_I(S.L.agreed_rates, 0);
    CHECK_EQ_I(S.L.back300, 0);
    CHECK(up_got == up_made && hu_in == hu_out && up_made >= 3000);
    CHECK(dn_got == dn_made && hd_in == hd_out && dn_made >= 3000);
    /* the pump's rates reach T401 */
    t401_2400 = E.l.mnp.t401_ms;
    S.L.tx_bps = 9600; S.L.rx_bps = 450;
    v42ec_tick(&E, 1);
    CHECK_EQ_I(E.l.mnp.tx_bps, 9600);
    CHECK_EQ_I(E.l.mnp.rx_bps, 450);
    CHECK(E.l.mnp.t401_ms != t401_2400);
    printf("plain_mnp_over_link: agreed %d, rates 0x%02X, back300 %d; state %d at 6500 ms; 3000 octets each way %s; "
           "T401 %d ms at 2400, %d ms at 9600/450\n",
           S.L.agreed, S.L.agreed_rates, S.L.back300, st6500,
           hu_in == hu_out && hd_in == hd_out ? "intact" : "DAMAGED", t401_2400, E.l.mnp.t401_ms);
}

/* ---- the next call on the same endpoint: a caller at 2400 bit/s ---- */
static v42ec_t O;                      /* an originator with V.42 (ODP, LAPM) */

/* our endpoint E against a caller: 0, an originator with V.42; 1, one
 * without error control (characters after mark). ms of it; our bits'
 * FNV-1a in *h, the ms E left detection in *t_left (-1 never); returns
 * E's protocol */
static int call_run(int caller, int ms, uint32_t *h, int *t_left)
{
    int t, acc = 0, ci = 0;
    v42ec_cfg_t oc;
    static const char text[] = "hello, no error control here\r\n";
    *h = 2166136261u;
    *t_left = -1;
    v42ec_cfg_default(&oc, 1);
    v42ec_init(&O, &oc);
    cb_n = cb_i = 0;
    for (t = 0; t < 120; t++) cbit(1);
    if (caller == 1)
        for (ci = 0; text[ci]; ci++) {
            int b;
            cbit(0);
            for (b = 0; b < 8; b++) cbit(((unsigned char)text[ci] >> b) & 1);
            cbit(1); cbit(1);
        }
    for (t = 1; t <= ms; t++) {
        v42ec_tick(&E, 1);
        if (caller == 0) v42ec_tick(&O, 1);
        acc += 2400;
        while (acc >= 1000) {
            int y = v42ec_tx_bit(&E), x;
            acc -= 1000;
            if (caller == 0) x = v42ec_tx_bit(&O);
            else x = cb_i < cb_n ? cb[cb_i++] : 1;
            v42ec_rx_bit(&E, x);
            if (caller == 0) v42ec_rx_bit(&O, y);
            *h = (*h ^ (uint32_t)y) * 16777619u;
        }
        if (*t_left < 0 && v42ec_state(&E) != V42EC_DETECT) *t_left = t;
    }
    return ep_proto();
}

/* a linked endpoint whose modem says NONE (from CONNECTED on: not an HST
 * call) is the unlinked one, bit for bit, with an ODP caller and with one
 * without error control */
static void none_same_as_unlinked(void)
{
    uint32_t h[2][2];
    int p[2][2], tl[2][2], c, l;
    for (c = 0; c < 2; c++)
        for (l = 0; l < 2; l++) {
            ep_init(l, HST_LINK_NONE);
            p[c][l] = call_run(c, 4000, &h[c][l], &tl[c][l]);
        }
    CHECK_EQ_I(p[0][0], V42EC_PROTO_LAPM);
    CHECK_EQ_I(p[1][0], V42EC_PROTO_ASYNC);
    CHECK_EQ_I(tl[1][0], 750);
    for (c = 0; c < 2; c++) {
        CHECK_EQ_I(p[c][1], p[c][0]);
        CHECK_EQ_I(tl[c][1], tl[c][0]);
        CHECK(h[c][1] == h[c][0]);
    }
    printf("none_same_as_unlinked: ODP caller: protocol %d, our bits %s; no EC: start-stop at %d ms, our bits %s\n",
           p[0][1], h[0][1] == h[0][0] ? "the same" : "DIFFER", tl[1][1], h[1][1] == h[1][0] ? "the same" : "DIFFER");
}

/* an HST call with its LR taken, then the next call on the same
 * endpoint, reset at its CONNECTED: nothing of the first call is kept */
static void two_calls(void)
{
    uint32_t h0[2], h1[2];
    int p0[2], p1[2], tl0[2], tl1[2], c, t, ok, at, end = -1, proto_reset;

    for (c = 0; c < 2; c++) {
        /* the reference: an unlinked endpoint's call */
        ep_init(0, HST_LINK_NONE);
        p0[c] = call_run(c, 4000, &h0[c], &tl0[c]);
        /* call 1: HST, the LR taken during our training, answered, data */
        ep_init(1, HST_LINK_ON);
        caller_lr(4);
        for (t = 0; t < 1400; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
        S.L.trained = 1;
        ok = our_lr_first(&at);
        CHECK(ok);
        /* call 2: not HST, the modem says NONE at CONNECTED; the host resets */
        memset(&S.L, 0, sizeof S.L);
        S.state = HST_LINK_NONE;
        v42ec_reset(&E, &C);
        CHECK_EQ_I(v42ec_linked(&E), 1);
        CHECK_EQ_I(ep_proto(), V42EC_PROTO_NONE);
        p1[c] = call_run(c, 4000, &h1[c], &tl1[c]);
        CHECK_EQ_I(p1[c], p0[c]);
        CHECK_EQ_I(tl1[c], tl0[c]);
        CHECK(h1[c] == h0[c]);
    }

    /* call 1: HST, LR taken, window started, ended before CONNECTED (no
     * bit sent); call 2: HST again, a new mailbox, no LR yet: no replay,
     * and its own window from its own training */
    ep_init(1, HST_LINK_ON);
    caller_lr(4);
    for (t = 0; t < 1400; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
    S.L.trained = 1;
    for (t = 0; t < 3000; t++) v42ec_tick(&E, 1);
    memset(&S.L, 0, sizeof S.L);
    S.state = HST_LINK_ON;
    S.L.trained = 1;
    v42ec_reset(&E, &C);
    proto_reset = ep_proto();
    CHECK_EQ_I(proto_reset, V42EC_PROTO_NONE);
    for (t = 1; t <= 8000 && end < 0; t++) {
        v42ec_tick(&E, 1);
        if (v42ec_state(&E) == V42EC_DISCONNECTED) end = t;
    }
    CHECK_NEAR(end, 5000, 1);
    /* call 1: HST, LR taken and answered (bits sent); call 2: HST, a new
     * mailbox the host ticks through its training (its ring read further
     * than call 1's), no LR yet: the reset at its CONNECTED replays nothing */
    ep_init(1, HST_LINK_ON);
    caller_lr(4);
    for (t = 0; t < 1400; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
    ok = our_lr_first(&at);
    CHECK(ok);
    memset(&S.L, 0, sizeof S.L);
    cb_n = cb_i = 0;
    for (t = 0; t < 2000; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
    CHECK(S.L.pre_r > 630);
    v42ec_reset(&E, &C);
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_NONE);

    printf("two_calls: HST then ODP: protocol %d, our bits %s; HST then no EC: start-stop at %d ms, our bits %s; "
           "HST then HST: protocol %d after the reset, NO_EC %d ms later\n",
           p1[0], h1[0] == h0[0] ? "as unlinked" : "DIFFER", tl1[1], h1[1] == h0[1] ? "as unlinked" : "DIFFER",
           proto_reset, end);
}

/* ON is for the whole call: a later NONE while hunting gives neither T400
 * nor start-stop, and the window (started then, or kept) ends with NO_EC */
static int on_then_none_run(int trained_at, int none_at)
{
    int t, end = -1, async = 0;
    ep_init(1, HST_LINK_ON);
    for (t = 1; t <= 10000 && end < 0; t++) {
        if (t == trained_at) S.L.trained = 1;
        if (t == none_at) S.state = HST_LINK_NONE;
        v42ec_tick(&E, 1);
        if (t > none_at) {
            int acc;
            for (acc = 0; acc < 2; acc++) { v42ec_rx_bit(&E, 1); (void)v42ec_tx_bit(&E); }
        }
        if (v42ec_state(&E) == V42EC_ASYNC) async = 1;
        if (v42ec_state(&E) == V42EC_DISCONNECTED) end = t;
    }
    CHECK(!async);
    CHECK_EQ_I(ep_reason(), V42EC_R_NO_EC);
    return end;
}

static void on_then_none(void)
{
    int e1 = on_then_none_run(0, 1000), e2 = on_then_none_run(1400, 2000);
    CHECK_NEAR(e1, 1000 + 5000, 1);
    CHECK_NEAR(e2, 1400 + 5000, 1);
    printf("on_then_none: NONE at 1000 ms before training's end: NO_EC at %d ms; trained at 1400, NONE at 2000: NO_EC at %d ms\n",
           e1, e2);
}

/* a window that ran past zero on a coarse tick is not started again by a
 * reset */
static void window_overshoot(void)
{
    int st;
    ep_init(1, HST_LINK_ON);
    S.L.trained = 1;
    v42ec_tick(&E, 3000);                /* the window starts */
    v42ec_tick(&E, 3000);
    v42ec_tick(&E, 3000);                /* and runs out, 1000 ms past */
    CHECK_EQ_I(v42ec_state(&E), V42EC_DISCONNECTED);
    v42ec_reset(&E, &C);                 /* still this call: ON, nothing sent */
    v42ec_tick(&E, 1);
    st = v42ec_state(&E);
    CHECK_EQ_I(st, V42EC_DISCONNECTED);
    CHECK_EQ_I(ep_reason(), V42EC_R_NO_EC);
    printf("window_overshoot: run out on a 3000 ms tick, reset: state %d at the next tick\n", st);
}

/* A host that ticks the endpoint before CONNECTED and never resets it
 * between calls. Call 1: HST, our training out, the window started, and
 * either its caller's LR taken (lr1 1: MNP establishing) or none (lr1 0:
 * the window runs out while the modem says UNDECIDED through call 2's
 * start); the call ends before CONNECTED. Call 2: HST on a new, zeroed
 * mailbox (its ring's read count has gone back): a new call, nothing of
 * call 1 kept. With its caller's LR during our training (lr2 1): answered
 * at our first bit; without (lr2 0): NO_EC at its own training's end plus
 * 5 s. */
static int new_mailbox_run(int lr1, int lr2, int *st_before)
{
    int t, at = -1, end = -1, down = 0;
    ep_init(1, HST_LINK_ON);
    if (lr1) caller_lr(4);
    else cb_n = cb_i = 0;
    for (t = 0; t < 1400; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
    S.L.trained = 1;
    for (t = 0; t < 3100; t++) { if (t % 20 < 9) pump_bits(1); v42ec_tick(&E, 1); }
    CHECK_EQ_I(ep_proto(), lr1 ? V42EC_PROTO_MNP : V42EC_PROTO_NONE);
    S.state = HST_LINK_UNDECIDED;
    for (t = 0; t < 3000; t++) v42ec_tick(&E, 1);
    *st_before = v42ec_state(&E);
    memset(&S.L, 0, sizeof S.L);
    S.state = HST_LINK_ON;
    if (lr2) caller_lr(4);
    else cb_n = cb_i = 0;
    for (t = 1; t <= 1400; t++) {
        if (t % 20 < 9) pump_bits(1);
        v42ec_tick(&E, 1);
        if (v42ec_state(&E) == V42EC_DISCONNECTED) down = 1;
    }
    CHECK_EQ_I(down, 0);
    S.L.trained = 1;
    if (lr2) {
        int ok;
        CHECK_EQ_I(ep_proto(), V42EC_PROTO_MNP);
        CHECK_EQ_I(v42ec_state(&E), V42EC_ESTABLISH);
        ok = our_lr_first(&at);
        CHECK(ok);
        CHECK_EQ_I(at, 16);
        return ok ? at : -1;
    }
    CHECK_EQ_I(ep_proto(), V42EC_PROTO_NONE);
    for (t = 1; t <= 8000 && end < 0; t++) {
        if (t % 20 < 9) pump_bits(1);
        v42ec_tick(&E, 1);
        if (v42ec_state(&E) == V42EC_DISCONNECTED) end = t;
    }
    CHECK_NEAR(end, 5000, 1);
    CHECK_EQ_I(ep_reason(), V42EC_R_NO_EC);
    return end;
}

static void new_mailbox(void)
{
    int s1 = -1, s2 = -1, at, end;
    at = new_mailbox_run(0, 1, &s1);
    end = new_mailbox_run(1, 0, &s2);
    CHECK_EQ_I(s1, V42EC_DISCONNECTED);
    CHECK_EQ_I(s2, V42EC_ESTABLISH);
    printf("new_mailbox: no reset between calls; call 1 no LR (state %d at call 2), call 2 with an LR: our LR out at bit %d; "
           "call 1 MNP (state %d), call 2 without an LR: NO_EC %d ms after its training\n", s1, at, s2, end);
}

/* the ring: counters that wrap, and a full ring dropping its oldest bits */
static int pat(unsigned i) { return (int)((i * 7u ^ (i >> 3)) & 1u); }

static void ring_overflow(void)
{
    unsigned i, start = 0xFFFFFFFFu - 5u;
    int refused = 0, bad = 0, b, n = 0;
    memset(&S.L, 0, sizeof S.L);
    S.L.pre_w = S.L.pre_r = start;
    for (i = 0; i < 1100; i++) if (hst_pre_put(&S.L, pat(i)) < 0) refused++;
    CHECK_EQ_I(refused, 1100 - HST_PRE_BITS);
    CHECK_EQ_I(S.L.pre_lost, 1100 - HST_PRE_BITS);
    while ((b = hst_pre_get(&S.L)) >= 0) {
        if (b != pat((unsigned)(1100 - HST_PRE_BITS + n))) bad++;
        n++;
    }
    CHECK_EQ_I(n, HST_PRE_BITS);
    CHECK_EQ_I(bad, 0);
    CHECK_EQ_I(S.L.pre_r, S.L.pre_w);
    printf("ring_overflow: 1100 bits across the counters' wrap: %lu lost, the newest %d read back, %d wrong\n",
           S.L.pre_lost, n, bad);
}

int main(void)
{
    undecided_holds_t400();
    lr_from_ring();
    window();
    reset_keeps();
    plain_mnp_over_link();
    none_same_as_unlinked();
    two_calls();
    on_then_none();
    window_overshoot();
    new_mailbox();
    ring_overflow();
    T_REPORT("test_ec_link");
}
