/* See orig.h. */
#include <string.h>
#include "orig.h"
#include "v21h_bpf.h"
#include "probe.h"

#define FS 8000
#define MS(ms) ((int32_t)(ms) * 8)

#define LEGACY_MS      10000   /* after ANS: how long to wait for V.22bis or V.21 */
#define V22_TOTAL_MS   30000   /* give up on a V.22bis start-up */
#define V21_WAIT_MS    20000
#define V21_CD_ON_MS     100   /* as in modem/answer.c */
#define V21_MARK_MS      300   /* after ANS: a steady channel 2 mark this long is a V.21 answerer */
#define CD_LOSS_MS       400
#define V21_CD_AMP       110   /* carrier detect, peak amplitude (about -43 dBm0) */

static void fail(orig_t *o, int why) { o->phase = ORIG_FAILED; o->connected = 0; o->end = why; o->t = 0; }

static void start_v22(orig_t *o)
{
    v22m_init(&o->v22, V22_CALLER, 1, o->cfg.level, o->cfg.getbit, o->cfg.putbit, o->cfg.user);
    o->cd_run = 0;
    o->phase = ORIG_V22; o->t = 0;
}

/* V.21 receiver on channel 2: what the answerer sends */
static void v21_rx_init(orig_t *o)
{
    v21demod_init(&o->fdem, V21H_MARK, V21H_SPACE, FS, 300);
    arx_init(&o->uart, FS, 300);
    o->cd_thresh = v21demod_energy_of(&o->fdem, V21_CD_AMP);
    for (o->bpos = 0; o->bpos < 128; o->bpos++) o->bpf[o->bpos] = 0;
    o->bpos = 0;
}

static void start_v21(orig_t *o)
{
    /* peak amplitude of a sine with the configured RMS */
    v21mod_init(&o->fmod, V21L_MARK, V21L_SPACE, FS, 300, (int16_t)(o->cfg.level * 181 / 128));
    if (o->phase != ORIG_LEGACY) v21_rx_init(o);     /* in ORIG_LEGACY it has been listening already */
    o->fbit = 1; o->cd_run = 0;
    o->phase = ORIG_V21; o->t = 0;
}

void orig_init(orig_t *o, const orig_cfg *cfg)
{
    v8orig_cfg vc;
    memset(o, 0, sizeof *o);
    o->cfg = *cfg;
    memset(&vc, 0, sizeof vc);
    vc.amp = (int16_t)(cfg->level * 181 / 128);
    vc.local.call_function = V8_CALLF_DATA;
    vc.local.modes = (uint16_t)cfg->modes;
    vc.local.n_mod_octets = 3;
    /* LAPM offered: the library has V.42 (V.8 6.4) */
    vc.local.has_prot = 1; vc.local.prot = V8_PROT_LAPM;
    vc.send_ci = cfg->send_ci;
    v8orig_init(&o->v8, &vc);
    o->phase = ORIG_V8;
}

/* Keep the echo of our own channel 1 (980 / 1180 Hz) out of the channel 2
 * receiver (modem/v21h_bpf.h: 1650 / 1850 Hz pass, 980 Hz 56 dB down). */
static int16_t v21_bandpass(orig_t *o, int16_t x)
{
    int32_t acc = 0;
    int k;
    o->bpf[o->bpos] = x;
    for (k = 0; k < V21H_BPF_N; k++) acc += (int32_t)o->bpf[(o->bpos - k) & 127] * v21h_bpf[k] >> 4;
    o->bpos = (o->bpos + 1) & 127;
    acc >>= 11;
    return (int16_t)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
}

/* one sample of the channel 2 receiver: the line level, 1 under the squelch */
static int v21_rx(orig_t *o, int16_t rx, int *carrier)
{
    int level = v21demod_tick(&o->fdem, v21_bandpass(o, rx));
    *carrier = o->fdem.energy >= o->cd_thresh;
    return *carrier ? level : 1;
}

static int16_t tick_v21(orig_t *o, int16_t rx)
{
    int carrier, level = v21_rx(o, rx, &carrier), c, done, i;
    int16_t out;
    if (!carrier) {
        if (o->cd_run > 0) o->cd_run = 0;
        if (o->connected && --o->cd_run < -MS(CD_LOSS_MS)) { fail(o, ORIG_END_LOST); return 0; }
    } else {
        if (o->cd_run < 0) o->cd_run = 0;
        if (o->cd_run < MS(V21_CD_ON_MS)) o->cd_run++;
        else if (!o->connected) { o->connected = 1; o->modulation = ORIG_MOD_V21; o->bitrate = 300; }
    }
    c = arx_tick(&o->uart, level);
    if (c >= 0 && o->connected) {            /* hand the character on as start-stop bits */
        o->cfg.putbit(o->cfg.user, 0);
        for (i = 0; i < 8; i++) o->cfg.putbit(o->cfg.user, (c >> i) & 1);
        o->cfg.putbit(o->cfg.user, 1);
    }
    out = v21mod_tick(&o->fmod, o->fbit, &done);
    if (done) o->fbit = o->connected ? (o->cfg.getbit(o->cfg.user) & 1) : 1;
    if (!o->connected && o->t > MS(V21_WAIT_MS)) fail(o, ORIG_END_NO_CARRIER);
    return out;
}

int16_t orig_tick(orig_t *o, int16_t rx)
{
    int16_t tx = 0;
    o->t++;
    switch (o->phase) {
    case ORIG_V8:
        if (v8orig_tick(&o->v8, rx, &tx) != V8O_DONE) break;
        switch (o->v8.result) {
        case V8O_RES_ANS:
            /* a pre-V.8 answerer: listen for which modulation it starts */
            if (o->cfg.modes & V8_MOD_V22) v22m_init(&o->v22, V22_CALLER, 1, o->cfg.level, o->cfg.getbit, o->cfg.putbit, o->cfg.user);
            v21_rx_init(o);
            o->mark_run = 0;
            o->phase = ORIG_LEGACY; o->t = 0;
            break;
        case V8O_RES_OK:
            o->used_v8 = 1;
            if (o->v8.selected == 4 && (o->cfg.modes & V8_MOD_V22)) start_v22(o);
            else if (o->v8.selected == 12 && (o->cfg.modes & V8_MOD_V21)) start_v21(o);
            else fail(o, ORIG_END_NO_COMMON);      /* a modulation not built yet */
            break;
        case V8O_RES_NO_TONE: fail(o, ORIG_END_NO_TONE); break;
        case V8O_RES_NO_JM: fail(o, ORIG_END_NO_JM); break;
        default: fail(o, ORIG_END_NO_COMMON); break;
        }
        break;

    case ORIG_LEGACY: {
        /* V.22bis: its caller is silent until the answerer's unscrambled
         * binary 1 (6.3.1.1), so it runs as it is; V.21: a steady mark on
         * channel 2 while there is no V.22bis carrier */
        int carrier, level = v21_rx(o, rx, &carrier);
        if (o->cfg.modes & V8_MOD_V22) {
            tx = v22m_tick(&o->v22, rx);
            /* it is V.22bis once its caller has heard 155 ms of the
             * unscrambled binary 1 and left V22S_WAIT (its carrier flag
             * alone also rises on a V.21 channel 2 mark) */
            if (o->v22.state != V22S_WAIT) {
                o->cd_run = 0;
                o->phase = ORIG_V22;                                  /* the start-up goes on */
                break;
            }
        }
        if (carrier && level == 1) o->mark_run++; else o->mark_run = 0;
        if ((o->cfg.modes & V8_MOD_V21) && o->mark_run >= MS(V21_MARK_MS)) { start_v21(o); break; }
        if (o->t > MS(LEGACY_MS)) fail(o, ORIG_END_NO_CARRIER);
        break;
    }

    case ORIG_V22:
        tx = v22m_tick(&o->v22, rx);
        if (!o->connected) {
            if (o->v22.tx_ready && o->v22.rx_ready) {
                o->connected = 1; o->bitrate = o->v22.bitrate;
                o->modulation = o->bitrate == 2400 ? ORIG_MOD_V22BIS : ORIG_MOD_V22;
                o->cd_run = 0;
            } else if (o->t > MS(V22_TOTAL_MS) || o->v22.state == V22S_FAILED) {
                fail(o, ORIG_END_NO_CARRIER);
            }
        } else {
            if (o->v22.state == V22S_FAILED) fail(o, ORIG_END_LOST);     /* a retrain (6.4) that never completed */
            else if (o->v22.rx.carrier) o->cd_run = 0;
            else if (++o->cd_run > MS(CD_LOSS_MS)) fail(o, ORIG_END_LOST);
        }
        break;

    case ORIG_V21:
        tx = tick_v21(o, rx);
        break;

    case ORIG_FAILED:
        break;
    }
    return tx;
}

int orig_moving(const orig_t *o)
{
    if (!o->connected) return 0;
    if (o->phase == ORIG_V22) return !o->v22.retraining && o->v22.tx_ready && o->v22.rx_ready;
    return 1;
}

int orig_snr_db10(const orig_t *o)
{
    int v;
    if (!o->connected || o->phase != ORIG_V22) return -1;
    /* as answer_snr_db10: 4096 = one unit squared, the points have a mean
     * power of 10 */
    v = probe_db10(10 * 4096, o->v22.rx.mse > 0 ? o->v22.rx.mse : 1);
    return v < 0 ? 0 : v > 600 ? 600 : v;
}

int orig_retrain(orig_t *o)
{
    if (o->phase != ORIG_V22 || !o->connected || o->v22.bitrate != 2400) return -1;
    v22m_retrain(&o->v22);
    return 0;
}
