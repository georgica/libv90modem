/* The start-up diagnostics (v90m_get_diag, v90m_err_name), from the outside:
 * the modems come from libv90modem.a as it is installed; an API caller and
 * an API answerer back to back, the line between them cut where a start-up
 * is to fail. After the end, each side's error, phase and modulation
 * chosen; and a name for every error code. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "v90modem.h"
#include "test.h"
/* the families of error codes and their counts (never installed) */
#include "../../api/v90modem_diag.h"

static const char *phase_name(v90m_phase p)
{
    static const char *const n[] = { "none", "tone", "V.8", "phase 2", "phase 3", "phase 4", "train", "data" };
    return (unsigned)p < sizeof n / sizeof n[0] ? n[p] : "?";
}
static const char *conn_name(v90m_conn c)
{
    static const char *const n[] = { "none", "V.21", "V.22", "V.22bis", "V.90", "V.34", "V.32bis", "V.92",
                                      "Bell 103", "Bell 212A", "x2", "K56flex" };
    return (unsigned)c < sizeof n / sizeof n[0] ? n[c] : "?";
}
static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

/* ---- every code has a name, and no two the same ---- */
static void test_names(void)
{
    static const char *seen[DG_NFAM * 100];
    int f, k, j, total = 0;
    CHECK(strcmp(v90m_err_name(0), "none") == 0);
    CHECK(strcmp(v90m_err_name(-1), "?") == 0);
    CHECK(strcmp(v90m_err_name(99), "?") == 0);
    CHECK(strcmp(v90m_err_name(V90M_DIAG_CODE(DG_NFAM, 1)), "?") == 0);
    CHECK(strcmp(v90m_err_name(V90M_DIAG_CODE(DG_P2D, P2D_ERR_INFO1A)), "P2D_ERR_INFO1A") == 0);
    CHECK(strcmp(v90m_err_name(V90M_DIAG_CODE(DG_V90A, V90A_ERR_RETRAINS)), "V90A_ERR_RETRAINS") == 0);
    for (f = 1; f < DG_NFAM; f++) {
        int n = dg_count(f);
        const char *first = v90m_err_name(V90M_DIAG_CODE(f, 1));
        size_t pre = strstr(first, "_ERR_") ? (size_t)(strstr(first, "_ERR_") - first) + 5 : 0;
        CHECK(n > 0 && n < 100);
        CHECK(pre > 0);
        CHECK(strcmp(v90m_err_name(V90M_DIAG_CODE(f, 0)), "?") == 0);
        CHECK(strcmp(v90m_err_name(V90M_DIAG_CODE(f, n + 1)), "?") == 0);
        for (k = 1; k <= n; k++) {
            const char *s = v90m_err_name(V90M_DIAG_CODE(f, k));
            CHECK(s != NULL);
            if (!s) continue;
            CHECK(strcmp(s, "?") != 0 && strcmp(s, "none") != 0);
            CHECK(strncmp(s, first, pre) == 0);         /* one prefix per family */
            for (j = 0; j < total; j++) CHECK(strcmp(seen[j], s) != 0);
            seen[total++] = s;
        }
    }
    printf("  names: %d codes in %d families, each named once\n", total, DG_NFAM - 1);
}

/* ---- an API caller and an API answerer back to back ---- */
typedef struct { v90m_t *m; v90m_async_t *a; } end_t;
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
    CHECK(e->m != NULL);
}
static void end_free(end_t *e) { v90m_destroy(e->m); v90m_async_destroy(e->a); }

typedef struct { v90m_diag d; v90m_state s; } side_t;

#define CUT_NEVER V90M_PHASE_NONE
/* the call up to both ends (or secs): from the first sample the answerer
 * is in phase cut (CUT_NEVER: never), it hears silence. data: once data
 * moves both ways at both ends, the diagnostics are checked against
 * want_sel (DATA_CHECK), and the answerer clears down (DATA_CLEAR). */
enum { DATA_NONE, DATA_CHECK, DATA_CLEAR };
static void pair(unsigned c_mods, unsigned a_mods, v90m_phase cut, int data, v90m_conn want_sel, int secs,
                 side_t *c, side_t *a)
{
    static end_t cl, an;
    int16_t c_rx = 0, a_rx = 0;
    long k, cut_at = -1;
    int seen = 0;
    end_init(&cl, V90M_ROLE_ORIGINATE, c_mods);
    end_init(&an, V90M_ROLE_ANSWER, a_mods);
    memset(c, 0, sizeof *c); memset(a, 0, sizeof *a);
    if (cl.m && an.m) {
        for (k = 0; k < 8000L * secs; k++) {
            int16_t c_tx, a_tx, in = cut_at >= 0 ? 0 : a_rx;
            v90m_process_linear(cl.m, &c_rx, &c_tx, 1);
            v90m_process_linear(an.m, &in, &a_tx, 1);
            a_rx = c_tx; c_rx = a_tx;
            v90m_get_diag(an.m, &a->d);
            if (cut_at < 0 && cut != CUT_NEVER && a->d.phase == cut) cut_at = k;
            if (data != DATA_NONE && !seen) {
                v90m_get_state(an.m, &a->s); v90m_get_state(cl.m, &c->s);
                if (a->s.moving && c->s.moving) {
                    v90m_get_diag(cl.m, &c->d);
                    printf("    in data mode: answerer %s %s, error %s; caller %s %s, error %s\n",
                           phase_name(a->d.phase), conn_name(a->d.selected), v90m_err_name(a->d.error),
                           phase_name(c->d.phase), conn_name(c->d.selected), v90m_err_name(c->d.error));
                    CHECK_EQ_I(a->d.phase, V90M_PHASE_DATA); CHECK_EQ_I(a->d.selected, want_sel); CHECK_EQ_I(a->d.error, 0);
                    CHECK_EQ_I(c->d.phase, V90M_PHASE_DATA); CHECK_EQ_I(c->d.selected, want_sel); CHECK_EQ_I(c->d.error, 0);
                    if (data == DATA_CLEAR) CHECK_EQ_I(v90m_cleardown(an.m), V90M_OK);
                    seen = 1;
                }
            }
            if (k % 80 == 0) {
                v90m_get_state(an.m, &a->s); v90m_get_state(cl.m, &c->s);
                if (a->s.end != V90M_END_NONE && c->s.end != V90M_END_NONE) break;
            }
        }
        v90m_get_diag(an.m, &a->d); v90m_get_state(an.m, &a->s);
        v90m_get_diag(cl.m, &c->d); v90m_get_state(cl.m, &c->s);
    }
    end_free(&cl); end_free(&an);
}

static void show(const char *what, const side_t *c, const side_t *a)
{
    printf("  %s:\n    answerer end %d: %s, %s, %s\n    caller end %d: %s, %s, %s\n", what,
           (int)a->s.end, v90m_err_name(a->d.error), phase_name(a->d.phase), conn_name(a->d.selected),
           (int)c->s.end, v90m_err_name(c->d.error), phase_name(c->d.phase), conn_name(c->d.selected));
}

#define V90_MODS (V90M_MOD_V90 | V90M_MOD_V34)

static void test_failures(void)
{
    side_t c, a;

    /* V.90: the caller goes silent once the answerer's Phase 2 begins */
    pair(V90_MODS, V90_MODS, V90M_PHASE_PHASE2, DATA_NONE, V90M_CONN_NONE, 60, &c, &a);
    show("V.90, the answerer hears nothing from its Phase 2 on", &c, &a);
    CHECK_EQ_I(a.s.end, V90M_END_FAILED);
    CHECK(starts(v90m_err_name(a.d.error), "P2D_ERR_"));
    CHECK_EQ_I(a.d.phase, V90M_PHASE_PHASE2); CHECK_EQ_I(a.d.selected, V90M_CONN_V90);
    CHECK(c.s.end != V90M_END_NONE);
    CHECK(c.d.error != 0 && strcmp(v90m_err_name(c.d.error), "?") != 0);
    CHECK_EQ_I(c.d.phase, V90M_PHASE_PHASE2); CHECK_EQ_I(c.d.selected, V90M_CONN_V90);

    /* ... from its Phase 3 on */
    pair(V90_MODS, V90_MODS, V90M_PHASE_PHASE3, DATA_NONE, V90M_CONN_NONE, 60, &c, &a);
    show("V.90, the answerer hears nothing from its Phase 3 on", &c, &a);
    CHECK_EQ_I(a.s.end, V90M_END_FAILED);
    CHECK(starts(v90m_err_name(a.d.error), "P34_ERR_"));
    CHECK_EQ_I(a.d.phase, V90M_PHASE_PHASE3); CHECK_EQ_I(a.d.selected, V90M_CONN_V90);
    CHECK(c.s.end != V90M_END_NONE);
    CHECK(starts(v90m_err_name(c.d.error), "V90A_ERR_"));
    CHECK(c.d.phase == V90M_PHASE_PHASE3 || c.d.phase == V90M_PHASE_PHASE4); CHECK_EQ_I(c.d.selected, V90M_CONN_V90);

    /* V.32bis chosen in V.8, and the caller never answers our AC */
    pair(V90M_MOD_V32BIS, V90M_MOD_V32BIS | V90M_MOD_V22, V90M_PHASE_TRAIN, DATA_NONE, V90M_CONN_NONE, 60, &c, &a);
    show("V.32bis, the caller silent after V.8", &c, &a);
    CHECK_EQ_I(a.s.end, V90M_END_FAILED);
    CHECK(strcmp(v90m_err_name(a.d.error), "V32_ERR_NO_TONE") == 0);
    CHECK_EQ_I(a.d.phase, V90M_PHASE_TRAIN); CHECK_EQ_I(a.d.selected, V90M_CONN_V32BIS);
    CHECK(starts(v90m_err_name(c.d.error), "V32_ERR_"));
    CHECK_EQ_I(c.d.phase, V90M_PHASE_TRAIN); CHECK_EQ_I(c.d.selected, V90M_CONN_V32BIS);

    /* V.8 with nothing in common */
    pair(V90M_MOD_V21, V90M_MOD_V22, CUT_NEVER, DATA_NONE, V90M_CONN_NONE, 30, &c, &a);
    show("V.8, nothing in common", &c, &a);
    CHECK_EQ_I(a.s.end, V90M_END_NO_COMMON); CHECK_EQ_I(c.s.end, V90M_END_NO_COMMON);
    CHECK(strcmp(v90m_err_name(a.d.error), "V8_ERR_NO_COMMON") == 0);
    CHECK(strcmp(v90m_err_name(c.d.error), "V8_ERR_NO_COMMON") == 0);
    CHECK_EQ_I(a.d.phase, V90M_PHASE_V8); CHECK_EQ_I(a.d.selected, V90M_CONN_NONE);
    CHECK_EQ_I(c.d.phase, V90M_PHASE_V8); CHECK_EQ_I(c.d.selected, V90M_CONN_NONE);
}

/* calling into silence: no answer tone */
static void test_no_tone(void)
{
    end_t cl;
    v90m_diag d; v90m_state s;
    int16_t rx = 0, tx;
    long k;
    end_init(&cl, V90M_ROLE_ORIGINATE, V90M_MOD_V32BIS);
    if (!cl.m) return;
    for (k = 0; k < 8000L * 60; k++) {
        v90m_process_linear(cl.m, &rx, &tx, 1);
        if (k % 80 == 0) { v90m_get_state(cl.m, &s); if (s.end != V90M_END_NONE) break; }
    }
    v90m_get_state(cl.m, &s); v90m_get_diag(cl.m, &d);
    printf("  V.32bis caller, no answer: end %d: %s, %s, %s\n", (int)s.end, v90m_err_name(d.error),
           phase_name(d.phase), conn_name(d.selected));
    CHECK_EQ_I(s.end, V90M_END_NO_ANSWER_TONE);
    CHECK(strcmp(v90m_err_name(d.error), "V8_ERR_NO_TONE") == 0);
    CHECK_EQ_I(d.phase, V90M_PHASE_TONE); CHECK_EQ_I(d.selected, V90M_CONN_NONE);
    end_free(&cl);
}

/* connected: no error, data mode, the modulation; then how the call ended */
static void test_connected(void)
{
    side_t c, a;

    pair(V90_MODS, V90_MODS, CUT_NEVER, DATA_CLEAR, V90M_CONN_V90, 60, &c, &a);
    show("V.90 to data mode, then our cleardown", &c, &a);
    CHECK_EQ_I(a.s.end, V90M_END_CLEARDOWN_OURS);
    CHECK(strcmp(v90m_err_name(a.d.error), "P34_ERR_HANGUP") == 0);
    CHECK_EQ_I(a.d.phase, V90M_PHASE_DATA); CHECK_EQ_I(a.d.selected, V90M_CONN_V90);
    CHECK_EQ_I(c.s.end, V90M_END_CLEARDOWN_FAR);
    CHECK(strcmp(v90m_err_name(c.d.error), "V90A_ERR_CLEARDOWN") == 0);
    CHECK_EQ_I(c.d.phase, V90M_PHASE_DATA); CHECK_EQ_I(c.d.selected, V90M_CONN_V90);

    pair(V90M_MOD_V22, V90M_MOD_V22, CUT_NEVER, DATA_CHECK, V90M_CONN_V22BIS, 10, &c, &a);
    show("V.22bis to data mode (no cleardown signal: the call goes on)", &c, &a);
    CHECK_EQ_I(a.s.end, V90M_END_NONE); CHECK_EQ_I(c.s.end, V90M_END_NONE);
    CHECK_EQ_I(a.d.error, 0); CHECK_EQ_I(c.d.error, 0);
    CHECK_EQ_I(a.d.phase, V90M_PHASE_DATA); CHECK_EQ_I(a.d.selected, V90M_CONN_V22BIS);
    CHECK_EQ_I(c.d.phase, V90M_PHASE_DATA); CHECK_EQ_I(c.d.selected, V90M_CONN_V22BIS);
}

/* NULL, and v90m_reset clears them */
static void test_reset_null(void)
{
    end_t cl;
    v90m_cfg cfg;
    v90m_diag d;
    int16_t rx = 0, tx;
    long k;
    memset(&d, 0x5a, sizeof d);
    v90m_get_diag(NULL, &d);
    CHECK_EQ_I(d.error, 0x5a5a5a5a);                     /* untouched */
    end_init(&cl, V90M_ROLE_ORIGINATE, V90M_MOD_V22);
    if (!cl.m) return;
    v90m_get_diag(cl.m, NULL);
    v90m_get_diag(cl.m, &d);
    CHECK_EQ_I(d.error, 0); CHECK_EQ_I(d.phase, V90M_PHASE_NONE); CHECK_EQ_I(d.selected, V90M_CONN_NONE);
    for (k = 0; k < 8000L * 60; k++) {
        v90m_process_linear(cl.m, &rx, &tx, 1);
        v90m_get_diag(cl.m, &d);
        if (d.error) break;
    }
    CHECK(strcmp(v90m_err_name(d.error), "V8_ERR_NO_TONE") == 0);
    v90m_cfg_default(&cfg); cfg.role = V90M_ROLE_ORIGINATE; cfg.modulations = V90M_MOD_V22;
    CHECK_EQ_I(v90m_reset(cl.m, &cfg), V90M_OK);
    v90m_get_diag(cl.m, &d);
    CHECK_EQ_I(d.error, 0); CHECK_EQ_I(d.phase, V90M_PHASE_NONE); CHECK_EQ_I(d.selected, V90M_CONN_NONE);
    end_free(&cl);
}

int main(void)
{
    test_names();
    test_failures();
    test_no_tone();
    test_connected();
    test_reset_null();
    T_REPORT("diag");
}
