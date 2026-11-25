/* MNP (V.42 Annex A) through the public API: the library answers with a
 * v90m_ec_t on its bit pump, MNP on (cfg.mnp.enable), started the way
 * docs/error-control.md says (v90m_ec_reset at V90M_EV_CONNECTED, the line
 * rates given with v90m_ec_set_rates then and at each V90M_EV_RATES). The
 * callers are the simulator's modems with sim/mnppeer, an independent MNP
 * entity, on their bits: a V.32bis caller (sim/v32call through V.8, over
 * sim/v32line) and a V.22bis and a V.21 caller (sim/callpeer, its bit pump
 * taken over with callpeer_set_pump). Each MNP call moves a PRBS each way,
 * checked octet by octet, and is released by us with LD 255. With MNP off
 * the same callers get 0.6.0's endpoint (mnp_off below).
 * Linked with the development archive alone (the simulator's callers). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "v90modem.h"
#include "test.h"
#include "callpeer.h"
#include "v8call.h"
#include "v32call.h"
#include "v32line.h"
#include "mnppeer.h"

#ifdef TEST_SHORT
#define DATA_FAST 4096           /* octets each way, V.32bis and V.22bis */
#define DATA_V21  256            /* octets each way at 300 bit/s */
#else
#define DATA_FAST 32768
#define DATA_V21  2048
#endif

enum { CALLER_V32 = 0, CALLER_V22, CALLER_V21 };

/* PRBS-23 (x^23 + x^18 + 1), eight bits to the octet */
typedef struct { uint32_t r; size_t n; } prbs_t;
static uint8_t prbs_next(prbs_t *p)
{
    unsigned o = 0; int b;
    for (b = 0; b < 8; b++) {
        unsigned x = ((p->r >> 22) ^ (p->r >> 17)) & 1u;
        p->r = ((p->r << 1) | x) & 0x7FFFFFu;
        o |= x << b;
    }
    p->n++;
    return (uint8_t)o;
}

/* ---- our end: the modem and the endpoint, as a host would wire them ---- */
typedef struct {
    v90m_t *m;
    v90m_ec_t *ec;
    v90m_ec_cfg cfg;
    int ec_on;                   /* the endpoint was reset at CONNECTED */
    long ec_ms;                  /* ms ticked since */
    int rates_calls, rates_ev;
    int tx_bps, rx_bps;
} ans_t;

static int ans_get(void *u) { ans_t *a = u; return a->ec_on ? v90m_ec_tx_bit(a->ec) : 1; }
static void ans_put(void *u, int b) { ans_t *a = u; if (a->ec_on) v90m_ec_rx_bit(a->ec, b); }

/* the host pattern: reset at CONNECTED, the rates then and at RATES */
static void ans_event(void *u, v90m_event ev)
{
    ans_t *a = u;
    v90m_state st;
    if (ev != V90M_EV_CONNECTED && ev != V90M_EV_RATES) return;
    v90m_get_state(a->m, &st);
    if (ev == V90M_EV_CONNECTED) {
        CHECK_EQ_I(v90m_ec_reset(a->ec, &a->cfg), V90M_OK);
        a->ec_on = 1; a->ec_ms = 0;
    } else a->rates_ev++;
    CHECK_EQ_I(v90m_ec_set_rates(a->ec, st.tx_bps, st.rx_bps), V90M_OK);
    a->rates_calls++;
    a->tx_bps = st.tx_bps; a->rx_bps = st.rx_bps;
}

static void ans_init(ans_t *a, unsigned mods, int mnp_on)
{
    v90m_cfg c; int err;
    memset(a, 0, sizeof *a);
    v90m_ec_cfg_default(&a->cfg, 0);
    a->cfg.mnp.enable = mnp_on;
    a->ec = v90m_ec_create(&a->cfg, &err);
    CHECK(a->ec != NULL);
    v90m_cfg_default(&c);
    c.role = V90M_ROLE_ANSWER; c.modulations = mods;
    c.getbit = ans_get; c.putbit = ans_put; c.on_event = ans_event; c.user = a;
    a->m = v90m_create(&c, &err);
    CHECK(a->m != NULL);
}

/* ---- the caller: a modem with mnppeer on its bits once it is connected ---- */
typedef struct {
    int kind;
    mnppeer_t p; int p_on;
    v8call_t v8; v32call_t vc; v32line_t line;     /* CALLER_V32 */
    callpeer_t cp;                                 /* CALLER_V22, CALLER_V21 */
    int16_t x;                                     /* the sample in flight: to us (V.32bis),
                                                      to the caller (callpeer) */
} cal_t;

static int cal_get(void *u) { cal_t *c = u; return c->p_on ? mnppeer_tx_bit(&c->p) : 1; }
static void cal_put(void *u, int b) { cal_t *c = u; if (c->p_on) mnppeer_rx_bit(&c->p, b); }

/* the V.32bis caller on the line's clock: V.8, then V.32bis */
static double v32_peer(void *u, double rx)
{
    cal_t *c = u;
    if (c->v8.state != V8C_DONE) {
        int16_t tx = 0; long q = lrint(rx);
        v8call_tick(&c->v8, (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q), &tx);
        return tx;
    }
    return v32call_tick(&c->vc, rx);
}

static mnp_peer_cfg peer_cfg(int framing)
{
    mnp_peer_cfg pc;
    memset(&pc, 0, sizeof pc);
    pc.originator = 1;
    pc.framing = framing;
    pc.k = 8; pc.n401 = 64;
    if (framing == 2) { pc.send_opt = 1; pc.opt = 2; }
    pc.seed = 11u;
    return pc;
}

static void cal_init(cal_t *c, int kind)
{
    memset(c, 0, sizeof *c);
    c->kind = kind;
    if (kind == CALLER_V32) {
        vc_cfg cc; v32line_cfg lc; v8_menu_t cm;
        memset(&lc, 0, sizeof lc);
        lc.mulaw = 1; lc.loss_db = 6; lc.delay = 200; lc.near_db = 15; lc.far_db = 20;
        lc.c_near_db = 15; lc.c_far_db = 20; lc.noise_a_dbm0 = -300; lc.noise_c_dbm0 = -300; lc.seed = 1;
        v32line_init(&c->line, &lc);
        memset(&cc, 0, sizeof cc);
        cc.level = 3000; cc.rates = 0x1Fu;   /* every rate, 4800 to 14400 */ cc.trn_len = 1280;
        cc.getbit = cal_get; cc.putbit = cal_put; cc.user = c;
        v32call_init(&c->vc, &cc);
        memset(&cm, 0, sizeof cm);
        cm.call_function = V8_CALLF_DATA; cm.modes = V8_MOD_V32 | V8_MOD_V22; cm.n_mod_octets = 3;
        v8call_init(&c->v8, 8000, 4243, &cm);
    } else {
        callpeer_init(&c->cp, 1, kind == CALLER_V22 ? V8_MOD_V22 : V8_MOD_V21, 1, 3000);
        callpeer_set_pump(&c->cp, cal_get, cal_put, c);
    }
}

static int cal_connected(const cal_t *c)
{
    return c->kind == CALLER_V32 ? c->vc.connected && c->vc.state == VCS_DATA : c->cp.connected;
}

/* ---- one call ---- */
typedef struct { ans_t a; cal_t c; long ms; } call_t;
static call_t K;

/* one ms: 8 samples both ways, then the timers */
static void call_ms(call_t *k)
{
    int i;
    for (i = 0; i < 8; i++) {
        int16_t tx;
        if (k->c.kind == CALLER_V32) {
            v90m_process_linear(k->a.m, &k->c.x, &tx, 1);
            k->c.x = v32line_tick(&k->c.line, tx, v32_peer, &k->c);
        } else {
            int16_t rx = callpeer_tick(&k->c.cp, k->c.x);
            v90m_process_linear(k->a.m, &rx, &tx, 1);
            k->c.x = tx;
        }
    }
    if (!k->c.p_on && cal_connected(&k->c)) k->c.p_on = 1;
    if (k->c.p_on) mnppeer_tick(&k->c.p, 1);
    if (k->a.ec_on) { v90m_ec_tick(k->a.ec, 1); k->a.ec_ms++; }
    k->ms++;
}

static void call_start(call_t *k, int kind, int framing, int mnp_on)
{
    mnp_peer_cfg pc = peer_cfg(framing);
    unsigned mods = kind == CALLER_V32 ? V90M_MOD_V32BIS | V90M_MOD_V22 | V90M_MOD_V21
                                       : V90M_MOD_V22 | V90M_MOD_V21;
    /* sim/callpeer counts as connected in V.21 as soon as V.8 has chosen
     * it, some 100 ms before we are: its LR waits as a caller's would for
     * our carrier */
    if (kind == CALLER_V21) pc.lr_delay_ms = 500;
    ans_init(&k->a, mods, mnp_on);
    cal_init(&k->c, kind);
    mnppeer_init(&k->c.p, &pc);
    k->ms = 0;
}

static void call_done(call_t *k)
{
    v90m_destroy(k->a.m);
    v90m_ec_destroy(k->a.ec);
}

static const char *cname(int kind)
{
    return kind == CALLER_V32 ? "V.32bis" : kind == CALLER_V22 ? "V.22bis" : "V.21";
}

/* MNP on: connected in MNP with the caller's framing, total octets each
 * way intact, released by us with LD 255 */
static void mnp_call(const char *name, int kind, int framing, size_t total)
{
    call_t *k = &K;
    v90m_ec_status s;
    v90m_state st;
    prbs_t up_w = { 1u, 0 }, up_r = { 1u, 0 }, dn_w = { 0x5A5A5u, 0 }, dn_r = { 0x5A5A5u, 0 };
    size_t up_got = 0, dn_got = 0;
    long up_bad = 0, dn_bad = 0, t_mnp = -1, t_data = -1, bits = 0;
    int st750 = -1;
    uint8_t buf[256];

    call_start(k, kind, framing, 1);
    while (k->ms < 60000 && t_mnp < 0) {
        call_ms(k);
        if (k->a.ec_on && k->a.ec_ms == 750) st750 = v90m_ec_state(k->a.ec);
        if (k->a.ec_on && v90m_ec_state(k->a.ec) == V90M_EC_CONNECTED) t_mnp = k->ms;
        if (k->a.ec_on && v90m_ec_state(k->a.ec) == V90M_EC_ASYNC) break;
    }
    CHECK(t_mnp > 0);
    v90m_ec_get_status(k->a.ec, &s);
    v90m_get_state(k->a.m, &st);
    CHECK_EQ_I(s.protocol, V90M_EC_PROTO_MNP);
    CHECK_EQ_I(s.mnp_framing, framing);
    CHECK_EQ_I(mnppeer_state(&k->c.p), MNPP_CONNECTED);
    CHECK_EQ_I(k->a.rates_calls, 1);
    CHECK_EQ_I(k->a.tx_bps, st.tx_bps);
    CHECK_EQ_I(st.tx_bps, kind == CALLER_V32 ? 14400 : kind == CALLER_V22 ? 2400 : 300);
    /* at 300 bit/s the LR outlasts T400, which was held for it */
    if (kind == CALLER_V21) CHECK_EQ_I(st750, V90M_EC_DETECT);

    /* the PRBS both ways: written as room allows, every octet read checked */
    while (k->ms < t_mnp + 1200000 && (up_got < total || dn_got < total)) {
        size_t n, i;
        call_ms(k);
        while (up_w.n < total && v90m_ec_write_space(k->a.ec) > 0) {
            uint8_t b; prbs_t save = up_w;
            b = prbs_next(&up_w);
            if (v90m_ec_write(k->a.ec, &b, 1) != 1) { up_w = save; break; }
        }
        while (dn_w.n < total) {
            prbs_t save = dn_w;
            uint8_t b = prbs_next(&dn_w);
            if (mnppeer_write(&k->c.p, &b, 1) != 1) { dn_w = save; break; }
        }
        while ((n = mnppeer_read(&k->c.p, buf, sizeof buf)) > 0)
            for (i = 0; i < n; i++, up_got++) up_bad += buf[i] != prbs_next(&up_r);
        while ((n = v90m_ec_read(k->a.ec, buf, sizeof buf)) > 0)
            for (i = 0; i < n; i++, dn_got++) dn_bad += buf[i] != prbs_next(&dn_r);
        if (v90m_ec_state(k->a.ec) != V90M_EC_CONNECTED) break;
    }
    t_data = k->ms - t_mnp;
    CHECK_EQ_I((long)up_got, (long)total); CHECK_EQ_I((long)dn_got, (long)total);
    CHECK_EQ_I(up_bad, 0); CHECK_EQ_I(dn_bad, 0);
    v90m_ec_get_status(k->a.ec, &s);
    CHECK_EQ_I(s.state, V90M_EC_CONNECTED);
    CHECK_EQ_I(s.rx_frames_bad, 0);
    CHECK_EQ_I(s.t401_expiries, 0);

    /* our release once everything we wrote is acknowledged: LD 255, then
     * the line until it has left */
    while (v90m_ec_tx_idle(k->a.ec) != 1 && k->ms < t_mnp + 1260000) call_ms(k);
    CHECK(v90m_ec_tx_idle(k->a.ec) == 1);
    v90m_ec_release(k->a.ec);
    CHECK_EQ_I(v90m_ec_state(k->a.ec), V90M_EC_DISCONNECTED);
    while (v90m_ec_tx_busy(k->a.ec) == 1 && bits++ < 100000) call_ms(k);
    {
        long i;
        for (i = 0; i < 500; i++) call_ms(k);    /* the line's delay */
    }
    v90m_ec_get_status(k->a.ec, &s);
    CHECK_EQ_I(s.reason, V90M_EC_R_LOCAL);
    CHECK_EQ_I(k->c.p.rx_ld_reason, 255);
    CHECK_EQ_I(mnppeer_state(&k->c.p), MNPP_DISCONNECTED);
    CHECK_EQ_I(k->a.rates_ev, 0);
    printf("%s: %s %d/%d bit/s, MNP framing %d after %.3f s%s; %d octets each way %s in %.1f s; "
           "T401 expiries %lu, retransmitted %lu; LD %d to the caller\n",
           name, cname(kind), st.tx_bps, st.rx_bps, s.mnp_framing, t_mnp / 1000.0,
           st750 == V90M_EC_DETECT ? " (T400 held for the LR)" : "", (int)total,
           up_bad == 0 && dn_bad == 0 && up_got == total && dn_got == total ? "intact" : "DAMAGED",
           t_data / 1000.0, s.t401_expiries, s.tx_i_retx, k->c.p.rx_ld_reason);
    call_done(k);
}

/* MNP off: the same callers get 0.6.0's endpoint. An LR in start-stop is
 * characters: start-stop at T400, 750 ms after the reset. An LR in HDLC
 * comes with flags, which say LAPM (V.42 7.2.1.3): the protocol phase,
 * where no SABME ever comes, so NO_SABME after establish_ms (30 s) */
static void mnp_off(int kind, int framing)
{
    call_t *k = &K;
    int at749 = -1, at750 = -1;
    long t_end = -1;
    v90m_ec_status s;
    call_start(k, kind, framing, 0);
    while (k->ms < 60000 && (!k->a.ec_on || k->a.ec_ms < 750)) {
        call_ms(k);
        if (k->a.ec_on && k->a.ec_ms == 749) at749 = v90m_ec_state(k->a.ec);
    }
    at750 = v90m_ec_state(k->a.ec);
    v90m_ec_get_status(k->a.ec, &s);
    CHECK(k->c.p.est_tries >= 1);
    if (framing == 2) {
        CHECK_EQ_I(at749, V90M_EC_DETECT);
        CHECK_EQ_I(at750, V90M_EC_ASYNC);
        CHECK_EQ_I(s.protocol, V90M_EC_PROTO_ASYNC);
    } else {
        CHECK_EQ_I(at750, V90M_EC_ESTABLISH);
        CHECK_EQ_I(s.protocol, V90M_EC_PROTO_LAPM);
#ifndef TEST_SHORT
        while (k->ms < 60000 && v90m_ec_state(k->a.ec) != V90M_EC_DISCONNECTED) call_ms(k);
        t_end = k->a.ec_ms;
        v90m_ec_get_status(k->a.ec, &s);
        CHECK_EQ_I(s.state, V90M_EC_DISCONNECTED);
        CHECK_EQ_I(s.reason, V90M_EC_R_NO_SABME);
        CHECK(t_end > 30000 && t_end <= 30750);
#endif
    }
    CHECK(s.protocol != V90M_EC_PROTO_MNP);
    if (framing == 2)
        printf("mnp_off: %s, the caller's LR in framing 2: state %d at 749 ms, %d (start-stop) at 750 ms\n",
               cname(kind), at749, at750);
    else if (t_end >= 0)
        printf("mnp_off: %s, the caller's LR in framing 3: LAPM's flags, state %d at 750 ms; NO_SABME at %.3f s\n",
               cname(kind), at750, t_end / 1000.0);
    else
        printf("mnp_off: %s, the caller's LR in framing 3: LAPM's flags, state %d at 750 ms\n", cname(kind), at750);
    call_done(k);
}

int main(void)
{
    mnp_call("v32bis_mode3", CALLER_V32, 3, DATA_FAST);
    mnp_call("v32bis_mode2", CALLER_V32, 2, DATA_FAST);
    mnp_call("v22bis_mode3", CALLER_V22, 3, DATA_FAST);
    mnp_call("v22bis_mode2", CALLER_V22, 2, DATA_FAST);
    mnp_call("v21_mode2", CALLER_V21, 2, DATA_V21);
    mnp_off(CALLER_V32, 3);
    mnp_off(CALLER_V32, 2);
    mnp_off(CALLER_V22, 3);
    mnp_off(CALLER_V22, 2);
    T_REPORT("test_mnp_call");
}
