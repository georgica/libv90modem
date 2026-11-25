/* v90m_ec_link_hst through the public API, before the HST engine exists:
 * the endpoint linked to the answering modem before its first sample, by a
 * host that feeds it the modem's bits and ticks it from that first sample
 * on and never resets it (spec decision 18, Review Focus 2: the
 * linked endpoint holds T400 until the modem has connected on a path
 * other than HST). The callers are the simulator's modems: a V.32bis
 * caller (sim/v32call through V.8, over sim/v32line) with a V.42
 * originator (an ODP) or without error control, and a V.22bis caller
 * (sim/callpeer) with sim/mnppeer on its bits. The link bit in the
 * modem's modes and the modem's side of the mailbox (answer_hst_link) are
 * read through api/v90modem_priv.h.
 * Linked with the development archive alone. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "v90modem.h"
#include "v90modem_priv.h"
#include "test.h"
#include "callpeer.h"
#include "v8call.h"
#include "v32call.h"
#include "v32line.h"
#include "mnppeer.h"

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p; size_t i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static int linked_bit(v90m_t *m) { return (v90m__answer(m)->cfg.modes & ANS_MODE_HST_LINKED) != 0; }
static int link_state(v90m_t *m)
{
    int st = -1;
    hst_link_t *L = answer_hst_link(v90m__answer(m), &st);
    return L ? -2 : st;
}

/* ---- our end ---- */
typedef struct {
    v90m_t *m;
    v90m_ec_t *ec;
    v90m_ec_cfg cfg;
    int always;                  /* 1: bits and ticks from the first sample, never reset;
                                    0: the documented host (reset at CONNECTED) */
    int ec_on;
    long conn_ms;                /* ms at CONNECTED, -1 before */
    int conn_state;              /* answer_hst_link's state at CONNECTED */
    int undecided_before;        /* every ms before CONNECTED said UNDECIDED */
} ans_t;

static long now_ms;

static int ans_get(void *u) { ans_t *a = u; return a->ec_on ? v90m_ec_tx_bit(a->ec) : 1; }
static void ans_put(void *u, int b) { ans_t *a = u; if (a->ec_on) v90m_ec_rx_bit(a->ec, b); }

static void ans_event(void *u, v90m_event ev)
{
    ans_t *a = u;
    v90m_state st;
    if (ev == V90M_EV_DATA_STOPPED && a->ec_on) v90m_ec_line_stopped(a->ec);
    if (ev == V90M_EV_DATA_MOVING && a->ec_on) v90m_ec_line_restored(a->ec);
    if (ev != V90M_EV_CONNECTED && ev != V90M_EV_RATES) return;
    v90m_get_state(a->m, &st);
    if (ev == V90M_EV_CONNECTED) {
        a->conn_ms = now_ms;
        a->conn_state = link_state(a->m);
        if (!a->always) CHECK_EQ_I(v90m_ec_reset(a->ec, &a->cfg), V90M_OK);
        a->ec_on = 1;
    }
    CHECK_EQ_I(v90m_ec_set_rates(a->ec, st.tx_bps, st.rx_bps), V90M_OK);
}

static void ans_init(ans_t *a, unsigned mods, int link, int always)
{
    v90m_cfg c; int err;
    memset(a, 0, sizeof *a);
    a->always = always; a->ec_on = always; a->conn_ms = -1; a->conn_state = -1; a->undecided_before = 1;
    v90m_ec_cfg_default(&a->cfg, 0);
    a->cfg.mnp.enable = 1;
    a->ec = v90m_ec_create(&a->cfg, &err);
    CHECK(a->ec != NULL);
    v90m_cfg_default(&c);
    c.role = V90M_ROLE_ANSWER; c.modulations = mods;
    c.getbit = ans_get; c.putbit = ans_put; c.on_event = ans_event; c.user = a;
    a->m = v90m_create(&c, &err);
    CHECK(a->m != NULL);
    if (link) {
        CHECK_EQ_I(v90m_ec_link_hst(a->ec, a->m), V90M_OK);
        CHECK_EQ_I(linked_bit(a->m), 1);
    }
}

/* ---- the caller ---- */
enum { CAL_ODP = 0, CAL_NOEC, CAL_MNP_V22 };
typedef struct {
    int kind;
    v90m_ec_t *ec; int ec_on;                      /* CAL_ODP: a V.42 originator */
    mnppeer_t p; int p_on;                         /* CAL_MNP_V22 */
    v8call_t v8; v32call_t vc; v32line_t line;     /* CAL_ODP, CAL_NOEC */
    callpeer_t cp;                                 /* CAL_MNP_V22 */
    int16_t x;
} cal_t;

static int cal_get(void *u)
{
    cal_t *c = u;
    if (c->kind == CAL_ODP) return c->ec_on ? v90m_ec_tx_bit(c->ec) : 1;
    if (c->kind == CAL_MNP_V22) return c->p_on ? mnppeer_tx_bit(&c->p) : 1;
    return 1;                                      /* no error control: marks */
}
static void cal_put(void *u, int b)
{
    cal_t *c = u;
    if (c->kind == CAL_ODP && c->ec_on) v90m_ec_rx_bit(c->ec, b);
    if (c->kind == CAL_MNP_V22 && c->p_on) mnppeer_rx_bit(&c->p, b);
}

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

static void cal_init(cal_t *c, int kind)
{
    memset(c, 0, sizeof *c);
    c->kind = kind;
    if (kind != CAL_MNP_V22) {
        vc_cfg cc; v32line_cfg lc; v8_menu_t cm;
        memset(&lc, 0, sizeof lc);
        lc.mulaw = 1; lc.loss_db = 6; lc.delay = 200; lc.near_db = 15; lc.far_db = 20;
        lc.c_near_db = 15; lc.c_far_db = 20; lc.noise_a_dbm0 = -300; lc.noise_c_dbm0 = -300; lc.seed = 1;
        v32line_init(&c->line, &lc);
        memset(&cc, 0, sizeof cc);
        cc.level = 3000; cc.rates = 0x1Fu; cc.trn_len = 1280;
        cc.getbit = cal_get; cc.putbit = cal_put; cc.user = c;
        v32call_init(&c->vc, &cc);
        memset(&cm, 0, sizeof cm);
        cm.call_function = V8_CALLF_DATA; cm.modes = V8_MOD_V32 | V8_MOD_V22; cm.n_mod_octets = 3;
        v8call_init(&c->v8, 8000, 4243, &cm);
        if (kind == CAL_ODP) {
            v90m_ec_cfg oc; int err;
            v90m_ec_cfg_default(&oc, 1);
            c->ec = v90m_ec_create(&oc, &err);
            CHECK(c->ec != NULL);
        }
    } else {
        mnp_peer_cfg pc;
        memset(&pc, 0, sizeof pc);
        pc.originator = 1; pc.framing = 2; pc.k = 8; pc.n401 = 64; pc.send_opt = 1; pc.opt = 2; pc.seed = 11u;
        mnppeer_init(&c->p, &pc);
        callpeer_init(&c->cp, 1, V8_MOD_V22, 1, 3000);
        callpeer_set_pump(&c->cp, cal_get, cal_put, c);
    }
}

static int cal_connected(const cal_t *c)
{
    return c->kind == CAL_MNP_V22 ? c->cp.connected : c->vc.connected && c->vc.state == VCS_DATA;
}

/* ---- one call, 1 ms at a time ---- */
typedef struct { ans_t a; cal_t c; uint32_t h; } call_t;
static call_t K;

static void call_ms(call_t *k)
{
    int i;
    for (i = 0; i < 8; i++) {
        int16_t tx;
        if (k->c.kind != CAL_MNP_V22) {
            v90m_process_linear(k->a.m, &k->c.x, &tx, 1);
            k->c.x = v32line_tick(&k->c.line, tx, v32_peer, &k->c);
        } else {
            int16_t rx = callpeer_tick(&k->c.cp, k->c.x);
            v90m_process_linear(k->a.m, &rx, &tx, 1);
            k->c.x = tx;
        }
        k->h = fnv(k->h, &tx, sizeof tx);
    }
    if (cal_connected(&k->c)) {
        if (k->c.kind == CAL_ODP && !k->c.ec_on) k->c.ec_on = 1;
        if (k->c.kind == CAL_MNP_V22 && !k->c.p_on) k->c.p_on = 1;
    }
    if (k->c.ec_on) v90m_ec_tick(k->c.ec, 1);
    if (k->c.p_on) mnppeer_tick(&k->c.p, 1);
    if (k->a.conn_ms < 0 && link_state(k->a.m) != HST_LINK_UNDECIDED) k->a.undecided_before = 0;
    if (k->a.ec_on) v90m_ec_tick(k->a.ec, 1);
    now_ms++;
}

static void call_start(call_t *k, int kind, unsigned mods, int link, int always)
{
    now_ms = 0;
    k->h = 2166136261u;
    ans_init(&k->a, mods, link, always);
    cal_init(&k->c, kind);
}

/* the endpoint first: linked, it lets go of the modem (which must still be there) */
static void call_done(call_t *k)
{
    v90m_ec_destroy(k->a.ec);
    v90m_destroy(k->a.m);
    if (k->c.ec) v90m_ec_destroy(k->c.ec);
}

/* a V.32bis caller with V.42 (its ODP): LAPM, data both ways */
static void linked_v32_odp(void)
{
    static const char msg[] = "linked from the first sample, never reset";
    call_t *k = &K;
    v90m_ec_status s;
    uint8_t got[64], back[64];
    size_t n = 0, nb = 0;
    call_start(k, CAL_ODP, V90M_MOD_V32BIS | V90M_MOD_V22 | V90M_MOD_V21, 1, 1);
    while (now_ms < 30000 && (v90m_ec_state(k->a.ec) != V90M_EC_CONNECTED || v90m_ec_state(k->c.ec) != V90M_EC_CONNECTED))
        call_ms(k);
    CHECK_EQ_I(v90m_ec_state(k->a.ec), V90M_EC_CONNECTED);
    CHECK_EQ_I(v90m_ec_write(k->a.ec, (const uint8_t *)msg, sizeof msg), sizeof msg);
    CHECK_EQ_I(v90m_ec_write(k->c.ec, (const uint8_t *)msg, sizeof msg), sizeof msg);
    while (now_ms < 40000 && (n < sizeof msg || nb < sizeof msg)) {
        call_ms(k);
        n += v90m_ec_read(k->c.ec, got + n, sizeof got - n);
        nb += v90m_ec_read(k->a.ec, back + nb, sizeof back - nb);
    }
    v90m_ec_get_status(k->a.ec, &s);
    CHECK_EQ_I(s.protocol, V90M_EC_PROTO_LAPM);
    CHECK_EQ_I(s.state, V90M_EC_CONNECTED);
    CHECK_EQ_I(n, sizeof msg); CHECK(memcmp(got, msg, sizeof msg) == 0);
    CHECK_EQ_I(nb, sizeof msg); CHECK(memcmp(back, msg, sizeof msg) == 0);
    CHECK(k->a.conn_ms > 0);
    CHECK_EQ_I(k->a.undecided_before, 1);
    CHECK_EQ_I(k->a.conn_state, HST_LINK_NONE);
    printf("linked_v32_odp: CONNECTED at %ld ms (modem: UNDECIDED before, %d then); protocol %d, %d octets each way\n",
           k->a.conn_ms, k->a.conn_state, s.protocol, (int)n);
    call_done(k);
}

/* a V.32bis caller without error control: start-stop 750 ms after CONNECTED */
static void linked_v32_noec(void)
{
    call_t *k = &K;
    v90m_ec_status s;
    long at = -1;
    call_start(k, CAL_NOEC, V90M_MOD_V32BIS | V90M_MOD_V22 | V90M_MOD_V21, 1, 1);
    while (now_ms < 30000 && at < 0) {
        call_ms(k);
        if (v90m_ec_state(k->a.ec) != V90M_EC_DETECT) at = now_ms;
    }
    v90m_ec_get_status(k->a.ec, &s);
    CHECK_EQ_I(s.state, V90M_EC_ASYNC);
    CHECK_EQ_I(s.protocol, V90M_EC_PROTO_ASYNC);
    CHECK(k->a.conn_ms > 0);
    CHECK_NEAR(at - k->a.conn_ms, 750, 10);
    CHECK_EQ_I(k->a.undecided_before, 1);
    printf("linked_v32_noec: CONNECTED at %ld ms, start-stop %ld ms later\n", k->a.conn_ms, at - k->a.conn_ms);
    call_done(k);
}

/* the link bit: set by the link, gone with v90m_reset (link again), cleared
 * by the endpoint's reset with MNP off and by its destroy */
static void relink_after_reset(void)
{
    v90m_cfg c; v90m_ec_cfg ec; v90m_t *m; v90m_ec_t *e; int err, b[6];
    v90m_cfg_default(&c); c.modulations |= V90M_MOD_HST;
    m = v90m_create(&c, &err);
    v90m_ec_cfg_default(&ec, 0); ec.mnp.enable = 1;
    e = v90m_ec_create(&ec, &err);
    CHECK(m != NULL && e != NULL);
    if (!m || !e) return;
    CHECK_EQ_I(v90m_ec_link_hst(e, m), V90M_OK);
    b[0] = linked_bit(m);
    CHECK_EQ_I(link_state(m), HST_LINK_UNDECIDED);
    CHECK_EQ_I(v90m_ec_reset(e, &ec), V90M_OK);                 /* MNP on: the link stays */
    b[1] = linked_bit(m);
    CHECK_EQ_I(v90m_reset(m, &c), V90M_OK);
    b[2] = linked_bit(m);
    CHECK_EQ_I(v90m_ec_link_hst(e, m), V90M_OK);
    b[3] = linked_bit(m);
    ec.mnp.enable = 0;
    CHECK_EQ_I(v90m_ec_reset(e, &ec), V90M_OK);
    b[4] = linked_bit(m);
    ec.mnp.enable = 1;
    CHECK_EQ_I(v90m_ec_reset(e, &ec), V90M_OK);
    CHECK_EQ_I(v90m_ec_link_hst(e, m), V90M_OK);
    v90m_ec_destroy(e);
    b[5] = linked_bit(m);
    CHECK_EQ_I(b[0], 1); CHECK_EQ_I(b[1], 1); CHECK_EQ_I(b[2], 0);
    CHECK_EQ_I(b[3], 1); CHECK_EQ_I(b[4], 0); CHECK_EQ_I(b[5], 0);
    printf("relink_after_reset: linked %d; ec reset %d; v90m_reset %d; linked again %d; ec reset MNP off %d; ec destroyed %d\n",
           b[0], b[1], b[2], b[3], b[4], b[5]);
    v90m_destroy(m);
}

/* a linked modem that the host resets to the calling role: no HST call
 * (the endpoint counts T400 at once, as unlinked), and its destroy leaves
 * the calling modem as a fresh one */
static void role_change(void)
{
    v90m_cfg c; v90m_ec_cfg ec; v90m_t *m, *f; v90m_ec_t *e; int err, t = 0, j;
    uint32_t h[2] = { 2166136261u, 2166136261u };
    int16_t rx[8], tx[8];
    v90m_cfg_default(&c); c.modulations |= V90M_MOD_HST;
    m = v90m_create(&c, &err);
    v90m_ec_cfg_default(&ec, 0); ec.mnp.enable = 1;
    e = v90m_ec_create(&ec, &err);
    CHECK(m != NULL && e != NULL);
    if (!m || !e) return;
    CHECK_EQ_I(v90m_ec_link_hst(e, m), V90M_OK);
    c.role = V90M_ROLE_ORIGINATE; c.modulations = V90M_MOD_V22 | V90M_MOD_V21;
    CHECK_EQ_I(v90m_reset(m, &c), V90M_OK);
    while (t < 3000 && v90m_ec_state(e) == V90M_EC_DETECT) { v90m_ec_tick(e, 1); t++; }
    CHECK_EQ_I(t, 750);
    CHECK_EQ_I(v90m_ec_state(e), V90M_EC_ASYNC);
    v90m_ec_destroy(e);
    f = v90m_create(&c, &err);
    CHECK(f != NULL);
    if (!f) { v90m_destroy(m); return; }
    memset(rx, 0, sizeof rx);
    for (j = 0; j < 8000; j++) {
        v90m_process_linear(m, rx, tx, 8); h[0] = fnv(h[0], tx, sizeof tx);
        v90m_process_linear(f, rx, tx, 8); h[1] = fnv(h[1], tx, sizeof tx);
    }
    CHECK(h[0] == h[1]);
    printf("role_change: reset to calling while linked: start-stop %d ms later; after the destroy, 8 s %s a fresh caller's\n",
           t, h[0] == h[1] ? "the same as" : "DIFFER from");
    v90m_destroy(m);
    v90m_destroy(f);
}

/* HST offered and linked, a V.22bis caller with MNP: everything as with HST
 * not offered and no link (no HST engine yet); the documented host */
static void hst_offered_now(void)
{
    call_t *k = &K;
    uint32_t h[2]; long conn[2]; int proto[2], st[2], mod[2], j;
    for (j = 0; j < 2; j++) {
        v90m_state ms;
        v90m_ec_status s;
        unsigned mods = V90M_MOD_V22 | V90M_MOD_V21 | (j ? V90M_MOD_HST : 0);
        call_start(k, CAL_MNP_V22, mods, j, 0);
        while (now_ms < 12000) call_ms(k);
        v90m_get_state(k->a.m, &ms);
        v90m_ec_get_status(k->a.ec, &s);
        h[j] = k->h; conn[j] = k->a.conn_ms; proto[j] = s.protocol; st[j] = s.state; mod[j] = ms.modulation;
        call_done(k);
    }
    CHECK_EQ_I(mod[0], V90M_CONN_V22BIS);
    CHECK_EQ_I(proto[0], V90M_EC_PROTO_MNP);
    CHECK_EQ_I(st[0], V90M_EC_CONNECTED);
    CHECK_EQ_I(mod[1], mod[0]); CHECK_EQ_I(conn[1], conn[0]);
    CHECK_EQ_I(proto[1], proto[0]); CHECK_EQ_I(st[1], st[0]);
    CHECK(h[1] == h[0]);
    printf("hst_offered_now: V.22bis caller with MNP: CONNECTED at %ld / %ld ms, protocol %d / %d, our samples %s\n",
           conn[0], conn[1], proto[0], proto[1], h[1] == h[0] ? "the same" : "DIFFER");
}

int main(void)
{
    linked_v32_odp();
    linked_v32_noec();
    relink_after_reset();
    role_change();
    hst_offered_now();
    T_REPORT("test_ec_hstlink");
}
