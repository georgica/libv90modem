/* Test peer: a whole V.92 analogue modem call with its line. See v92call.h. */
#include <string.h>
#include <math.h>
#include "v92call.h"
#include "g711.h"
#include "../modem/pcmenc.h"
#include "../v90/v90_pcm.h"

#define DLY 20          /* samples from the digital modem's octet to the V.92 model */
#define TWO_PI 6.283185307179586476925286766559

/* VCL_QC's steps */
enum { QS_WAIT,         /* silent after QCA1d, for QTS */
       QS_QTS,          /* QTS and QTS\ */
       QS_ANSPCM,       /* ANSpcm, TONEq from 20 ms into it */
       QS_POST,         /* the 75 ms after TONEq */
       QS_GONE };       /* silent, for ANSam: the digital modem gave up */

static void line(v92call_t *c)
{
    static const double one[1] = { 1.0 };
    channel_cfg cc; v92chan_cfg vc;
    memset(&cc, 0, sizeof cc);
    cc.ds_noise_dbfs = -60.0; cc.us_noise_dbfs = c->cfg.us_noise_dbfs < 0 ? c->cfg.us_noise_dbfs : -60.0;
    cc.ds_taps = one; cc.ds_ntaps = 1; cc.us_taps = one; cc.us_ntaps = 1;
    cc.echo_gain = c->cfg.echo_gain; cc.echo_delay = c->cfg.echo_delay; cc.full_scale = 32124.0; cc.seed = 5;
    channel_init(&c->ch, &cc);
    memset(&vc, 0, sizeof vc);
    vc.alaw = c->cfg.alaw; vc.shape = c->cfg.shape;
    vc.level = c->cfg.up_level > 0 ? c->cfg.up_level : 4000;
    vc.noise = c->cfg.up_noise > 0 ? c->cfg.up_noise : 3;
    /* half of it, as the stage 2 calls ran (0.05 there against 0.1 on sim/channel) */
    vc.echo_gain = c->cfg.echo_gain_pcm > 0 ? c->cfg.echo_gain_pcm : c->cfg.echo_gain / 2; vc.echo_delay = c->cfg.echo_delay;
    vc.rx_linear = c->cfg.rx_linear;
    vc.up_delay = 24; vc.seed = 5;
    v92chan_init(&c->vch, &vc);
}

static void p2cfg(const v92call_t *c, p2a_cfg *pc, int retrain)
{
    memset(pc, 0, sizeof *pc);
    pc->level = c->cfg.level; pc->v90 = 1; pc->up_sr = -1; pc->uinfo = 91;
    pc->info0a.lo3000 = pc->info0a.hi3000 = 1; pc->info0a.lo3200 = pc->info0a.hi3200 = 1;
    pc->info0a.pwr_reduce = 1; pc->info0a.max_sr_diff = 5; pc->info0a.c1664 = 1;
    pc->info0a.v92 = (uint8_t)(c->cfg.v92 != 0);
    /* a retrain has no INFO0: what the first Phase 2 found holds (9.3/V.92) */
    pc->pcm_up = c->cfg.v92 && c->cfg.pcm_up && (!retrain || c->d_v92);
    pc->sections = 2; pc->ltot_code = 1; pc->lmax_code = 1; pc->md_len = 0;
    /* short Phase 2 (9.4.2): asked for in INFO0a, so not in a retrain */
    pc->short_p2 = c->cfg.short_p2 && !retrain;
    pc->info0a.short_p2 = (uint8_t)(pc->short_p2 != 0);
    pc->short_t19 = c->cfg.short_t19; pc->short_sr = c->cfg.short_sr; pc->short_hi = c->cfg.short_hi;
    pc->short_no_rev = c->cfg.short_no_rev; pc->short_no_info1a = c->cfg.short_no_info1a;
}

/* the CM: data, V.34 V.22 V.21, the analogue PCM modem, LAPM if asked */
static void cm_menu(const v92call_cfg *cfg, v8_menu_t *cm)
{
    memset(cm, 0, sizeof *cm);
    cm->call_function = V8_CALLF_DATA;
    cm->modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21; cm->n_mod_octets = 3;
    cm->has_pcm = 1; cm->pcm = V8_PCM_ANALOG;
    if (cfg->lapm) { cm->has_prot = 1; cm->prot = V8_PROT_LAPM; cm->has_access = 1; cm->access = 0; }
}

void v92call_init(v92call_t *c, const v92call_cfg *cfg)
{
    v8_menu_t cm;
    int k;
    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    if (c->cfg.level <= 0) c->cfg.level = 4000;
    line(c);
    for (k = 0; k < (int)sizeof c->dly; k++) c->dly[k] = v90_pcm_octet(cfg->alaw, 0, 1);
    cm_menu(cfg, &cm);
    v8call_init(&c->v8, 8000, (int16_t)c->cfg.level, &cm);
    c->stage = VCL_V8;
    c->qts_at = c->toneq_on = c->toneq_off = c->anspcm_end_at = c->info0d_at = -1;
    for (k = 0; k < 4; k++) c->sd_at[k] = -1;
    if (cfg->v8bis_ms && cfg->v8bis_ms_len > 0) v8bisc_init(&c->bis, cfg->v8bis_ms, cfg->v8bis_ms_len, c->cfg.level);
    if (cfg->qc) {
        uint8_t bits[QCA_QC1A_BITS];
        int n = qca_qc1a_bits(cfg->qc_wxyz, cfg->qc_lapm, bits);
        v8call_prefix(&c->v8, bits, n);
        qca_rx_init(&c->qrx);
        c->qc_on = 1;
    }
}

/* the ANSam the digital modem sends when short Phase 1 failed (9.2.4.3):
 * 2100 Hz in 20 ms blocks; a block lost to a phase reversal is forgiven.
 * Returns 1 once it has been there for 1 s. */
static int ansam_1s(v92call_t *c, int16_t x)
{
    c->ai += x * cos(c->aph); c->aq += x * sin(c->aph);
    c->aph += TWO_PI * 2100.0 / 8000.0;
    if (c->aph > TWO_PI) c->aph -= TWO_PI;
    if (++c->an == 160) {
        double a = 2.0 * sqrt(c->ai * c->ai + c->aq * c->aq) / 160.0;
        if (a > 300.0) c->ahits = (c->ahits < 0 ? -c->ahits : c->ahits) + 1;
        else c->ahits = c->ahits > 0 ? -c->ahits : 0;     /* one block missed is held, two are not */
        c->ai = c->aq = 0; c->an = 0;
    }
    return c->ahits >= 50;
}

static void ansam_reset(v92call_t *c) { c->ai = c->aq = 0; c->an = 0; c->ahits = 0; }

/* short Phase 1 has failed: CM at once, and V.8 from there (9.2.4.3) */
static void qc_fallback(v92call_t *c)
{
    v8_menu_t cm;
    cm_menu(&c->cfg, &cm);
    v8call_init(&c->v8, 8000, (int16_t)c->cfg.level, &cm);
    c->v8.state = V8C_CM;
    c->qc_on = 0;
    c->qc_fallbacks++;
    c->info0d_at = -1;
    if (c->toneq_on >= 0 && c->toneq_off < 0) c->toneq_off = c->n;
    c->stage = VCL_V8;
}

/* VCL_QC: one octet of the digital modem's, one sample of ours */
static double qc_tick(v92call_t *c, uint8_t o, int16_t x)
{
    int alaw = c->cfg.alaw, lm = qca_lm(c->qca1d), u = qca_uqts_ucode(c->cfg.qc_wxyz);
    long start = c->cfg.toneq_delay_ms > 0 ? (long)c->cfg.toneq_delay_ms * 8 : 160;
    double out = 0;
    switch (c->qs) {
    case QS_WAIT:
        if (o == qca_qts_octet(alaw, u, 0)) {
            c->qs = QS_QTS; c->qt = 1; c->qts_at = c->n; c->qts_ok = 1;
        } else if (ansam_1s(c, x) || ++c->qt > 3 * 8000) {
            qc_fallback(c);
        }
        break;
    case QS_QTS:
        if (o != qca_qts_octet(alaw, u, (int)c->qt)) {
            if (c->qt < 12) { c->qs = QS_WAIT; c->qt = 0; c->qts_at = -1; break; }     /* not QTS after all */
            c->qts_ok = 0;
        }
        if (++c->qt >= QCA_QTS_LEN + QCA_QTSB_LEN) { c->qs = QS_ANSPCM; c->qt = 0; }
        break;
    case QS_ANSPCM:
        if (o == qca_anspcm_octet(alaw, lm, c->qt) && c->qt < 4L * 8000) {
            c->qt++;
            if (!c->cfg.qc_no_toneq && c->qt >= start) {
                if (c->toneq_on < 0) c->toneq_on = c->n;
                out = c->cfg.level * cos(c->tph);
                c->tph += TWO_PI * 980.0 / 8000.0;
                if (c->tph > TWO_PI) c->tph -= TWO_PI;
            }
        } else {
            /* 9.2.1.3: ANSpcm is no longer there */
            c->anspcm_n = c->qt; c->anspcm_end_at = c->n;
            c->anspcm_ok = c->qt >= 301;
            if (c->toneq_on >= 0) { c->toneq_off = c->n; c->qs = QS_POST; }
            else { c->qs = QS_GONE; ansam_reset(c); }
            c->qt = 0;
        }
        break;
    case QS_POST:
        /* the digital modem's 75 ms: an ANSpcm codeword that goes on was a
         * wrong one, and anything loud is its ANSam (TONEq too late) */
        if (c->qt < 24 && o == qca_anspcm_octet(alaw, lm, c->anspcm_n + 1 + c->qt)
            && qca_octet_ucode(alaw, o) > 8) c->anspcm_ok = 0;
        if (c->info0d_at < 0 && qca_octet_ucode(alaw, o) != 0) c->info0d_at = c->n;
        if (c->qt < 400 && qca_octet_ucode(alaw, o) > 16) {
            c->qs = QS_GONE; c->qt = 0; c->info0d_at = -1; ansam_reset(c);
            break;
        }
        if (++c->qt >= 599) {
            p2a_cfg pc;
            p2cfg(c, &pc, 0);
            p2a_init(&c->p2, &pc);
            c->stage = VCL_P2;
        }
        break;
    case QS_GONE:
        if (ansam_1s(c, x)) qc_fallback(c);
        else if (++c->qt > 6 * 8000) c->stage = VCL_FAILED;
        break;
    }
    return out;
}

/* Phase 2 is over: Phase 3 of what INFO1a selected */
static void phase3(v92call_t *c)
{
    c->phase2s++;
    if (c->phase2s == 1) c->d_v92 = c->p2.info0d.v92;
    if (c->p2.got0d) c->d_short = c->p2.info0d.short_p2;
    if (!c->p2.short_run) c->d_pcm_up = c->p2.info1d.sr[5].hi_carrier;
    c->p3_n0 = c->n; c->sd_seen = 0;
    dpskrx_init(&c->toneb, DPSK_FC_B);
    if (info1a_kind(&c->p2.info1a) == INFO1A_PCM) {
        v92a_cfg ac;
        memset(&ac, 0, sizeof ac);
        ac.alaw = c->cfg.alaw; ac.uinfo = c->p2.info1a.uinfo;
        ac.sil0 = 560; ac.ntrn1 = 2040; ac.ntrn1b = 2040; ac.jd_wait = 100;
        ac.dil_n = c->cfg.dil_n; ac.dil_syms = 1500; ac.trn2u_min = 12000;
        ac.up_rates = c->cfg.up_rates ? c->cfg.up_rates : 0x7FFFF;
        ac.Kt = 12; ac.Mt = 4; ac.Kd = 32; ac.Md = 41;
        ac.getbit = c->cfg.getbit; ac.putbit = c->cfg.putbit; ac.user = c->cfg.user;
        v92a_init(&c->va, &ac);
        c->va.phase = 0.3;
        c->pcm_starts++;
        c->stage = VCL_PCM;
    } else {
        p34a_cfg qc;
        int sr = c->p2.info1a.up_sr, hi = c->p2.info1d.sr[sr].hi_carrier;
        memset(&qc, 0, sizeof qc);
        qc.alaw = c->cfg.alaw;
        qc.uinfo = c->p2.info1a.uinfo; qc.sym_rate = sr == 3 ? 3000 : 3200;
        qc.fc = sr == 3 ? (hi ? 2000.0 : 1800.0) : (hi ? 1920.0 : 3200.0 * 4 / 7);
        if (c->p2.short_run) {
            /* Table 19: the carrier is the caller's own bit 33 (no INFO1d) */
            hi = c->p2.info1a.hi_carrier;
            qc.sym_rate = sr == 3 ? 3000 : sr == 4 ? 3200 : 3429;
            qc.fc = sr == 3 ? (hi ? 2000.0 : 1800.0) : sr == 4 ? (hi ? 1920.0 : 3200.0 * 4 / 7) : 3429.0 * 4 / 7;
        }
        qc.gain = c->cfg.level; qc.sil0 = 70 * qc.sym_rate / 1000; qc.ntrn = 600; qc.jd_wait = 50;
        qc.dil_n = 8; qc.dil_syms = 1200;
        qc.Kt = 12; qc.Mt = 4; qc.Kd = 32; qc.Md = 41; qc.up_rates = 0x1FFF;
        qc.getbit = c->cfg.getbit; qc.putbit = c->cfg.putbit; qc.user = c->cfg.user;
        p34a_init(&c->pa, &qc);
        c->v90_starts++;
        c->stage = VCL_V90;
    }
}

int v92call_data(const v92call_t *c)
{
    if (c->hung) return 0;
    if (c->stage == VCL_PCM) return c->va.tx_ready && c->va.rx_ready && !c->va.in_reneg;
    if (c->stage == VCL_V90) return c->pa.tx_ready && c->pa.rx_ready;
    return 0;
}

void v92call_retrain(v92call_t *c)
{
    if (c->stage != VCL_PCM && c->stage != VCL_V90) return;
    c->retrains++; c->retraining = 1; c->rt_n = 0;
    c->ctx = 0;
    c->stage = VCL_RTSIL;
}

int v92call_renegotiate(v92call_t *c, int cleardown)
{
    if (c->stage == VCL_PCM) return v92a_renegotiate(&c->va, 0, cleardown);
    if (c->stage == VCL_V90) return p34a_renegotiate(&c->pa, 0, cleardown);
    return -1;
}

void v92call_hangup(v92call_t *c) { c->hung = 1; c->ctx = 0; }

static uint8_t tick(v92call_t *c, uint8_t ds_octet);

/* the octet a G.711 A/D gives, and next to it (rx_lin) what the network
 * hands over: that octet's value, or with a linear A/D the sample itself */
uint8_t v92call_tick(v92call_t *c, uint8_t ds_octet)
{
    uint8_t o = tick(c, ds_octet);
    if (!c->cfg.rx_linear) c->rx_lin = (int16_t)v90_octet_linear(c->cfg.alaw, o);
    return o;
}

int16_t v92call_tick_lin(v92call_t *c, uint8_t ds_octet)
{
    (void)v92call_tick(c, ds_octet);
    return c->rx_lin;
}

static uint8_t tick(v92call_t *c, uint8_t ds_octet)
{
    int alaw = c->cfg.alaw;
    double crx; uint8_t srv; int16_t x, o = 0;
    uint8_t late;

    c->n++;
    c->dly[c->dpos] = ds_octet;
    late = c->dly[(c->dpos + (int)sizeof c->dly - DLY) % (int)sizeof c->dly];
    c->dpos = (c->dpos + 1) % (int)sizeof c->dly;

    if (c->hung) {
        /* the same line, nobody on it */
        if (c->stage == VCL_PCM) return pcm_encode(alaw, c->rx_lin = v92chan_tick(&c->vch, ds_octet, 0, 0, 0));
        channel_tick(&c->ch, alaw ? linear_to_ulaw((int16_t)v90_octet_linear(1, ds_octet)) : ds_octet, 0, &crx, &srv);
        c->rx_lin = c->ch.us_lin;
        return alaw ? pcm_encode(1, (int16_t)ulaw_to_linear(srv)) : srv;
    }
    if (c->stage == VCL_PCM) {
        double amp = 0, frac = 0;
        int valid = v92a_tick(&c->va, late, &amp, &frac);
        int16_t rx;
        if (c->pcm_starts <= c->cfg.mute_pcm) valid = 0;
        if (!c->sd_seen && c->va.sd_first > 0) {
            c->sd_seen = 1;
            if (c->n_sd < 4) c->sd_at[c->n_sd] = c->p3_n0 + c->va.sd_first - DLY;
            c->n_sd++;
        }
        rx = v92chan_tick(&c->vch, ds_octet, valid, amp, frac);
        c->rx_lin = rx;
        if (c->va.tx_ready && c->va.rx_ready) c->retraining = 0;
        /* 9.7.2.2: Tone B for more than 50 ms asks for a retrain */
        (void)dpskrx_tick(&c->toneb, (int16_t)v90_octet_linear(alaw, ds_octet));
        if (c->toneb.steady >= 400) v92call_retrain(c);
        return pcm_encode(alaw, rx);
    }

    channel_tick(&c->ch, alaw ? linear_to_ulaw((int16_t)v90_octet_linear(1, ds_octet)) : ds_octet, c->ctx, &crx, &srv);
    c->rx_lin = c->ch.us_lin;
    x = (int16_t)lround(crx);
    switch (c->stage) {
    case VCL_V8:
        if (c->qc_on && c->v8.state == V8C_CM && qca_rx_tick(&c->qrx, x)) {
            /* 9.2.1.1: QCA1d, CM stops at once, without completing its octet */
            c->qca1d = c->qrx.oct;
            c->qc_on = 0;
            if (!c->cfg.qc_deaf) {
                c->stage = VCL_QC; c->qs = QS_WAIT; c->qt = 0; ansam_reset(c);
                c->ctx = 0;
                break;
            }
        }
        if (c->cfg.v8bis_ms && c->cfg.v8bis_ms_len > 0) {
            c->bis_out = v8bisc_tick(&c->bis, x);
            c->v8bis_got = v8bisc_got(&c->bis);
        }
        if (v8call_tick(&c->v8, x, &o) == V8C_DONE) {
            if (c->v8.got_jm && v8_selected(&c->v8.jm) == V8_SEL_V90) {
                p2a_cfg pc;
                p2cfg(c, &pc, 0);
                p2a_init(&c->p2, &pc);
                c->stage = VCL_P2;
            } else {
                c->stage = c->v8.got_jm ? VCL_NO_PCM : VCL_FAILED;
            }
        }
        c->ctx = o + c->bis_out;
        break;
    case VCL_QC:
        c->ctx = qc_tick(c, ds_octet, x);
        break;
    case VCL_P2:
        if (c->anspcm_end_at >= 0 && c->phase2s == 0 && c->info0d_at < 0
            && qca_octet_ucode(alaw, ds_octet) != 0) c->info0d_at = c->n;
        c->ctx = p2a_tick(&c->p2, x);
        if (c->p2.state == P2A_DONE) phase3(c);
        else if (c->p2.state == P2A_FAILED) { c->stage = VCL_FAILED; c->ctx = 0; }
        break;
    case VCL_V90:
        c->ctx = p34a_tick(&c->pa, ds_octet);
        if (!c->sd_seen && c->pa.sd_first > 0) {
            c->sd_seen = 1;
            if (c->n_sd < 4) c->sd_at[c->n_sd] = c->p3_n0 + c->pa.sd_first;
            c->n_sd++;
        }
        if (c->pa.tx_ready && c->pa.rx_ready) c->retraining = 0;
        (void)dpskrx_tick(&c->toneb, x);
        if (c->toneb.steady >= 400) v92call_retrain(c);
        break;
    case VCL_RTSIL:
        /* 70 ms of silence, then Tone A and Phase 2 from 9.2.2.1.3/V.90 */
        c->ctx = 0;
        if (++c->rt_n >= 560) {
            p2a_cfg pc;
            p2cfg(c, &pc, 1);
            p2a_init_retrain(&c->p2, &pc);
            c->stage = VCL_P2;
        }
        break;
    default:
        c->ctx = 0;
        break;
    }
    return alaw ? pcm_encode(1, (int16_t)ulaw_to_linear(srv)) : srv;
}
