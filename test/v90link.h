/* Test harness for the V.90 analogue modem: a line between it and a digital
 * modem of the library's.
 *
 *   downstream: the digital modem's octets, pulled on the network clock by
 *     sim/dschan (pad, robbed bit, codec and loop filters, noise, our A/D on
 *     its own clock, the hybrid echo of what we send);
 *   upstream: what we send, on our clock, resampled on to the network clock
 *     (windowed sinc), a flat loss, white noise, the network codec (G.711)
 *     -> the digital modem.
 *
 * The digital modem is either the whole answerer (answer_t: V.8 and all) or
 * its V.90 parts alone (p2d, then p34d as answer.c starts it), for tests of
 * the analogue modem's own sequencer that leave V.8 out. The analogue modem
 * is orig_t or v90a_t. Data both ways is a 2^23 - 1 sequence (O.151), checked
 * by the self-synchronising rule b(n) = b(n - 18) XOR b(n - 23), so that a
 * check needs no alignment: each wrong bit fails it three times.
 * Host only. */
#ifndef V90D_TEST_V90LINK_H
#define V90D_TEST_V90LINK_H
#include <string.h>
#include <math.h>
#include "../modem/answer.h"
#include "../modem/orig.h"
#include "../modem/v90a.h"
#include "../modem/p2d.h"
#include "../modem/p34d.h"
#include "../modem/pcmenc.h"
#include "../v90/v90_pcm.h"
#include "../sim/dschan.h"
#include "../sim/resamp.h"
#include "../sim/noise.h"

/* ---- 2^23 - 1 data and its check ---- */
typedef struct { uint32_t gen; uint32_t chk; int64_t sent, got, fails; int armed; } bert_t;
static void bert_init(bert_t *b, uint32_t seed) { memset(b, 0, sizeof *b); b->gen = seed | 1; }
static int bert_get(bert_t *b)
{
    int bit = (int)(((b->gen >> 17) ^ (b->gen >> 22)) & 1);
    b->gen = ((b->gen << 1) | (uint32_t)bit) & 0x7fffff;
    b->sent++;
    return bit;
}
static void bert_put(bert_t *b, int bit)
{
    int want = (int)(((b->chk >> 17) ^ (b->chk >> 22)) & 1);
    b->chk = ((b->chk << 1) | (uint32_t)(bit & 1)) & 0x7fffff;
    if (++b->got > 23 && b->armed && want != (bit & 1)) b->fails++;
}

/* ---- the line ---- */
typedef struct {
    /* downstream (dschan) */
    double loop_km, noise_dbm0, ppm, phase, pad_db, echo_db;
    int rbs, rbs_phase, alaw;
    /* upstream */
    double up_loss_db, up_noise_dbm0;
    /* the digital modem */
    int full;                       /* 1: answer_t, 0: p2d + p34d */
    int max_down, max_up;           /* its ceilings */
    /* the analogue modem */
    int caller_orig;                /* 1: orig_t, 0: v90a_t */
    int caller_level;
    uint32_t seed;
} vl_cfg;

#define VL_FIFO 64
#define VL_PREFILL 8                /* samples in the FIFO before it is read: the two clocks may slip apart */

typedef struct {
    vl_cfg cfg;
    dschan_t ch;
    sresamp_t rs;
    double fifo[VL_FIFO]; int fh, ft, primed;
    rng_t rng;
    /* digital */
    answer_t ans;
    p2d_t p2; p34d_t p34; int dstate;  /* 0 Phase 2, 1 p34d, 2 dead */
    int dmute;                      /* the digital modem's output silenced (a test's doing) */
    /* analogue */
    orig_t org;
    v90a_t va;
    int16_t last_tx, last_rx;       /* what we sent and received in the last tick */
    int64_t k;                      /* our samples */
    int64_t n;                      /* network samples */
    bert_t down, up;
    double up_gain, up_nrms;
} vl_t;

static void vl_put_down(void *u, int bit) { bert_put(&((vl_t *)u)->down, bit); }
static int  vl_get_up(void *u) { return bert_get(&((vl_t *)u)->up); }
static int  vl_get_down(void *u) { return bert_get(&((vl_t *)u)->down); }
static void vl_put_up(void *u, int bit) { bert_put(&((vl_t *)u)->up, bit); }

/* the digital modem's V.90 parts alone: p2d, then p34d as answer.c's start_p34 */
static void vl_start_p34(vl_t *l)
{
    p34d_cfg c;
    int sr = l->p2.info1a.up_sr;
    memset(&c, 0, sizeof c);
    c.alaw = l->cfg.alaw;
    c.uinfo = l->p2.info1a.uinfo; c.md_len = l->p2.info1a.md_len; c.rtd = l->p2.rtd;
    c.echo_delay = -1;
    c.max_up_rate = l->cfg.max_up;
    c.getbit = vl_get_down; c.putbit = vl_put_up; c.user = l;
    if (sr == 3) { c.sym_rate = 3000; c.car_num = 3; c.car_den = 5; }
    else { c.sym_rate = 3200; c.car_num = 4; c.car_den = 7; }
    if (l->cfg.max_down > 0) {
        int r; uint32_t m = 0;
        for (r = 0; r < V90_NUM_RATES; r++) if ((int32_t)(r + 21) * 8000 / 6 <= l->cfg.max_down) m |= 1u << r;
        c.down_rates = m ? m : 1;
    }
    if (p34d_init(&l->p34, &c) != 0) l->dstate = 2; else l->dstate = 1;
}

/* one network sample: the upstream as the network codec has it, in; the
 * digital modem's octet, out */
static uint8_t vl_src(void *ctx)
{
    vl_t *l = (vl_t *)ctx;
    double up = 0;
    uint8_t in, out;
    int alaw = l->cfg.alaw;
    if (!l->primed && (l->fh - l->ft + VL_FIFO) % VL_FIFO >= VL_PREFILL) l->primed = 1;
    if (l->primed && l->fh != l->ft) { up = l->fifo[l->ft]; l->ft = (l->ft + 1) % VL_FIFO; }
    up = up * l->up_gain + l->up_nrms * rng_gauss(&l->rng);
    in = pcm_encode(alaw, (int16_t)(up > 32767 ? 32767 : up < -32768 ? -32768 : floor(up + 0.5)));
    l->n++;
    if (l->cfg.full) out = answer_tick_pcm(&l->ans, in);
    else {
        int16_t x = (int16_t)v90_octet_linear(alaw, in);
        if (l->dstate == 0) {
            out = pcm_encode(alaw, p2d_tick(&l->p2, x));
            if (l->p2.state == P2D_DONE) vl_start_p34(l);
            else if (l->p2.state == P2D_FAILED) l->dstate = 2;
        } else if (l->dstate == 1) out = p34d_tick(&l->p34, x);
        else out = v90_pcm_octet(alaw, 0, 1);
    }
    if (l->dmute) out = v90_pcm_octet(alaw, 0, 1);
    return out;
}

static void vl_init(vl_t *l, const vl_cfg *c)
{
    dschan_cfg dc;
    memset(l, 0, sizeof *l);
    l->cfg = *c;
    rng_seed(&l->rng, c->seed + 101);
    bert_init(&l->down, 0x1234 + c->seed); bert_init(&l->up, 0x4321 + c->seed);
    l->down.armed = l->up.armed = 1;
    memset(&dc, 0, sizeof dc);
    dc.alaw = c->alaw; dc.pad_db = c->pad_db; dc.rbs = c->rbs; dc.rbs_phase = c->rbs_phase;
    dc.loop_km = c->loop_km; dc.level_db = -6; dc.noise_dbm0 = c->noise_dbm0; dc.ppm = c->ppm;
    dc.phase = c->phase; dc.echo_db = c->echo_db; dc.seed = c->seed;
    dschan_init(&l->ch, &dc, vl_src, l);
    /* our samples, on the network clock: out / in = 1 / (1 + ppm) */
    sresamp_init(&l->rs, 1.0 / (1.0 + c->ppm * 1e-6));
    l->up_gain = pow(10, -c->up_loss_db / 20);
    l->up_nrms = 15770.0 * pow(10, c->up_noise_dbm0 / 20);
    if (c->full) {
        answer_cfg ac;
        p2d_cfg pc;
        memset(&ac, 0, sizeof ac);
        ac.level = 3000; ac.modes = V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21 | ANS_MODE_V90;
        ac.getbit = vl_get_down; ac.putbit = vl_put_up; ac.user = l;
        answer_init(&l->ans, &ac);
        pc = l->ans.p2cfg; pc.alaw = c->alaw;
        answer_v90_cfg(&l->ans, &pc);
        answer_v90_line(&l->ans, -1, c->max_down, c->max_up);
    } else {
        p2d_cfg pc;
        memset(&pc, 0, sizeof pc);
        pc.tx_dbm0 = -12; pc.max_tx_code = ANS_MAX_TX_CODE; pc.alaw = c->alaw; pc.up_sr_mask = (1 << 3) | (1 << 4);
        p2d_init(&l->p2, &pc);
    }
    if (c->caller_orig) {
        orig_cfg oc;
        memset(&oc, 0, sizeof oc);
        oc.level = c->caller_level ? c->caller_level : 3000;
        oc.modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21 | ORIG_MODE_V90;
        oc.echo_delay = 0; oc.law = 0;
        oc.getbit = vl_get_up; oc.putbit = vl_put_down; oc.user = l;
        orig_init(&l->org, &oc);
    } else {
        v90a_cfg vc;
        memset(&vc, 0, sizeof vc);
        vc.level = c->caller_level ? c->caller_level : 3000;
        vc.getbit = vl_get_up; vc.putbit = vl_put_down; vc.user = l;
        v90a_init(&l->va, &vc);
    }
}

/* one of our samples */
static void vl_tick(vl_t *l)
{
    int16_t rx = dschan_tick(&l->ch, l->last_tx), tx;
    double o[4];
    int i, n;
    l->last_rx = rx;
    tx = l->cfg.caller_orig ? orig_tick(&l->org, rx) : v90a_tick(&l->va, rx);
    n = sresamp_push(&l->rs, tx, o, 4);
    for (i = 0; i < n; i++) {
        int h = (l->fh + 1) % VL_FIFO;
        if (h != l->ft) { l->fifo[l->fh] = o[i]; l->fh = h; }
    }
    l->last_tx = tx;
    l->k++;
}

#endif
