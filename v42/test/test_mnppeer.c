/* sim/mnppeer against itself: an originator and a responder on a bit loop
 * at 2400 bit/s, 1 ms ticks. Each case connects, moves data both ways and
 * checks it arrives intact (FNV-1a of what went in and what came out), or
 * checks one procedure of V.42 Annex A. One line per case. */
#include <stdint.h>
#include "../../sim/mnppeer.h"
#include "test.h"

#define BPS 2400

static uint32_t fnv(uint32_t h, const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h;
}

/* the data of one direction: xorshift32 octets, so DLE, SYN and 0x7E occur */
typedef struct { uint32_t x; size_t made, got; uint32_t h_in, h_out; } stream_t;

static void stream_init(stream_t *s, uint32_t seed)
{
    s->x = seed; s->made = s->got = 0; s->h_in = s->h_out = 2166136261u;
}

/* write what fits of the next n octets (at most total), read what came */
static void stream_step(stream_t *s, mnppeer_t *from, mnppeer_t *to, size_t total)
{
    uint8_t buf[256];
    size_t n, i, w;
    n = total - s->made;
    if (n > sizeof buf) n = sizeof buf;
    if (n > 0) {
        uint32_t x = s->x;
        for (i = 0; i < n; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            buf[i] = (uint8_t)(x >> 24);
        }
        w = mnppeer_write(from, buf, n);
        /* keep only what was taken: run the generator again for w octets */
        for (i = 0; i < w; i++) { s->x ^= s->x << 13; s->x ^= s->x >> 17; s->x ^= s->x << 5; }
        s->h_in = fnv(s->h_in, buf, w);
        s->made += w;
    }
    while ((n = mnppeer_read(to, buf, sizeof buf)) > 0) {
        s->h_out = fnv(s->h_out, buf, n);
        s->got += n;
    }
}

typedef struct {
    mnppeer_t a, b;               /* a the originator, b the responder */
    stream_t ab, ba;
    long ms, con_a, con_b;
    int acc;
} loop_t;

static void loop_init(loop_t *l, const mnp_peer_cfg *ca, const mnp_peer_cfg *cb)
{
    mnppeer_init(&l->a, ca);
    mnppeer_init(&l->b, cb);
    stream_init(&l->ab, 0x1234567u);
    stream_init(&l->ba, 0x89ABCDEu);
    l->ms = 0; l->con_a = l->con_b = -1; l->acc = 0;
}

/* one millisecond: the ticks, then the bits of that millisecond */
static void loop_ms(loop_t *l)
{
    mnppeer_tick(&l->a, 1);
    mnppeer_tick(&l->b, 1);
    l->acc += BPS;
    while (l->acc >= 1000) {
        int x = mnppeer_tx_bit(&l->a), y = mnppeer_tx_bit(&l->b);
        l->acc -= 1000;
        mnppeer_rx_bit(&l->b, x);
        mnppeer_rx_bit(&l->a, y);
    }
    l->ms++;
    if (l->con_a < 0 && mnppeer_state(&l->a) == MNPP_CONNECTED) l->con_a = l->ms;
    if (l->con_b < 0 && mnppeer_state(&l->b) == MNPP_CONNECTED) l->con_b = l->ms;
}

/* data both ways until both have arrived or max_ms; 1 if they did */
static int loop_data(loop_t *l, size_t total, long max_ms)
{
    while (l->ms < max_ms) {
        loop_ms(l);
        stream_step(&l->ab, &l->a, &l->b, total);
        stream_step(&l->ba, &l->b, &l->a, total);
        if (l->ab.got >= total && l->ba.got >= total) return 1;
        if (mnppeer_state(&l->a) == MNPP_DISCONNECTED || mnppeer_state(&l->b) == MNPP_DISCONNECTED) return 0;
    }
    return 0;
}

static mnp_peer_cfg cfg(int originator, int framing)
{
    mnp_peer_cfg c;
    memset(&c, 0, sizeof c);
    c.originator = originator;
    c.framing = framing;
    c.k = 8;
    c.n401 = 64;
    c.seed = originator ? 11u : 22u;
    return c;
}

/* connect within 2 s, then 64 KiB each way intact */
static void transfer(const char *name, const mnp_peer_cfg *ca, const mnp_peer_cfg *cb, long max_ms)
{
    static const size_t total = 65536;
    loop_t l;
    int ok;
    loop_init(&l, ca, cb);
    ok = loop_data(&l, total, max_ms);
    CHECK(ok);
    CHECK(l.con_a > 0 && l.con_a <= 2000);
    CHECK(l.con_b > 0 && l.con_b <= 2000);
    CHECK_EQ_I(l.ab.got, total);
    CHECK_EQ_I(l.ba.got, total);
    CHECK_EQ_I(l.ab.h_out, l.ab.h_in);
    CHECK_EQ_I(l.ba.h_out, l.ba.h_in);
    CHECK_EQ_I(mnppeer_state(&l.a), MNPP_CONNECTED);
    CHECK_EQ_I(mnppeer_state(&l.b), MNPP_CONNECTED);
    CHECK_EQ_I(l.a.rx_over_credit, 0);
    CHECK_EQ_I(l.b.rx_over_credit, 0);
    printf("%s: connected at %ld/%ld ms, %u octets each way %s in %ld ms, LT %d/%d, retx %d/%d, bad %d/%d\n",
           name, l.con_a, l.con_b, (unsigned)total,
           (l.ab.h_out == l.ab.h_in && l.ba.h_out == l.ba.h_in && ok) ? "intact" : "DAMAGED", l.ms,
           l.a.rx_lt, l.b.rx_lt, l.a.tx_retx, l.b.tx_retx, l.a.rx_bad, l.b.rx_bad);
}

static void loop_mode3(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    transfer("loop_mode3", &a, &b, 600000);
}

static void loop_mode2(void)
{
    mnp_peer_cfg a = cfg(1, 2), b = cfg(0, 2);
    transfer("loop_mode2", &a, &b, 600000);
}

/* 1 % of the frames lost, both ways */
static void loop_loss(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    a.lose_ppm = b.lose_ppm = 10000;
    transfer("loop_loss", &a, &b, 900000);
}

/* 1 % of the frames with a bit inverted, both ways, mode 2 */
static void loop_corrupt(void)
{
    mnp_peer_cfg a = cfg(1, 2), b = cfg(0, 2);
    a.corrupt_ppm = b.corrupt_ppm = 10000;
    transfer("loop_corrupt", &a, &b, 900000);
}

/* negotiation: the lower framing mode, k and N401; parameter 8 only when
 * both send it, then the optimised LT and LA both ways */
static void loop_negotiate(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 2);
    loop_t l;
    int opt_layout;
    b.k = 4; a.n401 = 32;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 4096, 60000));
    CHECK_EQ_I(l.a.framing, 2); CHECK_EQ_I(l.b.framing, 2);
    CHECK_EQ_I(l.a.k, 4); CHECK_EQ_I(l.b.k, 4);
    CHECK_EQ_I(l.a.n401, 32); CHECK_EQ_I(l.b.n401, 32);
    CHECK_EQ_I(l.a.last_layout_rx, MNPP_LAYOUT_STD);
    CHECK_EQ_I(l.b.last_layout_rx, MNPP_LAYOUT_STD);
    printf("loop_negotiate: framing %d k %d n401 %d layout %d/%d\n",
           l.a.framing, l.a.k, l.a.n401, l.a.last_layout_rx, l.b.last_layout_rx);

    a = cfg(1, 3); b = cfg(0, 3);
    a.send_opt = b.send_opt = 1; a.opt = 3; b.opt = 2;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 4096, 60000));
    CHECK_EQ_I(l.a.layout, MNPP_LAYOUT_OPT); CHECK_EQ_I(l.b.layout, MNPP_LAYOUT_OPT);
    CHECK_EQ_I(l.a.last_layout_rx, MNPP_LAYOUT_OPT);
    CHECK_EQ_I(l.b.last_layout_rx, MNPP_LAYOUT_OPT);
    CHECK_EQ_I(l.ab.h_out, l.ab.h_in); CHECK_EQ_I(l.ba.h_out, l.ba.h_in);
    opt_layout = l.b.last_layout_rx;

    /* only the caller sends parameter 8: standard */
    b.send_opt = 0;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 4096, 60000));
    CHECK_EQ_I(l.a.layout, MNPP_LAYOUT_STD); CHECK_EQ_I(l.b.layout, MNPP_LAYOUT_STD);
    CHECK_EQ_I(l.b.last_layout_rx, MNPP_LAYOUT_STD);
    printf("loop_negotiate: parameter 8 both 3&2: layout %d; caller only: layout %d\n",
           opt_layout, l.a.layout);

    /* parameter 8 bit 1 agreed: N401 256 both ways (the LRs carry 64) */
    a = cfg(1, 2); b = cfg(0, 2);
    a.send_opt = b.send_opt = 1; a.opt = b.opt = 3;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 8192, 60000));
    CHECK_EQ_I(l.a.n401, 256); CHECK_EQ_I(l.b.n401, 256);
    CHECK_EQ_I(l.a.layout, MNPP_LAYOUT_OPT); CHECK_EQ_I(l.b.layout, MNPP_LAYOUT_OPT);
    CHECK_EQ_I(l.ab.h_out, l.ab.h_in); CHECK_EQ_I(l.ba.h_out, l.ba.h_in);
    CHECK_EQ_I(l.a.rx_lt, 32); CHECK_EQ_I(l.b.rx_lt, 32);
    printf("loop_negotiate: parameter 8 both 3: n401 %d, 8192 octets in %d LTs\n", l.a.n401, l.b.rx_lt);
}

/* mnppeer_release: LD reason 255 at the other end, both disconnected */
static void loop_ld(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    loop_t l;
    int i;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 2048, 60000));
    mnppeer_release(&l.a);
    CHECK_EQ_I(mnppeer_state(&l.a), MNPP_DISCONNECTED);
    for (i = 0; i < 500; i++) loop_ms(&l);
    CHECK_EQ_I(l.b.rx_ld_reason, 255);
    CHECK_EQ_I(mnppeer_state(&l.b), MNPP_DISCONNECTED);
    CHECK_EQ_I(l.a.rx_ld_reason, 0);

    /* the responder releases, in mode 2 */
    a = cfg(1, 2); b = cfg(0, 2);
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 2048, 60000));
    mnppeer_release(&l.b);
    for (i = 0; i < 500; i++) loop_ms(&l);
    CHECK_EQ_I(l.a.rx_ld_reason, 255);
    CHECK_EQ_I(mnppeer_state(&l.a), MNPP_DISCONNECTED);
    printf("loop_ld: release -> LD reason %d at the other end, both ways\n", l.a.rx_ld_reason);
}

/* LN: each code delivered once, in order, with 1 % loss */
static void loop_ln(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    loop_t l;
    int i, got[4], n = 0, c;
    a.lose_ppm = b.lose_ppm = 10000;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 1024, 60000));
    mnppeer_send_ln(&l.a, 1);
    mnppeer_send_ln(&l.a, 2);
    mnppeer_send_ln(&l.a, 3);
    for (i = 0; i < 30000 && n < 3; i++) {
        loop_ms(&l);
        while (n < 4 && (c = mnppeer_get_ln(&l.b)) >= 0) got[n++] = c;
    }
    for (i = 0; i < 10000; i++) loop_ms(&l);
    while (n < 4 && (c = mnppeer_get_ln(&l.b)) >= 0) got[n++] = c;
    CHECK_EQ_I(n, 3);
    CHECK(n == 3 && got[0] == 1 && got[1] == 2 && got[2] == 3);
    CHECK_EQ_I(mnppeer_get_ln(&l.a), -1);
    CHECK_EQ_I(l.a.ln_code, -1);
    CHECK_EQ_I(mnppeer_state(&l.a), MNPP_CONNECTED);
    printf("loop_ln: %d codes delivered once each, in order\n", n);

    /* the send queue: MNPP_LNQ codes, then "full" */
    for (i = 0; i < MNPP_LNQ; i++) CHECK_EQ_I(mnppeer_send_ln(&l.a, 2), 1);
    CHECK_EQ_I(mnppeer_send_ln(&l.a, 2), 0);
}

/* the receive queue full: the LN is neither taken nor acknowledged, and
 * counted; once read, the sender's T401 brings it again, nothing is lost */
static void loop_ln_full(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    loop_t l;
    int i, sent = 0, n = 0, c, inorder = 1;
    loop_init(&l, &a, &b);
    CHECK(loop_data(&l, 1024, 60000));
    /* 12 codes, more than b's queue holds; b reads none for 20 s */
    for (i = 0; i < 20000; i++) {
        while (sent < 12 && mnppeer_send_ln(&l.a, 0x20 + sent)) sent++;
        loop_ms(&l);
    }
    CHECK_EQ_I(sent, 12);
    CHECK_EQ_I(l.b.lnr_n, MNPP_LNQ);
    CHECK(l.b.ln_rx_full > 0);
    for (i = 0; i < 40000; i++) {
        while ((c = mnppeer_get_ln(&l.b)) >= 0) {
            if (c != 0x20 + n) inorder = 0;
            n++;
        }
        loop_ms(&l);
    }
    while ((c = mnppeer_get_ln(&l.b)) >= 0) { if (c != 0x20 + n) inorder = 0; n++; }
    CHECK_EQ_I(n, 12);
    CHECK(inorder);
    CHECK_EQ_I(mnppeer_state(&l.a), MNPP_CONNECTED);
    CHECK_EQ_I(l.a.ln_code, -1);
    printf("loop_ln_full: %d codes through a queue of %d, %d LNs refused while full, all in order\n",
           n, MNPP_LNQ, l.b.ln_rx_full);
}

/* the confirming LA in the standard layout although bit 2 is agreed [BM] */
static void loop_conf_std(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    loop_t l;
    int at_connect;
    a.send_opt = b.send_opt = 1; a.opt = b.opt = 2;
    a.confirm_la_standard = 1;
    loop_init(&l, &a, &b);
    while (l.ms < 5000 && l.con_b < 0) loop_ms(&l);
    at_connect = l.b.last_layout_rx;
    CHECK_EQ_I(at_connect, MNPP_LAYOUT_STD);
    CHECK(loop_data(&l, 4096, 60000));
    CHECK_EQ_I(l.b.last_layout_rx, MNPP_LAYOUT_OPT);
    CHECK_EQ_I(l.ab.h_out, l.ab.h_in); CHECK_EQ_I(l.ba.h_out, l.ba.h_in);
    printf("loop_conf_std: confirming LA layout %d, then layout %d\n", at_connect, l.b.last_layout_rx);
}

/* zero credit for 3 s after connecting: nothing comes before it ends */
static void loop_zero_credit(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    loop_t l;
    size_t early;
    b.zero_credit_ms = 3000;
    loop_init(&l, &a, &b);
    while (l.ms < 60000 && l.con_b < 0) {
        loop_ms(&l);
        stream_step(&l.ab, &l.a, &l.b, 8192);
        stream_step(&l.ba, &l.b, &l.a, 8192);
    }
    while (l.ms < l.con_b + 2900) {
        loop_ms(&l);
        stream_step(&l.ab, &l.a, &l.b, 8192);
        stream_step(&l.ba, &l.b, &l.a, 8192);
    }
    early = l.ab.got;
    CHECK(loop_data(&l, 8192, 120000));
    CHECK_EQ_I(early, 0);
    CHECK_EQ_I(l.a.rx_over_credit, 0);
    CHECK_EQ_I(l.b.rx_over_credit, 0);
    CHECK_EQ_I(l.ab.h_out, l.ab.h_in); CHECK_EQ_I(l.ba.h_out, l.ba.h_in);
    printf("loop_zero_credit: %u octets before the credit, then 8192 intact\n", (unsigned)early);
}

/* ---- the peer's line against octets written out here (so that two peers
 * sharing a mistake cannot pass): the originator's first two frames, the
 * LR (the Courier's template, `LR and 0xC0`) and the confirming LA ---- */

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
#ifdef MNP_FCS2
#define T_FCS2 MNP_FCS2
#else
#define T_FCS2 0
#endif

static const uint8_t lr_courier[] = {
    0x14, 0x01, 0x02, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x02, 0x01, 0x03, 0x03, 0x01, 0x08, 0x04, 0x02, 0x40, 0x00 };
static const uint8_t la_conf[] = { 0x07, 0x05, 0x01, 0x01, 0x00, 0x02, 0x01, 0x08 };

/* the originator's bits for the first 1500 ms */
static int wire_bits(int framing, uint8_t *bits, int max)
{
    mnp_peer_cfg a = cfg(1, framing), b = cfg(0, framing);
    loop_t l;
    int n = 0;
    loop_init(&l, &a, &b);
    while (n < max) {
        int x, y;
        if (l.acc < 1000) { mnppeer_tick(&l.a, 1); mnppeer_tick(&l.b, 1); l.acc += BPS; }
        l.acc -= 1000;
        x = mnppeer_tx_bit(&l.a); y = mnppeer_tx_bit(&l.b);
        mnppeer_rx_bit(&l.b, x); mnppeer_rx_bit(&l.a, y);
        bits[n++] = (uint8_t)x;
    }
    return n;
}

/* HDLC: frames (with their FCS) between flags, zeros after five ones taken out */
static int wire_hdlc(const uint8_t *bits, int nb, uint8_t fr[][64], int *len, int maxf)
{
    int i, ones = 0, in = 0, nbit = 0, nf = 0, n = 0;
    unsigned cur = 0;
    for (i = 0; i < nb && nf < maxf; i++) {
        if (bits[i]) {
            ones++;
            if (ones >= 7) { in = 0; continue; }
        } else {
            if (ones == 6) {
                if (in && n > 0) { len[nf++] = n; }
                in = 1; n = 0; nbit = 0; cur = 0; ones = 0;
                continue;
            }
            if (ones == 5) { ones = 0; continue; }
            ones = 0;
        }
        if (!in) continue;
        cur |= (unsigned)bits[i] << nbit;
        if (++nbit == 8) { if (n < 64) fr[nf][n] = (uint8_t)cur; n++; nbit = 0; cur = 0; }
    }
    return nf;
}

/* start-stop: the octets */
static int wire_async(const uint8_t *bits, int nb, uint8_t *oct, int max)
{
    int i = 0, n = 0, b;
    while (i + 10 <= nb && n < max) {
        unsigned c = 0;
        if (bits[i]) { i++; continue; }
        for (b = 0; b < 8; b++) c |= (unsigned)bits[i + 1 + b] << b;
        if (!bits[i + 9]) { i++; continue; }
        oct[n++] = (uint8_t)c;
        i += 10;
    }
    return n;
}

static void wire(void)
{
    static uint8_t bits[3600];
    static const uint8_t s9[] = "123456789";
    uint8_t fr[4][64], oct[200], want[80], f2[2] = { 0, 0 };
    int len[4], nb, nf, no, nw, i, j;
    uint16_t c;

    memset(fr, 0, sizeof fr);
    /* the CRCs written here, against their check values (`MNP framing`) */
    CHECK_EQ_I(crc_bits(0xFFFFu, 0x8408u, s9, 9) ^ 0xFFFFu, 0x906E);
    CHECK_EQ_I(crc_bits(0, 0xA001u, s9, 9), 0xBB3D);

    /* mode 3: LR, then LA, each with its FCS-16, low octet first */
    nb = wire_bits(3, bits, (int)sizeof bits);
    nf = wire_hdlc(bits, nb, fr, len, 4);
    CHECK(nf >= 2);
    if (nf >= 2) {
        CHECK_EQ_I(len[0], (int)sizeof lr_courier + 2);
        CHECK(memcmp(fr[0], lr_courier, sizeof lr_courier) == 0);
        c = (uint16_t)(crc_bits(0xFFFFu, 0x8408u, lr_courier, (int)sizeof lr_courier) ^ 0xFFFFu);
        CHECK(fr[0][sizeof lr_courier] == (c & 0xFF) && fr[0][sizeof lr_courier + 1] == (c >> 8));
        CHECK_EQ_I(len[1], (int)sizeof la_conf + 2);
        CHECK(memcmp(fr[1], la_conf, sizeof la_conf) == 0);
        c = (uint16_t)(crc_bits(0xFFFFu, 0x8408u, la_conf, (int)sizeof la_conf) ^ 0xFFFFu);
        CHECK(fr[1][sizeof la_conf] == (c & 0xFF) && fr[1][sizeof la_conf + 1] == (c >> 8));
    }

    /* mode 2: SYN DLE STX, body, DLE ETX, the mode 2 FCS of body and ETX;
     * the LR offers framing mode 2 */
    nb = wire_bits(2, bits, (int)sizeof bits);
    no = wire_async(bits, nb, oct, (int)sizeof oct);
    for (j = 0; j < 2; j++) {
        uint8_t lr2[sizeof lr_courier];
        const uint8_t *body = j ? la_conf : lr2;
        int nbody = j ? (int)sizeof la_conf : (int)sizeof lr_courier;
        memcpy(lr2, lr_courier, sizeof lr2);
        lr2[13] = 2;                        /* parameter 2, framing mode: 2 */
        static const uint8_t etx = 0x03;
        uint16_t f = (T_FCS2 == 2 || T_FCS2 == 3) ? 0xFFFFu : 0;
        int hi = T_FCS2 == 1 || T_FCS2 == 3;
        nw = 0;
        want[nw++] = 0x16; want[nw++] = 0x10; want[nw++] = 0x02;
        for (i = 0; i < nbody; i++) {
            want[nw++] = body[i];
            if (body[i] == 0x10) want[nw++] = 0x10;
        }
        want[nw++] = 0x10; want[nw++] = 0x03;
        f = crc_bits(f, 0xA001u, body, nbody);
        f = crc_bits(f, 0xA001u, &etx, 1);
        want[nw++] = (uint8_t)(hi ? f >> 8 : f & 0xFF);
        want[nw++] = (uint8_t)(hi ? f & 0xFF : f >> 8);
        if (j == 0) { f2[0] = want[nw - 2]; f2[1] = want[nw - 1]; }
        CHECK(no >= nw);
        CHECK(no >= nw && memcmp(oct, want, (size_t)nw) == 0);
        if (no >= nw) { memmove(oct, oct + nw, (size_t)(no - nw)); no -= nw; }
    }
    printf("wire: LR and LA octets as written out, mode 3 FCS %02X %02X, mode 2 FCS %02X %02X\n",
           fr[0][sizeof lr_courier], fr[0][sizeof lr_courier + 1], f2[0], f2[1]);
}

/* the responder never answers: the originator stops after N400 LRs, no LD */
static void loop_no_lr(void)
{
    mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
    loop_t l;
    a.t401_ms = 500; a.n400 = 3;
    b.never_lr = 1;
    loop_init(&l, &a, &b);
    while (l.ms < 5000 && mnppeer_state(&l.a) != MNPP_DISCONNECTED) loop_ms(&l);
    CHECK_EQ_I(mnppeer_state(&l.a), MNPP_DISCONNECTED);
    CHECK(l.ms >= 1500 && l.ms < 1700);
    CHECK_EQ_I(l.a.est_tries, 3);
    CHECK_EQ_I(l.b.rx_ld_reason, 0);
    printf("loop_no_lr: %d LRs, gave up at %ld ms\n", l.a.est_tries, l.ms);
}

/* ---- one peer against frames built here, bit by bit ---- */
typedef struct {
    mnppeer_t p;
    uint8_t in[20000]; int in_n, in_i;   /* bits to the peer */
    uint8_t out[40000]; int out_n;       /* bits from it */
    int acc;
} raw_t;

static void raw_init(raw_t *r, const mnp_peer_cfg *c)
{
    mnppeer_init(&r->p, c);
    r->in_n = r->in_i = r->out_n = 0;
    r->acc = 0;
}

static void raw_bit(raw_t *r, int b) { if (r->in_n < (int)sizeof r->in) r->in[r->in_n++] = (uint8_t)b; }

static void raw_flag(raw_t *r)
{
    int i;
    for (i = 0; i < 8; i++) raw_bit(r, (0x7E >> i) & 1);
}

/* an HDLC frame: flag, body and FCS-16 with zeros inserted, flag;
 * bad != 0 inverts the FCS's first octet */
static void raw_frame(raw_t *r, const uint8_t *body, int n, int bad)
{
    uint8_t f[300];
    uint16_t c = (uint16_t)(crc_bits(0xFFFFu, 0x8408u, body, n) ^ 0xFFFFu);
    int i, b, ones = 0;
    memcpy(f, body, (size_t)n);
    f[n] = (uint8_t)((c & 0xFF) ^ (bad ? 0xFF : 0));
    f[n + 1] = (uint8_t)(c >> 8);
    raw_flag(r);
    for (i = 0; i < n + 2; i++)
        for (b = 0; b < 8; b++) {
            int x = (f[i] >> b) & 1;
            raw_bit(r, x);
            ones = x ? ones + 1 : 0;
            if (ones == 5) { raw_bit(r, 0); ones = 0; }
        }
    raw_flag(r);
}

/* ms milliseconds at 2400 bit/s: our bits in (mark when none), its bits out */
static void raw_ms(raw_t *r, int ms)
{
    while (ms-- > 0) {
        mnppeer_tick(&r->p, 1);
        r->acc += BPS;
        while (r->acc >= 1000) {
            int x = mnppeer_tx_bit(&r->p);
            r->acc -= 1000;
            if (r->out_n < (int)sizeof r->out) r->out[r->out_n++] = (uint8_t)x;
            mnppeer_rx_bit(&r->p, r->in_i < r->in_n ? r->in[r->in_i++] : 1);
        }
    }
}

/* the reason of the last LD among the peer's frames, -1 none */
static int raw_last_ld(const raw_t *r)
{
    uint8_t fr[16][64];
    int len[16], nf, i, reason = -1;
    nf = wire_hdlc(r->out, r->out_n, fr, len, 16);
    for (i = 0; i < nf; i++)
        if (len[i] == 7 && fr[i][0] == 4 && fr[i][1] == 2 && fr[i][2] == 1 && fr[i][3] == 1
            && crc_bits(0xFFFFu, 0x8408u, fr[i], 7) == 0xF0B8u)
            reason = fr[i][4];
    return reason;
}

/* a sender that ignores credit: the responder (k 2, credit 0 from the
 * start) gets LTs 1..6 at once; 1 and 2 were within the k of its LR,
 * 3..6 beyond any credit it gave */
static void raw_over_credit(void)
{
    static raw_t r;
    mnp_peer_cfg b = cfg(0, 3);
    uint8_t lt[15], buf[64];
    int ns, i;
    b.k = 2;
    b.zero_credit_ms = 100000;
    raw_init(&r, &b);
    raw_frame(&r, lr_courier, (int)sizeof lr_courier, 0);
    raw_ms(&r, 600);
    raw_frame(&r, la_conf, (int)sizeof la_conf, 0);
    raw_ms(&r, 300);
    CHECK_EQ_I(mnppeer_state(&r.p), MNPP_CONNECTED);
    CHECK_EQ_I(r.p.k, 2);
    for (ns = 1; ns <= 6; ns++) {
        lt[0] = 4; lt[1] = 4; lt[2] = 1; lt[3] = 1; lt[4] = (uint8_t)ns;
        for (i = 5; i < 15; i++) lt[i] = (uint8_t)(ns * 16 + i);
        raw_frame(&r, lt, 15, 0);
    }
    raw_ms(&r, 2000);
    CHECK_EQ_I(r.p.rx_lt, 6);
    CHECK_EQ_I(r.p.rx_over_credit, 4);
    CHECK_EQ_I(mnppeer_read(&r.p, buf, sizeof buf), 0);
    printf("raw_over_credit: 6 LTs on credit 0 (k 2): %d beyond the credit given\n", r.p.rx_over_credit);
}

/* the responder's LD codes: 2 for constant parameter 1, 1 for parameters it
 * cannot take and for its LR tries running out (code 3 is the initiator's,
 * Table A.5 Note) */
static void raw_ld_codes(void)
{
    static raw_t r;
    mnp_peer_cfg b = cfg(0, 3);
    uint8_t lr[sizeof lr_courier];
    int c2, c1a, c1b;

    memcpy(lr, lr_courier, sizeof lr);
    lr[2] = 3;                                  /* constant parameter 1 */
    raw_init(&r, &b);
    raw_frame(&r, lr, (int)sizeof lr, 0);
    raw_ms(&r, 600);
    c2 = raw_last_ld(&r);
    CHECK_EQ_I(c2, 2);
    CHECK_EQ_I(mnppeer_state(&r.p), MNPP_DISCONNECTED);

    memcpy(lr, lr_courier, sizeof lr);
    lr[13] = 1;                                 /* framing mode 1 */
    raw_init(&r, &b);
    raw_frame(&r, lr, (int)sizeof lr, 0);
    raw_ms(&r, 600);
    c1a = raw_last_ld(&r);
    CHECK_EQ_I(c1a, 1);

    raw_init(&r, &b);                           /* an LR, then no LA */
    raw_frame(&r, lr_courier, (int)sizeof lr_courier, 0);
    raw_ms(&r, 6000);
    c1b = raw_last_ld(&r);
    CHECK_EQ_I(c1b, 1);
    CHECK_EQ_I(r.p.est_tries, 2);
    CHECK_EQ_I(mnppeer_state(&r.p), MNPP_DISCONNECTED);
    printf("raw_ld_codes: constant 1 -> LD %d, framing 1 -> LD %d, no LA -> LD %d\n", c2, c1a, c1b);
}

/* the originator sends its LR again on a frame with a bad FCS (A.7.1.2 b) */
static void raw_lr_on_bad(void)
{
    static raw_t r;
    mnp_peer_cfg a = cfg(1, 3);
    a.t401_ms = 5000;
    raw_init(&r, &a);
    raw_ms(&r, 300);
    CHECK_EQ_I(r.p.est_tries, 1);
    raw_frame(&r, lr_courier, (int)sizeof lr_courier, 1);
    raw_ms(&r, 300);
    CHECK_EQ_I(r.p.est_tries, 2);
    CHECK_EQ_I(mnppeer_state(&r.p), MNPP_IDLE);
    printf("raw_lr_on_bad: LR sent %d times by 600 ms (T401 5 s)\n", r.p.est_tries);
}

/* a destructive break (LN 1) mid-transfer, from either end, 1 % of the
 * frames lost: both ends reset their sequences once (Tables 4 and 5 note
 * a), the link stays up, and data written afterwards arrives intact */
static void loop_break(void)
{
    int from;
    for (from = 0; from < 2; from++) {
        mnp_peer_cfg a = cfg(1, 3), b = cfg(0, 3);
        loop_t l;
        mnppeer_t *s;
        uint8_t junk[256];
        long i, end;
        int vs_before;
        a.lose_ppm = b.lose_ppm = 10000;
        loop_init(&l, &a, &b);
        (void)loop_data(&l, (size_t)1 << 24, 3000);
        s = from ? &l.b : &l.a;
        vs_before = s->vs;
        CHECK(l.ab.got > 0 && l.ba.got > 0);
        CHECK_EQ_I(mnppeer_send_ln(s, 1), 1);
        for (i = 0; i < 60000 && !(l.a.resets == 1 && l.b.resets == 1 && s->ln_code < 0 && s->lnq_n == 0); i++) {
            loop_ms(&l);
            while (mnppeer_read(&l.a, junk, sizeof junk) > 0) {}
            while (mnppeer_read(&l.b, junk, sizeof junk) > 0) {}
        }
        end = l.ms + 5000;
        while (l.ms < end) {
            loop_ms(&l);
            while (mnppeer_read(&l.a, junk, sizeof junk) > 0) {}
            while (mnppeer_read(&l.b, junk, sizeof junk) > 0) {}
        }
        CHECK_EQ_I(l.a.resets, 1);
        CHECK_EQ_I(l.b.resets, 1);
        CHECK_EQ_I(mnppeer_get_ln(from ? &l.a : &l.b), 1);
        CHECK(vs_before > 1);
        stream_init(&l.ab, 0x5555u);
        stream_init(&l.ba, 0xAAAAu);
        CHECK(loop_data(&l, 16384, l.ms + 300000));
        CHECK_EQ_I(l.ab.h_out, l.ab.h_in);
        CHECK_EQ_I(l.ba.h_out, l.ba.h_in);
        CHECK_EQ_I(mnppeer_state(&l.a), MNPP_CONNECTED);
        CHECK_EQ_I(mnppeer_state(&l.b), MNPP_CONNECTED);
        CHECK_EQ_I(l.a.rx_over_credit, 0);
        CHECK_EQ_I(l.b.rx_over_credit, 0);
        printf("loop_break: LN 1 from the %s: one reset each end, then 16 KiB each way %s\n",
               from ? "responder" : "originator",
               (l.ab.h_out == l.ab.h_in && l.ba.h_out == l.ba.h_in) ? "intact" : "DAMAGED");
    }
}

/* while an LT keeps the line busy, an LNA for an older LN (N(RA) 0: the
 * LN is due again) and then the matching one: no LN goes after it */
static void raw_lna_match(void)
{
    static raw_t r;
    mnp_peer_cfg b = cfg(0, 3);
    uint8_t d[512], lna[5] = { 4, 7, 1, 1, 0 };
    int i;
    raw_init(&r, &b);
    raw_frame(&r, lr_courier, (int)sizeof lr_courier, 0);
    raw_ms(&r, 600);
    raw_frame(&r, la_conf, (int)sizeof la_conf, 0);
    raw_ms(&r, 300);
    CHECK_EQ_I(mnppeer_state(&r.p), MNPP_CONNECTED);
    for (i = 0; i < (int)sizeof d; i++) d[i] = (uint8_t)i;
    CHECK_EQ_I((int)mnppeer_write(&r.p, d, sizeof d), (int)sizeof d);
    CHECK_EQ_I(mnppeer_send_ln(&r.p, 2), 1);
    for (i = 0; i < 1000 && !(r.p.ln_code >= 0 && !r.p.ln_send); i++) raw_ms(&r, 1);
    raw_ms(&r, 100);
    CHECK(r.p.line_framing == 3 && r.p.tx_lt > 0);
    raw_frame(&r, lna, 5, 0);
    lna[4] = 1;
    raw_frame(&r, lna, 5, 0);
    raw_ms(&r, 600);
    CHECK_EQ_I(r.p.vsa, 2);
    CHECK_EQ_I(r.p.ln_code, -1);
    CHECK_EQ_I(r.p.ln_send, 0);
    CHECK_EQ_I(r.p.ln_t, 0);
    CHECK_EQ_I(r.p.ln_tries, 1);
    printf("raw_lna_match: an older LNA, then the match, while an LT is out: no LN after it\n");
}

int main(void)
{
    loop_mode3();
    loop_mode2();
    loop_loss();
    loop_corrupt();
    loop_negotiate();
    loop_ld();
    loop_ln();
    loop_ln_full();
    loop_conf_std();
    loop_zero_credit();
    loop_break();
    loop_no_lr();
    wire();
    raw_over_credit();
    raw_ld_codes();
    raw_lr_on_bad();
    raw_lna_match();
    T_REPORT("test_mnppeer");
}
