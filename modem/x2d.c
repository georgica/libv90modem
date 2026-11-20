/* x2 as its server, after the marker. See x2d.h.
 *
 * What the client sends, and when, is docs/history/x2-facts.md ("Phase 3
 * script", "Records", "Upstream"): RE of D-Modem
 * (https://github.com/D-Modem/D-Modem, docs/dsp/x2-details.md) and of
 * courier-emu (https://github.com/cryan209/courier-emu). Facts only; no code
 * of either. The receive parts are p34d's. */
#include <string.h>
#include "x2d.h"
#include "x2rec.h"
#include "v34mp.h"
#include "p2ans.h"
#include "../v90/v90_pcm.h"

#define MS(ms) ((int32_t)(ms) * 8)

#define TRN_KNOWN      512      /* symbols of the client's TRN used as a known reference (as p34d) */
#define MP_WORDS       4
#define MP_BITS        X2REC_BITS(MP_WORDS)
#define E_ONES         20

/* our echo canceller trains on our TRN while the client is quiet: it stops
 * its J some 150 ms after our preamble began ("Phase 3 script") */
#define EC_QUIET_MS    150
#define EC_GUARD_MS    20
#define EC_FAST        3000     /* samples at full NLMS step (as p34d) */

/* time limits: each ends the call well within 10 s of the client's silence */
#define T_J_MS         6000     /* the marker to the client's J */
#define T_S4_MS        3000     /* our J words (X2TX_LEAD_N + PRE + TRN after its J) to its S */
#define T_MP_MS        6000     /* its S-bar to its MP: its TRN is some 1.6 s */
#define T_E_MS         6000     /* our first record to its E */
#define T_B1_MS        3000     /* its E to B1 received and our start segment sent */
#define T_RENEG_MS     6000
#define CD_LOSS_SYMS   1600     /* about half a second without signal in data mode */
#define Q_BAD_SYMS     3200
#define Q_DEAD_MSE     (65536 / 8)  /* -9 dB: nothing is being received */

enum { RX_WAIT_S, RX_KNOWN, RX_J, RX_SRES, RX_P4, RX_DATA, RX_IDLE };

static void end(x2d_t *d, int err)
{
    d->state = X2D_ENDED; d->error = err; d->t = 0;
    d->tx_ready = d->rx_ready = 0;
    d->rxmode = RX_IDLE;
}

static void enter(x2d_t *d, x2d_state s) { d->state = s; d->t = 0; }

int x2d_init(x2d_t *d, const x2d_cfg *cfg)
{
    int idx = (cfg->marker >> 4) & 7;
    memset(d, 0, sizeof *d);
    d->cfg = *cfg;
    if (d->cfg.echo_delay > X2D_MAX_ECHO_DELAY) d->cfg.echo_delay = X2D_MAX_ECHO_DELAY;
    x2tx_init(&d->tx);
    ec_init(&d->ec); d->ec_mu = -1;
    ecdly_init(&d->ecd, d->cfg.echo_delay < 0 ? 0 : d->cfg.echo_delay);
    d->sym_rate = v34_symbol_rate(idx);
    v34_carrier(idx, cfg->marker & 1, &d->car_num, &d->car_den);
    d->rxmode = RX_WAIT_S;
    d->state = X2D_WAIT_J;
    if (d->sym_rate != 3000 && d->sym_rate != 3200) { end(d, X2D_ERR_CFG); return -1; }
    sdet_init(&d->sdet, d->sym_rate, d->car_num, d->car_den);
    d->sdet.div = 16;               /* nothing but S can be on the line */
    qamrx_init(&d->rx, d->sym_rate, d->car_num, d->car_den);
    v34_tdem_init(&d->tdem, V34_GPA, 4);
    return 0;
}

int x2d_down_rate(const x2d_t *d) { return d->state == X2D_DATA || d->state == X2D_RENEG ? d->down_rate : 0; }
int x2d_up_rate(const x2d_t *d) { return d->state == X2D_DATA || d->state == X2D_RENEG ? d->up_rate : 0; }

int x2d_cleardown(x2d_t *d)
{
    if (d->state != X2D_DATA) return -1;
    end(d, X2D_ERR_HANGUP);
    return 0;
}

/* ---- the client's MP ---- */

/* the highest rate of the client's upstream mask (word 1, bit n - 1 for
 * n x 2400 bit/s; at most 28800, "Upstream") that we can receive */
static int pick_up_rate(x2d_t *d, uint16_t w1)
{
    int n;
    for (n = 12; n >= 1; n--) {
        v34_cfg_t c;
        if (!((w1 >> (n - 1)) & 1)) continue;
        if (d->cfg.max_up_rate && n * 2400 > d->cfg.max_up_rate) continue;
        memset(&c, 0, sizeof c);
        c.symbol_rate = (uint16_t)d->sym_rate; c.data_rate = (uint16_t)(n * 2400);
        c.trellis = V34_TRELLIS_64; c.expanded = 1; c.nonlinear = 1; c.scrambler = V34_GPA;
        if (v34_params_init(&d->vpar, &c) == 0) return n * 2400;     /* (set again at E) */
    }
    return 0;
}

/* an MP that checks out; taken when the next one says the same (TRN's
 * decisions make a record now and then) */
static void on_mp(x2d_t *d, const uint16_t *w)
{
    int k, same = d->cand_ok;
    for (k = 0; k < MP_WORDS; k++) {
        uint16_t a = (uint16_t)(k == 0 ? w[k] & 0x7fff : w[k]), b = (uint16_t)(k == 0 ? d->cand[k] & 0x7fff : d->cand[k]);
        if (a != b) same = 0;
        d->cand[k] = w[k];
    }
    d->cand_ok = 1;
    if (!same || d->got_mp) return;
    memcpy(d->mp, w, sizeof d->mp);
    d->index = x2_mp_rate(w[0]);
    d->pad_mode = x2_mp_pad(w[0]);
    /* the exact index asked for, and only one we have a bank for ("Rate
     * change": a client given another rate's levels hangs up) */
    if (d->index > 15 || !((X2D_RATE_MASK >> d->index) & 1)) { end(d, X2D_ERR_RATE); return; }
    if (d->pad_mode > 2) { end(d, X2D_ERR_PAD); return; }
    d->up_rate = pick_up_rate(d, w[1]);
    if (!d->up_rate) { end(d, X2D_ERR_UP); return; }
    d->got_mp = 1;
    x2tx_data(&d->tx, d->index, d->pad_mode, d->cfg.getbit, d->cfg.user);
    x2tx_go(&d->tx);                            /* our record after the sync frames */
}

/* ---- upstream data mode ---- */

static void put_data(void *user, int bit)
{
    x2d_t *d = (x2d_t *)user;
    /* from B1 on (as p34d), while our start segment may still be going down */
    if (d->rx_ready && d->cfg.putbit) d->cfg.putbit(d->cfg.user, bit);
}

/* E has just ended: B1 starts with the next symbol */
static void rx_begin_data(x2d_t *d)
{
    v34_cfg_t c;
    memset(&c, 0, sizeof c);
    c.symbol_rate = (uint16_t)d->sym_rate; c.data_rate = (uint16_t)d->up_rate;
    c.trellis = V34_TRELLIS_64; c.expanded = 1; c.nonlinear = 1; c.scrambler = V34_GPA;
    if (v34_params_init(&d->vpar, &c) != 0) { end(d, X2D_ERR_UP); return; }
    v34_rx_init(&d->vrx, &d->vpar, put_data, d);
    d->vgain = (int32_t)v34_gain_q14(d->vpar.energy_tx_q8);
    d->rxmode = RX_DATA;
    d->quiet = 0; d->q_dead = 0;
    d->t_mark = d->now;
    sdet_init(&d->sdet, d->sym_rate, d->car_num, d->car_den);     /* watches for S from here on */
}

/* ---- descrambled bits ---- */

static uint16_t word16(const uint8_t *b)
{
    uint16_t w = 0; int i;
    for (i = 0; i < 16; i++) w |= (uint16_t)(b[i] << i);
    return w;
}

/* Phase 3: J (V.34's, either kind) twice in a row */
static void j_bit(x2d_t *d, int bit)
{
    int ty;
    d->jsr = (uint16_t)((d->jsr >> 1) | (bit << 15)); d->jbits++;
    ty = d->jsr == word16(v34_j4) ? 1 : d->jsr == word16(v34_j16) ? 2 : 0;
    if (!ty) return;
    if (ty == d->j_type && d->jbits - d->j_at == 16) {
        /* our training from the next octet; the client's Phase 4 begins with
         * S after a silence */
        x2tx_start(&d->tx);
        sres_init(&d->sres);
        d->rxmode = RX_SRES;
        d->t_mark = d->now;
        enter(d, X2D_TRAIN);
        return;
    }
    d->j_type = ty; d->j_at = d->jbits;
}

/* Phase 4: records (17 ones, a zero, ...) and E (20 ones) */
static void seq_bit(x2d_t *d, int bit)
{
    if (d->fn > 0) {
        d->fbuf[d->fn++] = (uint8_t)bit;
        if (d->fn == MP_BITS) {
            uint16_t w[MP_WORDS];
            if (x2rec_parse(d->fbuf, d->fn, w, MP_WORDS) == MP_WORDS) on_mp(d, w);
            d->fn = 0;
            if (d->state == X2D_ENDED) return;
        }
    }
    if (bit) {
        /* inside a record a zero breaks every run at 16 */
        if (++d->ones == E_ONES && d->got_mp && !d->got_e) {
            d->got_e = 1;
            rx_begin_data(d);
        }
        return;
    }
    if (d->ones >= 17) {
        memset(d->fbuf, 1, 17); d->fbuf[17] = 0; d->fn = 18;
    }
    d->ones = 0;
}

/* ---- one received symbol ---- */

static int16_t clip16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static void rx_symbol(x2d_t *d)
{
    qamrx_t *r = &d->rx;
    int32_t dr, di;
    int n = r->nsym - 1, k;

    d->zpw += (((int64_t)r->zr * r->zr + (int64_t)r->zi * r->zi) - d->zpw) / 16;
    switch (d->rxmode) {
    case RX_KNOWN:
        if (n < V34_PP_LEN) v34_pp(n, &dr, &di); else v34trn_next(&d->ref, &dr, &di);
        qamrx_update(r, dr, di, QRX_EQ | QRX_CARRIER | (n > 300 ? QRX_TIMING : 0), n < 600 ? 10 : 11);
        if (n >= V34_PP_LEN + TRN_KNOWN - 1) {
            v34_tdem_init(&d->tdem, V34_GPA, 4);
            d->jsr = 0; d->jbits = d->j_at = 0; d->j_type = 0;
            d->rxmode = RX_J;
        }
        break;

    case RX_J: {
        /* 4-point decisions: TRN, then J */
        v34_pt_t pt; unsigned bits;
        dr = r->zr < 0 ? -QRX_REF : QRX_REF; di = r->zi < 0 ? -QRX_REF : QRX_REF;
        qamrx_update(r, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING, 12);
        pt.re = (int16_t)(dr < 0 ? -V34_ONE : V34_ONE); pt.im = (int16_t)(di < 0 ? -V34_ONE : V34_ONE);
        bits = v34_tdem_put(&d->tdem, pt);
        for (k = 0; k < 2 && d->rxmode == RX_J; k++) j_bit(d, (int)((bits >> k) & 1));
        break;
    }

    case RX_SRES:
        if (sres_symbol(&d->sres, r)) {
            /* TRN (4 points, then 16), the MP and E on 16 points (the J
             * words: our disassembly's "0343 must equal 09B0", courier-emu);
             * the descrambler finds its way within 23 bits */
            v34_tdem_init(&d->tdem, V34_GPA, 16);
            d->fn = d->ones = 0;
            d->rxmode = RX_P4;
            d->t_mark = d->now;
        }
        break;

    case RX_P4: {
        /* 16-point decisions. The 4-point part of TRN lands between them
         * (its points are the 16-point signal's power, not its inner ring):
         * the equaliser learns only from decisions close to a point */
        v34_pt_t pt; unsigned bits;
        int32_t lev = (int32_t)(((int64_t)QRX_REF * v34_gain_q14(V34_ENERGY_16PT_Q8)) >> 14);
        int qr = r->zr >= 2 * lev ? 3 : r->zr >= 0 ? 1 : r->zr >= -2 * lev ? -1 : -3;
        int qi = r->zi >= 2 * lev ? 3 : r->zi >= 0 ? 1 : r->zi >= -2 * lev ? -1 : -3;
        int32_t er, ei;
        dr = qr * lev; di = qi * lev;
        er = r->zr - dr; ei = r->zi - di;
        if (er < 0) er = -er;
        if (ei < 0) ei = -ei;
        qamrx_update(r, dr, di, (er < lev / 2 && ei < lev / 2 ? QRX_EQ : 0) | QRX_CARRIER | QRX_TIMING | QRX_DENSE, 12);
        pt.re = (int16_t)(qr * V34_ONE); pt.im = (int16_t)(qi * V34_ONE);
        bits = v34_tdem_put(&d->tdem, pt);
        for (k = 0; k < 4 && d->rxmode == RX_P4; k++) seq_bit(d, (int)((bits >> k) & 1));
        break;
    }

    case RX_DATA: {
        v34_pt_t in, tent, sent;
        int64_t g = d->vgain;
        in.re = clip16((int32_t)(((int64_t)r->zr * 512) / g)); in.im = clip16((int32_t)(((int64_t)r->zi * 512) / g));
        in = v34_nl_decode(&d->vpar, in);
        v34_rx_put(&d->vrx, in, &tent);
        sent = v34_nl_encode(&d->vpar, tent);
        dr = (int32_t)(((int64_t)sent.re * g) / 512); di = (int32_t)(((int64_t)sent.im * g) / 512);
        qamrx_update(r, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING | QRX_DENSE, 13);
        if (!d->rx_ready && d->vrx.b1_bits >= d->vpar.map.N) {
            if (d->vrx.b1_errors * 8 > d->vrx.b1_bits) { end(d, X2D_ERR_B1); return; }
            d->rx_ready = 1;
        }
        if (d->rx_ready && d->state == X2D_DATA) {
            /* nothing is being received: there is no retrain in x2 */
            if (r->mse > Q_DEAD_MSE) { if (++d->q_dead > Q_BAD_SYMS) { end(d, X2D_ERR_CARRIER); return; } }
            else d->q_dead = 0;
        }
        if (d->zpw < (int64_t)QRX_REF * QRX_REF / 8) {
            if (++d->quiet > CD_LOSS_SYMS) end(d, X2D_ERR_CARRIER);
        } else {
            d->quiet = 0;
        }
        break;
    }

    default:    /* RX_IDLE: keep the grid running, change nothing */
        qamrx_update(r, r->zr, r->zi, 0, 12);
        break;
    }
}

/* the echo canceller on our TRN: the client is quiet from some 150 ms into
 * our preamble; first the delay (when not known), then the canceller */
static void ec_schedule(x2d_t *d, int16_t rxs)
{
    int32_t n = d->tx.n, from = MS(EC_QUIET_MS) + d->cfg.rtd + MS(EC_GUARD_MS);
    if (d->cfg.echo_delay < 0) {
        if (ecdly_measure(&d->ecd, n - from, rxs, 1)) ec_init(&d->ec);
        from += ECDLY_SAMPLES;
    }
    if (n < from) d->ec_mu = -1;
    else if (n < from + EC_FAST) d->ec_mu = 0;
    else d->ec_mu = 3;
}

/* the waits of the client's Phase 4 */
static int p4_late(const x2d_t *d)
{
    if (d->rxmode == RX_SRES)
        return d->now - d->t_mark > X2TX_LEAD_N + X2TX_PRE_N + X2TX_TRN_N + MS(T_S4_MS) + 2 * d->cfg.rtd;
    return !d->got_mp && d->now - d->t_mark > MS(T_MP_MS);
}

uint8_t x2d_tick(x2d_t *d, int16_t rxs)
{
    uint8_t octet;
    int16_t x;

    d->now++; d->t++;
    if (d->state == X2D_ENDED) return 0x7f;
    octet = x2tx_tick(&d->tx);
    /* the echo of this octet begins ecd.delay samples from now */
    ecdly_put(&d->ecd, (int16_t)v90_octet_linear(0, octet));
    x = ec_tick(&d->ec, ecdly_ref(&d->ecd, 0), rxs, d->ec_mu);
    ecdly_next(&d->ecd);

    /* receiver */
    if (d->rxmode == RX_WAIT_S) {
        (void)qamrx_tick(&d->rx, x);            /* input level for the AGC */
        if (sdet_tick(&d->sdet, x)) {
            /* PP(0) is centred 16.5 symbols after the S / S-bar boundary,
             * and the event came SDET_DELAY samples after it */
            int32_t dl = (int32_t)(((int64_t)132000 * 256) / d->sym_rate) - (SDET_DELAY(&d->sdet) + 1) * 256;
            qamrx_sync(&d->rx, dl);
            v34trn_init(&d->ref, V34T_GPA);
            d->rxmode = RX_KNOWN;
        }
    } else {
        if (qamrx_tick(&d->rx, x)) rx_symbol(d);
        if (d->state == X2D_ENDED) return 0x7f;
        if (d->rxmode == RX_DATA && d->state == X2D_DATA) {
            /* the client's S in data mode: its rate change (facts "Rate change") */
            (void)sdet_tick(&d->sdet, x);
            if (d->sdet.present >= SDET_PRESENT) { d->rx_ready = 0; enter(d, X2D_RENEG); }
        }
    }

    switch (d->state) {
    case X2D_WAIT_J:
        if (d->t > MS(T_J_MS)) end(d, X2D_ERR_NO_J);
        break;
    case X2D_TRAIN:
        if (d->tx.seg == X2TX_TRN) ec_schedule(d, rxs);
        else if (d->tx.seg > X2TX_TRN) d->ec_mu = -1;
        if (p4_late(d)) end(d, d->rxmode == RX_SRES ? X2D_ERR_NO_S4 : X2D_ERR_NO_MP);
        else if (d->tx.seg >= X2TX_SYNC) enter(d, X2D_MP);
        break;
    case X2D_MP:
        if (p4_late(d)) end(d, d->rxmode == RX_SRES ? X2D_ERR_NO_S4 : X2D_ERR_NO_MP);
        else if (d->tx.seg == X2TX_REC) { d->t_mark = d->now; enter(d, X2D_REC); }
        break;
    case X2D_REC:
        if (d->got_e && !d->e_sent) { x2tx_go(&d->tx); d->e_sent = 1; }   /* the start after this record */
        if (d->tx.seg == X2TX_START) enter(d, X2D_START);
        else if (!d->got_e && d->now - d->t_mark > MS(T_E_MS)) end(d, X2D_ERR_NO_E);
        break;
    case X2D_START:
        if (d->tx.seg == X2TX_DATA) d->tx_ready = 1;
        if (d->tx_ready && d->rx_ready) {
            d->down_rate = x2tx_rate(&d->tx);
            enter(d, X2D_DATA);
        } else if (d->now - d->t_mark > MS(T_B1_MS)) {
            end(d, X2D_ERR_B1);
        }
        break;
    case X2D_RENEG:
        if (d->t > MS(T_RENEG_MS)) end(d, X2D_ERR_RENEG);
        break;
    default:
        break;
    }
    return octet;
}
