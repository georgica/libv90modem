/* v90m_get_restarts (0.7.1): who began a call's rate renegotiations and
 * retrains, and why, on both ends of a call between this library's own
 * calling and answering modems:
 *  - a renegotiation our caller asks for through the engine
 *    (orig_renegotiate; the public API has no call for it): the caller's,
 *    ours there, the far end's at the answerer (V.34, V.90, V.92);
 *  - a renegotiation of the answerer's own receiver (V.34's rate_down, the
 *    line noisier into it): ours for its errors there, the far end's at the
 *    caller;
 *  - v90m_retrain on either end: the host's there, the far end's at the
 *    other (V.34, V.90, V.32bis).
 * Each time the split adds up to v90m_stats' counts, and the last one's
 * time is where the data stopped. HST's 0x2R / 0x3R are test_answer_hst's
 * (down, retrain_by_2R, retrain_back, up); the V.90 caller's watch on its
 * receiver (down, up again) test_orig_retrain's (noise +15 dB). Linked with the development
 * archive alone, for the engine's handles (api/v90modem_priv.h). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "v90modem.h"
#include "test.h"
#include "../../api/v90modem_priv.h"

typedef struct { v90m_t *m; v90m_async_t *a; long stop_ms, rt_ms; } end_t;
static int end_get(void *u) { return v90m_async_getbit(((end_t *)u)->a); }
static void end_put(void *u, int b) { v90m_async_putbit(((end_t *)u)->a, b); }

static void end_init(end_t *e, v90m_role role, unsigned mods)
{
    v90m_cfg c; int err;
    memset(e, 0, sizeof *e);
    e->a = v90m_async_create(&err);
    v90m_cfg_default(&c);
    c.role = role; c.modulations = mods;
    c.getbit = end_get; c.putbit = end_put; c.user = e;
    e->m = v90m_create(&c, &err);
    e->stop_ms = e->rt_ms = -1;
    CHECK(e->m != NULL);
}

static void done(end_t *cl, end_t *an)
{
    v90m_destroy(cl->m); v90m_destroy(an->m); v90m_async_destroy(cl->a); v90m_async_destroy(an->a);
}

/* the split against the counts, and a line for the log */
static void show(const char *who, const end_t *e, v90m_restarts *r)
{
    v90m_stats s;
    v90m_get_restarts(e->m, r);
    v90m_get_stats(e->m, &s);
    printf("    %-9s renegotiations ours %u far %u, retrains ours %u (host %u) far %u; last %s %s\n", who,
           r->reneg_ours, r->reneg_far, r->retrain_ours, r->retrain_host, r->retrain_far,
           r->last == V90M_CAUSE_NONE ? "-" : r->last_retrain ? "retrain" : "renegotiation", v90m_cause_name(r->last));
    CHECK_EQ_I(r->reneg_ours + r->reneg_far, s.renegotiations);
    CHECK_EQ_I(r->retrain_ours + r->retrain_far, s.retrains);
    CHECK(r->retrain_host <= r->retrain_ours);
}

/* noise into the answerer, 0 none: about this RMS, a sum of 12 uniforms */
static uint32_t lcg = 12345u;
static int16_t noise(int rms)
{
    int32_t s = 0;
    int i;
    if (!rms) return 0;
    for (i = 0; i < 12; i++) { lcg = lcg * 1664525u + 1013904223u; s += (int32_t)(lcg >> 16) - 32768; }
    return (int16_t)((int64_t)s * rms / 65536);     /* the sum's RMS is 2 x 32768 */
}

enum { ASK_RENEG, NOISE, RETRAIN_CALLER, RETRAIN_ANSWERER };

/* the call to data mode in conn, one event 1 s later, then until both ends
 * have moved data again for 2 s (at most 40 s); the stops seen */
static void call(const char *name, unsigned mods, int conn, int what, int nrms, v90m_restarts *rc, v90m_restarts *ra)
{
    static end_t cl, an;
    int16_t c_rx = 0, a_rx = 0;
    long k, k_ev = -1, k_end = -1;
    int fired = 0, rms = 0;
    v90m_state s1, s2;
    end_init(&cl, V90M_ROLE_ORIGINATE, mods);
    end_init(&an, V90M_ROLE_ANSWER, mods);
    if (cl.m && an.m) {
        for (k = 0; k < 8000L * 60; k++) {
            int16_t c_tx, a_tx;
            /* as the last sample left them (a stop the event's own call
             * made shows in the next sample) */
            int was1 = k ? s1.moving : 0, was2 = k ? s2.moving : 0;
            v90m_process_linear(cl.m, &c_rx, &c_tx, 1);
            v90m_process_linear(an.m, &a_rx, &a_tx, 1);
            a_rx = (int16_t)(c_tx + noise(rms));
            c_rx = a_tx;
            v90m_get_state(cl.m, &s1); v90m_get_state(an.m, &s2);
            /* the clock of v90m_get_restarts: the sample just processed is k + 1 */
            if (fired && was1 && !s1.moving && cl.stop_ms < 0) cl.stop_ms = (k + 1) / 8;
            if (fired && was2 && !s2.moving && an.stop_ms < 0) an.stop_ms = (k + 1) / 8;
            if (k_ev < 0 && s1.moving && s2.moving && s1.modulation == (v90m_conn)conn) k_ev = k + 8000;
            if (k == k_ev) {
                fired = 1;
                switch (what) {
                case ASK_RENEG: CHECK_EQ_I(orig_renegotiate(v90m__orig(cl.m), 0), 0); break;
                case NOISE: rms = nrms; break;
                case RETRAIN_CALLER: CHECK_EQ_I(v90m_retrain(cl.m), V90M_OK); cl.rt_ms = (k + 1) / 8; break;
                default: CHECK_EQ_I(v90m_retrain(an.m), V90M_OK); an.rt_ms = (k + 1) / 8; break;
                }
            }
            if (fired && cl.stop_ms >= 0 && an.stop_ms >= 0 && s1.moving && s2.moving && k_end < 0) k_end = k + 16000;
            if (k == k_end) break;
            if (s1.end != V90M_END_NONE || s2.end != V90M_END_NONE) break;
        }
        printf("  %s: %s at %ld ms; data stopped at %ld ms (caller), %ld ms (answerer); %s\n", name,
               what == ASK_RENEG ? "the caller's renegotiation asked" : what == NOISE ? "noise into the answerer"
               : what == RETRAIN_CALLER ? "v90m_retrain on the caller" : "v90m_retrain on the answerer",
               k_ev / 8, cl.stop_ms, an.stop_ms, k == k_end ? "data again both ways" : "not back in data");
        CHECK_EQ_I(k, k_end);
        show("caller", &cl, rc);
        show("answerer", &an, ra);
        /* the last began where that end's data stopped (a retrain of ours:
         * where v90m_retrain was called, or the sample after) */
        if (rc->last != V90M_CAUSE_NONE) CHECK(rc->last_ms + 1 >= (unsigned)(cl.rt_ms >= 0 ? cl.rt_ms : cl.stop_ms)
                                               && rc->last_ms <= (unsigned)(cl.rt_ms >= 0 ? cl.rt_ms : cl.stop_ms) + 1);
        if (ra->last != V90M_CAUSE_NONE && what != NOISE)
            CHECK(ra->last_ms + 1 >= (unsigned)(an.rt_ms >= 0 ? an.rt_ms : an.stop_ms)
                  && ra->last_ms <= (unsigned)(an.rt_ms >= 0 ? an.rt_ms : an.stop_ms) + 1);
    }
    done(&cl, &an);
}

int main(void)
{
    v90m_restarts c, a;
    const unsigned v34 = V90M_MOD_V34 | V90M_MOD_V22, v90 = V90M_MOD_V90 | V90M_MOD_V34,
                   v92 = V90M_MOD_V92 | V90M_MOD_V90 | V90M_MOD_V34;

    printf("restarts: who began the renegotiations and retrains, and why\n");
    /* the caller's renegotiation: ours there (V.34's engine says only the
     * receiver's reason, the one it has in use; V.90's and V.92's: asked
     * for), the far end's at the answerer */
    call("V.34", v34, V90M_CONN_V34, ASK_RENEG, 0, &c, &a);
    CHECK_EQ_I(c.reneg_ours, 1); CHECK_EQ_I(c.reneg_far, 0); CHECK_EQ_I(c.last, V90M_CAUSE_RX_ERRORS); CHECK_EQ_I(c.last_retrain, 0);
    CHECK_EQ_I(a.reneg_ours, 0); CHECK_EQ_I(a.reneg_far, 1); CHECK_EQ_I(a.last, V90M_CAUSE_FAR); CHECK_EQ_I(a.last_retrain, 0);
    call("V.90", v90, V90M_CONN_V90, ASK_RENEG, 0, &c, &a);
    CHECK_EQ_I(c.reneg_ours, 1); CHECK_EQ_I(c.reneg_far, 0); CHECK_EQ_I(c.last, V90M_CAUSE_OURS);
    CHECK_EQ_I(a.reneg_ours, 0); CHECK_EQ_I(a.reneg_far, 1); CHECK_EQ_I(a.last, V90M_CAUSE_FAR);
    call("V.92", v92, V90M_CONN_V92, ASK_RENEG, 0, &c, &a);
    CHECK_EQ_I(c.reneg_ours, 1); CHECK_EQ_I(c.reneg_far, 0); CHECK_EQ_I(c.last, V90M_CAUSE_OURS);
    CHECK_EQ_I(a.reneg_ours, 0); CHECK_EQ_I(a.reneg_far, 1); CHECK_EQ_I(a.last, V90M_CAUSE_FAR);
    CHECK_EQ_I(c.retrain_ours + c.retrain_far + a.retrain_ours + a.retrain_far, 0);

    /* the line noisier into the answerer: its rate_down, ours for its
     * receiver's errors; the far end's at the caller */
    call("V.34", v34, V90M_CONN_V34, NOISE, 140, &c, &a);
    CHECK(a.reneg_ours >= 1); CHECK_EQ_I(a.reneg_far, 0); CHECK_EQ_I(a.last, V90M_CAUSE_RX_ERRORS);
    CHECK_EQ_I(c.reneg_ours, 0); CHECK_EQ_I(c.reneg_far, a.reneg_ours); CHECK_EQ_I(c.last, V90M_CAUSE_FAR);
    CHECK_EQ_I(c.retrain_ours + c.retrain_far + a.retrain_ours + a.retrain_far, 0);

    /* v90m_retrain: the host's, the far end's on the other end */
    call("V.34", v34, V90M_CONN_V34, RETRAIN_ANSWERER, 0, &c, &a);
    CHECK_EQ_I(a.retrain_ours, 1); CHECK_EQ_I(a.retrain_host, 1); CHECK_EQ_I(a.retrain_far, 0);
    CHECK_EQ_I(a.last, V90M_CAUSE_HOST); CHECK_EQ_I(a.last_retrain, 1);
    CHECK_EQ_I(c.retrain_ours, 0); CHECK_EQ_I(c.retrain_far, 1); CHECK_EQ_I(c.last, V90M_CAUSE_FAR); CHECK_EQ_I(c.last_retrain, 1);
    call("V.34", v34, V90M_CONN_V34, RETRAIN_CALLER, 0, &c, &a);
    CHECK_EQ_I(c.retrain_ours, 1); CHECK_EQ_I(c.retrain_host, 1); CHECK_EQ_I(c.retrain_far, 0); CHECK_EQ_I(c.last, V90M_CAUSE_HOST);
    CHECK_EQ_I(a.retrain_ours, 0); CHECK_EQ_I(a.retrain_far, 1); CHECK_EQ_I(a.last, V90M_CAUSE_FAR);
    call("V.90", v90, V90M_CONN_V90, RETRAIN_CALLER, 0, &c, &a);
    CHECK_EQ_I(c.retrain_ours, 1); CHECK_EQ_I(c.retrain_host, 1); CHECK_EQ_I(c.retrain_far, 0); CHECK_EQ_I(c.last, V90M_CAUSE_HOST);
    CHECK_EQ_I(a.retrain_ours, 0); CHECK_EQ_I(a.retrain_far, 1); CHECK_EQ_I(a.last, V90M_CAUSE_FAR);
    call("V.90", v90, V90M_CONN_V90, RETRAIN_ANSWERER, 0, &c, &a);
    CHECK_EQ_I(a.retrain_ours, 1); CHECK_EQ_I(a.retrain_host, 1); CHECK_EQ_I(a.retrain_far, 0); CHECK_EQ_I(a.last, V90M_CAUSE_HOST);
    CHECK_EQ_I(c.retrain_ours, 0); CHECK_EQ_I(c.retrain_far, 1); CHECK_EQ_I(c.last, V90M_CAUSE_FAR);
    call("V.32bis", V90M_MOD_V32BIS, V90M_CONN_V32BIS, RETRAIN_CALLER, 0, &c, &a);
    CHECK_EQ_I(c.retrain_ours, 1); CHECK_EQ_I(c.retrain_host, 1); CHECK_EQ_I(c.retrain_far, 0); CHECK_EQ_I(c.last, V90M_CAUSE_HOST);
    CHECK_EQ_I(a.retrain_ours, 0); CHECK_EQ_I(a.retrain_far, 1); CHECK_EQ_I(a.last, V90M_CAUSE_FAR);

    /* the names, and none before a call */
    CHECK(!strcmp(v90m_cause_name(V90M_CAUSE_RX_ERRORS), "rx_errors")); CHECK(!strcmp(v90m_cause_name((v90m_cause)99), "?"));
    {
        end_t e;
        v90m_restarts r;
        memset(&r, 0x55, sizeof r);
        end_init(&e, V90M_ROLE_ANSWER, v34);
        v90m_get_restarts(e.m, &r);
        CHECK_EQ_I(r.reneg_ours + r.reneg_far + r.retrain_ours + r.retrain_far + r.retrain_host, 0);
        CHECK_EQ_I(r.last, V90M_CAUSE_NONE); CHECK_EQ_I(r.last_ms, 0);
        memset(&r, 0x55, sizeof r);
        v90m_get_restarts(NULL, &r);                    /* NULL: nothing */
        CHECK_EQ_I(r.reneg_ours, 0x55555555u); v90m_get_restarts(e.m, NULL);
        v90m_destroy(e.m); v90m_async_destroy(e.a);
    }
    T_REPORT("restarts");
}
