/* The negotiation report (v90m_get_negotiation, V90M_EV_NEGOTIATION), from
 * the outside: what each side heard of V.8, which modulation it is on while
 * the start-up runs, and why it is on it. The modems come from libv90modem.a
 * as it is installed; an API caller, a simulated caller without V.8 or a
 * V.92 caller with Quick Connect from the dev archive, or silence. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "v90modem.h"
#include "test.h"
#include "callpeer.h"
#include "v92call.h"

#define MAXSTEP 24

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

static void end_init(end_t *e, v90m_role role, unsigned mods)
{
    v90m_cfg c; int err;
    memset(e, 0, sizeof *e);
    e->a = v90m_async_create(&err);
    v90m_cfg_default(&c);
    c.role = role; c.modulations = mods | V90M_OPT_NEG_EVENTS;
    c.getbit = end_get; c.putbit = end_put; c.user = e; c.on_event = end_event;
    e->cfg = c;
    e->m = v90m_create(&c, &err);
    CHECK(e->m != NULL);
}
static void end_free(end_t *e) { v90m_destroy(e->m); v90m_async_destroy(e->a); }

static const char *basis_name(v90m_basis b)
{
    return b == V90M_BASIS_NONE ? "none" : b == V90M_BASIS_TRYING ? "trying" : b == V90M_BASIS_HEARD ? "heard"
           : b == V90M_BASIS_V8 ? "V.8" : "?";
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
 * no CM or JM, so no menu is reported. Not completed (no TONEq): V.8 goes
 * on, and its menus are reported as for any caller */
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
    CHECK_EQ_I(n.trying, V90M_CONN_V92); CHECK_EQ_I(n.basis, V90M_BASIS_V8);
    CHECK(find(&e, V90M_CONN_V92, V90M_BASIS_V8) != NULL);
    CHECK_EQ_I(n.far_menu, no_toneq);
    if (no_toneq) { CHECK(n.cm_modes & V90M_V8_V34); CHECK(n.jm_modes & V90M_V8_V34); }
    else { CHECK_EQ_I(n.cm_modes, 0); CHECK_EQ_I(n.jm_modes, 0); }
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
    T_REPORT("test_neg");
}
