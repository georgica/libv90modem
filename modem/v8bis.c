/* K56flex's V.8bis exchange as the initiating station. See v8bis.h. */
#include <string.h>
#include "v8bis.h"
#include "sintab.h"

#define MS(ms) ((int32_t)(ms) * 8)
#define DPH(f) ((uint32_t)(((uint64_t)(f) << 32) / 8000))
#define COS(ph) ((int32_t)sintab[(((ph) >> 22) + SIN_N / 4) & SIN_MASK])
#define SIN(ph) ((int32_t)sintab[((ph) >> 22) & SIN_MASK])

/* CRe, 14.6 dB below our messages in RMS (the real server's: k56-facts.md
 * "V.8bis signals"): a message of peak amp has RMS amp / sqrt 2; the dual
 * tone, each tone of peak a, has RMS a; one tone of peak b has b / sqrt 2.
 * a = amp * 10^(-14.6 / 20) / sqrt 2, b = amp * 10^(-14.6 / 20), in Q15. */
#define CRE_DUAL_Q15 4315
#define CRE_TONE_Q15 6102
#define CRE_F1 1375
#define CRE_F2 2002
#define CRE_F3 400      /* CRe's second segment (MRe's is 650 Hz) */

/* the CRd detector's blocks: not silent (RMS above 50, about -50 dBm0) */
#define CRD_MIN_PW  ((int64_t)V8B_CRD_BLOCK * 2500)
#define CRD_MISS    2       /* blocks between the pair and its 1900 Hz */
static const uint32_t crd_dph[3] = { DPH(1529), DPH(2225), DPH(1900) };

/* the receiver's squelch: as v8ans', about -46 dBm0 */
#define SQUELCH_AMP 110

/* NAK(1): type 1000, revision 1 (V.8bis Tables 3 and 4) */
#define NAK1 0x18
#define NAK3 0x1a       /* revision 1, NAK(3) */

void v8bis_crd_init(v8bis_crd_t *d) { memset(d, 0, sizeof *d); }

/* share of the power pw of n samples at the tone of correlation m, F = 2 m /
 * (n pw) (1 for a pure tone): F > num / den */
static int share(int64_t m, int64_t pw, int n, int num, int den) { return m * 2 * den > pw * n * num; }

static int pair_in(const v8bis_crd_t *d, const int64_t *m, int n, int lenient)
{
    if (d->pw <= (int64_t)n * 2500) return 0;
    if (lenient) return share(m[0], d->pw, n, 1, 10) && share(m[1], d->pw, n, 1, 10);
    return share(m[0], d->pw, n, 1, 4) && share(m[1], d->pw, n, 1, 4) && share(m[0] + m[1], d->pw, n, 7, 10);
}

static void corr(const v8bis_crd_t *d, int64_t *m)
{
    int k;
    for (k = 0; k < 3; k++) m[k] = (int64_t)d->re[k] * d->re[k] + (int64_t)d->im[k] * d->im[k];
}

int v8bis_crd_tick(v8bis_crd_t *d, int16_t x)
{
    int64_t m[3];
    int k, pair, f1900, r = V8B_CRD_NONE;
    for (k = 0; k < 3; k++) {
        d->re[k] += (x * COS(d->ph[k])) >> 15;
        d->im[k] += (x * SIN(d->ph[k])) >> 15;
        d->ph[k] += crd_dph[k];
    }
    d->pw += (int32_t)x * x;
    if (++d->n < V8B_CRD_BLOCK) return r;
    corr(d, m);
    pair = pair_in(d, m, V8B_CRD_BLOCK, 0);
    f1900 = d->pw > CRD_MIN_PW && share(m[2], d->pw, V8B_CRD_BLOCK, 1, 2);
    d->pair_now = pair;
    switch (d->phase) {
    case 0:
        d->run = pair ? d->run + 1 : 0;
        if (d->run >= V8B_CRD_HOLD) { d->phase = 1; d->miss = 0; r = V8B_CRD_TONE; }
        break;
    case 1:
        if (pair) d->miss = 0;
        else if (f1900) d->phase = 2;
        else if (++d->miss > CRD_MISS) { d->phase = 0; d->run = 0; }
        break;
    default:
        if (!f1900) { d->phase = 0; d->run = 0; r = V8B_CRD_END; }
        break;
    }
    d->n = 0; d->pw = 0;
    for (k = 0; k < 3; k++) d->re[k] = d->im[k] = 0;
    return r;
}

/* At CRd's deadline: has its pair begun? A run of blocks with it, or the
 * block under way (at least 2 ms of it) mostly the two tones. */
static int crd_begun(const v8bis_crd_t *d)
{
    int64_t m[3];
    if (d->phase > 0 || d->run > 0) return 1;
    if (d->n < 16) return 0;
    corr(d, m);
    return pair_in(d, m, d->n, 1);
}

void v8bis_init(v8bis_t *b, int16_t amp, int ns_form, int vers, int accept_v1)
{
    memset(b, 0, sizeof *b);
    b->state = V8B_SIL;
    b->amp = amp;
    b->a_dual = (int16_t)((amp * CRE_DUAL_Q15) >> 15);
    b->a_tone = (int16_t)((amp * CRE_TONE_Q15) >> 15);
    b->ns_form = ns_form;
    b->vers = vers & V8B_VER_MASK;
    b->accept_v1 = accept_v1 != 0;
    b->ms_ver = -1;
    b->result = V8B_RES_NONE;
}

int v8bis_result(const v8bis_t *b) { return b->result; }

static void go(v8bis_t *b, v8b_state s) { b->state = s; b->t = 0; }

/* a message on V.21 channel 1, from its first bit */
static void send(v8bis_t *b, const uint8_t *o, int n)
{
    b->nbits = v8bis_frame(o, n, b->bits);
    b->pos = 0;
    v21mod_init(&b->mod, V21L_MARK, V21L_SPACE, 8000, 300, b->amp);
}

/* one sample of the message under way; 1 when its last bit is out */
static int send_tick(v8bis_t *b, int16_t *tx)
{
    int done;
    *tx = v21mod_tick(&b->mod, b->bits[b->pos], &done);
    return done && ++b->pos >= b->nbits;
}

static void listen_ms(v8bis_t *b)
{
    v21demod_init(&b->dem, V21H_MARK, V21H_SPACE, 8000, 300);
    b->squelch = v21demod_energy_of(&b->dem, SQUELCH_AMP);
    b->level = 1; b->clk = 0;
    v8bis_rx_init(&b->rx);
}

/* the MS receiver: line level, bit clock from the level's edges (each one
 * puts the decision half a bit later, where the demodulator's window lies
 * inside the bit), the frame receiver. Returns v8bis_rx_bit's result. */
static int listen_tick(v8bis_t *b, int16_t rx)
{
    int level = v21demod_tick(&b->dem, rx);
    if (b->dem.energy < b->squelch) level = 1;
    if (level != b->level) { b->level = level; b->clk = 4000; }
    b->clk += 300;
    if (b->clk < 8000) return 0;
    b->clk -= 8000;
    return v8bis_rx_bit(&b->rx, level, b->msg);
}

v8b_state v8bis_tick(v8bis_t *b, int16_t rx, int16_t *tx)
{
    int r;
    uint8_t o[V8B_MAXOCT];
    *tx = 0;
    b->t++;
    switch (b->state) {
    case V8B_SIL:
        if (b->t >= MS(V8B_SIL_MS)) go(b, V8B_CRE);
        break;
    case V8B_CRE:
        if (b->t <= MS(V8B_CRE_DUAL_MS)) {
            *tx = (int16_t)(((SIN(b->ph1) + SIN(b->ph2)) * b->a_dual) >> 15);
            b->ph1 += DPH(CRE_F1); b->ph2 += DPH(CRE_F2);
            if (b->t == MS(V8B_CRE_DUAL_MS)) b->ph1 = 0;
        } else {
            *tx = (int16_t)((SIN(b->ph1) * b->a_tone) >> 15);
            b->ph1 += DPH(CRE_F3);
        }
        if (b->t >= MS(V8B_CRE_DUAL_MS + V8B_CRE_TONE_MS)) { v8bis_crd_init(&b->crd); go(b, V8B_WAIT_CRD); }
        break;
    case V8B_WAIT_CRD:
        r = v8bis_crd_tick(&b->crd, rx);
        if (r == V8B_CRD_TONE) { go(b, V8B_CRD); break; }
        /* the deadline is for its pair to begin; begun, it is followed until
         * it is held or gone */
        if ((b->t == MS(V8B_CRD_DEADLINE_MS) && !crd_begun(&b->crd))
            || (b->t > MS(V8B_CRD_DEADLINE_MS) && b->crd.n == 0 && b->crd.run == 0)) {
            b->result = V8B_RES_NO_CRD;
            go(b, V8B_DONE);
        }
        break;
    case V8B_CRD:
        r = v8bis_crd_tick(&b->crd, rx);
        if (r == V8B_CRD_END) {
            /* it ended a block ago: the gap counts from there */
            go(b, V8B_GAP_CL); b->t = V8B_CRD_BLOCK;
        } else if (b->crd.phase == 0 || b->t >= MS(V8B_CRD_MAX_MS)) {
            b->result = V8B_RES_NO_CRD;
            go(b, V8B_DONE);
        }
        break;
    case V8B_GAP_CL:
        if (b->t >= MS(V8B_CL_GAP_MS)) {
            send(b, o, v8bis_cl_octets(b->ns_form, b->vers, o));
            b->cl_sent = 1;
            go(b, V8B_CL);
        }
        break;
    case V8B_CL:
        if (send_tick(b, tx)) { listen_ms(b); go(b, V8B_WAIT_MS); }
        break;
    case V8B_WAIT_MS:
        r = listen_tick(b, rx);
        if (r > 0 && (b->msg[0] & 15) == V8B_MSG_MS && v8bis_ms_is_k56(b->msg, r)) {
            b->ms_ver = v8bis_ms_version(b->msg, r);
            /* version 1 alone, offered among others: declined (9.5) unless
             * accept_v1; any other choice, ACK(1) (ruling C6) */
            if ((b->vers & V8B_VERS_1) && b->vers != V8B_VERS_1 && !b->accept_v1
                && v8bis_version_of(b->ms_ver) == 1)
                b->result = V8B_RES_DECLINED;
            else
                b->result = V8B_RES_AGREED;
            go(b, V8B_GAP_ACK);
        } else if (r < 0) {
            b->result = V8B_RES_NO_MS;             /* NAK(1) to an invalid frame (7.2.9, 9.8) */
            go(b, V8B_GAP_ACK);
        } else if (r > 0 || b->t >= MS(V8B_MS_WAIT_MS)) {
            b->result = V8B_RES_NO_MS;             /* another message, or none */
            go(b, V8B_DONE);
        }
        break;
    case V8B_GAP_ACK:
        if (b->t >= MS(V8B_ACK_GAP_MS)) {
            if (b->result == V8B_RES_AGREED) r = v8bis_ack1_octets(o);
            else { o[0] = b->result == V8B_RES_DECLINED ? NAK3 : NAK1; r = 1; }
            send(b, o, r);
            go(b, V8B_ACK);
        }
        break;
    case V8B_ACK:
        if (send_tick(b, tx)) go(b, V8B_DONE);
        break;
    case V8B_DONE:
        break;
    }
    return b->state;
}
