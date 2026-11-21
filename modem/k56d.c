/* K56flex as its server, after our flagged INFO1a. See k56d.h.
 *
 * What the client sends, and when, is docs/history/k56-facts.md ("Upstream
 * and J24", "Phase 4"): D-Modem's reading of Motorola's SM56
 * (https://github.com/D-Modem/D-Modem at c06a380d, its notes on K56flex).
 * Facts only; no code of D-Modem. The transmit side is k56tx and k56enc;
 * the receive parts are x2d's and v34dx's (x2d itself is not changed): the
 * client is V.34's call modem, so its scrambler is GPC. */
#include <string.h>
#ifdef K56D_TRACE
#include <stdio.h>
#define TR(...) fprintf(stderr, __VA_ARGS__)
#else
#define TR(...) ((void)0)
#endif
#include "k56d.h"
#include "p2ans.h"
#include "../v90/v90_pcm.h"

#define MS(ms) ((int32_t)(ms) * 8)

#define SYM_RATE       3200     /* the upstream's ("Phase 2"; p2ans flags INFO1a at 3200 only) */
#define TRN_KNOWN      512      /* symbols of the client's TRN used as a known reference (as p34d) */
#define E_ONES         20
#define MP_MASK        0x3fff   /* our MP's rate mask ("Phase 4": D-Modem's server's) */

/* our echo canceller trains on our TRN1A to TRN1C, while the client is
 * silent ("Upstream and J24": until it has our Js) */
#define EC_SETTLE      80       /* samples of TRN1A before the canceller looks at it */
#define EC_FAST        3000     /* samples at full NLMS step (as p34d) */

/* data mode: the watchdog of p34d (and x2d) */
#define CD_LOSS_SYMS   1600     /* about half a second without signal */
#define Q_BAD_SYMS     3200
#define Q_DEAD_MSE     (65536 / 8)  /* -9 dB: nothing is being received */

enum { RX_OFF, RX_WAIT_S, RX_KNOWN, RX_J, RX_P4, RX_DATA, RX_IDLE };

static void end(k56d_t *d, int err)
{
    TR("K56D %.3f end %d (state %d, rxmode %d)\n", d->now / 8000.0, err, (int)d->state, d->rxmode);
    d->state = K56D_ENDED; d->error = err; d->t = 0;
    d->tx_ready = d->rx_ready = 0;
    d->rxmode = RX_IDLE;
}

static void enter(k56d_t *d, k56d_state s) { d->state = s; d->t = 0; }

int k56d_init(k56d_t *d, const k56d_cfg *cfg)
{
    memset(d, 0, sizeof *d);
    d->cfg = *cfg;
    if (d->cfg.echo_delay > K56D_MAX_ECHO_DELAY) d->cfg.echo_delay = K56D_MAX_ECHO_DELAY;
    k56tx_init(&d->tx);
    ec_init(&d->ec); d->ec_mu = -1;
    ecdly_init(&d->ecd, d->cfg.echo_delay < 0 ? 0 : d->cfg.echo_delay);
    v34_carrier(4, cfg->hi_carrier != 0, &d->car_num, &d->car_den);
    sdet_init(&d->sdet, SYM_RATE, d->car_num, d->car_den);
    d->sdet.div = 16;               /* nothing but S can be on the line */
    qamrx_init(&d->rx, SYM_RATE, d->car_num, d->car_den);
    d->rxmode = RX_OFF;
    d->t_mark = -1;
    d->state = K56D_TRAIN1;
    if (d->cfg.rtd < 0 || d->cfg.max_up_rate < 0 || d->cfg.max_down_rate < 0) { end(d, K56D_ERR_CFG); return -1; }
    return 0;
}

int k56d_down_rate(const k56d_t *d) { return d->state == K56D_DATA || d->state == K56D_RENEG ? d->down_rate : 0; }
int k56d_up_rate(const k56d_t *d) { return d->state == K56D_DATA || d->state == K56D_RENEG ? d->up_rate : 0; }
uint8_t k56d_rbs(const k56d_t *d) { return d->rbs; }
int k56d_pad(const k56d_t *d) { return d->pad; }

int k56d_cleardown(k56d_t *d)
{
    if (d->state != K56D_DATA && d->state != K56D_RENEG) return -1;
    end(d, K56D_ERR_HANGUP);
    return 0;
}

/* ---- the client's MP ---- */

/* our V.34 receiver at 3200 baud, with what our MP asks of the client's
 * transmitter (decision 9): 16 states, minimum shaping, linear; GPC */
static int up_params(k56d_t *d, int rate)
{
    v34_cfg_t c;
    memset(&c, 0, sizeof c);
    c.symbol_rate = SYM_RATE; c.data_rate = (uint16_t)rate;
    c.trellis = V34_TRELLIS_16; c.expanded = 0; c.nonlinear = 0; c.scrambler = V34_GPC;
    return v34_params_init(&d->vpar, &c);
}

/* the highest rate of the client's mask (bit n - 1 for n x 2400 bit/s), at
 * most its rate_ca and our max_up_rate, that we receive */
static int pick_up_rate(k56d_t *d, const v34_mp_t *m)
{
    int n;
    for (n = 14; n >= 1; n--) {
        if (!((m->rates >> (n - 1)) & 1) || n > m->max_ca) continue;
        if (d->cfg.max_up_rate && n * 2400 > d->cfg.max_up_rate) continue;
        if (up_params(d, n * 2400) == 0) return n * 2400;   /* (set again at its E) */
    }
    return 0;
}

/* the downstream rate code: the client's, our ceiling's, 13 at most */
static int pick_rate(const k56d_t *d, int asked)
{
    int r = asked > 13 ? 13 : asked;
    if (d->cfg.max_down_rate > 0) {
        int cap = (d->cfg.max_down_rate - 30000) / 2000;
        if (cap < 1) cap = 1;           /* below 32000: 32000, the lowest there is */
        if (r > cap) r = cap;
    }
    return r;
}

/* an MP that checks out, twice in a row (MP and MP' differ in the
 * acknowledge bit only): what TRN looks like through the differential
 * decoder passes the CRC once in 65536 tries (as v34dx) */
static void on_mp(k56d_t *d, const v34_mp_t *m)
{
    v34_mp_t a = *m, b = d->cand;
    a.ack = b.ack = 0;
    if (!d->cand_ok || memcmp(&a, &b, sizeof a) != 0) {
        d->cand = *m; d->cand_ok = 1;
        return;
    }
    if (d->got_mp) return;
    TR("K56D %.3f MP rate_ca %d rate_ac %d mask %04X\n", d->now / 8000.0, m->max_ca, m->max_ac, m->rates);
    d->far_mp = *m;
    /* the client clears down unless all three are non-zero ("Phase 4");
     * we cannot go on either */
    if (!m->max_ac || !m->rates) { end(d, K56D_ERR_RATE); return; }
    d->up_rate = pick_up_rate(d, m);
    if (!d->up_rate) { end(d, K56D_ERR_RATE); return; }
    d->rate = pick_rate(d, m->max_ac);
    d->got_mp = 1;
    memset(&d->mp, 0, sizeof d->mp);
    d->mp.type = 0;
    d->mp.max_ca = (uint8_t)(d->up_rate / 2400);
    d->mp.max_ac = (uint8_t)d->rate;
    d->mp.trellis = V34_TRELLIS_16; d->mp.nonlinear = 0; d->mp.shaping = 0;
    d->mp.rates = MP_MASK;
    d->mp.asym = 1;
    k56tx_data(&d->tx, d->rate, d->pad, d->rbs, d->cfg.getbit, d->cfg.user);
    k56tx_set_mp(&d->tx, &d->mp);
}

/* ---- upstream data mode ---- */

static void put_data(void *user, int bit)
{
    k56d_t *d = (k56d_t *)user;
    if (d->rx_ready && d->cfg.putbit) d->cfg.putbit(d->cfg.user, bit);
}

/* its E has just ended: B1 starts with the next symbol */
static void rx_begin_data(k56d_t *d)
{
    if (up_params(d, d->up_rate) != 0) { end(d, K56D_ERR_RATE); return; }
    v34_rx_init(&d->vrx, &d->vpar, put_data, d);
    d->vgain = (int32_t)v34_gain_q14(d->vpar.energy_tx_q8);
    d->rxmode = RX_DATA;
    d->quiet = 0; d->q_dead = 0;
    d->t_mark = d->now;
}

/* ---- descrambled bits ---- */

static uint16_t word16(const uint8_t *b)
{
    uint16_t w = 0; int i;
    for (i = 0; i < 16; i++) w |= (uint16_t)(b[i] << i);
    return w;
}

/* Phase 3: the J24, the J word and an octet, round and round: two equal J
 * words 24 bits apart, the octet the 8 bits between them. Then J', 24 bits
 * after the last J word ("Upstream and J24"). */
static void j_bit(k56d_t *d, int bit)
{
    uint16_t w;
    d->jsr = (d->jsr >> 1) | ((uint32_t)bit << 31); d->jbits++;
    w = (uint16_t)(d->jsr >> 16);
    if (d->got_j) {
        if (d->jbits - d->j_at < 24) return;
        if (w == word16(v34_j16)) { d->j_at = d->jbits; return; }
        if (w == word16(v34_jprime) && (d->jbits - d->j_at) % 24 == 0) {
            /* TRN, MP, MP' and E on the 16 points our Js asked for */
            TR("K56D %.3f J'\n", d->now / 8000.0);
            d->got_jp = 1;
            v34_tdem_set_points(&d->tdem, 16);
            d->fn = d->ones = 0;
            d->rxmode = RX_P4;
        }
        return;
    }
    if (w != word16(v34_j16)) return;
    if (d->j_seen && d->jbits - d->j_at == 24) {
        d->j_octet = (uint8_t)(d->jsr >> 8);
        d->got_j = 1;
        TR("K56D %.3f J24 octet %02X\n", d->now / 8000.0, d->j_octet);
    }
    d->j_seen = 1; d->j_at = d->jbits;
}

/* Phase 4: MP and MP' (frame sync 17 ones, a start bit, ...) and E (20 ones) */
static void seq_try(k56d_t *d)
{
    v34_mp_t m;
    memset(&m, 0, sizeof m);
    if (d->fn < V34_MP0_BITS - 3) return;
    if (v34_mp_parse(d->fbuf, d->fn, &m) > 0) { d->fn = 0; on_mp(d, &m); }
}

static void seq_bit(k56d_t *d, int bit)
{
    if (d->fn > 0 && d->fn < K56D_FBUF) d->fbuf[d->fn++] = (uint8_t)bit;
    if (bit) {
        /* inside an MP a start bit breaks every run at 17 */
        if (++d->ones == E_ONES && d->got_mp && !d->got_e) {
            d->got_e = 1;
            TR("K56D %.3f E\n", d->now / 8000.0);
            rx_begin_data(d);
        }
        return;
    }
    if (d->ones >= 17) {
        if (d->fn > 18) { d->fn -= 18; seq_try(d); if (d->state == K56D_ENDED) return; }
        memset(d->fbuf, 1, 17); d->fbuf[17] = 0; d->fn = 18;
    } else if (d->fn > 0 && (d->fn % 17) == 0) {
        seq_try(d);
        if (d->state == K56D_ENDED) return;
    }
    d->ones = 0;
    if (d->fn >= K56D_FBUF) d->fn = 0;
}

/* ---- one received symbol ---- */

static int16_t clip16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static void rx_symbol(k56d_t *d)
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
            v34_tdem_init(&d->tdem, V34_GPC, 4);
            d->jsr = 0; d->jbits = d->j_at = 0; d->j_seen = 0;
            d->rxmode = RX_J;
        }
        break;

    case RX_J: {
        /* 4-point decisions: TRN, J24, J' */
        v34_pt_t pt; unsigned bits;
        dr = r->zr < 0 ? -QRX_REF : QRX_REF; di = r->zi < 0 ? -QRX_REF : QRX_REF;
        qamrx_update(r, dr, di, QRX_EQ | QRX_CARRIER | QRX_TIMING, 12);
        pt.re = (int16_t)(dr < 0 ? -V34_ONE : V34_ONE); pt.im = (int16_t)(di < 0 ? -V34_ONE : V34_ONE);
        bits = v34_tdem_put(&d->tdem, pt);
        for (k = 0; k < 2 && d->rxmode == RX_J; k++) j_bit(d, (int)((bits >> k) & 1));
        break;
    }

    case RX_P4: {
        /* 16-point decisions (as x2d): the equaliser learns only from
         * decisions close to a point */
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
            TR("K56D %.3f B1 %d of %d bits wrong\n", d->now / 8000.0, (int)d->vrx.b1_errors, (int)d->vrx.b1_bits);
            if (d->vrx.b1_errors * 8 > d->vrx.b1_bits) { end(d, K56D_ERR_B1); return; }
            d->rx_ready = 1;
        }
        if (d->rx_ready && d->state == K56D_DATA) {
            /* nothing is being received: we never retrain (decision 14) */
            if (r->mse > Q_DEAD_MSE) { if (++d->q_dead > Q_BAD_SYMS) { end(d, K56D_ERR_CARRIER); return; } }
            else d->q_dead = 0;
        }
        if (d->zpw < (int64_t)QRX_REF * QRX_REF / 8) {
            if (++d->quiet > CD_LOSS_SYMS) end(d, K56D_ERR_CARRIER);
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

/* the echo canceller on our TRN1A .. TRN1C, the client silent: first the
 * delay (when not known), then the canceller */
static void ec_schedule(k56d_t *d, int16_t rxs)
{
    int32_t n = ++d->ec_n, from = EC_SETTLE;
    if (d->cfg.echo_delay < 0) {
        if (ecdly_measure(&d->ecd, n - EC_SETTLE, rxs, 1)) ec_init(&d->ec);
        from += ECDLY_SAMPLES;
    }
    if (n < from) d->ec_mu = -1;
    else if (n < from + EC_FAST) d->ec_mu = 0;
    else d->ec_mu = 3;
}

/* the client's S: PP(0) is centred 16.5 symbols after the S / S-bar
 * boundary, and the event came SDET_DELAY samples after it (as x2d) */
static void wait_s(k56d_t *d, int16_t x)
{
    int32_t dl;
    (void)qamrx_tick(&d->rx, x);                /* input level for the AGC */
    if (!sdet_tick(&d->sdet, x)) return;
    dl = (int32_t)(((int64_t)132000 * 256) / SYM_RATE) - (SDET_DELAY(&d->sdet) + 1) * 256;
    qamrx_sync(&d->rx, dl);
    v34trn_init(&d->ref, V34T_GPC);
    d->rxmode = RX_KNOWN;
    d->t_mark = d->now;                         /* K56D_T_J from here */
    TR("K56D %.3f S\n", d->now / 8000.0);
}

uint8_t k56d_tick(k56d_t *d, int16_t rxs)
{
    uint8_t octet;
    int16_t x;
    k56tx_seg seg;

    d->now++; d->t++;
    if (d->state == K56D_ENDED) return 0xff;
    seg = k56tx_segment(&d->tx);
    octet = k56tx_tick(&d->tx);
    /* the echo of this octet begins ecd.delay samples from now */
    ecdly_put(&d->ecd, (int16_t)v90_octet_linear(0, octet));
    x = ec_tick(&d->ec, ecdly_ref(&d->ecd, 0), rxs, d->ec_mu);
    ecdly_next(&d->ecd);

    if (seg >= K56TX_TRN1A && seg <= K56TX_TRN1C) ec_schedule(d, rxs);
    else d->ec_mu = -1;

    /* receiver: from our Js on (the client answers it) */
    if (d->rxmode == RX_OFF) {
        if (seg >= K56TX_JS) {
            d->rxmode = RX_WAIT_S;
            wait_s(d, x);
        }
    } else if (d->rxmode == RX_WAIT_S) {
        wait_s(d, x);
    } else {
        if (qamrx_tick(&d->rx, x)) rx_symbol(d);
        if (d->state == K56D_ENDED) return 0xff;
    }

    switch (d->state) {
    case K56D_TRAIN1:
        if (seg == K56TX_SIL2) {
            if (d->rxmode == RX_WAIT_S) d->t_mark = d->now;   /* K56D_T_S from the end of our Js */
            enter(d, K56D_WAIT_J);
        }
        break;
    case K56D_WAIT_J:
        if (d->got_j) {
            int pad = d->j_octet >> 6;
            /* a pad code we have no levels for (Ruling F3: refused here,
             * k56tx_got_j would only keep silence 2 going) */
            if (pad > 2) { end(d, K56D_ERR_J); break; }
            d->pad = pad; d->rbs = (uint8_t)(d->j_octet & 0x3f);
            k56tx_got_j(&d->tx, d->j_octet);
            d->t_mark = -1;
            enter(d, K56D_TRAIN2);
        } else if (d->rxmode == RX_WAIT_S) {
            if (d->now - d->t_mark > MS(K56D_T_S)) end(d, K56D_ERR_NO_S);
        } else if (d->now - d->t_mark > MS(K56D_T_J)) {
            end(d, K56D_ERR_NO_J);
        }
        break;
    case K56D_TRAIN2:
        /* K56D_T_MP from the start of our TRN2B (Ruling F4) */
        if (seg == K56TX_TRN2B && d->t_mark < 0) d->t_mark = d->now;
        if (seg >= K56TX_MP) {
            /* its E may be in already (rx_begin_data set t_mark for T_B1): keep that */
            if (!d->got_e) d->t_mark = -1;
            enter(d, K56D_MP);
        }
        else if (!d->got_mp && d->t_mark >= 0 && d->now - d->t_mark > MS(K56D_T_MP)) end(d, K56D_ERR_NO_MP);
        break;
    case K56D_MP:
        if (!d->got_e) {
            /* K56D_T_E from the start of our MP' */
            if (seg == K56TX_MPA && d->t_mark < 0) d->t_mark = d->now;
            if (d->t_mark >= 0 && d->now - d->t_mark > MS(K56D_T_E)) end(d, K56D_ERR_NO_E);
            break;
        }
        /* its E came (rx_begin_data set t_mark): B1 both ways */
        if (seg == K56TX_DATA && d->tx.b1 == 0 && d->tx.fpos == K56ENC_N) d->tx_ready = 1;   /* B1 sent */
        if (d->tx_ready && d->rx_ready) {
            d->down_rate = k56tx_rate(&d->tx);
            TR("K56D %.3f data %d / %d\n", d->now / 8000.0, d->down_rate, d->up_rate);
            enter(d, K56D_DATA);
        } else if (d->now - d->t_mark > MS(K56D_T_B1)) {
            end(d, K56D_ERR_B1);
        }
        break;
    default:
        break;
    }
    return d->state == K56D_ENDED ? 0xff : octet;
}
