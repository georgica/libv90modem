/* The HST mailbox: what the MNP endpoint (v42.h) and the HST pump
 * (modem/, the answerer's ANS_HST branch) tell each other on an HST call
 * (spec "Tying the endpoint to the modem", decisions 8, 18 to 21).
 *
 * The mailbox lives in the pump's engine. The endpoint reaches it through
 * a get function and a context given to v42ec_set_link(): get returns the
 * modem's decision in *state and, only while it is HST_LINK_ON, the
 * mailbox (NULL otherwise). The endpoint calls it from v42ec_tx_bit,
 * v42ec_rx_bit, v42ec_tick and v42ec_reset only, on the host's thread: no
 * concurrency. get has no side effects: it only reports.
 *
 *   HST_LINK_UNDECIDED  the modem has not decided yet (ANSam, the HST
 *                       wait, any training not yet connected): the
 *                       endpoint holds T400;
 *   HST_LINK_NONE       connected or ended on another path, reported from
 *                       the sample that raises CONNECTED on, and final for
 *                       the call: the endpoint is the unlinked one, T400
 *                       counted from here, and get is not asked again
 *                       until the endpoint's next init or reset;
 *   HST_LINK_ON         an HST call (ANS_HST): no T400, no start-stop; the
 *                       endpoint hunts for an LR from the first bit of the
 *                       pre-connect ring until our training ends plus
 *                       V42EC_HST_WINDOW_MS, then NO_EC. ON lasts the
 *                       whole call: the endpoint latches it, and a later
 *                       NONE (the modem left ANS_HST) gives neither T400
 *                       nor start-stop (an unstarted window starts then).
 *
 * The pump starts each call with a zeroed mailbox: the endpoint tells
 * calls apart by it (ep_seen 0, or the ring's read count gone back), at
 * its reset at CONNECTED and at every poll, so that an endpoint the host
 * drives before CONNECTED and never resets between calls starts again at
 * a new call's first ON. The rule for the pump that follows from it:
 * within a call it never zeroes the mailbox, clears ep_seen, or moves
 * pre_r back, whatever happens to the call (a channel reversal, a
 * retrain, a rate change, coming back to the HST phase after another
 * state used the storage the mailbox lives in). Any of those would read
 * as a new call, and the endpoint would drop its MNP link mid-call.
 *
 * After the LRs (the HST profile, v42/mnp.h): the endpoint writes agreed,
 * agreed_rates and back300 once, la_rx when the link is up, rev_lna_sent
 * and rev_code after the last bit of the closing flag of an LNA that
 * grants a 0x1R, rev_lna_rx and rev_code when our 0x1R's LNA arrives,
 * rate_order for a 0x2R or 0x3R (by role, below), far_ld on the far end's
 * LD, ep_idle after every bit, and rx_bad and recover at every poll. It
 * takes ln_req when no LN of its own is outstanding (and clears it), and
 * from an attention it sends or grants sends no LT until epoch changes (at
 * most 12 s); it reads role, cap_up_code and fwd_code at every poll and
 * the rates when they change.
 *
 * rate_order by role (Task 22b). Holding forward: the far end's 0x2R or
 * 0x3R we granted, written once that LNA and its closing flag have left
 * (as rev_lna_sent), for the pump to carry out (0x2R the short training at
 * R, 0x3R the switch by marks), between frames. Holding back: the change
 * the holder is about to make, for the forward receiver to follow: the far
 * holder's own 0x2R/0x3R at R <= cap_up_code, at once, or the LNA to our
 * own 0x2R/0x3R (ln_req's, or the endpoint's at the cap) when it arrives.
 * Either way the pump clears it and bumps epoch once the change is done
 * (or given up), which lets the endpoint's LTs go again.
 *
 * The pre-connect ring: from ANS_HST until the modem is connected the pump
 * appends the back receiver's bits (hst_pre_put); the endpoint drains them
 * into its receive path (hst_pre_get) in v42ec_tick and at the top of
 * v42ec_tx_bit and v42ec_rx_bit, before the bit in hand, so they count as
 * line bits for detection and an LR sent during our training is answered
 * at our first transmitted bit. pre_w and pre_r count bits and wrap (a
 * multiple of HST_PRE_BITS); bit i is bit i % 8 of pre[(i % HST_PRE_BITS)
 * / 8]. A full ring drops its oldest bit for the new one and counts it in
 * pre_lost, so the bits the endpoint reads stay continuous with the first
 * one putbit brings. HST_PRE_BITS covers 2.27 s at 450 bit/s, more than
 * our forward training (1.4 s, provisional, decision 4): a ring that is
 * never drained before CONNECTED (a host that does not tick the endpoint
 * then) still holds an LR sent during it.
 *
 * The pump stops appending at the sample where putbit starts: no bit
 * reaches the endpoint both ways.
 *
 * This header includes nothing of modem/: modem/ includes it. */
#ifndef V90D_HST_LINK_H
#define V90D_HST_LINK_H
#include <stdint.h>

enum { HST_LINK_UNDECIDED = 0, HST_LINK_NONE, HST_LINK_ON };
enum { HST_ROLE_FWD = 0, HST_ROLE_BACK };      /* we hold the forward / the back channel */
#define HST_PRE_BITS 1024

typedef struct {
    /* the pump writes */
    uint8_t  offer_rates;     /* 0xC0 rates: n = 2..6 with 2400n <= max_down_bps (0x7C unlimited) */
    uint8_t  cap_up_code;     /* highest rate code we may receive (max_up_bps), 6 unlimited */
    int      trained;         /* our first training has gone out */
    int      role, tx_bps, rx_bps, fwd_code;   /* the endpoint recomputes T401 (mnp_set_rates) when they change */
    int      ln_req;          /* attention the pump wants sent (0x1R/0x2R/0x3R), 0 = none; endpoint clears it */
    int      ln_acted;        /* holding back: the pump saw the holder's training for the 0x2R/0x3R it
                                 asked (ln_req), outstanding: the far end has it; the endpoint clears
                                 it at every LN it sends (mnp.h, the supersede) */
    unsigned epoch;           /* bumped when a reversal or rate change has completed */
    uint8_t  pre[HST_PRE_BITS / 8]; unsigned pre_w, pre_r; unsigned long pre_lost;
    /* the endpoint writes */
    int      agreed;          /* the LR exchange is done; agreed_rates, back300 valid */
    uint8_t  agreed_rates;    /* 0: plain MNP, the pump stays at the opening rate */
    int      back300;
    int      la_rx;           /* the caller's confirming LA arrived: the post-agreement retrain */
    int      rev_lna_sent;    /* we granted a 0x1R and that LNA and its closing flag have left */
    int      rev_lna_rx;      /* our 0x1R was granted (its LNA arrived) */
    int      rev_code;        /* R of that reversal */
    int      rate_order;      /* 0x2R or 0x3R, 0 = none; the pump clears it. Holding forward: ours to
                                 carry out (our LNA has gone); holding back: the holder's, the receiver
                                 follows it (above) */
    int      far_ld;          /* the far end sent LD */
    int      ep_idle;         /* after our last bit: between frames, at an octet boundary
                                 (a frame's closing flag is all out) */
    unsigned long rx_bad;     /* frames received broken (bad FCS, too long, not parsed), counting
                                 up: the rate policy's frame errors */
    int      recover;         /* holding forward: LTs out, credit given, and nothing acknowledged
                                 for 2 x T401 (the line not stopped); once per 2 x T401. The pump
                                 clears it (recovery, spec "Rate changes") */
    int      ep_seen;         /* the endpoint has polled this mailbox ON: 0 in a new call's
                                 zeroed mailbox, which is how the endpoint knows it */
} hst_link_t;

typedef hst_link_t *(*hst_link_get_fn)(void *ctx, int *state);   /* state: HST_LINK_* */

/* The pump: one received bit into the ring. 0, or -1 when it was full
 * and its oldest bit has been dropped (counted). */
static inline int hst_pre_put(hst_link_t *l, int bit)
{
    unsigned i;
    uint8_t m;
    int full = l->pre_w - l->pre_r >= HST_PRE_BITS;
    if (full) { l->pre_r++; l->pre_lost++; }
    i = l->pre_w % HST_PRE_BITS;
    m = (uint8_t)(1u << (i % 8));
    if (bit) l->pre[i / 8] = (uint8_t)(l->pre[i / 8] | m);
    else l->pre[i / 8] = (uint8_t)(l->pre[i / 8] & (uint8_t)~m);
    l->pre_w++;
    return full ? -1 : 0;
}

/* The endpoint: the oldest bit (0 or 1), or -1 when the ring is empty. */
static inline int hst_pre_get(hst_link_t *l)
{
    unsigned i;
    if (l->pre_r == l->pre_w) return -1;
    i = l->pre_r % HST_PRE_BITS;
    l->pre_r++;
    return (l->pre[i / 8] >> (i % 8)) & 1;
}

#endif
