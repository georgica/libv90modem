/* Interop: our V.8 CALL side (modem/v8orig) against spandsp's independent
 * V.8 ANSWERER, through the channel simulator: we sit at the network's codec
 * (as the box does), spandsp at the analogue end. Host-only (spandsp is a
 * test peer, never shipped): `make test-interop`. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "peer_spandsp.h"
#include "../modem/v8orig.h"
#include "../sim/channel.h"
#include "../sim/g711.h"

#define FS 8000

static v8orig_t O;

static void run(unsigned sp_mods, int sp_pcm, int sp_tone, unsigned our_modes, int our_pcm, int send_ci,
                double noise, double echo)
{
    static const double one[1] = {1.0};
    channel_cfg cfg; channel_t ch; v8orig_cfg oc;
    int16_t otx = 0, stx = 0, orx, srx;
    long n;

    sp_v8ans_start(sp_mods, sp_pcm, sp_tone);
    memset(&cfg, 0, sizeof cfg);
    cfg.ds_noise_dbfs = noise; cfg.us_noise_dbfs = noise;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.echo_gain = echo; cfg.echo_delay = 37; cfg.full_scale = 32124.0; cfg.seed = 99;
    channel_init(&ch, &cfg);
    memset(&oc, 0, sizeof oc);
    oc.amp = 5600; oc.send_ci = send_ci;
    oc.local.call_function = V8_CALLF_DATA; oc.local.modes = (uint16_t)our_modes; oc.local.n_mod_octets = 3;
    oc.local.has_prot = 1; oc.local.prot = V8_PROT_LAPM;
    if (our_pcm) { oc.local.has_pcm = 1; oc.local.pcm = V8_PCM_ANALOG; }
    v8orig_init(&O, &oc);

    for (n = 0; n < 20L * FS; n++) {
        double analog_rx; uint8_t net;
        /* the network side sends codewords (us), the analogue side linear (spandsp) */
        channel_tick(&ch, linear_to_ulaw(otx), (double)stx, &analog_rx, &net);
        orx = (int16_t)ulaw_to_linear(net);
        srx = (int16_t)lround(analog_rx);
        sp_v8call_tick(srx, &stx);
        if (v8orig_tick(&O, orx, &otx) == V8O_DONE) break;
    }
    for (n = 0; n < FS && !sp_v8call_result()->final; n++) sp_v8call_tick(0, &stx);
    sp_v8call_stop();
}

int main(void)
{
    const sp_v8_result *r = sp_v8call_result();
    int ci;

    for (ci = 0; ci <= 1; ci++) {
        run(SP_MOD_V34 | SP_MOD_V32 | SP_MOD_V22 | SP_MOD_V21, 0, 0,
            V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21, 0, ci, -50.0, 0.2);
        printf("  V.34 V.32 V.22 V.21 both ways%s: result %d, selected %d, JM modes 0x%04x; spandsp: ok %d, our CM modes 0x%x, protocol %d\n",
               ci ? ", with CI" : "", O.result, O.selected, O.jm.modes, r->ok, r->modulations, r->protocol);
        CHECK_EQ_I(O.result, V8O_RES_OK);
        CHECK_EQ_I(O.answer_tone, ANST_ANSAM);
        CHECK_EQ_I(O.selected, 1);                         /* V.34, item 1 of Table 4 */
        CHECK_EQ_I(O.jm.call_function, V8_CALLF_DATA);
        CHECK_EQ_I(O.jm.modes, V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21);
        CHECK(r->final && r->ok);
        CHECK_EQ_I(r->call_function, 6);
        CHECK_EQ_I(r->modulations, SP_MOD_V34 | SP_MOD_V32 | SP_MOD_V22 | SP_MOD_V21);
        CHECK_EQ_I(r->protocol, 1);                        /* LAPM */
    }

    /* a V.22bis-only answerer on a noisy, echoey line */
    run(SP_MOD_V22, 0, 0, V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21, 0, 0, -38.0, 0.35);
    printf("  V.22bis answerer, noise and echo: result %d, selected %d, JM modes 0x%04x; spandsp ok %d\n", O.result, O.selected, O.jm.modes, r->ok);
    CHECK_EQ_I(O.result, V8O_RES_OK);
    CHECK_EQ_I(O.selected, 4);
    CHECK(r->ok);

    /* a V.90 digital answerer: we offer the PCM analogue modem */
    run(SP_MOD_V90 | SP_MOD_V34 | SP_MOD_V22 | SP_MOD_V21, 2, 0, V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21, 1, 0, -50.0, 0.2);
    printf("  V.90 digital answerer: result %d, selected %d, JM pcm %d 0x%02x; spandsp ok %d, our pcm %d\n",
           O.result, O.selected, O.jm.has_pcm, O.jm.pcm, r->ok, r->pcm);
    CHECK_EQ_I(O.result, V8O_RES_OK);
    CHECK_EQ_I(O.selected, V8_SEL_V90);
    CHECK_EQ_I(r->pcm, 1);                                 /* it saw an analogue modem */

    /* a V.25 answerer (ANS): we must not send CM */
    run(SP_MOD_V22, 0, 1, V8_MOD_V22 | V8_MOD_V21, 0, 0, -50.0, 0.2);
    printf("  ANS answerer: result %d, tone %d, CM at %ld\n", O.result, O.answer_tone, O.at_cm);
    CHECK_EQ_I(O.result, V8O_RES_ANS);
    CHECK_EQ_I(O.at_cm, 0);

    T_REPORT("interop_v8orig_spandsp");
}
