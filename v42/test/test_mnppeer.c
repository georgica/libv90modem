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

int main(void)
{
    loop_mode3();
    loop_mode2();
    loop_loss();
    loop_corrupt();
    loop_negotiate();
    loop_ld();
    loop_ln();
    loop_zero_credit();
    loop_no_lr();
    wire();
    T_REPORT("test_mnppeer");
}
