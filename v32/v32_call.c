/* V.32bis calling modem: start-up (6.1), data, retrain (7.1).
 *
 *   AA -> CC -> (silent) ..................... S for NT, S, S-bar, TRN -> R2 .. E, B1   (we send)
 *   AC -> CA -> AC, 16 T, S, S-bar, TRN, R1 .. (silent) .. S, S-bar, TRN, R3 .. E, B1   (the answerer)
 *
 * The answerer's two phase reversals time the round trip: our AA-CC
 * transition reaches the line 64 T after its first reversal arrived, and its
 * second comes back 64 T after ours reached it, so the counter (NT) holds the
 * round trip and the answerer's 64 T. NT places the far echo canceller, and
 * is how long S is sent ahead of the receiver conditioning signal proper:
 * the answerer stops R1 on our S and waits its own round trip (MT) before
 * training. Our TRN trains the echo canceller with the answerer silent. R2
 * offers what R1, our configuration and our receiver allow; R3 names the
 * rate; E, scrambled ones (B1) until 128 T after the answerer's E, data.
 *
 * Shares the transmitter, receiver and echo canceller with the answering
 * modem (v32.c); the receiver's AC detector (v32_rx.c) is for this side.
 */
#include "v32_priv.h"

#define T_NO_AC    MS(20000)     /* AA with no AC (or no reversal in it) in reply */
#define T_RN       MS(3000)      /* a rate renegotiation step unanswered: retrain instead */
/* Our own AA-CC transition comes straight back as near echo before the
 * canceller has trained, and the AC detector sees the step: no reversal is
 * taken until it has gone by (the near window, a pulse, the detector). The
 * answerer's is at least 64 T later. */
#define BLANK      (V32_EC_NEAR + SYM2SAMP(16))

static void go(v32_t *m, v32_state s)
{
    m->seg_len[m->state] = m->seg;
    m->state = s; m->seg = 0; m->t_state = 0;
}

static void fail(v32_t *m, int error)
{
    go(m, V32S_FAILED);
    m->error = error;
    m->connected = 0; m->retraining = 0; m->tx_ready = m->rx_ready = 0;
    v32tx_set_mode(&m->tx, V32_TX_SILENCE);
    m->ec_mu = -1;
}

/* 6.1 from its second paragraph, and 7.1: AA, listening for AC */
void v32c_start(v32_t *m)
{
    v32rx_t *r = &m->rx;
    m->state = V32S_C_AA; m->seg = 0; m->t_state = 0;
    v32tx_set_mode(&m->tx, V32_TX_AA);
    v32rx_set_mode(r, V32_RX_TONE);
    r->ac_run = 0; r->ac_pure = 0; r->ac_rev = 0; r->ac_hold = 0; r->s_run = 0;
    m->ec_mu = -1;
    m->r1_word = m->r2_word = m->r3_word = 0;
    m->rx_e_seen = 0; m->tx_ready = m->rx_ready = 0;
    m->bad_run = 0; m->lost_run = 0; m->carrier = 0;
}

/* 7.1 from data mode or a renegotiation: retrain */
static void retrain(v32_t *m)
{
    m->retrains++; m->retraining = 1; m->rn = 0;
    m->seg_len[m->state] = m->seg;
    v32c_start(m);
}

/* the receiver, trained, reads rate signals again from the next symbol */
static void rate_search(v32rx_t *r)
{
    v32rx_set_mode(r, V32_RX_DD4);
    r->frame_bit = -1; r->nbits = 0; r->bits = 0;
    r->rate_word = 0; r->rate_seen = 0; r->e_word = 0;
}

/* the rates of `have` up to the highest that an error of `mse` allows (the
 * lowest at least) */
static unsigned rates_up_to(unsigned have, int32_t mse)
{
    int k, top = -1, low = -1;
    for (k = 0; k < V32_NRATES; k++)
        if (have & (1u << k)) {
            if (low < 0) low = k;
            if (mse <= v32_rate_mse[k]) top = k;
        }
    if (top < 0) top = low;
    return top < 0 ? 0 : have & ((2u << top) - 1);
}

/* R2 (6.1): rates of R1 that we have, less those our receiver did not look
 * fit for when it read R1 (its error then, against the thresholds the
 * answerer picks R3 by); none: a cleardown (Table 5, Note 3) */
static uint16_t r2_word(const v32_t *m)
{
    unsigned common = m->cfg.rates & v32_word_rates(m->r1_word);
    int top;
    /* Table 5 Note 1: B4 or B8 clear, V.32 rules (as the answerer has it) */
    if (!(m->r1_word & (1u << 11)) || !(m->r1_word & (1u << 7))) common &= V32_R4800;
    if (!common) return v32_rate_word(0);
    top = v32_pick_rate(m, common);
    return v32_rate_word(common & ((2u << top) - 1));
}

/* once per transmit symbol, before the symbol is generated: what to send */
void v32c_tx_control(v32_t *m)
{
    v32tx_t *t = &m->tx;
    switch (m->state) {
    case V32S_C_AA64:
        /* 6.1: the AA-CC transition at the line terminals 64 +- 2 T after
         * the reversal arrived there: the first CC symbol's pulse centre
         * within half a symbol of it (I/O buffering, echo_delay in all, made
         * the reversal earlier at the line and makes CC later) */
        if ((int32_t)(m->nsamp + V32_TX_DELAY + m->cfg.echo_delay - m->t_rev1) >= SYM2SAMP(64) - 2) {
            go(m, V32S_C_CC);
            v32tx_set_mode(t, V32_TX_CC);
            m->t_cc = m->nsamp + V32_TX_DELAY;
            m->rx.ac_rev = 0;
        }
        break;
    case V32S_C_SNT:
        /* S for NT, then S for 256 T: one run of ABAB */
        if ((int32_t)m->seg >= m->snt_len) go(m, V32S_C_S);
        break;
    case V32S_C_S:
        if (m->seg >= 256) { go(m, V32S_C_SBAR); v32tx_set_mode(t, V32_TX_SBAR); }
        break;
    case V32S_C_SBAR:
        if (m->seg >= 16) {
            go(m, V32S_C_TRN); v32tx_set_mode(t, V32_TX_TRN);
            m->ec_mu = 0;                       /* the answerer is silent: full step */
            m->ec_block = 0; m->ec_best = 0; m->ec_stall = 0;
        }
        break;
    case V32S_C_TRN:
        /* 6.1: TRN may be extended for the echo canceller: as the
         * answerer's TRN1 (v32.c), full step, then smaller ones, then on
         * while the residual echo still falls (by 1 dB or more in 256 T:
         * with no echo at all nothing falls) */
        if (m->seg + 512 == m->trn1_min) m->ec_mu = 1;
        if (m->seg + 256 == m->trn1_min) m->ec_mu = 2;
        if (m->seg >= m->trn1_min && ((m->seg - m->trn1_min) & 255) == 0) {
            int64_t out = m->ec.pw_out;
            int done = m->seg + 256 > TRN1_MAX || m->ec.pw_in > out * 10000;      /* (no further step past 8192) */
            if (!done && m->ec_block && out * 5 >= m->ec_best * 4) done = 1;
            m->ec_best = out; m->ec_block++;
            if (done) {
                m->erle_db10 = v32_db10(m->ec.pw_in, m->ec.pw_out);
                m->ec_mu = -1;
                go(m, V32S_C_R2);
                m->r2_word = r2_word(m);
                t->rword = m->r2_word;
                v32tx_set_mode(t, V32_TX_RATE);
                m->rx.s_run = 0;
            }
        }
        break;
    case V32S_C_R2:
        /* 6.1, 5.3.2: on R3, complete the current sequence, then E naming
         * R3's rate */
        if (m->r3_word && t->rbit == 0) {
            go(m, V32S_C_E);
            t->rword = (uint16_t)(v32_rate_word(1u << m->rate) | V32_RW_E);
        }
        break;
    case V32S_C_E:
        if (m->seg >= 8) {
            go(m, V32S_C_B1);
            t->rate = m->rate;
            v32tx_set_mode(t, V32_TX_ONES);     /* (encoder delay elements from zero: v32_tx.c) */
        }
        break;
    case V32S_C_B1:
        /* 6.1: circuit 106 follows 105 once 104 is unclamped, 128 T after
         * the answerer's E */
        if (m->rx.mode == V32_RX_DATA && m->rx.clamp == 0) {
            go(m, V32S_DATA);
            v32tx_set_mode(t, V32_TX_DATA);
            m->tx_ready = 1;
        }
        break;
    /* ---- 8.2: responding to a rate renegotiation ---- */
    case V32S_C_RN_AA:
        if (m->seg >= 56) { go(m, V32S_C_RN_CC); v32tx_set_mode(t, V32_TX_CC); }
        break;
    case V32S_C_RN_CC:
        if (m->seg >= 8) {
            /* 8: the rate signal with the scrambler from zero, the
             * differential encoder from the preamble's last symbol */
            go(m, V32S_C_RN_R5);
            t->scr.reg = 0;
            t->rword = m->r5_word;
            v32tx_set_mode(t, V32_TX_RATE);
        }
        break;
    case V32S_C_RN_R5:
        /* 8.2: R5 for 64 T, then E naming the highest rate common to R4 and R5 */
        if (m->seg >= 64 && t->rbit == 0) {
            go(m, V32S_C_RN_E);
            t->rword = (uint16_t)(v32_rate_word(m->rn_rate >= 0 ? 1u << m->rn_rate : 0) | V32_RW_E);
        }
        break;
    case V32S_C_RN_E:
        if (m->rn_rate < 0) {
            /* Note 2 of 8: nothing in common, E repeated for 64 T or more, then a cleardown */
            if (m->seg >= 64 && t->rbit == 0) fail(m, V32_ERR_NO_RATE);
        } else if (m->seg >= 8) {
            /* ones at the new rate for 24 T, the encoder's delay elements from zero */
            go(m, V32S_C_RN_B1);
            m->rate = m->rn_rate;
            t->rate = m->rate;
            v32tx_set_mode(t, V32_TX_ONES);
        }
        break;
    case V32S_C_RN_B1:
        if (m->seg >= 24) {
            go(m, V32S_DATA);
            v32tx_set_mode(t, V32_TX_DATA);
            m->tx_ready = 1;
        }
        break;
    default:
        break;
    }
    m->seg++;
}

/* every sample, after the receiver has run: what was heard */
void v32c_rx_control(v32_t *m)
{
    v32rx_t *r = &m->rx;
    switch (m->state) {
    case V32S_C_AA:
        /* 6.1: AC, then a phase reversal in it */
        if (r->ac_rev) {
            m->t_rev1 = r->ac_rev_t - V32_AC_LAT;
            go(m, V32S_C_AA64);
        } else if (m->t_state > T_NO_AC) fail(m, V32_ERR_NO_TONE);
        break;
    case V32S_C_CC:
        if (r->ac_rev && (int32_t)(r->ac_rev_t - m->t_cc) < m->cfg.echo_delay + BLANK) r->ac_rev = 0;
        if (r->ac_rev) {
            /* the second reversal: stop the counter, cease transmitting */
            int32_t rt;
            m->t_rev2 = r->ac_rev_t - V32_AC_LAT;
            m->nt = (int32_t)(m->t_rev2 - m->t_cc);
            /* far echo: back after the round trip, NT less the answerer's
             * 64 T; placed as the answerer does (v32.c) */
            rt = m->nt - SYM2SAMP(64);
            v32ec_set_bulk(&m->ec, rt - 28);
            m->trn1_min = TRN1_MIN + (rt > 0 ? (uint32_t)rt * 3 / 10 : 0);
            /* our TRN also trains the answerer's receiver, which picks R3 by
             * its error at the end of it: 1280 T left it short of where it
             * settles on a lossy line (R3 at 7200 where 9600 fits, 3 calls
             * in 14 on test_orig_v32's realistic line); 6.1 allows up to
             * 8192 T */
            if (m->trn1_min < C_TRN_MIN) m->trn1_min = C_TRN_MIN;
            if (m->trn1_min > TRN1_MAX - 512) m->trn1_min = TRN1_MAX - 512;
            /* NT in symbols, to the nearest even count */
            m->snt_len = (m->nt * 3 + 10) / 20 * 2;
            go(m, V32S_C_LISTEN);
            v32tx_set_mode(&m->tx, V32_TX_SILENCE);
            r->s_run = 0;
        } else if (m->t_state > T_REVERSAL) fail(m, V32_ERR_NO_TONE);
        break;
    case V32S_C_LISTEN:
        /* 6.1: on the answerer's S, train the receiver; then R1 */
        if (r->mode == V32_RX_TONE) {
            if (r->s_run >= 40) v32rx_set_mode(r, V32_RX_S);
        } else if (r->mode == V32_RX_DD4 && r->rate_seen >= 2) {
            m->r1_word = r->rate_word;
            v32rx_set_mode(r, V32_RX_TONE);     /* (its error stays as it was on R1: R2 is made of it) */
            r->s_run = 0;
            go(m, V32S_C_SNT);
            v32tx_set_mode(&m->tx, V32_TX_S);
            break;
        }
        if (m->t_state > T_TRAIN) fail(m, V32_ERR_TRAIN);
        break;
    case V32S_C_R2:
        /* the answerer's second S, S-bar and TRN train the receiver again,
         * our echo cancelled now; then R3 */
        if (r->mode == V32_RX_TONE) {
            if (r->s_run >= 40) v32rx_set_mode(r, V32_RX_S);
        } else if (r->mode == V32_RX_DD4 && r->rate_seen >= 2 && !m->r3_word) {
            unsigned rates = v32_word_rates(r->rate_word) & v32_word_rates(m->r2_word);
            int k;
            m->r3_word = r->rate_word;
            m->rate = -1;
            for (k = 0; k < V32_NRATES; k++) if (rates & (1u << k)) m->rate = k;
            /* R3 calling for a cleardown (or for nothing R2 had): disconnect */
            if (m->rate < 0) { fail(m, V32_ERR_NO_RATE); break; }
        }
        if (m->t_state > T_TRAIN) fail(m, V32_ERR_TRAIN);
        break;
    case V32S_C_E: case V32S_C_B1:
        if (r->e_word && r->mode != V32_RX_DATA) { fail(m, V32_ERR_NO_RATE); break; }     /* E naming no rate */
        if (m->state == V32S_C_B1 && m->t_state > T_TRAIN) fail(m, V32_ERR_TRAIN);
        break;
    case V32S_DATA:
        /* 7.1: the answerer's AC for more than 128 symbol intervals asks for
         * a retrain */
        if (r->ac_pure > 2 * 128 + 16) { retrain(m); break; }
        if (!m->connected || m->retraining) break;
        if (m->rn) {
            /* our part of a renegotiation is done; the answerer's E and
             * its 24 T are still to come */
            if (m->t_state > T_RN) retrain(m);
            break;
        }
        /* 8.2: the answerer's preamble, AC 56 T and CA 8 T (a retrain's
         * AC has no reversal before our AA): clamp 104, read R4 */
        if (r->ac_rev) {
            m->rn = 1; m->renegotiations++;
            m->rx_ready = 0; m->rx_e_seen = 0;
            m->r4_word = m->r5_word = 0; m->rn_rate = -1;
            rate_search(r);
            go(m, V32S_C_RN_R4);
            break;
        }
        if (r->ac_run == 0) m->mse_data = r->mse;
        /* line signal gone for CD_LOSS: the call is over. As v32.c, and
         * also 12 dB below the level data came in at: line noise or what
         * the canceller leaves of our echo (a far echo whose carrier has
         * shifted) can be above the fixed threshold */
        m->carrier = r->pw_raw * 3 > CD_OFF && r->pw_raw * 16 > m->pw_data;
        if (m->carrier) { m->lost_run = 0; m->pw_data += (r->pw_raw - m->pw_data) >> 10; }
        else if (++m->lost_run > (int32_t)MS(CD_LOSS_MS)) { fail(m, V32_ERR_CARRIER); break; }
        /* 7.1: reception too poor for this rate: retrain (as v32.c) */
        if (m->cfg.no_auto_retrain) break;
        if (r->mse > v32_retrain_mse[m->rate]) {
            if (++m->bad_run > (int32_t)MS(RT_BAD_MS)) {
                if (m->auto_rt >= RT_MAX) { fail(m, V32_ERR_CARRIER); break; }
                m->auto_rt++; m->good_run = 0;
                retrain(m);
            }
        } else {
            if (m->bad_run > 0) m->bad_run--;
            if (m->auto_rt > 0 && ++m->good_run > (int32_t)MS(60000)) { m->auto_rt--; m->good_run = 0; }
        }
        break;
    /* ---- 8.2 ---- */
    case V32S_C_RN_R4:
        if (r->ac_pure > 2 * 128 + 16) { retrain(m); break; }
        if (r->mode == V32_RX_DD4 && r->rate_seen >= 2) {
            /* R4: 106 OFF, our preamble, then R5: the rate our receiver
             * wants (by its error in data mode) and those below it that we
             * have, whatever R4 says */
            unsigned common;
            int k;
            m->r4_word = r->rate_word;
            m->r5_word = v32_rate_word(rates_up_to(m->cfg.rates, m->mse_data));
            common = v32_word_rates(m->r4_word) & v32_word_rates(m->r5_word);
            for (m->rn_rate = -1, k = 0; k < V32_NRATES; k++) if (common & (1u << k)) m->rn_rate = k;
            m->tx_ready = 0;
            go(m, V32S_C_RN_AA);
            v32tx_set_mode(&m->tx, V32_TX_AA);
        } else if (m->t_state > T_RN) retrain(m);
        break;
    case V32S_C_RN_AA: case V32S_C_RN_CC: case V32S_C_RN_R5: case V32S_C_RN_E: case V32S_C_RN_B1:
        if (m->t_state > T_RN) retrain(m);
        break;
    default:
        break;
    }
    if (m->rn && r->e_word && !m->rx_e_seen && m->state != V32S_FAILED) {
        /* 8.2: the far E: data at its rate, 104 unclamped after 24 T */
        m->rx_e_seen = 1;
        if (r->mode == V32_RX_DATA) r->clamp = 24;
        else if (m->state == V32S_DATA) fail(m, V32_ERR_NO_RATE);    /* E naming no rate, ours sent */
    }
    if (!m->rx_ready && r->mode == V32_RX_DATA && r->clamp == 0) m->rx_ready = 1;
    if (m->tx_ready && m->rx_ready && m->state == V32S_DATA && (!m->connected || m->retraining || m->rn)) {
        m->connected = 1; m->retraining = 0; m->rn = 0;
        m->bitrate = V32_BITRATE(m->rate);
        m->pw_data = r->pw_raw;
        r->ac_rev = 0;
    }
}
