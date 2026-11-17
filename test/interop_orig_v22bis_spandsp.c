/* Interop: our CALLING modem (modem/orig, from the answer tone on) against
 * spandsp's independent V.22bis ANSWERER, through the channel simulator: the
 * far end sends ANS (V.25), 75 ms of silence, then spandsp's start-up
 * (V.22bis 6.3.1.2); data both ways at 2400 and 1200 bit/s. Host only
 * (`make test-interop`). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "peer_spandsp.h"
#include "../modem/orig.h"
#include "../sim/channel.h"
#include "../sim/g711.h"

#define FS 8000
#define NB 8000
#define TWO_PI 6.283185307179586
#define PI 3.141592653589793
#define ANS_SAMPLES (FS * 33 / 10)
#define GAP_SAMPLES (FS * 75 / 1000)

typedef struct { uint32_t l; uint8_t tx[NB]; int ntx; uint8_t rx[NB + 4000]; int nrx; int sending; } side;
static int gb(void *u)
{
    side *s = u; int b;
    if (!s->sending) return 1;
    s->l = s->l * 1664525u + 1013904223u; b = (s->l >> 24) & 1;
    if (s->ntx < NB) s->tx[s->ntx++] = (uint8_t)b;
    return b;
}
static void pb(void *u, int b) { side *s = u; if (s->sending && s->nrx < NB + 4000) s->rx[s->nrx++] = (uint8_t)b; }

static int errors(const side *from, const side *to, int *compared)
{
    int off, best = 1 << 30;
    *compared = 0;
    for (off = 0; off < 1500; off++) {
        int j, err = 0, cnt = 0;
        for (j = 0; j < from->ntx && j + off < to->nrx; j++, cnt++)
            if (to->rx[j + off] != from->tx[j]) err++;
        if (cnt >= 3000 && err < best) { best = err; *compared = cnt; }
    }
    return best;
}

/* check != 0: the outcome is checked; 0: only printed (a finding, below) */
static void run(int sp_rate, double noise, double echo, int check)
{
    static const double one[1] = {1.0};
    static side us, sp;
    static orig_t o;
    channel_cfg cfg; channel_t ch; orig_cfg oc;
    int16_t otx = 0, stx = 0;
    long n, up_at = -1;
    int e_us, c_us, e_sp, c_sp;

    memset(&us, 0, sizeof us); memset(&sp, 0, sizeof sp); us.l = 5; sp.l = 6;
    memset(&cfg, 0, sizeof cfg);
    cfg.ds_noise_dbfs = noise; cfg.us_noise_dbfs = noise;
    cfg.ds_taps = one; cfg.ds_ntaps = 1; cfg.us_taps = one; cfg.us_ntaps = 1;
    cfg.echo_gain = echo; cfg.echo_delay = 19; cfg.full_scale = 32124.0; cfg.seed = 3;
    channel_init(&ch, &cfg);
    memset(&oc, 0, sizeof oc);
    oc.level = 3000; oc.modes = V8_MOD_V22 | V8_MOD_V21; oc.getbit = gb; oc.putbit = pb; oc.user = &us;
    orig_init(&o, &oc);
    sp_v22_start(sp_rate, 0, gb, pb, &sp);

    for (n = 0; n < 20L * FS; n++) {
        double far_rx; uint8_t net;
        channel_tick(&ch, linear_to_ulaw(otx), (double)stx, &far_rx, &net);
        otx = orig_tick(&o, (int16_t)ulaw_to_linear(net));
        if (n < ANS_SAMPLES) {
            /* V.25 ANS with phase reversals every 450 ms */
            double t = (double)n / FS, ph = TWO_PI * 2100 * t;
            if (((long)(t / 0.45)) % 2) ph += PI;
            stx = (int16_t)lround(4243 * cos(ph));
        } else if (n < ANS_SAMPLES + GAP_SAMPLES) {
            stx = 0;
        } else {
            sp_v22_tick((int16_t)lround(far_rx), &stx);
        }
        if (up_at < 0 && o.connected && sp_v22_trained()) up_at = n;
        if (up_at >= 0 && n == up_at + FS / 2) { us.sending = 1; sp.sending = 1; }
        if (up_at >= 0 && n > up_at + 7 * FS) break;
    }
    e_sp = errors(&sp, &us, &c_sp);
    e_us = errors(&us, &sp, &c_us);
    printf("  spandsp at %d bit/s, noise %.0f dBFS, echo %.2f: up at %.2f s, ours %d bit/s (modulation %d, V.8 %d), spandsp %d; "
           "errors spandsp->us %d of %d, us->spandsp %d of %d\n", sp_rate, noise, echo, up_at / (double)FS,
           o.bitrate, o.modulation, o.used_v8, sp_v22_rate(), e_sp, c_sp, e_us, c_us);
    if (!check) { sp_v22_stop(); return; }
    CHECK(up_at > 0);
    CHECK_EQ_I(o.used_v8, 0);
    CHECK_EQ_I(o.bitrate, sp_rate);
    CHECK_EQ_I(o.modulation, sp_rate == 2400 ? ORIG_MOD_V22BIS : ORIG_MOD_V22);
    CHECK_EQ_I(sp_v22_rate(), sp_rate);
    CHECK(c_sp >= 3000 && e_sp == 0);
    CHECK(c_us >= 3000 && e_us == 0);
    sp_v22_stop();
}

int main(void)
{
    run(2400, -50, 0.2, 1);
    run(1200, -50, 0.2, 1);
    run(2400, -44, 0.35, 1);
    /* A finding, not checked: with noise at -38 dBFS (about 20 dB below our
     * signal) spandsp's answerer does not take our S1, falls back to 1200
     * bit/s and then decodes about half of our bits wrong, while we decode
     * it without error. Our own V.22bis answerer, in the same channel, trains
     * on this caller at 2400 bit/s down to -32 dBFS; spandsp's caller trains
     * with our answerer at -38 dBFS (interop_v22bis_spandsp). Not explained:
     * PROGRESS.md, C2. */
    printf("  finding, not checked:\n");
    run(2400, -38, 0.35, 0);
    T_REPORT("interop_orig_v22bis_spandsp");
}
