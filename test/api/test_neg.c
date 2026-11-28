/* The negotiation report (v90m_get_negotiation, V90M_EV_NEGOTIATION), from
 * the outside: what each side heard of V.8, which modulation it is on while
 * the start-up runs, and why it is on it. The modems come from libv90modem.a
 * as it is installed; an API caller, a simulated caller without V.8 or a
 * V.92 caller with Quick Connect from the dev archive, or silence. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "v90modem.h"
#include "test.h"
#include "callpeer.h"
#include "v92call.h"
#include "v32call.h"
#include "x2call.h"
#include "k56call.h"

#define MAXSTEP 48

/* one line of what a modem reported, at each V90M_EV_NEGOTIATION */
typedef struct { long at; v90m_negotiation n; } step_t;
typedef struct {
    v90m_t *m; v90m_async_t *a;
    v90m_cfg cfg;
    long    k;                  /* the sample being processed */
    step_t  step[MAXSTEP];
    int     n_steps, n_ended, after_end;
} end_t;
static int end_get(void *u) { return v90m_async_getbit(((end_t *)u)->a); }
static void end_put(void *u, int b) { v90m_async_putbit(((end_t *)u)->a, b); }
static void end_event(void *u, v90m_event ev)
{
    end_t *e = u;
    if (ev == V90M_EV_ENDED) e->n_ended++;
    if (ev != V90M_EV_NEGOTIATION) return;
    if (e->n_ended) e->after_end++;
    if (e->n_steps < MAXSTEP) {
        e->step[e->n_steps].at = e->k;
        v90m_get_negotiation(e->m, &e->step[e->n_steps].n);
    }
    e->n_steps++;
}

static void end_init_opt(end_t *e, v90m_role role, unsigned mods, unsigned opt)
{
    v90m_cfg c; int err;
    memset(e, 0, sizeof *e);
    e->a = v90m_async_create(&err);
    v90m_cfg_default(&c);
    c.role = role; c.modulations = mods | opt;
    c.getbit = end_get; c.putbit = end_put; c.user = e; c.on_event = end_event;
    e->cfg = c;
    e->m = v90m_create(&c, &err);
    CHECK(e->m != NULL);
}
static void end_init(end_t *e, v90m_role role, unsigned mods) { end_init_opt(e, role, mods, V90M_OPT_NEG_EVENTS); }
/* every step kept: more would be a storm of events, and find() blind to them */
static void end_free(end_t *e) { CHECK(e->n_steps <= MAXSTEP); v90m_destroy(e->m); v90m_async_destroy(e->a); }

static const char *basis_name(v90m_basis b)
{
    return b == V90M_BASIS_NONE ? "none" : b == V90M_BASIS_TRYING ? "trying" : b == V90M_BASIS_HEARD ? "heard"
           : b == V90M_BASIS_V8 ? "V.8" : b == V90M_BASIS_QC ? "QC" : "?";
}

static void show(const char *who, const end_t *e)
{
    int i;
    for (i = 0; i < e->n_steps && i < MAXSTEP; i++) {
        const v90m_negotiation *n = &e->step[i].n;
        printf("    %s %6.2f s: far menu %d, CM %04x pcm %x, JM %04x pcm %x, %s (%s)\n", who, e->step[i].at / 8000.0,
               n->far_menu, n->cm_modes, n->cm_pcm, n->jm_modes, n->jm_pcm, v90m_conn_name(n->trying), basis_name(n->basis));
    }
}

/* the first step on this modulation for this reason, or NULL */
static const step_t *find(const end_t *e, v90m_conn trying, v90m_basis basis)
{
    int i;
    for (i = 0; i < e->n_steps && i < MAXSTEP; i++)
        if (e->step[i].n.trying == trying && e->step[i].n.basis == basis) return &e->step[i];
    return NULL;
}

/* an answering modem that hears nothing: V.22bis, then the Bell tone at
 * 4 s of it, then V.21 at 8 s, none of them heard */
static void test_silent_caller(void)
{
    static end_t e;
    const step_t *a, *b, *c;
    v90m_negotiation n;
    v90m_state s;
    int16_t rx = 0, tx;
    end_init(&e, V90M_ROLE_ANSWER, V90M_MOD_V22 | V90M_MOD_V21 | V90M_MOD_BELL212A);
    if (!e.m) return;
    for (e.k = 0; e.k < 8000L * 60; e.k++) {
        v90m_process_linear(e.m, &rx, &tx, 1);
        if (e.n_ended) break;
    }
    printf("  a silent caller:\n"); show("answerer", &e);
    v90m_get_state(e.m, &s); v90m_get_negotiation(e.m, &n);
    CHECK_EQ_I(s.end, V90M_END_FAILED);
    a = find(&e, V90M_CONN_V22BIS, V90M_BASIS_TRYING);
    b = find(&e, V90M_CONN_BELL212A, V90M_BASIS_TRYING);
    c = find(&e, V90M_CONN_V21, V90M_BASIS_TRYING);
    CHECK(a && b && c && a < b && b < c);
    if (a && b && c) {
        CHECK_NEAR((b->at - a->at) / 8000.0, 4.0, 0.1);
        CHECK_NEAR((c->at - a->at) / 8000.0, 8.0, 0.1);
    }
    CHECK(find(&e, V90M_CONN_V21, V90M_BASIS_HEARD) == NULL);
    CHECK_EQ_I(n.far_menu, 0); CHECK_EQ_I(n.cm_modes, 0); CHECK_EQ_I(n.jm_modes, 0);
    CHECK_EQ_I(n.trying, V90M_CONN_V21); CHECK_EQ_I(n.basis, V90M_BASIS_TRYING);   /* kept after the end */
    CHECK_EQ_I(e.after_end, 0);
    CHECK_EQ_I(v90m_reset(e.m, &e.cfg), V90M_OK);
    v90m_get_negotiation(e.m, &n);
    CHECK_EQ_I(n.trying, V90M_CONN_NONE); CHECK_EQ_I(n.basis, V90M_BASIS_NONE);
    end_free(&e);
}

/* a V.22bis caller without V.8: heard, never "V.8" */
static void test_legacy_caller(void)
{
    static end_t e;
    static callpeer_t p;
    v90m_negotiation n;
    v90m_diag d;
    int16_t line = 0, rx, tx;
    end_init(&e, V90M_ROLE_ANSWER, V90M_MOD_V22 | V90M_MOD_V21);
    if (!e.m) return;
    callpeer_init(&p, 0, 0, 1, 3000);
    for (e.k = 0; e.k < 8000L * 30; e.k++) {
        rx = callpeer_tick(&p, line);
        v90m_process_linear(e.m, &rx, &tx, 1);
        line = tx;
        v90m_get_diag(e.m, &d);
        if (d.data_reached || e.n_ended) break;
    }
    printf("  a V.22bis caller without V.8:\n"); show("answerer", &e);
    v90m_get_negotiation(e.m, &n);
    CHECK(d.data_reached);
    CHECK(find(&e, V90M_CONN_V22BIS, V90M_BASIS_HEARD) != NULL);
    CHECK_EQ_I(n.far_menu, 0);
    CHECK_EQ_I(n.basis, V90M_BASIS_HEARD);
    CHECK_EQ_I(n.trying, V90M_CONN_V22BIS);
    end_free(&e);
}

/* an API caller and an API answerer back to back, to data or to the end */
static void pair(end_t *c, end_t *a, unsigned c_mods, unsigned a_mods)
{
    int16_t c_rx = 0, a_rx = 0;
    long k;
    end_init(c, V90M_ROLE_ORIGINATE, c_mods);
    end_init(a, V90M_ROLE_ANSWER, a_mods);
    if (!c->m || !a->m) return;
    for (k = 0; k < 8000L * 30; k++) {
        int16_t c_tx, a_tx;
        v90m_diag cd, ad;
        c->k = a->k = k;
        v90m_process_linear(c->m, &c_rx, &c_tx, 1);
        v90m_process_linear(a->m, &a_rx, &a_tx, 1);
        a_rx = c_tx; c_rx = a_tx;
        v90m_get_diag(c->m, &cd); v90m_get_diag(a->m, &ad);
        if ((cd.data_reached && ad.data_reached) || (c->n_ended && a->n_ended)) break;
    }
}

static void test_v8(void)
{
    static end_t c, a;
    v90m_negotiation cn, an;

    pair(&c, &a, V90M_MOD_V22 | V90M_MOD_V21, V90M_MOD_V22 | V90M_MOD_V21);
    printf("  V.8, V.22 and V.21 at both ends:\n"); show("answerer", &a); show("caller", &c);
    v90m_get_negotiation(a.m, &an); v90m_get_negotiation(c.m, &cn);
    CHECK_EQ_I(an.far_menu, 1); CHECK_EQ_I(cn.far_menu, 1);
    CHECK_EQ_I(an.cm_modes, V90M_V8_V22 | V90M_V8_V21); CHECK_EQ_I(an.jm_modes, V90M_V8_V22 | V90M_V8_V21);
    CHECK_EQ_I(cn.cm_modes, an.cm_modes); CHECK_EQ_I(cn.jm_modes, an.jm_modes);
    CHECK_EQ_I(an.cm_pcm, 0); CHECK_EQ_I(an.jm_pcm, 0);
    CHECK(find(&a, V90M_CONN_V22BIS, V90M_BASIS_V8) != NULL);
    CHECK(find(&c, V90M_CONN_V22BIS, V90M_BASIS_V8) != NULL);
    CHECK_EQ_I(an.basis, V90M_BASIS_V8);
    end_free(&c); end_free(&a);

    /* PCM: the caller says it is an analogue modem, we a digital one */
    pair(&c, &a, V90M_MOD_V90 | V90M_MOD_V34, V90M_MOD_V90 | V90M_MOD_V34);
    printf("  V.8, V.90:\n"); show("answerer", &a);
    v90m_get_negotiation(a.m, &an); v90m_get_negotiation(c.m, &cn);
    CHECK_EQ_I(an.cm_pcm, V90M_V8_PCM_ANALOG); CHECK_EQ_I(an.jm_pcm, V90M_V8_PCM_DIGITAL);
    CHECK(an.cm_modes & V90M_V8_V34);
    CHECK_EQ_I(cn.cm_pcm, V90M_V8_PCM_ANALOG); CHECK_EQ_I(cn.jm_pcm, V90M_V8_PCM_DIGITAL);
    CHECK(find(&a, V90M_CONN_V90, V90M_BASIS_V8) != NULL);
    end_free(&c); end_free(&a);

    /* nothing in common: what the caller offered is still told */
    pair(&c, &a, V90M_MOD_V21, V90M_MOD_V22);
    printf("  V.8, nothing in common:\n"); show("answerer", &a); show("caller", &c);
    v90m_get_negotiation(a.m, &an);
    CHECK_EQ_I(an.far_menu, 1);
    CHECK_EQ_I(an.cm_modes, V90M_V8_V21); CHECK_EQ_I(an.jm_modes, 0);
    CHECK_EQ_I(an.trying, V90M_CONN_NONE); CHECK_EQ_I(an.basis, V90M_BASIS_NONE);
    CHECK(a.n_steps >= 1); CHECK_EQ_I(a.after_end, 0);
    end_free(&c); end_free(&a);
}

/* without the option no host hears of it, and the report is there all the same */
static void test_option_off(void)
{
    static end_t e;
    v90m_cfg c; v90m_negotiation n; int err;
    int16_t rx = 0, tx;
    long k;
    memset(&e, 0, sizeof e);
    e.a = v90m_async_create(&err);
    v90m_cfg_default(&c);
    c.modulations = V90M_MOD_V22 | V90M_MOD_V21;
    c.getbit = end_get; c.putbit = end_put; c.user = &e; c.on_event = end_event;
    e.m = v90m_create(&c, &err);
    CHECK(e.m != NULL);
    if (!e.m) return;
    for (k = 0; k < 8000L * 20; k++) v90m_process_linear(e.m, &rx, &tx, 1);
    v90m_get_negotiation(e.m, &n);
    CHECK_EQ_I(e.n_steps, 0);
    CHECK_EQ_I(n.trying, V90M_CONN_V21); CHECK_EQ_I(n.basis, V90M_BASIS_TRYING);
    v90m_get_negotiation(e.m, NULL);
    memset(&n, 0x55, sizeof n);
    v90m_get_negotiation(NULL, &n);
    CHECK_EQ_I(n.far_menu, 0x55555555);
    end_free(&e);
}

/* a V.92 caller that sends QC1a. Taken: V.92 is chosen in V.8's place with
 * no CM or JM, so no menu is reported and the basis is Quick Connect. Not
 * completed (no TONEq): V.8 goes on, and its menus and basis are reported as
 * for any caller */
static void quick_connect(int no_toneq)
{
    static end_t e;
    static v92call_t p;
    v92call_cfg cc;
    v90m_negotiation n;
    v90m_diag d;
    v90m_qc q;
    uint8_t rx, tx = 0xFF;
    end_init(&e, V90M_ROLE_ANSWER, V90M_MOD_V34 | V90M_MOD_V90 | V90M_MOD_V92);
    if (!e.m) return;
    memset(&cc, 0, sizeof cc);
    cc.v92 = 1; cc.pcm_up = 1; cc.qc = 1; cc.qc_wxyz = 5; cc.short_p2 = 1; cc.qc_no_toneq = no_toneq;
    cc.echo_gain = 0.1; cc.echo_delay = 21;
    cc.getbit = end_get; cc.putbit = end_put; cc.user = &e;
    v92call_init(&p, &cc);
    for (e.k = 0; e.k < 8000L * 30; e.k++) {
        rx = v92call_tick(&p, tx);
        v90m_process_g711(e.m, &rx, &tx, 1);
        v90m_get_diag(e.m, &d);
        if (d.data_reached || e.n_ended) break;
    }
    printf("  V.92 Quick Connect, %s:\n", no_toneq ? "not completed" : "taken"); show("answerer", &e);
    v90m_get_negotiation(e.m, &n); v90m_get_qc(e.m, &q);
    CHECK(d.data_reached);
    CHECK_EQ_I(q.accepted, 1); CHECK_EQ_I(q.fallback, no_toneq);
    CHECK_EQ_I(n.trying, V90M_CONN_V92); CHECK_EQ_I(n.basis, no_toneq ? V90M_BASIS_V8 : V90M_BASIS_QC);
    CHECK(find(&e, V90M_CONN_V92, no_toneq ? V90M_BASIS_V8 : V90M_BASIS_QC) != NULL);
    if (!no_toneq) CHECK(find(&e, V90M_CONN_V92, V90M_BASIS_V8) == NULL);
    CHECK_EQ_I(n.far_menu, no_toneq);
    if (no_toneq) { CHECK(n.cm_modes & V90M_V8_V34); CHECK(n.jm_modes & V90M_V8_V34); }
    else { CHECK_EQ_I(n.cm_modes, 0); CHECK_EQ_I(n.jm_modes, 0); }
    end_free(&e);
}

/* the option changes nothing on the line: the same pair with it and without,
 * every sample sent both ways hashed */
static uint32_t pair_hash(unsigned c_mods, unsigned a_mods, unsigned opt, int *events)
{
    static end_t c, a;
    int16_t c_rx = 0, a_rx = 0;
    uint32_t h = 2166136261u;
    long k;
    end_init_opt(&c, V90M_ROLE_ORIGINATE, c_mods, opt);
    end_init_opt(&a, V90M_ROLE_ANSWER, a_mods, opt);
    if (!c.m || !a.m) return 0;
    for (k = 0; k < 8000L * 15; k++) {
        int16_t c_tx, a_tx;
        c.k = a.k = k;
        v90m_process_linear(c.m, &c_rx, &c_tx, 1);
        v90m_process_linear(a.m, &a_rx, &a_tx, 1);
        a_rx = c_tx; c_rx = a_tx;
        h = (h ^ (uint16_t)c_tx) * 16777619u;
        h = (h ^ (uint16_t)a_tx) * 16777619u;
    }
    *events = c.n_steps + a.n_steps;
    end_free(&c); end_free(&a);
    return h;
}

static void test_same_signal(void)
{
    static const unsigned mods[3][2] = {
        { V90M_MOD_V90 | V90M_MOD_V34, V90M_MOD_V90 | V90M_MOD_V92 | V90M_MOD_V34 },
        { V90M_MOD_V22 | V90M_MOD_V21, V90M_MOD_V22 | V90M_MOD_V21 | V90M_MOD_BELL212A },
        { V90M_MOD_V32BIS, V90M_MOD_V32BIS | V90M_MOD_V22 }
    };
    int i, off, on;
    for (i = 0; i < 3; i++) {
        uint32_t without = pair_hash(mods[i][0], mods[i][1], 0, &off);
        uint32_t with = pair_hash(mods[i][0], mods[i][1], V90M_OPT_NEG_EVENTS, &on);
        CHECK(without == with);
        CHECK_EQ_I(off, 0); CHECK(on > 0);
    }
}

/* nothing more once data mode was reached, through a retrain too */
static void test_quiet_after_data(void)
{
    static end_t c, a;
    int16_t c_rx = 0, a_rx = 0;
    long k, data_at = -1;
    int at_data = -1;
    v90m_stats sx;
    end_init(&c, V90M_ROLE_ORIGINATE, V90M_MOD_V34);
    end_init(&a, V90M_ROLE_ANSWER, V90M_MOD_V34);
    if (!c.m || !a.m) return;
    for (k = 0; k < 8000L * 40; k++) {
        int16_t c_tx, a_tx;
        v90m_diag d;
        c.k = a.k = k;
        v90m_process_linear(c.m, &c_rx, &c_tx, 1);
        v90m_process_linear(a.m, &a_rx, &a_tx, 1);
        a_rx = c_tx; c_rx = a_tx;
        v90m_get_diag(a.m, &d);
        if (d.data_reached && data_at < 0) { data_at = k; at_data = a.n_steps; }
        if (data_at >= 0 && k == data_at + 4000) CHECK_EQ_I(v90m_retrain(a.m), V90M_OK);
        if (data_at >= 0 && k == data_at + 8000L * 15) break;
    }
    CHECK(data_at >= 0);
    v90m_get_stats(a.m, &sx);
    CHECK(sx.retrains >= 1);
    CHECK_EQ_I(a.n_steps, at_data);
    end_free(&c); end_free(&a);
}

/* calling, an answerer without V.8 (a plain 2100 Hz answer tone): training,
 * and nothing to report until its signal says which */
static void test_calling_no_v8(void)
{
    static end_t e;
    v90m_negotiation n;
    v90m_diag d;
    int16_t rx, tx;
    int i;
    end_init(&e, V90M_ROLE_ORIGINATE, V90M_MOD_V22 | V90M_MOD_V21);
    if (!e.m) return;
    for (e.k = 0; e.k < 8000L * 5; e.k++) {
        rx = e.k < 8000L * 3 ? (int16_t)lrint(3000.0 * sin(6.283185307179586 * 2100.0 * (double)e.k / 8000.0)) : 0;
        v90m_process_linear(e.m, &rx, &tx, 1);
    }
    v90m_get_negotiation(e.m, &n); v90m_get_diag(e.m, &d);
    CHECK_EQ_I(d.phase, V90M_PHASE_TRAIN);
    CHECK_EQ_I(n.far_menu, 0); CHECK_EQ_I(n.trying, V90M_CONN_NONE); CHECK_EQ_I(n.basis, V90M_BASIS_NONE);
    for (i = 0; i < e.n_steps && i < MAXSTEP; i++) CHECK_EQ_I(e.step[i].n.basis, V90M_BASIS_NONE);
    end_free(&e);
}

/* a V.32bis caller without V.8: its AA during our answer tone is heard */
static void test_v32_caller(void)
{
    static end_t e;
    static v32call_t p;
    vc_cfg cc;
    v90m_diag d;
    int16_t line = 0, rx, tx;
    end_init(&e, V90M_ROLE_ANSWER, V90M_MOD_V32BIS | V90M_MOD_V22);
    if (!e.m) return;
    memset(&cc, 0, sizeof cc);
    cc.level = 3000; cc.rates = 0x1Fu; cc.trn_len = 1280;
    cc.getbit = end_get; cc.putbit = end_put; cc.user = &e;
    v32call_init(&p, &cc);
    for (e.k = 0; e.k < 8000L * 30; e.k++) {
        long q = e.k < 8000L * 2 ? 0 : lrint(v32call_tick(&p, line));
        rx = (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q);
        v90m_process_linear(e.m, &rx, &tx, 1);
        line = tx;
        v90m_get_diag(e.m, &d);
        if (d.data_reached || e.n_ended) break;
    }
    CHECK(d.data_reached);
    CHECK(find(&e, V90M_CONN_V32BIS, V90M_BASIS_HEARD) != NULL);
    CHECK(find(&e, V90M_CONN_V32BIS, V90M_BASIS_V8) == NULL);
    end_free(&e);
}

/* HST offered with an endpoint linked, a silent caller: our wait for its back
 * channel is a fallback we try */
static void test_hst_wait(void)
{
    static end_t e;
    v90m_ec_cfg ec_cfg;
    v90m_ec_t *ec;
    int16_t rx = 0, tx;
    int err;
    end_init(&e, V90M_ROLE_ANSWER, V90M_MOD_HST | V90M_MOD_V22);
    if (!e.m) return;
    v90m_ec_cfg_default(&ec_cfg, 0);
    ec_cfg.mnp.enable = 1;
    ec = v90m_ec_create(&ec_cfg, &err);
    CHECK(ec != NULL);
    if (!ec) return;
    CHECK_EQ_I(v90m_ec_link_hst(ec, e.m), V90M_OK);
    for (e.k = 0; e.k < 8000L * 30; e.k++) {
        v90m_process_linear(e.m, &rx, &tx, 1);
        if (e.n_ended || find(&e, V90M_CONN_HST, V90M_BASIS_TRYING)) break;
    }
    CHECK(find(&e, V90M_CONN_HST, V90M_BASIS_TRYING) != NULL);
    end_free(&e);
    v90m_ec_destroy(ec);
}

/* x2 and K56flex: the client's own signals choose them, so "heard", not V.8 */
static void test_x2_k56(int k56)
{
    static end_t e;
    static x2call_t x;
    static k56call_t k;
    static uint8_t down[64];
    v90m_conn want = k56 ? V90M_CONN_K56FLEX : V90M_CONN_X2;
    v90m_negotiation n;
    v90m_diag d;
    uint8_t rx = 0xFF, tx;
    end_init(&e, V90M_ROLE_ANSWER, V90M_MOD_V34 | V90M_MOD_V90
             | (k56 ? V90M_MOD_K56 | V90M_OPT_K56_V2_ONLY : V90M_MOD_X2));
    if (!e.m) return;
    if (k56) {
        k56call_cfg kc;
        k56call_default(&kc, 3000);
        kc.rate = 1; kc.getbit = end_get; kc.putbit = end_put; kc.user = &e;
        k56call_init(&k, &kc);
    } else {
        x2call_cfg xc;
        memset(&xc, 0, sizeof xc);
        xc.level = 3000; xc.max_index = 1; xc.up_mask = 0x03FE;
        xc.getbit = end_get; xc.putbit = end_put; xc.user = &e;
        x2call_init(&x, &xc);
    }
    memset(down, k56 ? 0xFF : 0x7F, sizeof down);
    for (e.k = 0; e.k < 8000L * 40; e.k++) {
        int16_t s;
        v90m_process_g711(e.m, &rx, &tx, 1);
        down[e.k & 63] = tx;
        s = k56 ? k56call_tick(&k, down[(e.k - 30) & 63]) : x2call_tick(&x, down[(e.k - 30) & 63]);
        rx = v90m_g711_encode(0, (int16_t)(s / 2));
        v90m_get_diag(e.m, &d);
        if (d.data_reached || e.n_ended) break;
    }
    v90m_get_negotiation(e.m, &n);
    CHECK(d.data_reached);
    CHECK_EQ_I(n.trying, want); CHECK_EQ_I(n.basis, V90M_BASIS_HEARD);
    CHECK(find(&e, want, V90M_BASIS_HEARD) != NULL);
    CHECK(find(&e, want, V90M_BASIS_V8) == NULL);
    end_free(&e);
}

int main(void)
{
    test_silent_caller();
    test_legacy_caller();
    test_v8();
    test_option_off();
    quick_connect(0);
    quick_connect(1);
    test_same_signal();
    test_quiet_after_data();
    test_calling_no_v8();
    test_v32_caller();
    test_hst_wait();
    test_x2_k56(0);
    test_x2_k56(1);
    T_REPORT("test_neg");
}
