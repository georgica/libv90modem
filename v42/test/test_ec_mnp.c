/* The endpoint (v42ec_t) as answerer with MNP on: detection of an LR beside
 * the ODP, the MNP link in the endpoint (data, breaks, busy, release),
 * status. The caller is sim/mnppeer, an independent MNP entity, another
 * v42ec_t (LAPM), or bits built here (characters, mode 2 and HDLC frames,
 * with their own CRCs). 2400 bit/s (300 where a case says), 1 ms ticks. One line per case. */
#include <stdint.h>
#include "v42.h"
#include "../../sim/mnppeer.h"
#include "test.h"

#define BPS 2400

static uint32_t fnv(uint32_t h, const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

/* x^16 + x^12 + x^5 + 1 and x^16 + x^15 + x^2 + 1, reflected, bit by bit */
static uint16_t crc_bits(uint16_t crc, uint16_t poly, const uint8_t *d, int n)
{
    int i, b;
    for (i = 0; i < n; i++)
        for (b = 0; b < 8; b++) {
            unsigned x = (crc ^ (unsigned)(d[i] >> b)) & 1u;
            crc = (uint16_t)(x ? (crc >> 1) ^ poly : crc >> 1);
        }
    return crc;
}

/* the Courier's LR (hst-facts.md `LR and 0xC0`): framing 3, k 8, N401 64,
 * no parameter 8 */
static const uint8_t lr_courier[] = {
    0x14, 0x01, 0x02, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x02, 0x01, 0x03, 0x03, 0x01, 0x08, 0x04, 0x02, 0x40, 0x00 };

/* ---- the rig: our answerer and one caller ---- */
enum { SRC_BITS = 0, SRC_PEER, SRC_LAPM };

typedef struct {
    v42ec_t e;
    int src;
    mnppeer_t p;                       /* SRC_PEER */
    v42ec_t o;                         /* SRC_LAPM: an originator */
    uint8_t in[40000]; int in_n, in_i; /* SRC_BITS: the caller's bits (then mark) */
    uint8_t out[60000]; int out_n;     /* our bits, the first ones */
    uint32_t h_out;                    /* FNV-1a of all our bits */
    long ms; int acc;
    int max_proto;                     /* every protocol seen, as a bit map */
    int cut;                           /* 1: the caller's bits no longer reach us (mark) */
    int bps;                           /* the line rate (BPS unless a case sets it) */
} rig_t;

/* our link's N400 for the next rig_init (0: the default) */
static int cfg_n400;

static void rig_init(rig_t *r, int src, int mnp_on, int fallback, const mnp_peer_cfg *pc)
{
    v42ec_cfg_t c;
    v42ec_cfg_default(&c, 0);
    c.mnp.enable = mnp_on;
    c.fallback = fallback;
    if (cfg_n400 > 0) c.mnp.link.n400 = cfg_n400;
    v42ec_init(&r->e, &c);
    r->src = src;
    if (src == SRC_PEER) mnppeer_init(&r->p, pc);
    if (src == SRC_LAPM) {
        v42ec_cfg_t o;
        v42ec_cfg_default(&o, 1);
        v42ec_init(&r->o, &o);
    }
    r->in_n = r->in_i = r->out_n = 0;
    r->h_out = 2166136261u;
    r->ms = 0; r->acc = 0;
    r->max_proto = 0;
    r->cut = 0;
    r->bps = BPS;
}

static int proto(const v42ec_t *e)
{
    v42ec_status_t s;
    v42ec_get_status(e, &s);
    return s.protocol;
}

static void rig_ms(rig_t *r)
{
    v42ec_tick(&r->e, 1);
    if (r->src == SRC_PEER) mnppeer_tick(&r->p, 1);
    if (r->src == SRC_LAPM) v42ec_tick(&r->o, 1);
    r->acc += r->bps;
    while (r->acc >= 1000) {
        int x, y = v42ec_tx_bit(&r->e);
        r->acc -= 1000;
        if (r->src == SRC_PEER) x = mnppeer_tx_bit(&r->p);
        else if (r->src == SRC_LAPM) x = v42ec_tx_bit(&r->o);
        else x = r->in_i < r->in_n ? r->in[r->in_i++] : 1;
        v42ec_rx_bit(&r->e, r->cut ? 1 : x);
        if (r->src == SRC_PEER) mnppeer_rx_bit(&r->p, y);
        if (r->src == SRC_LAPM) v42ec_rx_bit(&r->o, y);
        if (r->out_n < (int)sizeof r->out) r->out[r->out_n++] = (uint8_t)y;
        r->h_out = (r->h_out ^ (uint32_t)y) * 16777619u;
    }
    r->ms++;
    r->max_proto |= 1 << proto(&r->e);
}

static void rig_run(rig_t *r, long ms) { while (ms-- > 0) rig_ms(r); }

/* ---- the caller's bits, built here ---- */
static void bit_in(rig_t *r, int b) { if (r->in_n < (int)sizeof r->in) r->in[r->in_n++] = (uint8_t)b; }
static void mark_in(rig_t *r, int n) { while (n-- > 0) bit_in(r, 1); }

static void char_in(rig_t *r, uint8_t c)
{
    int b;
    bit_in(r, 0);
    for (b = 0; b < 8; b++) bit_in(r, (c >> b) & 1);
    bit_in(r, 1);
    mark_in(r, 2);
}

/* framing mode 2: SYN DLE STX, the body with DLEs doubled, DLE ETX, the
 * FCS (CRC-16/ARC over the body and ETX, low octet first; hst-facts.md
 * `MNP framing`); bad inverts the FCS's first octet */
static void frame2_in(rig_t *r, const uint8_t *body, int n, int bad)
{
    uint8_t etx = 0x03;
    uint16_t c = crc_bits(crc_bits(0, 0xA001u, body, n), 0xA001u, &etx, 1);
    int i;
    char_in(r, 0x16); char_in(r, 0x10); char_in(r, 0x02);
    for (i = 0; i < n; i++) {
        char_in(r, body[i]);
        if (body[i] == 0x10) char_in(r, 0x10);
    }
    char_in(r, 0x10); char_in(r, 0x03);
    char_in(r, (uint8_t)((c & 0xFF) ^ (bad ? 0xFF : 0)));
    char_in(r, (uint8_t)(c >> 8));
}

static void flag_in(rig_t *r)
{
    int i;
    for (i = 0; i < 8; i++) bit_in(r, (0x7E >> i) & 1);
}

/* framing mode 3: flag, the body and FCS-16 with zeros inserted, flag */
static void frame3_in(rig_t *r, const uint8_t *body, int n, int bad)
{
    uint8_t f[300];
    uint16_t c = (uint16_t)(crc_bits(0xFFFFu, 0x8408u, body, n) ^ 0xFFFFu);
    int i, b, ones = 0;
    memcpy(f, body, (size_t)n);
    f[n] = (uint8_t)((c & 0xFF) ^ (bad ? 0xFF : 0));
    f[n + 1] = (uint8_t)(c >> 8);
    flag_in(r);
    for (i = 0; i < n + 2; i++)
        for (b = 0; b < 8; b++) {
            int x = (f[i] >> b) & 1;
            bit_in(r, x);
            ones = x ? ones + 1 : 0;
            if (ones == 5) { bit_in(r, 0); ones = 0; }
        }
    flag_in(r);
}

/* our bits as start-stop characters: is pat among them? */
static int async_has(const rig_t *r, const uint8_t *pat, int n)
{
    uint8_t ch[4000];
    int nc = 0, i = 0, k, j;
    while (i + 10 <= r->out_n && nc < (int)sizeof ch) {
        unsigned c = 0;
        if (r->out[i] != 0) { i++; continue; }
        for (k = 0; k < 8; k++) c |= (unsigned)r->out[i + 1 + k] << k;
        if (r->out[i + 9] != 1) { i++; continue; }
        ch[nc++] = (uint8_t)c;
        i += 10;
    }
    for (i = 0; i + n <= nc; i++) {
        for (j = 0; j < n && ch[i + j] == pat[j]; j++) {}
        if (j == n) return 1;
    }
    return 0;
}

/* ---- data both ways between our endpoint and the peer ---- */
typedef struct { uint32_t x; size_t made, got; uint32_t h_in, h_out; } stream_t;

static void stream_init(stream_t *s, uint32_t seed)
{
    s->x = seed; s->made = s->got = 0; s->h_in = s->h_out = 2166136261u;
}

static uint8_t stream_next(uint32_t *x)
{
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    return (uint8_t)(*x >> 24);
}

/* write what fits of the next octets (at most total) to the endpoint
 * (to_peer) or the peer; read what came (unless hold) */
static void stream_step(stream_t *s, rig_t *r, int to_peer, size_t total, int hold)
{
    uint8_t buf[256];
    size_t n = total - s->made, i, w;
    if (n > sizeof buf) n = sizeof buf;
    if (n > 0) {
        uint32_t x = s->x;
        for (i = 0; i < n; i++) buf[i] = stream_next(&x);
        w = to_peer ? v42ec_write(&r->e, buf, n) : mnppeer_write(&r->p, buf, n);
        for (i = 0; i < w; i++) (void)stream_next(&s->x);
        s->h_in = fnv(s->h_in, buf, w);
        s->made += w;
    }
    if (hold) return;
    while ((n = to_peer ? mnppeer_read(&r->p, buf, sizeof buf) : v42ec_read(&r->e, buf, sizeof buf)) > 0) {
        s->h_out = fnv(s->h_out, buf, n);
        s->got += n;
    }
}

/* tu octets to the peer, td from it; 1 when both arrived by max_ms */
static int rig_data2(rig_t *r, stream_t *up, stream_t *down, size_t tu, size_t td, long max_ms)
{
    while (r->ms < max_ms) {
        rig_ms(r);
        stream_step(up, r, 1, tu, 0);
        stream_step(down, r, 0, td, 0);
        if (up->got >= tu && down->got >= td) return 1;
        if (v42ec_state(&r->e) == V42EC_DISCONNECTED) return 0;
    }
    return 0;
}

static int rig_data(rig_t *r, stream_t *up, stream_t *down, size_t total, long max_ms)
{
    return rig_data2(r, up, down, total, total, max_ms);
}

static mnp_peer_cfg peer_cfg(int framing)
{
    mnp_peer_cfg c;
    memset(&c, 0, sizeof c);
    c.originator = 1;
    c.framing = framing;
    c.k = 8;
    c.n401 = 64;
    c.seed = 7u;
    return c;
}

static rig_t R;

/* an ODP, then SABME: LAPM, with our bits the same as with MNP off */
static void odp_lapm(void)
{
    uint32_t h[2];
    int st[2], pr[2], on;
    for (on = 0; on <= 1; on++) {
        rig_init(&R, SRC_LAPM, on, 1, NULL);
        rig_run(&R, 3000);
        h[on] = R.h_out;
        st[on] = v42ec_state(&R.e);
        pr[on] = proto(&R.e);
        CHECK_EQ_I(R.max_proto & (1 << V42EC_PROTO_MNP), 0);
    }
    CHECK_EQ_I(st[1], V42EC_CONNECTED);
    CHECK_EQ_I(st[0], st[1]);
    CHECK_EQ_I(pr[1], V42EC_PROTO_LAPM);
    CHECK_EQ_I(pr[0], pr[1]);
    CHECK(h[0] == h[1]);
    printf("odp_lapm: MNP on: %s, protocol %d, our bits %s MNP off's\n",
           st[1] == V42EC_CONNECTED ? "connected" : "NOT CONNECTED", pr[1], h[0] == h[1] ? "the same as" : "DIFFERENT FROM");
}

/* an LR in framing mode 2 or 3: MNP, our LR in that framing, then 2 KiB
 * each way */
static void lr_mode(int framing)
{
    static const uint8_t syn_lr[] = { 0x16, 0x10, 0x02 };
    mnp_peer_cfg pc = peer_cfg(framing);
    v42ec_status_t s;
    stream_t up, down;
    int ok, lr2;
    if (framing == 2) { pc.send_opt = 1; pc.opt = 2; }
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 1500);
    lr2 = async_has(&R, syn_lr, 3);
    v42ec_get_status(&R.e, &s);
    CHECK_EQ_I(s.protocol, V42EC_PROTO_MNP);
    CHECK_EQ_I(s.state, V42EC_CONNECTED);
    CHECK_EQ_I(s.mnp_framing, framing);
    CHECK_EQ_I(s.mnp_opt, framing == 2 ? 2 : 0);
    CHECK_EQ_I(lr2, framing == 2);
    CHECK_EQ_I(mnppeer_state(&R.p), MNPP_CONNECTED);
    CHECK_EQ_I(R.p.framing, framing);
    stream_init(&up, 0x1234567u);
    stream_init(&down, 0x89ABCDEu);
    ok = rig_data(&R, &up, &down, 2048, 60000);
    CHECK(ok);
    CHECK_EQ_I(up.h_out, up.h_in);
    CHECK_EQ_I(down.h_out, down.h_in);
    printf("lr_mode%d: MNP, framing %d, our LR in %s, 2 KiB each way %s\n", framing, s.mnp_framing,
           lr2 ? "start-stop" : "HDLC", ok && up.h_out == up.h_in && down.h_out == down.h_in ? "intact" : "DAMAGED");
}

/* an ODP for T400, then an LR: the ODP wins, LAPM */
static void odp_then_lr(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    pc.odp_first = 1;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 3000);
    CHECK_EQ_I(proto(&R.e), V42EC_PROTO_LAPM);
    CHECK_EQ_I(R.max_proto & (1 << V42EC_PROTO_MNP), 0);
    CHECK(R.p.est_tries >= 1);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_ESTABLISH);
    printf("odp_then_lr: protocol %d (LAPM) after the peer's %d LRs\n", proto(&R.e), R.p.est_tries);
}

/* no ODP, no LR: start-stop after T400, or NO_EC with fallback off */
static void nothing(void)
{
    int fb;
    for (fb = 1; fb >= 0; fb--) {
        v42ec_status_t s;
        rig_init(&R, SRC_BITS, 1, fb, NULL);
        rig_run(&R, 749);
        CHECK_EQ_I(v42ec_state(&R.e), V42EC_DETECT);
        CHECK_EQ_I(proto(&R.e), V42EC_PROTO_NONE);
        rig_run(&R, 1);
        v42ec_get_status(&R.e, &s);
        CHECK_EQ_I(s.state, fb ? V42EC_ASYNC : V42EC_DISCONNECTED);
        CHECK_EQ_I(s.protocol, fb ? V42EC_PROTO_ASYNC : V42EC_PROTO_NONE);
        if (!fb) CHECK_EQ_I(s.reason, V42EC_R_NO_EC);
    }
    printf("nothing: start-stop at 750 ms; with fallback 0, NO_EC\n");
}

/* not an LR: a mode 2 LR with a bad FCS; an HDLC LR with a bad FCS; an
 * HDLC frame 07 01 03 (no constant parameters) */
static void junk(void)
{
    static const uint8_t not_lr[] = { 0x07, 0x01, 0x03, 0x01, 0x01, 0x02, 0x01, 0x03 };
    rig_init(&R, SRC_BITS, 1, 1, NULL);
    mark_in(&R, 100);
    frame2_in(&R, lr_courier, (int)sizeof lr_courier, 1);
    rig_run(&R, 760);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_ASYNC);
    CHECK_EQ_I(proto(&R.e), V42EC_PROTO_ASYNC);
    CHECK_EQ_I(R.max_proto & (1 << V42EC_PROTO_MNP), 0);

    rig_init(&R, SRC_BITS, 1, 1, NULL);
    mark_in(&R, 100);
    frame3_in(&R, lr_courier, (int)sizeof lr_courier, 1);
    rig_run(&R, 760);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_ASYNC);
    CHECK_EQ_I(R.max_proto & (1 << V42EC_PROTO_MNP), 0);

    rig_init(&R, SRC_BITS, 1, 1, NULL);
    mark_in(&R, 100);
    frame3_in(&R, not_lr, (int)sizeof not_lr, 0);
    rig_run(&R, 760);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_ESTABLISH);
    CHECK_EQ_I(proto(&R.e), V42EC_PROTO_LAPM);
    CHECK_EQ_I(R.max_proto & (1 << V42EC_PROTO_MNP), 0);
    printf("junk: bad FCS in either framing, an LR-like frame without its constants: not MNP\n");
}

/* Review Focus 3: typed text with SYN DLE STX (and a DLE ETX later): no
 * LR, start-stop at T400, every character delivered */
static void typed_syn(void)
{
    static const uint8_t text[] = "ab\x16\x10\x02" "cd and more\x10\x03xy, then more text";
    uint8_t got[100];
    size_t n;
    int i;
    rig_init(&R, SRC_BITS, 1, 1, NULL);
    mark_in(&R, 50);
    for (i = 0; i < (int)sizeof text - 1; i++) char_in(&R, text[i]);
    rig_run(&R, 760);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_ASYNC);
    CHECK_EQ_I(proto(&R.e), V42EC_PROTO_ASYNC);
    n = v42ec_read(&R.e, got, sizeof got);
    CHECK_EQ_I((int)n, (int)sizeof text - 1);
    CHECK(n == sizeof text - 1 && memcmp(got, text, n) == 0);
    printf("typed_syn: start-stop at T400, %d of %d characters delivered\n", (int)n, (int)sizeof text - 1);
}

/* MNP off: an LR is characters; start-stop at T400, the hunter untouched */
static void lr_off(void)
{
    mnp_peer_cfg pc = peer_cfg(2);
    rig_init(&R, SRC_PEER, 0, 1, &pc);
    rig_run(&R, 760);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_ASYNC);
    CHECK_EQ_I(proto(&R.e), V42EC_PROTO_ASYNC);
    CHECK_EQ_I((long)R.e.hunt2.frames, 0);
    CHECK_EQ_I((long)R.e.hunt2.bad, 0);
    CHECK_EQ_I(R.e.hunt2.len, 0);
    CHECK(v42ec_read_avail(&R.e) > 0);
    printf("lr_off: MNP off, an LR in start-stop: start-stop at T400, as 0.6.0\n");
}

/* status after the Courier's LR, and after the peer's LD */
static void status(void)
{
    v42ec_status_t s;
    rig_init(&R, SRC_BITS, 1, 1, NULL);
    mark_in(&R, 100);
    frame3_in(&R, lr_courier, (int)sizeof lr_courier, 0);
    rig_run(&R, 200);
    v42ec_get_status(&R.e, &s);
    CHECK_EQ_I(s.protocol, V42EC_PROTO_MNP);
    CHECK_EQ_I(s.state, V42EC_ESTABLISH);
    CHECK_EQ_I(s.k_tx, 8);
    CHECK_EQ_I(s.n401_tx, 64);
    CHECK_EQ_I(s.mnp_opt, 0);
    CHECK_EQ_I(s.mnp_framing, 3);
    CHECK_EQ_I(s.peer_ld_reason, 0);

    {
        mnp_peer_cfg pc = peer_cfg(3);
        rig_init(&R, SRC_PEER, 1, 1, &pc);
        rig_run(&R, 1500);
        CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
        mnppeer_release(&R.p);
        rig_run(&R, 500);
        v42ec_get_status(&R.e, &s);
        CHECK_EQ_I(s.peer_ld_reason, 255);
        CHECK_EQ_I(s.state, V42EC_DISCONNECTED);
        CHECK_EQ_I(s.reason, V42LAPM_R_PEER_DISC);
        CHECK_EQ_I(s.protocol, V42EC_PROTO_MNP);
        CHECK_EQ_I(s.k_tx, 8);
        CHECK_EQ_I(s.n401_tx, 64);
    }
    printf("status: k %d, N401 %d, opt %d; the peer's LD %d: reason %d\n", s.k_tx, s.n401_tx, s.mnp_opt,
           s.peer_ld_reason, s.reason);
}

/* destructive breaks: the peer's, then ours with data written after it
 * held until the LNA and delivered; the link stays up */
static void breaks(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    stream_t up, down;
    uint8_t junk[256];
    int opt = -1, len = -1, i, ok;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    stream_init(&up, 1u); stream_init(&down, 2u);
    (void)rig_data(&R, &up, &down, 1 << 20, 3000);
    CHECK(up.got > 0 && down.got > 0);

    /* the peer's LN 1 */
    CHECK_EQ_I(mnppeer_send_ln(&R.p, 1), 1);
    for (i = 0; i < 5000 && !(R.p.resets == 1 && R.p.ln_code < 0); i++) {
        rig_ms(&R);
        while (mnppeer_read(&R.p, junk, sizeof junk) > 0) {}
        if (opt < 0 && v42ec_get_break(&R.e, &opt, &len)) {}
        while (v42ec_read(&R.e, junk, sizeof junk) > 0) {}
    }
    CHECK_EQ_I(R.e.l.mnp.resets, 1);
    CHECK_EQ_I(opt, V42_BRK_DESTRUCTIVE_EXPEDITED);
    CHECK_EQ_I(len, 0);
    stream_init(&up, 3u); stream_init(&down, 4u);
    ok = rig_data(&R, &up, &down, 4096, R.ms + 60000);
    CHECK(ok && up.h_out == up.h_in && down.h_out == down.h_in);

    /* ours, data written after it (the peer's own, written while our LN
     * is on its way, its reset discards: only ours is followed) */
    CHECK_EQ_I(v42ec_send_break(&R.e, V42_BRK_DESTRUCTIVE_EXPEDITED, 0), 0);
    CHECK_EQ_I(v42ec_send_break(&R.e, V42_BRK_NONDESTRUCTIVE_EXPEDITED, 0), -1);
    stream_init(&up, 5u); stream_init(&down, 6u);
    stream_step(&up, &R, 1, 300, 1);
    stream_step(&up, &R, 1, 300, 1);
    CHECK_EQ_I((int)up.made, 300);
    ok = rig_data2(&R, &up, &down, 300, 0, R.ms + 60000);
    CHECK(ok && up.h_out == up.h_in);
    CHECK_EQ_I(R.e.l.mnp.resets, 2);
    CHECK_EQ_I(R.p.resets, 2);
    CHECK_EQ_I(mnppeer_get_ln(&R.p), 1);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    printf("breaks: the peer's LN 1 delivered as a break, ours with 300 octets after it: %s\n",
           ok && up.h_out == up.h_in ? "delivered" : "LOST");
}

/* the user does not read: the endpoint goes busy (credit 0), nothing is
 * lost, everything arrives once it reads */
static void busy(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    stream_t up, down;
    int was_busy = 0, ok;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 1500);
    stream_init(&up, 8u); stream_init(&down, 9u);
    while (R.ms < 25000) {
        rig_ms(&R);
        stream_step(&down, &R, 0, 12000, 1);
        if (R.e.l.mnp.busy) was_busy = 1;
    }
    CHECK(was_busy);
    CHECK(v42ec_read_avail(&R.e) > V42EC_RXBUF - 8 * 64);
    ok = rig_data(&R, &up, &down, 12000, R.ms + 120000);
    CHECK(ok && down.h_out == down.h_in);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    printf("busy: the user held off, credit 0, then 12000 octets %s\n", ok && down.h_out == down.h_in ? "intact" : "DAMAGED");
}

/* a release in framing mode 2: the transmitter is busy until the LD has
 * left (F24) */
static void release_mode2(void)
{
    mnp_peer_cfg pc = peer_cfg(2);
    int bits = 0;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 1500);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    CHECK_EQ_I(R.e.l.mnp.framing, 2);
    rig_run(&R, 300);
    CHECK_EQ_I(v42ec_tx_busy(&R.e), 0);
    v42ec_release(&R.e);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_DISCONNECTED);
    CHECK_EQ_I(v42ec_tx_busy(&R.e), 1);
    while (v42ec_tx_busy(&R.e) && bits < 10000) {
        mnppeer_rx_bit(&R.p, v42ec_tx_bit(&R.e));
        (void)mnppeer_tx_bit(&R.p);
        bits++;
    }
    /* SYN DLE STX, 04 02 01 01 FF, DLE ETX, FCS: 12 characters of 10 bits;
     * the last of them is out when busy ends */
    CHECK(bits >= 120);
    CHECK_EQ_I(R.p.rx_ld_reason, 255);
    CHECK_EQ_I(R.e.reason, V42LAPM_R_LOCAL);
    printf("release_mode2: busy for %d bits, the peer got LD %d\n", bits, R.p.rx_ld_reason);
}

/* framing mode 3: busy lasts until the LD's closing flag has gone, so a
 * host that stops the line at busy 0 still delivers the LD */
static void release_mode3(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    int bits = 0;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 1500);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    CHECK_EQ_I(R.e.l.mnp.framing, 3);
    rig_run(&R, 300);
    CHECK_EQ_I(v42ec_tx_busy(&R.e), 0);
    v42ec_release(&R.e);
    CHECK_EQ_I(v42ec_tx_busy(&R.e), 1);
    while (v42ec_tx_busy(&R.e) && bits < 10000) {
        mnppeer_rx_bit(&R.p, v42ec_tx_bit(&R.e));
        (void)mnppeer_tx_bit(&R.p);
        bits++;
    }
    CHECK_EQ_I(R.p.rx_ld_reason, 255);
    printf("release_mode3: busy for %d bits, the peer got LD %d\n", bits, R.p.rx_ld_reason);
}

/* an abort in the data phase: nothing more goes, not even an LD */
static void abort_mnp(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    int frames;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 1500);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    rig_run(&R, 100);
    v42ec_abort(&R.e);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_DISCONNECTED);
    frames = R.p.rx_frames;
    (void)mnppeer_write(&R.p, (const uint8_t *)"data", 4);
    rig_run(&R, 10000);
    CHECK_EQ_I(R.p.rx_frames, frames);
    CHECK_EQ_I(R.p.rx_ld_reason, 0);
    CHECK_EQ_I(R.e.reason, V42LAPM_R_LOCAL);
    printf("abort_mnp: after the abort the peer heard %d frames more\n", R.p.rx_frames - frames);
}

/* an LR whose constant parameter 1 is not 2: MNP, then LD 2 and the
 * endpoint's reason NEGOTIATION (M3) */
static void ld_const1(void)
{
    uint8_t lr[sizeof lr_courier];
    v42ec_status_t s;
    memcpy(lr, lr_courier, sizeof lr);
    lr[2] = 0x03;
    rig_init(&R, SRC_BITS, 1, 1, NULL);
    mark_in(&R, 100);
    frame3_in(&R, lr, (int)sizeof lr, 0);
    rig_run(&R, 300);
    v42ec_get_status(&R.e, &s);
    CHECK_EQ_I(s.protocol, V42EC_PROTO_MNP);
    CHECK_EQ_I(s.state, V42EC_DISCONNECTED);
    CHECK_EQ_I(s.reason, V42LAPM_R_NEGOTIATION);
    CHECK_EQ_I(R.e.l.mnp.ld_reason, 2);
    CHECK_EQ_I(v42ec_tx_busy(&R.e), 0);
    printf("ld_const1: constant parameter 1 = 3: LD %d, reason %d (NEGOTIATION)\n", R.e.l.mnp.ld_reason, s.reason);
}

/* the caller goes silent with LTs unacknowledged: N400 tries, LD 4, the
 * endpoint's reason N400 (M3) */
static void n400_end(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    static uint8_t d[1000];
    v42ec_status_t s;
    cfg_n400 = 3;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    cfg_n400 = 0;
    rig_run(&R, 1500);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    CHECK_EQ_I((int)v42ec_write(&R.e, d, sizeof d), (int)sizeof d);
    R.cut = 1;
    while (R.ms < 60000 && v42ec_state(&R.e) != V42EC_DISCONNECTED) rig_ms(&R);
    rig_run(&R, 200);
    v42ec_get_status(&R.e, &s);
    CHECK_EQ_I(s.state, V42EC_DISCONNECTED);
    CHECK_EQ_I(s.reason, V42LAPM_R_N400);
    CHECK_EQ_I((long)s.t401_expiries, 4);
    CHECK_EQ_I(R.p.rx_ld_reason, 4);
    printf("n400_end: the caller silent, N400 3: %lu T401 expiries, LD %d to the peer, reason %d (N400)\n",
           s.t401_expiries, R.p.rx_ld_reason, s.reason);
}

/* the non-destructive breaks through the endpoint (M3): the peer's LN 3
 * after the 300 octets it wrote before asking for it, its LN 2; ours, LN 3
 * after our 500 octets, and LN 2. No reset, the data intact */
static void ln_brk(void)
{
    mnp_peer_cfg pc = peer_cfg(3);
    uint8_t buf[700], got[700];
    size_t n, ng = 0, at = 0;
    int opt = -1, len = -1, code = -1, i;
    for (i = 0; i < (int)sizeof buf; i++) buf[i] = (uint8_t)(i * 13 + 5);
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    rig_run(&R, 1500);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);

    /* the peer's LN 3 */
    CHECK_EQ_I((int)mnppeer_write(&R.p, buf, 300), 300);
    CHECK_EQ_I(mnppeer_send_ln(&R.p, 3), 1);
    CHECK_EQ_I((int)mnppeer_write(&R.p, buf + 300, 100), 100);
    for (i = 0; i < 30000 && ng < 400; i++) {
        rig_ms(&R);
        ng += v42ec_read(&R.e, got + ng, sizeof got - ng);
        if (opt < 0 && v42ec_get_break(&R.e, &opt, &len)) at = ng;
    }
    CHECK_EQ_I(opt, V42_BRK_NONDESTRUCTIVE_INSEQ);
    CHECK_EQ_I((int)at, 300);
    CHECK_EQ_I((int)ng, 400);
    CHECK(memcmp(got, buf, 400) == 0);
    printf("ln_brk: the peer's LN 3: a break after %d of the 400 octets (300 written before it)\n", (int)at);

    /* the peer's LN 2 */
    opt = -1;
    CHECK_EQ_I(mnppeer_send_ln(&R.p, 2), 1);
    for (i = 0; i < 10000 && opt < 0; i++) {
        rig_ms(&R);
        if (v42ec_get_break(&R.e, &opt, &len)) break;
    }
    CHECK_EQ_I(opt, V42_BRK_NONDESTRUCTIVE_EXPEDITED);

    /* ours: LN 3 after 500 octets, 100 after it */
    CHECK_EQ_I((int)v42ec_write(&R.e, buf, 500), 500);
    CHECK_EQ_I(v42ec_send_break(&R.e, V42_BRK_NONDESTRUCTIVE_INSEQ, 0), 0);
    CHECK_EQ_I((int)v42ec_write(&R.e, buf + 500, 100), 100);
    ng = 0; at = 0;
    for (i = 0; i < 30000 && ng < 600; i++) {
        rig_ms(&R);
        while ((n = mnppeer_read(&R.p, got + ng, sizeof got - ng)) > 0) ng += n;
        if (code < 0 && (code = mnppeer_get_ln(&R.p)) >= 0) at = ng;
    }
    CHECK_EQ_I(code, 3);
    CHECK_EQ_I((int)at, 500);
    CHECK_EQ_I((int)ng, 600);
    CHECK(memcmp(got, buf, 600) == 0);

    /* ours: LN 2 */
    for (i = 0; i < 10000 && v42ec_send_break(&R.e, V42_BRK_NONDESTRUCTIVE_EXPEDITED, 0) != 0; i++) rig_ms(&R);
    for (code = -1, i = 0; i < 10000 && code < 0; i++) {
        rig_ms(&R);
        code = mnppeer_get_ln(&R.p);
    }
    CHECK_EQ_I(code, 2);
    CHECK_EQ_I(R.e.l.mnp.resets, 0);
    CHECK_EQ_I(R.p.resets, 0);
    CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
    printf("ln_brk: ours: LN 3 after %d of 600 octets (500 written before it), then LN %d; no reset\n", (int)at, code);
}

/* 300 bit/s (V.21): a mode 2 LR of about 31 characters takes about 1 s,
 * longer than T400. T400 is held while an LR is partly received, until
 * that frame ends, for at most V42EC_T400_LR_HOLD_MS (2000 ms) */
static void lr_300(void)
{
    mnp_peer_cfg pc = peer_cfg(2);
    v42ec_status_t s;
    stream_t up, down;
    int ok, st750;
    pc.send_opt = 1; pc.opt = 2;
    pc.t401_ms = 30000;
    rig_init(&R, SRC_PEER, 1, 1, &pc);
    R.bps = 300;
    rig_run(&R, 750);
    st750 = v42ec_state(&R.e);
    rig_run(&R, 4250);
    v42ec_get_status(&R.e, &s);
    CHECK_EQ_I(st750, V42EC_DETECT);
    CHECK_EQ_I(s.protocol, V42EC_PROTO_MNP);
    CHECK_EQ_I(s.state, V42EC_CONNECTED);
    CHECK_EQ_I(s.mnp_framing, 2);
    stream_init(&up, 31u); stream_init(&down, 32u);
    ok = rig_data(&R, &up, &down, 256, R.ms + 120000);
    CHECK(ok && up.h_out == up.h_in && down.h_out == down.h_in);
    printf("lr_300: an MNP caller at 300 bit/s, framing 2: state %d at 750 ms (T400 held), then MNP; "
           "256 octets each way %s\n", st750, ok && up.h_out == up.h_in && down.h_out == down.h_in ? "intact" : "DAMAGED");
}

/* the hold's ends at 300 bit/s: a frame that begins as an LR (LI, type
 * 1) and never ends gives start-stop at 750 + 2000 ms; an LR that ends
 * with a bad FCS gives start-stop at its end. A caller that sends no LR
 * gets T400 as 0.6.0 did: typed text with SYN DLE STX just before T400,
 * start-stop at 750 ms exactly with every character delivered; with MNP
 * off, the same */
static void t400_hold(void)
{
    static const uint8_t typed[] = "\x16\x10\x02" "abcdefgh";
    uint8_t got[64];
    int i, on, t_async = -1, t_bad = -1, t_typed[2] = { -1, -1 }, n_typed[2] = { 0, 0 };
    rig_init(&R, SRC_BITS, 1, 1, NULL);
    R.bps = 300;
    char_in(&R, 0x16); char_in(&R, 0x10); char_in(&R, 0x02);
    char_in(&R, 0x14); char_in(&R, 0x01);
    for (i = 0; i < 200; i++) char_in(&R, (uint8_t)'a');
    while (R.ms < 5000 && t_async < 0) {
        rig_ms(&R);
        if (v42ec_state(&R.e) == V42EC_ASYNC) t_async = (int)R.ms;
    }
    CHECK_EQ_I(t_async, 2750);

    rig_init(&R, SRC_BITS, 1, 1, NULL);
    R.bps = 300;
    mark_in(&R, 30);
    frame2_in(&R, lr_courier, (int)sizeof lr_courier, 1);
    while (R.ms < 5000 && t_bad < 0) {
        rig_ms(&R);
        if (v42ec_state(&R.e) == V42EC_ASYNC) t_bad = (int)R.ms;
    }
    /* 30 marks, then SYN DLE STX, 21 octets, DLE ETX and the FCS: 28
     * characters of 12 bits (char_in's two marks after each), the frame
     * over at the last one's stop bit */
    CHECK(t_bad > 750 && t_bad < 2750);
    CHECK_EQ_I(t_bad, ((30 + 28 * 12 - 2) * 1000 + 299) / 300);
    CHECK_EQ_I(R.max_proto & (1 << V42EC_PROTO_MNP), 0);

    /* typed: 180 marks (600 ms), then SYN DLE STX and text, a character
     * every 40 ms: inside that "frame" when T400 runs out */
    for (on = 1; on >= 0; on--) {
        int st749;
        rig_init(&R, SRC_BITS, on, 1, NULL);
        R.bps = 300;
        mark_in(&R, 180);
        for (i = 0; i < (int)sizeof typed - 1; i++) char_in(&R, typed[i]);
        rig_run(&R, 749);
        st749 = v42ec_state(&R.e);
        rig_run(&R, 1);
        if (st749 == V42EC_DETECT && v42ec_state(&R.e) == V42EC_ASYNC) t_typed[on] = 750;
        if (on) CHECK(mnp2_rx_in_frame(&R.e.hunt2));    /* the hunter was inside the "frame" */
        rig_run(&R, 1000);
        n_typed[on] = (int)v42ec_read(&R.e, got, sizeof got);
        CHECK_EQ_I(t_typed[on], 750);
        CHECK_EQ_I(n_typed[on], (int)sizeof typed - 1);
        CHECK(memcmp(got, typed, sizeof typed - 1) == 0);
    }
    printf("t400_hold: at 300 bit/s an LR's start that never ends: start-stop at %d ms; a bad LR: at its end, %d ms; "
           "SYN DLE STX typed at 600 ms: start-stop at %d ms (MNP off: %d ms), %d of %d characters\n",
           t_async, t_bad, t_typed[1], t_typed[0], n_typed[1], (int)sizeof typed - 1);
}

/* the line rates given (v42ec_set_rates, before MNP starts or once it is
 * up): T401 from 300 bit/s, no LT sent again in 4 KiB each way at 300
 * bit/s, framing 3 (with T401 computed at 2400 about half went again) */
static void rates_300(void)
{
    int when;
    for (when = 0; when < 2; when++) {
        mnp_peer_cfg pc = peer_cfg(3);
        v42ec_status_t s;
        stream_t up, down;
        int ok;
        pc.t401_ms = 30000;
        rig_init(&R, SRC_PEER, 1, 1, &pc);
        R.bps = 300;
        if (when == 0) CHECK_EQ_I(v42ec_set_rates(&R.e, 300, 300), 0);
        rig_run(&R, 3000);
        CHECK_EQ_I(v42ec_state(&R.e), V42EC_CONNECTED);
        if (when == 1) CHECK_EQ_I(v42ec_set_rates(&R.e, 300, 300), 0);
        CHECK_EQ_I(v42ec_set_rates(&R.e, 0, 300), -1);
        stream_init(&up, 41u); stream_init(&down, 42u);
        ok = rig_data(&R, &up, &down, 4096, R.ms + 600000);
        v42ec_get_status(&R.e, &s);
        CHECK(ok && up.h_out == up.h_in && down.h_out == down.h_in);
        CHECK_EQ_I((long)s.t401_expiries, 0);
        CHECK_EQ_I((long)s.tx_i_retx, 0);
        printf("rates_300: rates given %s: 4 KiB each way %s at 300 bit/s in %.1f s, T401 expiries %lu, LTs again %lu\n",
               when ? "once connected" : "before MNP", ok && up.h_out == up.h_in && down.h_out == down.h_in ? "intact" : "DAMAGED",
               R.ms / 1000.0, s.t401_expiries, s.tx_i_retx);
    }
}

int main(void)
{
    odp_lapm();
    lr_mode(2);
    lr_mode(3);
    odp_then_lr();
    nothing();
    junk();
    typed_syn();
    lr_off();
    status();
    breaks();
    busy();
    release_mode2();
    release_mode3();
    abort_mnp();
    ld_const1();
    n400_end();
    ln_brk();
    lr_300();
    t400_hold();
    rates_300();
    T_REPORT("test_ec_mnp");
}
