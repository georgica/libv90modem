/* Test peer: a K56flex client. See k56call.h. Written from
 * docs/history/k56-facts.md; no code of D-Modem. */
#include <math.h>
#include <string.h>
#include "k56call.h"
#include "g711.h"
#include "../modem/v8msg.h"

#define MS(ms) ((int32_t)(ms) * 8)
#define TWO_PI 6.283185307179586
#define BLOCK 80

/* the responder's steps */
enum { B_CRE, B_CRE_IN, B_CRD, B_CL, B_MS, B_ACK, B_OFF };

/* SM56's MS (k56-facts.md "V.8bis messages") */
static const uint8_t sm56_ms[13] = { 0x11, 0xc9, 0x80, 0x80, 0x80, 0x07, 0xb5, 0x02, 0x00, 0x94, 0x81, 0x83, 0x21 };

void k56call_default(k56call_cfg *cfg, int level)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->level = level;
    cfg->v8bis = K56C_V8BIS_FULL;
    cfg->info1c_k56 = 1;
}

void k56call_init(k56call_t *c, const k56call_cfg *cfg)
{
    v8_menu_t cm;
    memset(c, 0, sizeof *c);
    c->cfg = *cfg;
    c->state = K56C_V8BIS;
    c->bst = cfg->v8bis == K56C_V8BIS_NONE ? B_OFF : B_CRE;
    c->crd_at = -1; c->ms_at = -1; c->v8_sel = -1;
    /* the CM of v34peer's own V.8; with cm_pcm, an analogue modem's PCM octet too */
    memset(&cm, 0, sizeof cm);
    cm.call_function = V8_CALLF_DATA;
    cm.modes = V8_MOD_V34;
    cm.n_mod_octets = 3;
    if (cfg->cm_pcm) { cm.has_pcm = 1; cm.pcm = V8_PCM_ANALOG; cm.has_access = 1; }
    v8call_init(&c->v8, 8000, (int16_t)cfg->level, &cm);
}

k56c_state k56call_state(const k56call_t *c) { return c->state; }

static void listen_ch1(k56call_t *c)
{
    v21demod_init(&c->dem, V21L_MARK, V21L_SPACE, 8000, 300);
    c->squelch = v21demod_energy_of(&c->dem, 110);
    c->level = 1; c->clk = 0;
    v8bis_rx_init(&c->rx);
}

/* V.21 channel 1: a frame's octet count when one ends, -1 an invalid one, 0 */
static int listen_tick(k56call_t *c, int16_t x)
{
    int level = v21demod_tick(&c->dem, x);
    if (c->dem.energy < c->squelch) level = 1;
    if (level != c->level) { c->level = level; c->clk = 4000; }
    c->clk += 300;
    if (c->clk < 8000) return 0;
    c->clk -= 8000;
    return v8bis_rx_bit(&c->rx, level, c->msg);
}

/* our CL's NS block, after the S field's data Par(2) block if there is one */
static int cl_ok(const uint8_t *o, int n)
{
    int i = 5;
    if (n < 6 || (o[0] & 15) != V8B_MSG_CL) return 0;
    if (o[4] & 1) { while (i < n && !(o[i] & 0x40)) i++; i++; }
    return i < n && v8bis_cl_is_k56(o + i, n - i);
}

/* the initiator's dual tone (1375 + 2002 Hz) in 10 ms blocks; 1 at a
 * block's end with it, 0 without, -1 within a block. quiet: the block's
 * RMS was below 50. */
static int dual_block(k56call_t *c, int16_t x, int *quiet)
{
    static const double f[2] = { 1375, 2002 };
    int k, on = 1;
    for (k = 0; k < 2; k++) {
        c->gr[k] += x * cos(c->gph[k]); c->gi[k] += x * sin(c->gph[k]);
        c->gph[k] += TWO_PI * f[k] / 8000;
        if (c->gph[k] > TWO_PI) c->gph[k] -= TWO_PI;
    }
    c->gpw += (double)x * x;
    if (++c->gn < BLOCK) return -1;
    /* a tone's share of the block's power: 2 |sum|^2 / (N pw) */
    for (k = 0; k < 2; k++) {
        double sh = 2 * (c->gr[k] * c->gr[k] + c->gi[k] * c->gi[k]) / (BLOCK * (c->gpw + 1));
        if (sh < 0.25) on = 0;
        c->gr[k] = c->gi[k] = 0;
    }
    *quiet = c->gpw < (double)BLOCK * 2500;
    if (*quiet) on = 0;
    c->gpw = 0; c->gn = 0;
    return on;
}

static int16_t responder(k56call_t *c, int16_t x)
{
    int r, quiet = 0, done;
    double y = 0;
    switch (c->bst) {
    case B_CRE:
        r = dual_block(c, x, &quiet);
        if (r >= 0) c->gblocks = r ? c->gblocks + 1 : 0;
        if (c->gblocks >= 10) { c->got_cre = 1; c->bst = B_CRE_IN; }
        break;
    case B_CRE_IN:
        /* its second segment over: CRd 175 ms later (SM56's) */
        if (dual_block(c, x, &quiet) >= 0 && quiet) { c->crd_at = c->now + MS(175); c->bst = B_CRD; }
        break;
    case B_CRD:
        if (c->now >= c->crd_at) {
            int32_t k = c->now - c->crd_at;
            if (k < MS(400)) {
                y = c->cfg.level * (sin(c->ph[0]) + sin(c->ph[1]));
                c->ph[0] += TWO_PI * 1529 / 8000; c->ph[1] += TWO_PI * 2225 / 8000;
            } else if (k < MS(500)) {
                y = c->cfg.level * 1.41421356 * sin(c->ph[2]);
                c->ph[2] += TWO_PI * 1900 / 8000;
            } else {
                listen_ch1(c); c->bst = B_CL;
            }
        }
        break;
    case B_CL:
        r = listen_tick(c, x);
        if (r > 0 && (c->msg[0] & 15) == V8B_MSG_CL) {
            c->got_cl = 1; c->cl_ok = cl_ok(c->msg, r);
            listen_ch1(c);
            if (c->cfg.v8bis == K56C_V8BIS_NO_MS || !c->cl_ok) { c->bst = B_ACK; break; }
            c->nbits = v8bis_frame(sm56_ms, 13, c->bits);
            /* a bad FCS: octet 0's first bit (a one) sent as a zero */
            if (c->cfg.v8bis == K56C_V8BIS_BAD_FCS) c->bits[32 + 24] = 0;
            c->pos = 0; c->ms_at = c->now + MS(10);
            v21mod_init(&c->mod, V21H_MARK, V21H_SPACE, 8000, 300, (int16_t)(c->cfg.level * 181 / 128));
            c->bst = B_MS;
        }
        break;
    case B_MS:
        if (c->now < c->ms_at) break;
        y = v21mod_tick(&c->mod, c->bits[c->pos], &done);
        if (done && ++c->pos >= c->nbits) { c->sent_ms = 1; c->bst = B_ACK; }
        break;
    case B_ACK:
        r = listen_tick(c, x);
        if (r == 1 && c->msg[0] == 0x14) c->got_ack = 1;
        if (r == 1 && c->msg[0] == 0x18) c->got_nak = 1;
        if (r != 0) c->bst = B_OFF;
        break;
    default:
        break;
    }
    return (int16_t)lround(y);
}

int16_t k56call_tick(k56call_t *c, uint8_t octet)
{
    int16_t x = (int16_t)ulaw_to_linear(octet), out = 0, v = 0;
    int32_t s;
    c->now++;
    switch (c->state) {
    case K56C_V8BIS:
    case K56C_V8:
        if (c->bst != B_OFF) out = responder(c, x);
        if (v8call_tick(&c->v8, x, &v) != V8C_WAIT) c->state = K56C_V8;
        if (c->v8.state == V8C_DONE) {
            c->v8_sel = v8_selected(&c->v8.jm);
            if (c->v8_sel == V8_SEL_V34) {
                /* as v34peer after its own V.8: Phase 2 from the next sample */
                v34peer_cfg pc;
                v34peer_default(&pc, c->cfg.level);
                pc.skip_v8 = 1;
                pc.dx.getbit = c->cfg.getbit; pc.dx.putbit = c->cfg.putbit; pc.dx.user = c->cfg.user;
                if (c->cfg.up_sr_3000) pc.p2.info0c.lo3200 = pc.p2.info0c.hi3200 = 0;
                if (c->got_ack) {               /* agreed in V.8bis */
                    pc.k56 = 1;
                    pc.p2.k56 = c->cfg.info1c_k56 ? P2C_K56_ACCEPT : P2C_K56_DECLINE;
                }
                v34peer_init(&c->peer, &pc);
                c->state = K56C_V34;
            } else {
                c->state = K56C_IDLE;
            }
        }
        break;
    case K56C_V34:
        v = v34peer_tick(&c->peer, x);
        if (c->peer.state == VP_ENDED) c->state = K56C_ENDED;
        break;
    default:
        break;
    }
    s = (int32_t)out + v;
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}
