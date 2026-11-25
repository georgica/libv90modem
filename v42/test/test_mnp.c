/* v42/mnp: the MNP link as responder (V.42 Annex A), against sim/mnppeer as
 * the originator on a bit loop, framed by the small harness below (HDLC
 * from v42_hdlc.h for mode 3; mnp_frame.h's mode 2 framers on v42async
 * characters), 1 ms ticks; and frame by frame, with frames written out
 * here octet by octet, for the procedures the peer has no knob for. One
 * line per case. */
#include <stdint.h>
#include "mnp.h"
#include "v42_detect.h"
#include "v42_hdlc.h"
#include "../../sim/mnppeer.h"
#include "test.h"

/* the Courier's LR template (`LR and 0xC0`) */
static const uint8_t courier_lr[21] = {
    0x14, 0x01, 0x02, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x02, 0x01, 0x03, 0x03, 0x01, 0x08, 0x04, 0x02, 0x40, 0x00
};

static uint32_t fnv(uint32_t h, const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

static uint8_t xs_next(uint32_t *x)
{
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    return (uint8_t)(*x >> 24);
}

/* one direction's data: xorshift32 octets, so DLE, SYN and 0x7E occur */
typedef struct { uint32_t x; size_t made, got; uint32_t h_in, h_out; } stream_t;

static void stream_init(stream_t *s, uint32_t seed)
{
    s->x = seed; s->made = s->got = 0; s->h_in = s->h_out = 2166136261u;
}

/* ------------------------------------------------------------ the rig */
typedef struct {
    mnp_t m;                      /* ours, the responder */
    mnppeer_t p;                  /* the originator */
    v42hdlc_tx_t htx; v42hdlc_rx_t hrx;
    v42async_tx_t atx; v42async_rx_t arx;
    int hdlc_used;                /* an HDLC frame has gone: mode 3 idles on flags */
    int marks;                    /* mark bits owed before a mode 2 frame */
    int last_hdlc;                /* the last bit sent was the HDLC sender's */
    int bps, acc;
    long ms, con_ms;
    uint32_t rng; int lose_ppm;   /* our frames lost */
    unsigned long our_frames, our_lost, accepts, load_fail;
    stream_t up, down;            /* up: ours to the peer; down: the peer's to us */
} rig_t;

static rig_t R;

static mnp_peer_cfg peer_cfg(int framing)
{
    mnp_peer_cfg c;
    memset(&c, 0, sizeof c);
    c.originator = 1;
    c.framing = framing;
    c.k = 8;
    c.n401 = 64;
    c.seed = 11u;
    return c;
}

static void rig_init(rig_t *r, const mnp_cfg_t *mc, const mnp_peer_cfg *pc, int bps)
{
    memset(r, 0, sizeof *r);
    mnp_init(&r->m, mc);
    mnppeer_init(&r->p, pc);
    v42hdlc_tx_init(&r->htx);
    v42hdlc_rx_init(&r->hrx);
    v42async_tx_init(&r->atx);
    v42async_rx_init(&r->arx);
    r->bps = bps;
    r->con_ms = -1;
    r->rng = 0x9E3779B9u;
    mnp_set_rates(&r->m, bps, bps);
    stream_init(&r->up, 0x1234567u);
    stream_init(&r->down, 0x89ABCDEu);
}

/* the framing our idle line is in: the agreed one once connected */
static int our_framing(const mnp_t *m)
{
    return m->state == MNP_S_CONNECTED ? m->framing : m->est_framing;
}

static int rig_tx_bit(rig_t *r)
{
    uint8_t f[MNP_MAX_BODY];
    for (;;) {
        int n;
        if (r->marks > 0) { r->marks--; return 1; }
        if (!v42async_tx_idle(&r->atx)) return v42async_tx_bit(&r->atx);
        if (mnp2_tx_busy(&r->m.tx2)) {
            int o = mnp2_tx_octet(&r->m.tx2);
            if (o >= 0) { v42async_tx_char(&r->atx, (uint8_t)o); continue; }
        }
        if (r->hdlc_used && !v42hdlc_tx_need_frame(&r->htx)) { r->last_hdlc = 1; return v42hdlc_tx_bit(&r->htx); }
        n = mnp_tx_frame(&r->m, f);
        if (n > 0) {
            r->our_frames++;
            r->rng ^= r->rng << 13; r->rng ^= r->rng >> 17; r->rng ^= r->rng << 5;
            if (r->rng % 1000000u < (uint32_t)r->lose_ppm) { r->our_lost++; continue; }
            if (r->m.tx_framing == 2) {
                if (mnp2_tx_frame(&r->m.tx2, f, n) != 0) r->load_fail++;
                if (r->last_hdlc) { r->marks = 20; r->last_hdlc = 0; }
                continue;
            }
            if (v42hdlc_tx_frame(&r->htx, f, n, 0) != 0) r->load_fail++;
            r->hdlc_used = 1;
            r->last_hdlc = 1;
            return v42hdlc_tx_bit(&r->htx);
        }
        if (r->hdlc_used && our_framing(&r->m) == 3) { r->last_hdlc = 1; return v42hdlc_tx_bit(&r->htx); }
        r->last_hdlc = 0;
        return 1;
    }
}

/* the framings ours hears now */
static int hears(const mnp_t *m, int framing)
{
    switch (m->state) {
    case MNP_S_IDLE: return 1;
    case MNP_S_CONFIRMING: return framing == m->est_framing || framing == m->framing;
    case MNP_S_CONNECTED: return framing == m->framing;
    default: return 0;
    }
}

static void rig_deliver(rig_t *r, const uint8_t *f, int n, int framing)
{
    if (!hears(&r->m, framing)) return;
    if (r->m.state == MNP_S_IDLE) {
        if (n >= 2 && f[1] == MNP_LR) { r->accepts++; mnp_accept_lr(&r->m, f, n, framing); }
        return;
    }
    mnp_rx_frame(&r->m, f, n);
}

static void rig_rx_bit(rig_t *r, int bit)
{
    int n = v42hdlc_rx_bit(&r->hrx, bit), c;
    /* a bad frame counts in the framing ours is in now: the other one's
     * octets can look like broken frames */
    if (n > 0) rig_deliver(r, r->hrx.buf, n, 3);
    else if (n == V42HDLC_RX_BAD_FCS && r->m.state != MNP_S_IDLE && our_framing(&r->m) == 3) mnp_rx_bad(&r->m);
    c = v42async_rx_bit(&r->arx, bit);
    if (c >= 0) {
        int res = mnp2_rx_octet(&r->m.rx2, (uint8_t)c);
        if (res == MNP2_RX_FRAME) rig_deliver(r, r->m.rx2.buf, r->m.rx2.len, 2);
        else if (res == MNP2_RX_BAD && r->m.state != MNP_S_IDLE && our_framing(&r->m) == 2) mnp_rx_bad(&r->m);
    }
}

/* one millisecond: the ticks, then that millisecond's bits */
static void rig_ms(rig_t *r)
{
    mnp_tick(&r->m, 1);
    mnppeer_tick(&r->p, 1);
    r->acc += r->bps;
    while (r->acc >= 1000) {
        int x = rig_tx_bit(r), y = mnppeer_tx_bit(&r->p);
        r->acc -= 1000;
        mnppeer_rx_bit(&r->p, x);
        rig_rx_bit(r, y);
    }
    r->ms++;
    if (r->con_ms < 0 && r->m.state == MNP_S_CONNECTED) r->con_ms = r->ms;
}

/* move what fits of `total` each way, read what came */
static void rig_data(rig_t *r, size_t total)
{
    uint8_t buf[MNP_MAX_N401];
    size_t n, i;
    const uint8_t *p;
    int a;
    /* ours to the peer, one LT at a time */
    while (r->up.made < total && (a = mnp_can_send(&r->m)) > 0) {
        n = total - r->up.made;
        if (n > (size_t)a) n = (size_t)a;
        for (i = 0; i < n; i++) buf[i] = xs_next(&r->up.x);
        if (mnp_send(&r->m, buf, (int)n) != 0) r->load_fail++;
        r->up.h_in = fnv(r->up.h_in, buf, n);
        r->up.made += n;
    }
    while ((n = mnppeer_read(&r->p, buf, sizeof buf)) > 0) {
        r->up.h_out = fnv(r->up.h_out, buf, n);
        r->up.got += n;
    }
    /* the peer's to us */
    n = total - r->down.made;
    if (n > sizeof buf) n = sizeof buf;
    if (n > 0) {
        size_t w;
        uint32_t x = r->down.x;
        for (i = 0; i < n; i++) buf[i] = xs_next(&x);
        w = mnppeer_write(&r->p, buf, n);
        for (i = 0; i < w; i++) (void)xs_next(&r->down.x);
        r->down.h_in = fnv(r->down.h_in, buf, w);
        r->down.made += w;
    }
    while ((a = mnp_rx_avail(&r->m, &p)) > 0) {
        r->down.h_out = fnv(r->down.h_out, p, (size_t)a);
        r->down.got += (size_t)a;
        mnp_rx_consume(&r->m, a);
    }
}

static int rig_connect(rig_t *r, long max_ms)
{
    while (r->ms < max_ms && r->m.state != MNP_S_CONNECTED && r->m.state != MNP_S_DISCONNECTED) rig_ms(r);
    return r->m.state == MNP_S_CONNECTED && mnppeer_state(&r->p) == MNPP_CONNECTED;
}

/* data both ways until both arrived or max_ms; 1 if they did */
static int rig_transfer(rig_t *r, size_t total, long max_ms)
{
    while (r->ms < max_ms) {
        rig_ms(r);
        rig_data(r, total);
        if (r->up.got >= total && r->down.got >= total) return 1;
        if (r->m.state == MNP_S_DISCONNECTED || mnppeer_state(&r->p) == MNPP_DISCONNECTED) return 0;
    }
    return 0;
}

static int intact(const rig_t *r, size_t total)
{
    return r->up.got == total && r->down.got == total
        && r->up.h_in == r->up.h_out && r->down.h_in == r->down.h_out;
}

/* what every two-end case checks of the peer: no window broken, no LN refused */
static void peer_clean(const rig_t *r)
{
    CHECK_EQ_I(r->load_fail, 0);
    CHECK_EQ_I(r->p.rx_over_credit, 0);
    CHECK_EQ_I(r->p.ln_rx_full, 0);
}

/* ------------------------------------------------ frames, octet by octet */
static int lt_std(uint8_t *b, int ns, const uint8_t *d, int n)
{
    b[0] = 4; b[1] = 4; b[2] = 1; b[3] = 1; b[4] = (uint8_t)ns;
    memcpy(b + 5, d, (size_t)n);
    return 5 + n;
}

static int la_std(uint8_t *b, int nr, int nk)
{
    b[0] = 7; b[1] = 5; b[2] = 1; b[3] = 1; b[4] = (uint8_t)nr; b[5] = 2; b[6] = 1; b[7] = (uint8_t)nk;
    return 8;
}

static int is_la_std(const uint8_t *f, int n, int nr, int nk)
{
    uint8_t w[8];
    la_std(w, nr, nk);
    return n == 8 && memcmp(f, w, 8) == 0;
}

/* N(S) of a standard LT, -1 if f is not one */
static int lt_ns(const uint8_t *f, int n)
{
    if (n < 6 || f[0] != 4 || f[1] != 4 || f[2] != 1 || f[3] != 1) return -1;
    return f[4];
}

/* ours connected on the Courier's LR in mode 3 and an LA N(R) 0, N(k) 8 */
static void fl_connect(mnp_t *m, const mnp_cfg_t *c)
{
    uint8_t f[MNP_MAX_BODY];
    int n;
    mnp_init(m, c);
    CHECK_EQ_I(mnp_accept_lr(m, courier_lr, sizeof courier_lr, 3), 0);
    CHECK_EQ_I(mnp_tx_frame(m, f), (int)sizeof courier_lr);
    n = la_std(f, 0, 8);
    mnp_rx_frame(m, f, n);
    CHECK_EQ_I(m->state, MNP_S_CONNECTED);
    CHECK_EQ_I(mnp_tx_frame(m, f), 0);
}

/* ---------------------------------------------------------------- cases */
static void t_t401(void)
{
    CHECK_EQ_I(mnp_t401_ms(2, MNP_LAYOUT_STD, 8, 64, 2400, 2400, 1500), 3659);
    CHECK_EQ_I(mnp_t401_ms(2, MNP_LAYOUT_STD, 8, 64, 1200, 1200, 1500), 6317);
    CHECK_EQ_I(mnp_t401_ms(3, MNP_LAYOUT_STREAM, 8, 64, 14400, 450, 1500), 1560);
    CHECK_EQ_I(mnp_t401_ms(3, MNP_LAYOUT_OPT, 8, 64, 14400, 450, 1500), 1600);
    CHECK_EQ_I(mnp_t401_ms(3, MNP_LAYOUT_OPT, 8, 64, 33600, 33600, 1500), 1500);
    CHECK_EQ_I(mnp_t401_ms(3, MNP_LAYOUT_STD, 8, 64, 2400, 2400, 1500), 3027);
    printf("t401: 3659 6317 1560 1600 1500 ms\n");
}

static void t_establish(void)
{
    static mnp_t m;
    mnp_cfg_t c;
    uint8_t f[MNP_MAX_BODY], lr[32];
    int n;

    mnp_cfg_default(&c);
    CHECK_EQ_I(c.k, 8); CHECK_EQ_I(c.n401, 64); CHECK_EQ_I(c.opt, 2);
    CHECK_EQ_I(c.framing, 3); CHECK_EQ_I(c.t401_min_ms, 1500); CHECK_EQ_I(c.n400, 12);

    /* the template: our LR is {3, 8, 64} with no parameter 8 (octet for
     * octet the template), in the framing it came in */
    mnp_init(&m, &c);
    CHECK_EQ_I(m.state, MNP_S_IDLE);
    CHECK_EQ_I(mnp_accept_lr(&m, courier_lr, sizeof courier_lr, 3), 0);
    CHECK_EQ_I(m.state, MNP_S_CONFIRMING);
    n = mnp_tx_frame(&m, f);
    CHECK_EQ_I(n, (int)sizeof courier_lr);
    CHECK(memcmp(f, courier_lr, sizeof courier_lr) == 0);
    CHECK_EQ_I(m.tx_framing, 3);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    CHECK_EQ_I(mnp_can_send(&m), 0);
    n = la_std(f, 0, 8);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(m.state, MNP_S_CONNECTED);
    CHECK_EQ_I(m.framing, 3); CHECK_EQ_I(m.k, 8); CHECK_EQ_I(m.n401, 64);
    CHECK_EQ_I(m.layout, MNP_LAYOUT_STD); CHECK_EQ_I(m.credit, 8);
    CHECK_EQ_I(mnp_can_send(&m), 64);

    /* an LT first (the LA was lost): connected, that LT dropped, an LA */
    mnp_init(&m, &c);
    CHECK_EQ_I(mnp_accept_lr(&m, courier_lr, sizeof courier_lr, 3), 0);
    CHECK_EQ_I(mnp_tx_frame(&m, f), (int)sizeof courier_lr);
    n = lt_std(f, 1, (const uint8_t *)"AB", 2);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(m.state, MNP_S_CONNECTED);
    CHECK_EQ_I(mnp_rx_avail(&m, NULL), 0);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 0, 8));
    CHECK_EQ_I(m.tx_framing, 3);
    /* that LT again is taken */
    n = lt_std(f, 1, (const uint8_t *)"AB", 2);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(mnp_rx_avail(&m, NULL), 2);

    /* a repeated LR: our LR again, once per LR */
    mnp_init(&m, &c);
    CHECK_EQ_I(mnp_accept_lr(&m, courier_lr, sizeof courier_lr, 2), 0);
    CHECK_EQ_I(mnp_tx_frame(&m, f), (int)sizeof courier_lr);
    CHECK_EQ_I(m.tx_framing, 2);
    mnp_rx_frame(&m, courier_lr, sizeof courier_lr);
    n = mnp_tx_frame(&m, f);
    CHECK_EQ_I(n, (int)sizeof courier_lr);
    CHECK(memcmp(f, courier_lr, sizeof courier_lr) == 0);
    CHECK_EQ_I(m.tx_framing, 2);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    CHECK_EQ_I(m.state, MNP_S_CONFIRMING);

    /* constant parameter 1 not 2: LD reason 2, the negotiation reason */
    memcpy(lr, courier_lr, sizeof courier_lr);
    lr[2] = 3;
    mnp_init(&m, &c);
    CHECK_EQ_I(mnp_accept_lr(&m, lr, sizeof courier_lr, 3), MNP_R_NEGOTIATION);
    CHECK_EQ_I(m.state, MNP_S_DISCONNECTED);
    CHECK_EQ_I(m.reason, MNP_R_NEGOTIATION);
    n = mnp_tx_frame(&m, f);
    CHECK_EQ_I(n, 5);
    CHECK(n == 5 && f[0] == 4 && f[1] == 2 && f[2] == 1 && f[3] == 1 && f[4] == 2);
    CHECK_EQ_I(m.tx_framing, 3);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);

    /* parameter 8 = 3 from the caller: ours is 2 (the AND), the optimised
     * layout; with ours 3 and N401 256, bit 1 too: N401 256 */
    memcpy(lr, courier_lr, sizeof courier_lr);
    lr[21] = 8; lr[22] = 1; lr[23] = 3; lr[0] = 23;
    mnp_init(&m, &c);
    CHECK_EQ_I(mnp_accept_lr(&m, lr, 24, 3), 0);
    n = mnp_tx_frame(&m, f);
    CHECK_EQ_I(n, 24);
    CHECK(n == 24 && f[0] == 23 && f[21] == 8 && f[22] == 1 && f[23] == 2);
    CHECK_EQ_I(m.layout, MNP_LAYOUT_OPT); CHECK_EQ_I(m.n401, 64);
    c.opt = 3; c.n401 = 256;
    mnp_init(&m, &c);
    CHECK_EQ_I(mnp_accept_lr(&m, lr, 24, 3), 0);
    n = mnp_tx_frame(&m, f);
    CHECK(n == 24 && f[19] == 0x40 && f[20] == 0x00 && f[23] == 3);
    CHECK_EQ_I(m.n401, 256);
    mnp_cfg_default(&c);

    /* no LA: our LR again after T401 (2 s), then LD 1 */
    mnp_init(&m, &c);
    CHECK_EQ_I(mnp_accept_lr(&m, courier_lr, sizeof courier_lr, 3), 0);
    CHECK_EQ_I(mnp_tx_frame(&m, f), (int)sizeof courier_lr);
    mnp_tick(&m, 1999);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    mnp_tick(&m, 1);
    CHECK_EQ_I(mnp_tx_frame(&m, f), (int)sizeof courier_lr);
    mnp_tick(&m, 2000);
    n = mnp_tx_frame(&m, f);
    CHECK(n == 5 && f[1] == 2 && f[4] == 1);
    CHECK_EQ_I(m.state, MNP_S_DISCONNECTED);
    CHECK_EQ_I(m.reason, MNP_R_N400);
    printf("establish: the template answered by itself; LT first, repeated LR, constant 1, parameter 8, no LA\n");
}

/* over the bits: modes 3 and 2, the confirming LA in either layout */
static void t_establish_peer(void)
{
    static const struct { int pf, of, opt, std_la; } v[] = {
        { 3, 3, 0, 0 }, { 2, 3, 0, 0 }, { 3, 2, 0, 0 }, { 3, 3, 2, 0 }, { 3, 3, 2, 1 }, { 2, 2, 2, 1 }
    };
    unsigned i;
    for (i = 0; i < sizeof v / sizeof v[0]; i++) {
        mnp_cfg_t c;
        mnp_peer_cfg pc = peer_cfg(v[i].pf);
        int ok;
        mnp_cfg_default(&c);
        c.framing = v[i].of;
        if (v[i].opt) { pc.send_opt = 1; pc.opt = v[i].opt; }
        pc.confirm_la_standard = v[i].std_la;
        rig_init(&R, &c, &pc, 2400);
        ok = rig_connect(&R, 5000);
        CHECK(ok);
        CHECK_EQ_I(R.accepts, 1);
        CHECK_EQ_I(R.m.est_framing, v[i].pf);
        CHECK_EQ_I(R.m.framing, v[i].pf < v[i].of ? v[i].pf : v[i].of);
        CHECK_EQ_I(R.m.layout, v[i].opt ? MNP_LAYOUT_OPT : MNP_LAYOUT_STD);
        CHECK_EQ_I(R.p.layout, R.m.layout);
        ok = ok && rig_transfer(&R, 4096, 60000);
        CHECK(ok);
        CHECK(intact(&R, 4096));
        CHECK_EQ_I(R.p.last_layout_rx, R.m.layout);
        CHECK_EQ_I(R.p.rx_bad, 0);
        CHECK_EQ_I(R.m.rx_bad, 0);
        peer_clean(&R);
        printf("establish_peer: caller mode %d, ours up to %d, parameter 8 %d, LA %s: connected at %ld ms, framing %d layout %d, 4 KiB each way %s\n",
               v[i].pf, v[i].of, v[i].opt, v[i].std_la ? "standard" : "as agreed",
               R.con_ms, R.m.framing, R.m.layout, intact(&R, 4096) ? "intact" : "DAMAGED");
    }
}

static void t_data(void)
{
    static const size_t total = 65536;
    mnp_cfg_t c;
    mnp_peer_cfg pc;
    int ok;

    /* 64 KiB each way at k 8, mode 3 and mode 2 */
    mnp_cfg_default(&c);
    pc = peer_cfg(3);
    rig_init(&R, &c, &pc, 9600);
    ok = rig_connect(&R, 5000) && rig_transfer(&R, total, 600000);
    CHECK(ok);
    CHECK(intact(&R, total));
    CHECK_EQ_I(R.m.k, 8);
    CHECK_EQ_I(R.m.tx_retx, 0); CHECK_EQ_I(R.p.tx_retx, 0);
    peer_clean(&R);
    printf("data: mode 3, 64 KiB each way %s in %ld ms, LT out %lu in %lu, LA out %lu in %lu\n",
           intact(&R, total) ? "intact" : "DAMAGED", R.ms, R.m.tx_lt, R.m.rx_lt, R.m.tx_la, R.m.rx_la);

    pc = peer_cfg(2);
    rig_init(&R, &c, &pc, 9600);
    ok = rig_connect(&R, 5000) && rig_transfer(&R, total, 600000);
    CHECK(ok);
    CHECK(intact(&R, total));
    CHECK_EQ_I(R.m.framing, 2);
    peer_clean(&R);
    printf("data: mode 2, 64 KiB each way %s in %ld ms\n", intact(&R, total) ? "intact" : "DAMAGED", R.ms);

    /* 1 % of the peer's frames with a bit inverted, mode 2: our LA on a
     * bad frame (A.7.3.3 a) */
    pc = peer_cfg(2);
    pc.corrupt_ppm = 10000;
    rig_init(&R, &c, &pc, 9600);
    ok = rig_connect(&R, 5000) && rig_transfer(&R, 16384, 600000);
    CHECK(ok);
    CHECK(intact(&R, 16384));
    CHECK(R.m.rx_bad > 0);
    peer_clean(&R);
    printf("data: mode 2, 1 %% of the peer's frames corrupted: 16 KiB each way %s, bad frames %lu, retx %d\n",
           intact(&R, 16384) ? "intact" : "DAMAGED", R.m.rx_bad, R.p.tx_retx);

    /* parameter 8 = 3 both: optimised, N401 256 both ways */
    c.opt = 3; c.n401 = 256;
    pc = peer_cfg(3);
    pc.send_opt = 1; pc.opt = 3;
    rig_init(&R, &c, &pc, 9600);
    ok = rig_connect(&R, 5000) && rig_transfer(&R, 16384, 600000);
    CHECK(ok);
    CHECK(intact(&R, 16384));
    CHECK_EQ_I(R.m.n401, 256); CHECK_EQ_I(R.p.n401, 256);
    CHECK_EQ_I(R.m.layout, MNP_LAYOUT_OPT);
    CHECK_EQ_I(mnp_can_send(&R.m) == 0 || mnp_can_send(&R.m) == 256, 1);
    CHECK(R.m.rx_lt <= 16384 / 256 + 8);
    peer_clean(&R);
    printf("data: parameter 8 = 3, N401 %d both ways, 16 KiB each way %s, LT in %lu\n",
           R.m.n401, intact(&R, 16384) ? "intact" : "DAMAGED", R.m.rx_lt);
}

/* the peer's credit is 0 for 3 s after it connects: no LT of ours reaches
 * it meanwhile; then everything does */
static void t_zero_credit(void)
{
    mnp_cfg_t c;
    mnp_peer_cfg pc = peer_cfg(3);
    int ok, lt_in_zero = 0, saw_zero = 0;
    long zero_ms = 0;
    mnp_cfg_default(&c);
    pc.zero_credit_ms = 3000;
    rig_init(&R, &c, &pc, 9600);
    ok = rig_connect(&R, 5000);
    CHECK(ok);
    while (ok && R.ms < 120000) {
        rig_ms(&R);
        rig_data(&R, 8192);
        if (R.p.zc_left > 0) {
            zero_ms++;
            if (R.p.rx_lt != 0) lt_in_zero = 1;
            if (R.m.credit == 0 && mnp_unacked(&R.m) > 0) saw_zero = 1;
        }
        if (R.up.got >= 8192 && R.down.got >= 8192) break;
    }
    CHECK(zero_ms >= 2900);
    CHECK_EQ_I(lt_in_zero, 0);
    CHECK_EQ_I(saw_zero, 1);
    CHECK(intact(&R, 8192));
    peer_clean(&R);
    printf("zero_credit: %ld ms at credit 0 with LTs waiting, none sent; then 8 KiB each way %s\n",
           zero_ms, intact(&R, 8192) ? "intact" : "DAMAGED");
}

/* an LA repeating N(R): from the first unacknowledged LT again */
static void t_goback(void)
{
    static mnp_t m;
    mnp_cfg_t c;
    uint8_t f[MNP_MAX_BODY];
    int n, i;
    mnp_cfg_default(&c);
    fl_connect(&m, &c);
    for (i = 0; i < 3; i++) CHECK_EQ_I(mnp_send(&m, (const uint8_t *)"xyz", 3), 0);
    for (i = 1; i <= 3; i++) { n = mnp_tx_frame(&m, f); CHECK_EQ_I(lt_ns(f, n), i); }
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    n = la_std(f, 1, 8);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(mnp_unacked(&m), 2);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    n = la_std(f, 1, 8);
    mnp_rx_frame(&m, f, n);
    n = mnp_tx_frame(&m, f); CHECK_EQ_I(lt_ns(f, n), 2);
    n = mnp_tx_frame(&m, f); CHECK_EQ_I(lt_ns(f, n), 3);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    CHECK_EQ_I(m.tx_retx, 2);
    /* credit: N(R) 3, N(k) 1: one LT, then none */
    n = la_std(f, 3, 1);
    mnp_rx_frame(&m, f, n);
    for (i = 0; i < 3; i++) CHECK_EQ_I(mnp_send(&m, (const uint8_t *)"abc", 3), 0);
    n = mnp_tx_frame(&m, f); CHECK_EQ_I(lt_ns(f, n), 4);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    /* an N(R) never sent is not taken */
    n = la_std(f, 6, 8);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(m.ack, 3);
    CHECK_EQ_I(m.violations, 1);
    printf("goback: LA N(R) 1 twice: LT 2 and 3 again; credit 1: one LT\n");
}

/* the first LT with N(S) = V(R) - 1 draws no LA, a second does */
static void t_dup(void)
{
    static mnp_t m;
    mnp_cfg_t c;
    uint8_t f[MNP_MAX_BODY];
    int n;
    mnp_cfg_default(&c);
    fl_connect(&m, &c);
    n = lt_std(f, 1, (const uint8_t *)"Q", 1);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(mnp_rx_avail(&m, NULL), 1);
    mnp_rx_consume(&m, 1);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 1, 8));
    n = lt_std(f, 1, (const uint8_t *)"Q", 1);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(mnp_tx_frame(&m, f), 0);
    CHECK_EQ_I(mnp_rx_avail(&m, NULL), 0);
    n = lt_std(f, 1, (const uint8_t *)"Q", 1);
    mnp_rx_frame(&m, f, n);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 1, 8));
    CHECK_EQ_I(mnp_rx_avail(&m, NULL), 0);
    /* out of sequence: an LA at once */
    n = lt_std(f, 3, (const uint8_t *)"Q", 1);
    mnp_rx_frame(&m, f, n);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 1, 8));
    /* busy: N(k) 0 at once, the credit back when it clears */
    mnp_set_busy(&m, 1);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 1, 0));
    n = lt_std(f, 2, (const uint8_t *)"Q", 1);
    mnp_rx_frame(&m, f, n);
    CHECK_EQ_I(mnp_rx_avail(&m, NULL), 0);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 1, 0));
    mnp_set_busy(&m, 0);
    n = mnp_tx_frame(&m, f);
    CHECK(is_la_std(f, n, 1, 8));
    printf("dup: N(S) = V(R) - 1 once: no LA; twice: LA; out of sequence: LA; busy: N(k) 0, then 8\n");
}

/* across V(S) 255 -> 0 with 1 % of the frames lost both ways: data and LNs
 * (types 2 and 3: the peer does not apply a destructive break) */
static void t_wrap(void)
{
    static const int lts = 300;
    const size_t total = (size_t)lts * 64;
    mnp_cfg_t c;
    mnp_peer_cfg pc = peer_cfg(3);
    int up_sent = 0, up_got = 0, down_sent = 0, down_got = 0, up_order = 1, down_order = 1, code, ok;
    mnp_cfg_default(&c);
    pc.lose_ppm = 10000;
    rig_init(&R, &c, &pc, 9600);
    R.lose_ppm = 10000;
    ok = rig_connect(&R, 10000);
    CHECK(ok);
    while (ok && R.ms < 1500000) {
        rig_ms(&R);
        rig_data(&R, total);
        if (up_sent < 300 && mnp_send_ln(&R.m, 2 + (up_sent & 1)) == 0) up_sent++;
        if (down_sent < 300 && R.ms % 20 == 0 && mnppeer_send_ln(&R.p, 2 + (down_sent & 1))) down_sent++;
        while ((code = mnppeer_get_ln(&R.p)) >= 0) {
            if (code != 2 + (up_got & 1)) up_order = 0;
            up_got++;
        }
        while (mnp_get_ln(&R.m, &code)) {
            if (code != 2 + (down_got & 1)) down_order = 0;
            down_got++;
        }
        if (R.m.state != MNP_S_CONNECTED || mnppeer_state(&R.p) != MNPP_CONNECTED) break;
        if (R.up.got >= total && R.down.got >= total && up_got == 300 && down_got == 300
            && R.m.ln_code < 0 && R.p.ln_code < 0) break;
    }
    /* settle: nothing more arrives */
    {
        long end = R.ms + 20000;
        while (R.ms < end) {
            rig_ms(&R);
            while ((code = mnppeer_get_ln(&R.p)) >= 0) up_got++;
            while (mnp_get_ln(&R.m, &code)) down_got++;
        }
    }
    CHECK(intact(&R, total));
    CHECK_EQ_I(up_got, 300); CHECK_EQ_I(down_got, 300);
    CHECK_EQ_I(up_order, 1); CHECK_EQ_I(down_order, 1);
    CHECK(R.m.tx_lt >= 300); CHECK(R.p.tx_lt >= 300);
    CHECK(R.m.rx_lt >= 300);
    CHECK(R.our_lost > 0 && R.m.tx_retx > 0 && R.p.tx_retx > 0);
    CHECK_EQ_I(R.m.state, MNP_S_CONNECTED);
    CHECK_EQ_I(mnppeer_state(&R.p), MNPP_CONNECTED);
    peer_clean(&R);
    printf("wrap: %u octets each way %s, LT out %lu in %lu, LN %d/%d each delivered once in order, our frames lost %lu, retx %lu/%d\n",
           (unsigned)total, intact(&R, total) ? "intact" : "DAMAGED", R.m.tx_lt, R.m.rx_lt,
           up_got, down_got, R.our_lost, R.m.tx_retx, R.p.tx_retx);
}

/* the peer goes silent: 12 retransmissions, then LD 4 */
static void t_n400(void)
{
    mnp_cfg_t c;
    mnp_peer_cfg pc = peer_cfg(3);
    unsigned long retx0;
    int ok;
    mnp_cfg_default(&c);
    rig_init(&R, &c, &pc, 2400);
    ok = rig_connect(&R, 5000);
    CHECK(ok);
    while (R.ms < 1000) rig_ms(&R);
    R.p.cfg.lose_ppm = 1000000;
    retx0 = R.m.tx_retx;
    CHECK_EQ_I(mnp_send(&R.m, (const uint8_t *)"hello", 5), 0);
    while (R.ms < 200000 && R.m.state == MNP_S_CONNECTED) rig_ms(&R);
    while (R.ms < 201000) rig_ms(&R);
    CHECK_EQ_I(R.m.tx_retx - retx0, 12);
    CHECK_EQ_I(R.m.t401_expiries, 13);
    CHECK_EQ_I(R.m.state, MNP_S_DISCONNECTED);
    CHECK_EQ_I(R.m.reason, MNP_R_N400);
    CHECK_EQ_I(R.p.rx_ld_reason, 4);
    peer_clean(&R);
    printf("n400: silent peer: %lu retransmissions, then LD %d, reason N400\n",
           R.m.tx_retx - retx0, R.p.rx_ld_reason);
}

/* line stopped for 20 s: no expiry, nothing sent again; T401 resumes */
static void t_timers_held(void)
{
    mnp_cfg_t c;
    mnp_peer_cfg pc = peer_cfg(3);
    unsigned long retx;
    int left, ok;
    long at, i, expired;
    mnp_cfg_default(&c);
    rig_init(&R, &c, &pc, 2400);
    ok = rig_connect(&R, 5000);
    CHECK(ok);
    R.p.cfg.lose_ppm = 1000000;
    CHECK_EQ_I(mnp_send(&R.m, (const uint8_t *)"hello", 5), 0);
    while (R.m.tx_lt == 0) rig_ms(&R);
    for (i = 0; i < 1000; i++) rig_ms(&R);
    left = R.m.t401;
    CHECK(left > 0 && left == R.m.t401_ms - 1000);
    retx = R.m.tx_retx;
    mnp_line_stopped(&R.m);
    for (i = 0; i < 20000; i++) rig_ms(&R);
    CHECK_EQ_I(R.m.t401, left);
    CHECK_EQ_I(R.m.tx_retx, retx);
    CHECK_EQ_I(R.m.t401_expiries, 0);
    CHECK_EQ_I(R.m.state, MNP_S_CONNECTED);
    mnp_line_restored(&R.m);
    at = R.ms;
    while (R.m.t401_expiries == 0 && R.ms < at + 10000) rig_ms(&R);
    expired = R.ms - at;
    CHECK_EQ_I(expired, left);
    CHECK_EQ_I(R.m.t401_expiries, 1);
    while (R.m.tx_retx == retx && R.ms < at + 10000) rig_ms(&R);
    CHECK_EQ_I(R.m.tx_retx, retx + 1);
    peer_clean(&R);
    printf("timers_held: T401 at %d ms held 20 s, then expired %ld ms after the restore\n", left, expired);
}

static void t_break(void)
{
    mnp_cfg_t c;
    mnp_peer_cfg pc = peer_cfg(3);
    int code = -1, got = 0, ok;
    long i;
    mnp_cfg_default(&c);
    CHECK_EQ_I(mnp_ln_of_brk(V42_BRK_DESTRUCTIVE_EXPEDITED), 1);
    CHECK_EQ_I(mnp_ln_of_brk(V42_BRK_NONDESTRUCTIVE_EXPEDITED), 2);
    CHECK_EQ_I(mnp_ln_of_brk(V42_BRK_NONDESTRUCTIVE_INSEQ), 3);
    CHECK_EQ_I(mnp_brk_of_ln(1), V42_BRK_DESTRUCTIVE_EXPEDITED);
    CHECK_EQ_I(mnp_brk_of_ln(2), V42_BRK_NONDESTRUCTIVE_EXPEDITED);
    CHECK_EQ_I(mnp_brk_of_ln(3), V42_BRK_NONDESTRUCTIVE_INSEQ);
    CHECK_EQ_I(mnp_brk_of_ln(4), -1);
    rig_init(&R, &c, &pc, 2400);
    CHECK_EQ_I(mnp_send_ln(&R.m, 1), -1);
    ok = rig_connect(&R, 5000);
    CHECK(ok);
    CHECK_EQ_I(mnp_send_ln(&R.m, 1), 0);
    CHECK_EQ_I(mnp_send_ln(&R.m, 1), -1);
    for (i = 0; i < 1000; i++) rig_ms(&R);
    CHECK_EQ_I(mnppeer_get_ln(&R.p), 1);
    CHECK_EQ_I(mnppeer_get_ln(&R.p), -1);
    CHECK_EQ_I(R.m.ln_code, -1);
    CHECK_EQ_I(R.m.attn_tx, 1);
    CHECK_EQ_I(mnppeer_send_ln(&R.p, 3), 1);
    for (i = 0; i < 1000; i++) {
        rig_ms(&R);
        while (mnp_get_ln(&R.m, &code)) got++;
    }
    CHECK_EQ_I(got, 1);
    CHECK_EQ_I(code, 3);
    CHECK_EQ_I(R.m.attn_rx, 3);
    CHECK_EQ_I(R.p.ln_code, -1);
    /* the release: LD 255 */
    mnp_release(&R.m);
    CHECK_EQ_I(R.m.state, MNP_S_DISCONNECTED);
    CHECK_EQ_I(R.m.reason, MNP_R_LOCAL);
    for (i = 0; i < 500; i++) rig_ms(&R);
    CHECK_EQ_I(R.p.rx_ld_reason, 255);
    peer_clean(&R);
    printf("break: LN 1 to the peer, LN 3 from it, each once; release: LD %d\n", R.p.rx_ld_reason);

    /* the peer's LD: the peer's reason */
    rig_init(&R, &c, &pc, 2400);
    ok = rig_connect(&R, 5000);
    CHECK(ok);
    mnppeer_release(&R.p);
    for (i = 0; i < 500; i++) rig_ms(&R);
    CHECK_EQ_I(R.m.state, MNP_S_DISCONNECTED);
    CHECK_EQ_I(R.m.reason, MNP_R_PEER_DISC);
    CHECK_EQ_I(R.m.peer_ld_reason, 255);
    printf("break: the peer's LD 255: reason PEER_DISC\n");
}

static void t_size(void)
{
    CHECK(sizeof(mnp_t) <= 4096);
    printf("size: mnp_t within 4096 octets\n");
}

int main(void)
{
    t_t401();
    t_establish();
    t_establish_peer();
    t_data();
    t_zero_credit();
    t_goback();
    t_dup();
    t_wrap();
    t_n400();
    t_timers_held();
    t_break();
    t_size();
    T_REPORT("test_mnp");
}
