/* The answering modem's HST scheme (modem/ans_scheme.h): ANS_HST_WAIT and
 * ANS_HST (spec "The answering sequence", "HST pump", "Retrain, watchdogs,
 * cleardown and end codes"; decisions 9, 18 to 21, 25).
 *
 * ANS_HST_WAIT (a caller sent neither CM nor AA during ANSam, HST offered
 * and an MNP endpoint linked): silence, the back channel's detector on
 * what we hear (u.hst.det). It fires: ANS_HST. Not by HST_W_MS (nor, while
 * its energy stage holds, by HST_W_MAX_MS): V.22bis or V.21 exactly as
 * without HST (ans_start_no_cm).
 *
 * ANS_HST, the point of no return, holding the forward channel:
 *  - our forward transmitter's long training at HST_OPEN_CODE, then data:
 *    scrambled ones for HST_ONES_N samples after each training (the far
 *    receiver enters data on them; without them it lost our first frame,
 *    the LR, and MNP took one T401 more), getbit's bits from then on;
 *  - both back receivers (the detector's, carrying on locked): the bits of
 *    the locked one go on, the agreed speed's when it is locked (before the
 *    agreement 450 first), into the mailbox's pre-connect ring until
 *    connected and to putbit from the sample connected comes up on (the
 *    pump contract, v42/hst_link.h: never both, and the mailbox is never
 *    zeroed within the call);
 *  - connected once our training has gone out (link.trained), with its
 *    ones, and a back receiver is locked: tx_bps the opening rate, rx_bps
 *    that receiver's;
 *  - the caller's confirming LA (link.la_rx, MNP up) with a rate agreed
 *    (agreed_rates): the short training at the best agreed code, a retrain
 *    at the API (retraining, retrains; decision 21), tx_bps the new rate
 *    when it is over. Plain MNP (no 0xC0 agreed) keeps the opening rate.
 *    The back channel's bits go on through it (the endpoint holds its
 *    timers between line_stopped and line_restored);
 *  - the backstop: no MNP link (la_rx) 15 s after ANS_HST began ends the
 *    call ANS_END_HST_FAILED; connected, no back carrier (neither back
 *    receiver locked) for 5 s, ANS_END_LOST.
 * Channel reversal (spec "Channel reversal", decision 13; Task 21), both
 * roles, `rev` (HST_REV_*):
 *  - holding forward, the endpoint's LNA granting the caller's 0x1R has
 *    drained (rev_lna_sent): GRANT, HST_REV_DRAIN_MS, then from the
 *    endpoint's first bit between frames ones (`cut`; under the BinModem
 *    profile V.17's turn-off, HST_BM_TOFF_ONES symbols of them); the
 *    switch: our forward carrier stops; the back receivers' held bits go
 *    to putbit and the receivers are left; 8 ones to putbit (the
 *    endpoint's HDLC receiver aborts: reset for the new channel, [BM]);
 *    our back transmitter starts (preamble, then ones: getbit not called)
 *    at the agreed back speed; the forward receiver listens, from the
 *    equaliser kept from our last time on it (eq_fwd). LISTEN until it is
 *    in data: epoch (the endpoint's LTs go again, and it acknowledges
 *    anew), getbit's bits on the back channel. Not heard within
 *    HST_REV_BOUND_MS (or not in data 2 s later): the forward channel
 *    taken back with a short training, as a retrain;
 *  - holding back, our 0x1R's LNA (rev_lna_rx): our back transmitter
 *    stops at once, the forward equaliser kept, 8 ones to putbit, both back
 *    receivers afresh; GAP, HST_REV_GAP_MS; then TRAIN, our short training
 *    at R (the far receiver kept its equaliser) and its HST_ONES_N ones
 *    (getbit not called), then epoch.
 * At each switch tx and rx exchange (RATES), `reversals` counts it, and
 * data does not stop (moving stays 1); the taking back is also a retrain.
 * Rate changes are Task 22, the other watchdogs and the far end's
 * cleardown Task 23. */
#include <string.h>
#include "ans_scheme.h"

#define MS(ms) ((int32_t)(ms) * 8)

static int lane_bps(int lane) { return lane ? 300 : 450; }

/* the forward transmitter's bits: the host's while data moves, ones before
 * and through a retrain or a reversal's training (getbit is called only
 * then) */
static int fwd_getbit(void *u)
{
    answer_t *a = (answer_t *)u;
    hst_t *h = &a->u.hst;
    int b;
    if (!a->connected || a->retraining || h->rev == HST_REV_TRAIN || h->cut) return 1;
    b = a->cfg.getbit(a->cfg.user) & 1;
    /* a reversal granted, its 20 ms over: from the endpoint's first bit
     * between frames on, ones until the switch (a symbol's bits seldom end
     * on an octet's boundary) */
    if (h->rev == HST_REV_GRANT && h->rt >= MS(HST_REV_DRAIN_MS) && h->link.ep_idle) h->cut = 1;
    return b;
}

/* the back transmitter's (holding the back channel): the host's once the
 * far end's training is in, ones before (its back receiver starts with its
 * training) */
static int back_getbit(void *u)
{
    answer_t *a = (answer_t *)u;
    return a->u.hst.rev != HST_REV_LISTEN ? a->cfg.getbit(a->cfg.user) & 1 : 1;
}

/* 0xC0's rates (bit n: code n, 2400n bit/s) for a ceiling in bit/s, 0 none */
static uint8_t rates_under(int bps)
{
    uint8_t m = 0;
    int n;
    for (n = 2; n <= 6; n++) if (bps <= 0 || 2400 * n <= bps) m = (uint8_t)(m | 1u << n);
    return m;
}

/* the highest code in a rate mask, 0 none */
static int best_code(unsigned rates)
{
    int n;
    for (n = 6; n >= 2; n--) if (rates & (1u << n)) return n;
    return 0;
}

void ans_start_hst_wait(answer_t *a)
{
    hst_t *h = &a->u.hst;
    /* a new call: the mailbox zeroed here, never again until the next one */
    memset(h, 0, sizeof *h);
    hst_det_init(&h->det);
    ans_enter(a, ANS_HST_WAIT); a->t = 0;
}

void ans_start_hst(answer_t *a, int det)
{
    hst_t *h = &a->u.hst;
    int cap;
    h->code = h->next = HST_OPEN_CODE;
    h->lane = det == HST_DET_300 ? 1 : 0;
    h->link.offer_rates = rates_under(a->max_down);       /* decision 20 */
    cap = best_code(rates_under(a->max_up));
    h->link.cap_up_code = (uint8_t)(cap ? cap : 2);
    h->link.role = HST_ROLE_FWD;
    h->link.fwd_code = h->code;
    h->link.tx_bps = 2400 * h->code;
    h->link.rx_bps = lane_bps(h->lane);
    hst_ftx_init(&h->ftx, a->cfg.level, fwd_getbit, a);
    hst_ftx_train(&h->ftx, h->code, 1);
    ans_enter(a, ANS_HST); a->t = 0;
}

static int16_t tick_wait(answer_t *a, int16_t rx)
{
    hst_t *h = &a->u.hst;
    int det = hst_det_tick(&h->det, rx);
    if (det) { ans_start_hst(a, det); return 0; }
    /* W; past it only while the band's energy holds, up to HST_W_MAX_MS */
    if (a->t >= MS(HST_W_MS) && (a->t >= MS(HST_W_MAX_MS) || !hst_det_energy(&h->det))) ans_start_no_cm(a);
    return 0;
}

/* the back receivers: the lane whose bits go on, and those bits */
static void back_rx(answer_t *a, int16_t rx)
{
    hst_t *h = &a->u.hst;
    hst_brx_t *r;
    int pref = h->link.agreed && h->link.agreed_rates && h->link.back300 ? 1 : 0, k, n;
    hst_det_rx(&h->det, rx);
    if (hst_brx_locked(&h->det.rx[pref])) h->lane = pref;
    else if (hst_brx_locked(&h->det.rx[1 - pref])) h->lane = 1 - pref;
    r = &h->det.rx[h->lane];
    n = h->det.nb[h->lane];               /* locked, or the bits a lost lock releases */
    for (k = 0; k < n; k++) {
        if (a->connected) a->cfg.putbit(a->cfg.user, r->bits[k]);
        else (void)hst_pre_put(&h->link, r->bits[k]);
    }
}

/* a reversal's switch: the endpoint's HDLC receiver reset (an abort: 8
 * ones) for the stream that follows, the far end's other channel [BM] */
static void rx_abort(answer_t *a)
{
    int k;
    for (k = 0; k < 8; k++) a->cfg.putbit(a->cfg.user, 1);
}

/* the back channel's speed agreed (the one we send at) */
static int back_bps(const hst_t *h) { return h->link.back300 ? 300 : 450; }

/* holding forward, our LNA granting a 0x1R drained 20 ms ago, between
 * frames: the forward carrier stops; the back receivers' held bits are
 * given and the receivers left (the far end stops its back channel on our
 * LNA, by which time we send there ourselves); our back transmitter
 * starts (its preamble), and the forward receiver, from the equaliser
 * kept from our last time on it (spec "Channel reversal", steps 2 and 4) */
static void to_back(answer_t *a)
{
    hst_t *h = &a->u.hst;
    uint8_t bits[HST_BRX_FLUSH];
    int k, n;
    hst_ftx_stop(&h->ftx);
    n = hst_brx_flush(&h->det.rx[h->lane], bits);
    for (k = 0; k < n; k++) a->cfg.putbit(a->cfg.user, bits[k]);
    (void)hst_brx_flush(&h->det.rx[1 - h->lane], bits);
    rx_abort(a);
    h->code = h->link.rev_code;
    hst_btx_init(&h->btx, back_bps(h), a->cfg.level, back_getbit, a);
    hst_btx_preamble(&h->btx);
    hst_frx_init(&h->frx);
    hst_frx_restore(&h->frx, &h->eq_fwd);
    hst_frx_set_code(&h->frx, h->code);
    h->link.role = HST_ROLE_BACK;
    h->link.fwd_code = h->code;
    a->bitrate = h->link.tx_bps = back_bps(h);
    a->rx_bitrate = h->link.rx_bps = 2400 * h->code;
    h->reversals++;
    h->ones = 0;
    h->cut = 0;
    h->rev = HST_REV_LISTEN; h->rt = 0;
}

/* holding back, to the forward channel: our back transmitter stops at
 * once, the forward receiver's equaliser kept, the back receivers start
 * afresh (the far end's back channel begins at its switch). Our training
 * follows (`rev`: GAP, 200 ms, then the short one; TRAIN at once, taking
 * the forward channel back unasked) */
static void to_fwd(answer_t *a, int rev)
{
    hst_t *h = &a->u.hst;
    hst_btx_stop(&h->btx);
    if (hst_frx_state(&h->frx) == HST_FRX_DATA) hst_frx_store(&h->frx, &h->eq_fwd);
    rx_abort(a);
    hst_brx_init(&h->det.rx[0], 450);
    hst_brx_init(&h->det.rx[1], 300);
    h->lane = h->link.back300 ? 1 : 0;
    h->quiet = 0;
    h->link.role = HST_ROLE_FWD;
    h->link.fwd_code = h->code;
    a->bitrate = h->link.tx_bps = 2400 * h->code;
    a->rx_bitrate = h->link.rx_bps = lane_bps(h->lane);
    h->reversals++;
    h->rev = rev; h->rt = 0;
    if (rev == HST_REV_TRAIN) {
        h->next = h->code; h->ones = 0;
        hst_ftx_train(&h->ftx, h->code, 0);
    }
}

/* the reversal in progress, if any (spec "Channel reversal") */
static void reversal(answer_t *a)
{
    hst_t *h = &a->u.hst;
    if (h->rev != HST_REV_NONE) h->rt++;
    switch (h->rev) {
    case HST_REV_NONE:
        if (h->link.role == HST_ROLE_FWD && h->link.rev_lna_sent && !a->retraining) {
            h->link.rev_lna_sent = 0;
            h->rev = HST_REV_GRANT; h->rt = 0;
        } else if (h->link.role == HST_ROLE_BACK && h->link.rev_lna_rx) {
            h->link.rev_lna_rx = 0;
            h->code = h->link.rev_code;
            to_fwd(a, HST_REV_GAP);
        }
        break;
    case HST_REV_GRANT:
        /* 20 ms [OURS], and the frame that began in them out (fwd_getbit's
         * cut; at most 0.5 s). Under the BinModem profile V.17's turn-off
         * ends the burst: HST_BM_TOFF_ONES symbols of ones (the cut's),
         * then the forward carrier's silence from the switch, which is
         * longer than its HST_BM_TOFF_QUIET symbols (Task 15's ruling) */
        if (h->cut > (HST_LONG_V17 ? HST_BM_TOFF_ONES : 0) || h->rt >= MS(500)) to_back(a);
        break;
    case HST_REV_LISTEN:
        /* the far end's training: in data, LTs go again (epoch). Not heard
         * within HST_REV_BOUND_MS (nor training by then, nor in data within
         * HST_REV_BOUND_MS + 2 s): the forward channel taken back, as a
         * retrain [OURS] (Task 23: the watchdogs after it) */
        if (hst_frx_state(&h->frx) == HST_FRX_DATA) { h->link.epoch++; h->rev = HST_REV_NONE; }
        else if ((h->rt >= MS(HST_REV_BOUND_MS) && hst_frx_state(&h->frx) != HST_FRX_TRAIN)
                 || h->rt >= MS(HST_REV_BOUND_MS + 2000)) {
            a->retraining = 1; a->retrains++;
            to_fwd(a, HST_REV_TRAIN);
        }
        break;
    case HST_REV_GAP:
        if (h->rt >= MS(HST_REV_GAP_MS)) {
            h->next = h->code; h->ones = 0;
            hst_ftx_train(&h->ftx, h->code, 0);   /* short: the far receiver kept its equaliser */
            h->rev = HST_REV_TRAIN; h->rt = 0;
        }
        break;
    default:   /* HST_REV_TRAIN: done with its ones */
        if (h->ones >= HST_ONES_N) { h->link.epoch++; h->rev = HST_REV_NONE; }
        break;
    }
}

/* holding the back channel: the forward receiver's bits */
static void fwd_rx(answer_t *a, int16_t rx)
{
    hst_t *h = &a->u.hst;
    int k, n = hst_frx_tick(&h->frx, rx);
    for (k = 0; k < n; k++) a->cfg.putbit(a->cfg.user, h->frx.bits[k]);
}

static int16_t tick_hst(answer_t *a, int16_t rx)
{
    hst_t *h = &a->u.hst;
    int32_t tx;
    if (h->link.role == HST_ROLE_FWD) back_rx(a, rx);
    else fwd_rx(a, rx);
    /* both transmitters: the one stopped rings out */
    tx = hst_ftx_tick(&h->ftx);
    if (h->cut && h->ftx.tx.newsym) h->cut++;
    if (h->btx.mode != HST_BTX_SILENT || h->link.role == HST_ROLE_BACK || h->rev == HST_REV_GAP) tx += hst_btx_tick(&h->btx);
    if (!hst_ftx_in_data(&h->ftx)) h->ones = 0;
    else {
        h->link.trained = 1;
        if (h->ones < HST_ONES_N) h->ones++;
    }
    if (!a->connected) {
        if (h->ones >= HST_ONES_N && hst_brx_locked(&h->det.rx[h->lane])) {
            a->connected = 1; a->modulation = ANS_MOD_HST;
            a->bitrate = 2400 * h->code;
        }
    }
    if (a->connected) {
        reversal(a);
        if (h->link.role == HST_ROLE_FWD) {
            /* the receive rate is the bits' (RATES when it changes) */
            a->rx_bitrate = h->link.rx_bps = lane_bps(h->lane);
            /* MNP up: the post-agreement retrain at the best agreed code */
            if (h->link.la_rx && !h->la_seen) {
                int best = best_code(h->link.agreed_rates & h->link.offer_rates);
                h->la_seen = 1;
                if (best) {
                    h->next = best;
                    h->ones = 0;
                    hst_ftx_train(&h->ftx, best, 0);
                    a->retraining = 1; a->retrains++;
                }
            }
            if (a->retraining && h->ones >= HST_ONES_N) {
                a->retraining = 0;
                h->code = h->next;
                a->bitrate = h->link.tx_bps = 2400 * h->code;
                h->link.fwd_code = h->code;
            }
            /* the back channel is continuous: 5 s without it is a caller gone.
             * Its carrier is a receiver's lock, not the carrier detect: the
             * echo of our forward channel (or line noise) holds that on with no
             * back channel at all (the Task 13 review), and never locks one */
            if (hst_brx_locked(&h->det.rx[0]) || hst_brx_locked(&h->det.rx[1])) h->quiet = 0;
            else if (++h->quiet > MS(HST_LOST_MS)) { ans_fail(a, ANS_END_LOST); return 0; }
        }
    }
    /* the backstop (spec "Retrain, watchdogs, cleardown and end codes") */
    if (!h->link.la_rx && a->t > MS(HST_FAIL_MS)) { ans_fail(a, ANS_END_HST_FAILED); return 0; }
    return (int16_t)(tx > 32767 ? 32767 : tx < -32768 ? -32768 : tx);
}

static int16_t tick(answer_t *a, int16_t rx)
{
    return a->phase == ANS_HST_WAIT ? tick_wait(a, rx) : tick_hst(a, rx);
}

/* ours: both carriers stop now (the host has had the endpoint release
 * first, docs/error-control.md) */
static int cleardown(answer_t *a)
{
    if (a->phase != ANS_HST) return -1;
    hst_ftx_stop(&a->u.hst.ftx);
    ans_fail(a, ANS_END_CLEARDOWN_OURS);
    return 0;
}

/* holding the forward channel: the short training at the rate in force
 * (not through a reversal; holding back, Task 23's 0x2R) */
static int retrain(answer_t *a)
{
    hst_t *h = &a->u.hst;
    if (a->phase != ANS_HST || !a->connected || a->retraining || h->link.role != HST_ROLE_FWD
        || h->rev != HST_REV_NONE) return -1;
    h->next = h->code;
    h->ones = 0;
    hst_ftx_train(&h->ftx, h->code, 0);
    a->retraining = 1; a->retrains++;
    return 0;
}

/* holding the forward channel, the back receiver's, the one we take bits
 * from; holding the back channel, the forward receiver's */
static int snr_den(const answer_t *a, int64_t *num, int64_t *den)
{
    const hst_t *h = &a->u.hst;
    if (a->phase != ANS_HST) return -1;
    if (h->link.role == HST_ROLE_BACK) return hst_frx_snr(&h->frx, num, den) ? 0 : -1;
    return hst_brx_snr(&h->det.rx[h->lane], num, den) ? 0 : -1;
}

const ans_scheme_t ans_hst = { tick, cleardown, retrain, NULL, snr_den, NULL, NULL, 0 };
