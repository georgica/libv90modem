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
 * Reversals and rate changes are Tasks 21 and 22, the other watchdogs and
 * the far end's cleardown Task 23. */
#include <string.h>
#include "ans_scheme.h"

#define MS(ms) ((int32_t)(ms) * 8)

static int lane_bps(int lane) { return lane ? 300 : 450; }

/* the forward transmitter's bits: the host's while data moves, ones before
 * and through a retrain (getbit is called only then) */
static int fwd_getbit(void *u)
{
    answer_t *a = (answer_t *)u;
    return a->connected && !a->retraining ? a->cfg.getbit(a->cfg.user) & 1 : 1;
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
    n = hst_brx_locked(r) ? h->det.nb[h->lane] : 0;
    for (k = 0; k < n; k++) {
        if (a->connected) a->cfg.putbit(a->cfg.user, r->bits[k]);
        else (void)hst_pre_put(&h->link, r->bits[k]);
    }
}

static int16_t tick_hst(answer_t *a, int16_t rx)
{
    hst_t *h = &a->u.hst;
    int16_t tx;
    back_rx(a, rx);
    tx = hst_ftx_tick(&h->ftx);
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
    /* the backstop (spec "Retrain, watchdogs, cleardown and end codes") */
    if (!h->link.la_rx && a->t > MS(HST_FAIL_MS)) { ans_fail(a, ANS_END_HST_FAILED); return 0; }
    return tx;
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

/* holding the forward channel: the short training at the rate in force */
static int retrain(answer_t *a)
{
    hst_t *h = &a->u.hst;
    if (a->phase != ANS_HST || !a->connected || a->retraining) return -1;
    h->next = h->code;
    h->ones = 0;
    hst_ftx_train(&h->ftx, h->code, 0);
    a->retraining = 1; a->retrains++;
    return 0;
}

/* holding the forward channel: the back receiver's, the one we take bits from */
static int snr_den(const answer_t *a, int64_t *num, int64_t *den)
{
    const hst_t *h = &a->u.hst;
    if (a->phase != ANS_HST) return -1;
    return hst_brx_snr(&h->det.rx[h->lane], num, den) ? 0 : -1;
}

const ans_scheme_t ans_hst = { tick, cleardown, retrain, NULL, snr_den, NULL, NULL, 0 };
