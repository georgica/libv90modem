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
 * At each switch tx and rx exchange (RATES) and data does not stop
 * (moving stays 1); `reversals` counts the agreed ones. The taking back is
 * a retrain only (data stops; the far end never switched).
 * Rate changes (spec "Rate changes", decision 15; Task 22b), `rc`
 * (HST_RC_*):
 *  - holding forward, the far end's 0x2R/0x3R once our granting LNA has
 *    gone (rate_order): from the endpoint's first bit between frames, the
 *    switch by marks (hst_ftx_marks_switch; under HST_3R_TRAINS, or with
 *    4800 either side, a training instead), or 20 ms of ones and the short
 *    training at R, a retrain; then epoch. Recovery: the endpoint's
 *    `recover` (nothing acknowledged for 2 x T401) with a back receiver
 *    locked: the short training at the rate in force, a retrain;
 *  - holding back, the policy (hst.h HST_UP_MS ...) asks (ln_req): 0x3R up,
 *    0x2R down or again; the holder's change (rate_order: the LNA to ours,
 *    or its own) is followed by the forward receiver, armed for the marks
 *    (hst_frx_arm_switch) or for a training (hst_frx_arm_train, armed at
 *    the request already, as the LNA may be lost); a training stops data
 *    (retraining) once under way, until the receiver is in data at R; then
 *    epoch. A change given up (not in data at R by HST_RC_BOUND_MS, the
 *    training failed) also bumps epoch, and counts as a failed retrain.
 * The other watchdogs and the far end's cleardown are Task 23's. */
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
    /* a rate change granted, our LNA gone: likewise, the change at the cut */
    if (h->rc == HST_RC_WAIT && h->link.ep_idle) h->cut = 1;
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
    h->fe_t = h->fl_t = -1;
    h->since_down = MS(HST_UP_MS);
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

/* ------------------------------------------------------------ rate changes
 * (spec "Rate changes"; hst.h HST_UP_MS ...; Task 22b) */

/* a change carried out with a training, not by marks: 0x2R; 0x3R under
 * HST_3R_TRAINS; code 2 on either side (no trellis to carry on) */
static int by_training(const hst_t *h, int order)
{
    return (order & 0xF0) == 0x20 || HST_3R_TRAINS || (order & 15) == 2 || h->code == 2;
}

/* the agreed rate next above the one in force, up to max_up; the next
 * below r; 0 none */
static int code_up(const hst_t *h)
{
    int c;
    for (c = h->code + 1; c <= 6 && c <= h->link.cap_up_code; c++) if (h->link.agreed_rates & (1u << c)) return c;
    return 0;
}
static int code_below(const hst_t *h, int r)
{
    int c;
    for (c = r - 1; c >= 2; c--) if (h->link.agreed_rates & (1u << c)) return c;
    return 0;
}

/* the upshift's and the frame errors' windows start again */
static void win_reset(hst_t *h)
{
    h->up_t = h->up_all = h->up_good = 0;
    h->fe_t = -1; h->fe_n = 0;
}

/* nothing pending, at a switch of role: what the endpoint or the policy
 * left for the other role is stale */
static void rc_clear(hst_t *h)
{
    h->rc = HST_RC_NONE;
    h->link.rate_order = 0;
    h->link.ln_req = 0;
    h->link.ln_acted = 0;
    h->link.recover = 0;
    h->hold_t = 0;
    win_reset(h);
}

/* holding back, the policy's request: the endpoint sends it (ln_req) and
 * holds its LTs until the epoch. A training's: the receiver armed for it
 * at once, as the LNA may be lost where the training is not */
static void ask(answer_t *a, int order)
{
    hst_t *h = &a->u.hst;
    h->link.ln_req = order;
    h->rc = HST_RC_ASK; h->rc_order = order; h->rct = 0; h->rc_left = 0; h->rc_counted = 0;
    h->rc_late = 0;
    h->rc_st = hst_frx_state(&h->frx);
    if (by_training(h, order)) hst_frx_arm_train(&h->frx, order & 15);
    win_reset(h);
}

/* holding back, a retrain at r failed: again at r, or after HST_RC_FAILS
 * within HST_DOWN_MS of the first one rate lower; at the lowest no more
 * from here (spec "end codes": Task 23's) */
static void failed(answer_t *a, int r)
{
    hst_t *h = &a->u.hst;
    int lower;
    h->hold_t = 0;
    if (h->fl_t < 0) { h->fl_t = 0; h->fl_n = 0; }
    if (++h->fl_n < HST_RC_FAILS) { ask(a, 0x20 | r); return; }
    lower = code_below(h, r);
    if (lower) {
        h->fl_t = -1;
        h->since_down = 0;
        ask(a, 0x20 | lower);
    }
}

/* holding back, the change pending given up: the endpoint's LTs go again,
 * the receiver disarmed; data is not stopped meanwhile (our endpoint sends
 * the next request) */
static void rc_given_up(answer_t *a)
{
    hst_t *h = &a->u.hst;
    int r = h->rc_order & 15;
    h->rc = HST_RC_NONE;
    h->link.epoch++;
    if (hst_frx_armed(&h->frx)) hst_frx_set_code(&h->frx, h->frx.code);
    a->retraining = 0;
    failed(a, r);
}

/* holding back, the holder's change (rate_order: its own 0x2R/0x3R, or
 * the LNA to ours): the forward receiver follows. A training stops data
 * until the receiver is in data again (decision 21) */
static void follow(answer_t *a, int order)
{
    hst_t *h = &a->u.hst;
    int r = order & 15;
    if (h->rc == HST_RC_FOLLOW && h->rc_order == order) return;   /* the same again (an LNA repeated) */
    if (h->rc != HST_RC_ASK || (h->rc_order & 15) != r) {
        h->rc_left = 0; h->rc_counted = 0;
        h->rc_st = hst_frx_state(&h->frx);
    }
    h->rc = HST_RC_FOLLOW; h->rc_order = order; h->rct = 0;
    h->code = r;
    if (by_training(h, order)) {
        if (hst_frx_state(&h->frx) != HST_FRX_DATA || hst_frx_armed(&h->frx) != r || !h->frx.sw_tonly)
            hst_frx_arm_train(&h->frx, r);
        a->retraining = 1;
        if (!h->rc_counted) { h->rc_counted = 1; a->retrains++; }
    } else {
        hst_frx_arm_switch(&h->frx, r);
    }
    win_reset(h);
}

/* holding back (we receive the forward channel), connected, no reversal
 * in progress, HST rates agreed: the change pending, else the policy */
static void rate_back(answer_t *a)
{
    hst_t *h = &a->u.hst;
    int st = hst_frx_state(&h->frx), r, nxt, lower, d;
    uint32_t bad = (uint32_t)h->link.rx_bad;
    d = (int)(uint32_t)(bad - h->rx_bad);
    h->rx_bad = bad;
    if (h->since_down < MS(HST_UP_MS)) h->since_down++;
    if (h->fl_t >= 0 && ++h->fl_t > MS(HST_DOWN_MS)) h->fl_t = -1;
    if (h->link.rate_order) {
        int order = h->link.rate_order;
        h->link.rate_order = 0;
        if (order == h->rc_late && h->rc == HST_RC_NONE) h->rc_late = 0;   /* done already */
        else follow(a, order);
    }
    if (h->rc != HST_RC_NONE) {
        r = h->rc_order & 15;
        if (h->rc == HST_RC_ASK && h->link.ln_req) h->rct = 0;   /* not yet taken by the endpoint */
        else if (h->rct < (1 << 30)) h->rct++;
        if (st != HST_FRX_DATA) h->rc_left = 1;
        if (st == HST_FRX_TRAIN && !h->rc_counted) {
            /* the holder's training under way (its LNA may have been lost):
             * data stops now, not before, so that our request goes out;
             * the far end has our LN (the endpoint's supersede) */
            h->rc_counted = 1;
            if (h->rc == HST_RC_ASK) h->link.ln_acted = 1;   /* our own request's, not a follow's */
            a->retrains++;
            a->retraining = 1;
        }
        if (st == HST_FRX_DATA && !hst_frx_armed(&h->frx) && h->frx.code == r && (h->rc == HST_RC_FOLLOW || h->rc_left)) {
            /* done: in data at R (asked, the LNA not yet in: it is late) */
            h->rc_late = h->rc == HST_RC_ASK ? h->rc_order : 0;
            h->rc = HST_RC_NONE;
            h->link.epoch++;
            h->code = r;
            h->link.fwd_code = r;
            a->rx_bitrate = h->link.rx_bps = 2400 * r;
            h->hold_t = h->rc_left ? MS(HST_RC_HOLD_MS) : 0;
            a->retraining = 0;
            win_reset(h);
        } else if (h->rct > MS(HST_RC_BOUND_MS) || ((h->rc_st == HST_FRX_TRAIN || h->rc_st == HST_FRX_DATA)
                   && (st == HST_FRX_TRAINFAIL || st == HST_FRX_UNLOCKED || st == HST_FRX_NOCARRIER))) {
            rc_given_up(a);
        } else {
            h->rc_st = st;
        }
        return;
    }
    /* the upshift's window: each ms, good when the receiver is in data
     * under the next rate's threshold (docs/limits.md: read at the rate in
     * force, the receiver's error is the next rate's on the same line) */
    nxt = code_up(h);
    if ((++h->up_t & 7) == 0) {
        h->up_all++;
        if (nxt && st == HST_FRX_DATA && hst_frx_mse(&h->frx) < hst_good_mse[nxt]) h->up_good++;
    }
    if (st != HST_FRX_DATA) {
        /* the receiver out of data by itself, the carrier there: a retrain
         * at the rate in force; within HST_RC_HOLD_MS of a retrain's data,
         * that retrain failed (no carrier: Task 23's watchdogs) */
        if (st == HST_FRX_UNLOCKED || st == HST_FRX_TRAINFAIL) {
            if (h->hold_t > 0) failed(a, h->code);
            else if (!(h->fl_t >= 0 && h->fl_n >= HST_RC_FAILS)) ask(a, 0x20 | h->code);
        }
        return;
    }
    if (h->hold_t > 0) h->hold_t--;
    /* frame errors: HST_DOWN_ERRS within HST_DOWN_MS of the first */
    if (h->fe_t >= 0 && ++h->fe_t > MS(HST_DOWN_MS)) h->fe_t = -1;
    if (d > 0) {
        if (h->fe_t < 0) { h->fe_t = 0; h->fe_n = 0; }
        h->fe_n += d;
    }
    if (h->fe_t >= 0 && h->fe_n >= HST_DOWN_ERRS) {
        h->fe_t = -1;
        lower = code_below(h, h->code);
        if (lower) {
            h->since_down = 0;
            ask(a, 0x20 | lower);
            return;
        }
    }
    if (h->up_t >= MS(HST_UP_MS)) {
        if (nxt && h->since_down >= MS(HST_UP_MS) && h->up_good * 100 >= h->up_all * HST_UP_PCT) {
            ask(a, 0x30 | nxt);
            return;
        }
        h->up_t = h->up_all = h->up_good = 0;
    }
}

/* holding forward, our short training at the code in force (a retrain) */
static void fwd_retrain(answer_t *a)
{
    hst_t *h = &a->u.hst;
    h->next = h->code;
    h->ones = 0;
    hst_ftx_train(&h->ftx, h->code, 0);
    a->retraining = 1; a->retrains++;
}

/* holding forward, connected, no reversal in progress: the far end's
 * 0x2R or 0x3R we granted (rate_order, once our LNA has gone), carried
 * out between frames; and recovery */
static void rate_fwd(answer_t *a)
{
    hst_t *h = &a->u.hst;
    int r = h->rc_order & 15;
    switch (h->rc) {
    case HST_RC_NONE:
        if (h->link.rate_order && !a->retraining) {
            h->rc_order = h->link.rate_order;
            h->link.rate_order = 0;
            r = h->rc_order & 15;
            if (!by_training(h, h->rc_order) && r == h->code) { h->link.epoch++; break; }   /* no change */
            h->rc = HST_RC_WAIT; h->rct = 0;
        } else if (h->link.recover) {
            /* nothing acknowledged for 2 x T401, the far end's back channel
             * there: the short training at the code in force [OURS] */
            h->link.recover = 0;
            if (!a->retraining && (hst_brx_locked(&h->det.rx[0]) || hst_brx_locked(&h->det.rx[1]))) fwd_retrain(a);
        }
        break;
    case HST_RC_WAIT:
        /* the endpoint's frame finishing: fwd_getbit's cut, at most 500 ms */
        if (!h->cut && ++h->rct < MS(500)) break;
        h->cut = 1;
        if (!by_training(h, h->rc_order)) {
            /* the marks from the next symbol (getbit is not called in them) */
            hst_ftx_marks_switch(&h->ftx, r);
            h->cut = 0;
            h->rc = HST_RC_MARKS;
        } else {
            h->rc = HST_RC_ONES; h->rct = 0;
        }
        break;
    case HST_RC_ONES:
        /* ones, so that the far decoder gives the LNA's end before the
         * training restarts it (as a reversal's 20 ms) */
        if (++h->rct >= MS(HST_REV_DRAIN_MS)) {
            h->next = r;
            h->ones = 0;
            hst_ftx_train(&h->ftx, r, 0);
            a->retraining = 1; a->retrains++;
            h->cut = 0;
            h->rc = HST_RC_TRAIN;
        }
        break;
    case HST_RC_MARKS:
        if (!hst_ftx_marking(&h->ftx)) {
            h->code = r;
            a->bitrate = h->link.tx_bps = 2400 * r;
            h->link.fwd_code = r;
            h->link.epoch++;
            h->rc = HST_RC_NONE;
        }
        break;
    default:   /* HST_RC_TRAIN: the retrain's end below set the code */
        if (!a->retraining) { h->link.epoch++; h->rc = HST_RC_NONE; }
        break;
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
 * LNA, by which time we send there ourselves: what it sends between our
 * switch and its stop, control frames only while the reversal is pending,
 * is lost, which is why the endpoint acknowledges anew at the epoch; both
 * back channels share one band, and listening on would be under our own
 * back channel's echo; Task 21 review M2); our back transmitter
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
    rc_clear(h);
    h->rev = HST_REV_LISTEN; h->rt = 0;
}

/* holding back, to the forward channel: our back transmitter stops at
 * once, the forward receiver's equaliser kept, the back receivers start
 * afresh (the far end's back channel begins at its switch). Our training
 * follows (`rev`: GAP, 200 ms, then the short one; TRAIN at once, taking
 * the forward channel back unasked). The endpoint's HDLC transmitter is
 * not reset (spec step 4): a frame the stop cut goes on, after our
 * training, without its opening flag, and the far receiver drops it; the
 * endpoint resends what it carried (Task 21 review M4) */
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
    rc_clear(h);
    if (rev == HST_REV_GAP) h->reversals++;   /* taking it back unasked is a retrain only: the
                                                 far end never switched (Task 21 review M1) */
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
        if (h->link.role == HST_ROLE_FWD && h->link.rev_lna_sent && !a->retraining && h->rc == HST_RC_NONE) {
            h->link.rev_lna_sent = 0;
            h->rev = HST_REV_GRANT; h->rt = 0;
        } else if (h->link.role == HST_ROLE_BACK && h->link.rev_lna_rx && h->rc == HST_RC_NONE) {
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
            if (h->rev == HST_REV_NONE && h->link.agreed_rates) rate_fwd(a);
            /* the back channel is continuous: 5 s without it is a caller gone.
             * Its carrier is a receiver's lock, not the carrier detect: the
             * echo of our forward channel (or line noise) holds that on with no
             * back channel at all (the Task 13 review), and never locks one */
            if (hst_brx_locked(&h->det.rx[0]) || hst_brx_locked(&h->det.rx[1])) h->quiet = 0;
            else if (++h->quiet > MS(HST_LOST_MS)) { ans_fail(a, ANS_END_LOST); return 0; }
        } else if (h->rev == HST_REV_NONE && h->link.agreed_rates) {
            rate_back(a);
        }
        if (h->link.role == HST_ROLE_FWD || h->rev != HST_REV_NONE) {
            /* the frame errors and the upshift count holding back alone */
            h->rx_bad = (uint32_t)h->link.rx_bad;
            win_reset(h);
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
        || h->rev != HST_REV_NONE || h->rc != HST_RC_NONE) return -1;
    fwd_retrain(a);
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
