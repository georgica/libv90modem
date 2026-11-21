/* End-to-end V.8: our answer side against a calling-modem peer, through the
 * PSTN channel simulator (mu-law both ways, hybrid echo, noise). */
#include <string.h>
#include "test.h"
#include "../modem/v8ans.h"
#include "../sim/v8call.h"
#include "../sim/channel.h"
#include "../sim/g711.h"

#define FS 8000
#define AMP 5600          /* about -12 dBm0 */

typedef struct {
    int ans_result, selected, done_at;
    int first_tx, ansam_start, jm_start, post_start;
    int cm_sent_at_jm, caller_done, quiet_ok, post_quiet;
    v8_menu_t cm, jm_seen;
    int caller_got_jm;
    int x2;                 /* v8ans_x2() at the end */
    int njm_tx;             /* JM samples sent (in jm_tx) */
    int wrap[64], nwrap;    /* indices into jm_tx where a JM begins again */
} outcome;

/* x2 (modem/v8ans, docs/history/x2-facts.md "Phase 1"): 0 not offered,
 * 1 offered on a mu-law line, 2 offered on an A-law line */
static int g_x2;
#define JM_MAX (5 * FS)
static int16_t jm_tx[2][JM_MAX];
static int g_slot;          /* which jm_tx the run fills */

static void run(const v8_menu_t *caller_cm, const v8_menu_t *local,
                double noise_dbfs, double echo, int max_s, outcome *o)
{
    static const double one[1] = {1.0};
    channel_cfg cfg; channel_t ch;
    v8ans_t ans; v8call_t call;
    int16_t atx = 0, ctx = 0, arx, crx;
    v8a_state prev = V8A_SILENCE, st;
    int n;

    memset(&cfg, 0, sizeof cfg);
    cfg.ds_pad_db = 0; cfg.ds_noise_dbfs = noise_dbfs; cfg.us_noise_dbfs = noise_dbfs;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.echo_gain = echo; cfg.echo_delay = 37; cfg.full_scale = 32124.0; cfg.seed = 4242;
    channel_init(&ch, &cfg);
    v8ans_init(&ans, FS, AMP, local);
    if (g_x2) v8ans_offer_x2(&ans, g_x2 == 2);
    if (caller_cm) v8call_init(&call, FS, AMP, caller_cm);
    memset(o, 0, sizeof *o);
    o->first_tx = o->ansam_start = o->jm_start = o->post_start = o->done_at = -1;
    o->quiet_ok = 1; o->post_quiet = 1;

    for (n = 0; n < max_s * FS; n++) {
        double client_rx; uint8_t srv;
        channel_tick(&ch, linear_to_ulaw(atx), (double)ctx, &client_rx, &srv);
        arx = (int16_t)ulaw_to_linear(srv);
        crx = (int16_t)lround(client_rx);
        if (caller_cm) v8call_tick(&call, crx, &ctx);
        { int was_post = (ans.state == V8A_POST), was_jm = (ans.state == V8A_JM), pos = ans.jmpos;
          st = v8ans_tick(&ans, arx, &atx);
          if (was_post && atx != 0) o->post_quiet = 0;
          if (was_jm && o->njm_tx < JM_MAX) {
              if (pos != 0 && ans.jmpos == 0 && o->nwrap < 64) o->wrap[o->nwrap++] = o->njm_tx + 1;
              jm_tx[g_slot][o->njm_tx++] = atx;
          } }
        if (atx != 0 && o->first_tx < 0) o->first_tx = n;
        if (st != prev) {
            if (st == V8A_ANSAM) o->ansam_start = n;
            if (st == V8A_JM) { o->jm_start = n; o->cm_sent_at_jm = caller_cm ? call.cm_sent : 0; }
            if (st == V8A_POST) o->post_start = n;
            if (st == V8A_DONE) o->done_at = n;
            prev = st;
        }
        if (st == V8A_DONE) break;
    }
    o->ans_result = ans.result; o->selected = ans.selected; o->cm = ans.cm;
    o->x2 = v8ans_x2(&ans);
    if (caller_cm) {
        /* let the caller finish its own 75 ms */
        int k; for (k = 0; k < FS && call.state != V8C_DONE; k++) v8call_tick(&call, 0, &ctx);
        o->caller_done = (call.state == V8C_DONE);
        o->caller_got_jm = call.got_jm; o->jm_seen = call.jm;
    }
}

int main(void)
{
    v8_menu_t local, cm;
    outcome o;

    memset(&local, 0, sizeof local);
    local.call_function = V8_CALLF_DATA;
    local.modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21;
    local.pcm = V8_PCM_DIGITAL;
    local.prot = V8_PROT_LAPM;
    local.access = V8_ACC_DIGITAL;

    /* 1. V.90 analogue caller, clean-ish line with hybrid echo */
    memset(&cm, 0, sizeof cm);
    cm.call_function = V8_CALLF_DATA;
    cm.modes = V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21;
    cm.n_mod_octets = 3;
    cm.has_pcm = 1; cm.pcm = V8_PCM_ANALOG;
    cm.has_access = 1;
    cm.has_prot = 1; cm.prot = V8_PROT_LAPM;
    run(&cm, &local, -55.0, 0.2, 15, &o);
    CHECK_EQ_I(o.ans_result, V8A_RES_OK);
    CHECK_EQ_I(o.selected, V8_SEL_V90);
    CHECK(o.done_at > 0);
    CHECK_EQ_I(o.cm.modes, cm.modes);                 /* CM decoded as sent */
    CHECK(o.cm.has_pcm && o.cm.pcm == V8_PCM_ANALOG);
    CHECK(o.first_tx >= FS / 5);                      /* >= 0.2 s initial silence */
    CHECK(o.cm_sent_at_jm >= 2);                      /* JM only after 2 CMs */
    CHECK(o.caller_done && o.caller_got_jm);          /* peer saw 2 identical JMs */
    CHECK(o.jm_seen.has_pcm && o.jm_seen.pcm == V8_PCM_DIGITAL);
    CHECK(o.jm_seen.has_access && (o.jm_seen.access & V8_ACC_DIGITAL));
    CHECK_EQ_I(o.jm_seen.modes, V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21);
    CHECK(o.post_quiet);                              /* silence after CJ ... */
    CHECK_NEAR((o.done_at - o.post_start) * 1000.0 / FS, 75.0, 5.0);   /* ... for 75 +/- 5 ms */

    /* 2. V.22bis-only V.8 caller */
    memset(&cm, 0, sizeof cm);
    cm.call_function = V8_CALLF_DATA; cm.modes = V8_MOD_V22; cm.n_mod_octets = 3;
    run(&cm, &local, -55.0, 0.2, 15, &o);
    CHECK_EQ_I(o.ans_result, V8A_RES_OK);
    CHECK_EQ_I(o.selected, 4);

    /* 3. noisy line (about 20 dB SNR) with strong echo */
    cm.modes = V8_MOD_V34 | V8_MOD_V22; cm.has_pcm = 1; cm.pcm = V8_PCM_ANALOG; cm.has_access = 1;
    run(&cm, &local, -35.0, 0.35, 15, &o);
    CHECK_EQ_I(o.ans_result, V8A_RES_OK);
    CHECK_EQ_I(o.selected, V8_SEL_V90);

    /* 4. no V.8 caller: only echo + noise come back. ANSam runs 5 +/- 1 s, then
     *    75 ms silence, result NO_CM (no false CM from our own echo) */
    run(NULL, &local, -45.0, 0.35, 15, &o);
    CHECK_EQ_I(o.ans_result, V8A_RES_NO_CM);
    CHECK_NEAR((o.post_start - o.ansam_start) / (double)FS, 5.0, 1.0);
    CHECK_NEAR((o.done_at - o.post_start) * 1000.0 / FS, 75.0, 5.0);
    CHECK(o.jm_start < 0);

    /* 5. x2 (docs/history/x2-facts.md "Phase 1"). A caller whose CM offers
     *    V.34 and no PCM, on a mu-law line, with x2 offered: the JM octets as
     *    today, the carrier inverted after each complete JM. */
    {
        outcome o0, o1;
        int k, j, n, same = 1, flip_ok = 1;
        memset(&cm, 0, sizeof cm);
        cm.call_function = V8_CALLF_DATA; cm.modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21; cm.n_mod_octets = 3;
        cm.has_prot = 1; cm.prot = V8_PROT_LAPM;
        g_x2 = 0; g_slot = 0; run(&cm, &local, -55.0, 0.2, 15, &o0);
        g_x2 = 1; g_slot = 1; run(&cm, &local, -55.0, 0.2, 15, &o1);
        CHECK_EQ_I(o0.x2, 0); CHECK_EQ_I(o1.x2, 1);
        CHECK_EQ_I(o1.ans_result, V8A_RES_OK); CHECK_EQ_I(o1.selected, 1);
        CHECK(o1.caller_done && o1.caller_got_jm);
        CHECK(!o1.jm_seen.has_access);                  /* this CM has no access octet */
        CHECK_EQ_I(o1.jm_seen.modes, o0.jm_seen.modes);
        CHECK(o1.nwrap >= 2);
        n = o0.njm_tx < o1.njm_tx ? o0.njm_tx : o1.njm_tx;
        CHECK(n > o1.wrap[1]);
        /* JM number j (from 0) is today's times (-1)^j */
        for (k = 0, j = 0; k < n; k++) {
            int sign;
            while (j < o1.nwrap && k >= o1.wrap[j]) j++;
            sign = (j & 1) ? -1 : 1;
            if (jm_tx[1][k] != sign * jm_tx[0][k]) flip_ok = 0;
        }
        CHECK(flip_ok);
        CHECK_EQ_I(o1.wrap[0], o0.wrap[0]);
        printf("  x2 JM: %d JMs sent, %d samples compared, the sign flips after each\n", o1.nwrap, n);

        /* the CM carries the PSTN access octet (the Courier's 0d): the JM
         * returns it with "DCE on digital network" (the I-modem's 8d) */
        cm.has_access = 1; cm.access = 0;
        g_x2 = 1; g_slot = 1; run(&cm, &local, -55.0, 0.2, 15, &o1);
        CHECK_EQ_I(o1.x2, 1); CHECK_EQ_I(o1.selected, 1);
        CHECK(o1.caller_done && o1.caller_got_jm);
        CHECK(o1.jm_seen.has_access); CHECK_EQ_I(o1.jm_seen.access, V8_ACC_DIGITAL);
        {
            uint8_t oct[V8_MAX_OCTETS]; int no = v8_encode(&o1.jm_seen, oct, V8_MAX_OCTETS);
            CHECK(no >= 1); CHECK_EQ_I(oct[no - 1], 0x8d);
        }
        g_x2 = 0; g_slot = 0; run(&cm, &local, -55.0, 0.2, 15, &o0);
        CHECK_EQ_I(o0.x2, 0); CHECK(!o0.jm_seen.has_access);      /* today: none */

        /* A-law: no x2, JM as today */
        g_x2 = 2; g_slot = 1; run(&cm, &local, -55.0, 0.2, 15, &o1);
        CHECK_EQ_I(o1.x2, 0); CHECK(!o1.jm_seen.has_access);
        CHECK_EQ_I(o1.njm_tx, o0.njm_tx);
        for (k = 0; k < o0.njm_tx && k < o1.njm_tx; k++) if (jm_tx[0][k] != jm_tx[1][k]) same = 0;
        CHECK(same);

        /* a CM with PCM (a V.90 caller): V.90 and the JM as today */
        cm.has_pcm = 1; cm.pcm = V8_PCM_ANALOG;
        g_x2 = 0; g_slot = 0; run(&cm, &local, -55.0, 0.2, 15, &o0);
        g_x2 = 1; g_slot = 1; run(&cm, &local, -55.0, 0.2, 15, &o1);
        CHECK_EQ_I(o1.x2, 0); CHECK_EQ_I(o1.selected, V8_SEL_V90);
        CHECK_EQ_I(o1.njm_tx, o0.njm_tx);
        same = 1;
        for (k = 0; k < o0.njm_tx && k < o1.njm_tx; k++) if (jm_tx[0][k] != jm_tx[1][k]) same = 0;
        CHECK(same);

        /* a CM with digital PCM and V.34 and no analogue PCM: the plan's gate
         * is "V.34 and no PCM", so no x2 */
        cm.has_pcm = 1; cm.pcm = V8_PCM_DIGITAL;
        g_x2 = 1; g_slot = 1; run(&cm, &local, -55.0, 0.2, 15, &o1);
        CHECK_EQ_I(o1.x2, 0);
        g_x2 = 0;
    }

    T_REPORT("v8ans");
}
