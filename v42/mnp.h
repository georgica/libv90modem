/* MNP error control, the link (V.42 Annex A, the alternative procedure):
 * establishment as responder, LT and LA with credit and go-back, LN and
 * LNA, LD, the timers T401, T402 and T404 and the count N400.
 *
 * Frame level only, as v42_lapm.h: frame bodies (header and information,
 * no FCS) come in through mnp_rx_frame and go out through mnp_tx_frame;
 * the caller frames them, in framing mode 3 with v42_hdlc.h or in mode 2
 * with the framers of mnp_frame.h (tx2 and rx2 below are there for it) and
 * v42async. `tx_framing` says which framing the frame just returned by
 * mnp_tx_frame goes in: our LR and anything before the link is up go in
 * the framing the caller's LR came in, the rest in the agreed one. While
 * MNP_S_CONFIRMING the caller hears both. mnp_rx_bad is for the framing in
 * use only (the other one's octets can look like broken frames).
 *
 * Receiving: the information of one LT is held at a time. The caller takes
 * it (mnp_rx_avail, mnp_rx_consume) after every mnp_rx_frame, and sets
 * mnp_set_busy while its own buffer has less than k x N401 octets free:
 * our credit is k, not counted down per LT held.
 *
 * Breaks (A.7.4, Tables 4 and 5): an LN of type 1, 2 or 3 holds the
 * user's data (mnp_can_send is 0) until its LNA; type 3 goes out only
 * once the LTs stored at its request are acknowledged. Type 1, the
 * destructive break, resets the link (Tables 4 and 5 note a): every state
 * variable but V(SA) and V(RA) to its initial value, stored LTs and the
 * held LT discarded. Sent: at once LTs are dropped, nothing goes and what
 * comes is discarded until the LNA, then the reset. Received: the reset
 * when the LN is taken, then the LNA; if our own LN 1 is waiting for its
 * LNA, only that LNA resets (both ends broke at once: one reset). Each
 * reset counts in `resets` (compare with a remembered copy, as
 * v42lapm_t's): the caller flushes what it holds for and from the far end
 * at each change. From its own mnp_send_ln(1) until the LNA (ln_code < 0)
 * it holds what the user sends (Table 4: "hold data until receive
 * acknowledgement"): that data is held until the LNA and goes after that
 * LNA's reset, not flushed by it. Every later change of `resets` (a later
 * break from the far end) flushes as usual.
 *
 * The HST profile (spec "HST profile"), on an HST call only
 * (mnp_hst_offer before mnp_accept_lr): our LR carries 0xC0 when the
 * caller's has it with bit 0; agreed (bit 0 from both, a rate in common),
 * `hst` is 1 and:
 *  - we send the streamlined layouts when both LRs carry a non-zero
 *    parameter 8 (`streamlined`); we read every layout, the agreed one
 *    winning a tie; with no parameter 8 from both, the caller's layout is
 *    learned from its first frame that settles it once connected: one
 *    that reads only one way, or an LT with exactly one reading in
 *    sequence. Until then a tie of two types, neither an LT in sequence,
 *    is not acted on (an LA asks again), and the expected N(S) breaks the
 *    others. A standard LD from a caller sending streamlined reads as an
 *    LT (the formats collide), so its LD is missed [OURS];
 *  - LN codes are attention codes, never breaks: as forward holder we
 *    grant 0x1R (a reversal), 0x2R and 0x3R (orders for the pump), each
 *    with its LNA, and the endpoint reports when that LNA's closing flag
 *    has gone (rev_lna_sent; rate_order); holding the back channel, the
 *    far holder's 0x2R/0x3R is passed on (rate_order: the receiver
 *    follows), or above max_up (cap_up_code) refused with a count and our
 *    own 0x2R at the cap, which also follows a reversal granted above it
 *    once the caller's training is in (the next epoch); the LNA to our own
 *    0x2R/0x3R is passed on too (rate_order); a rate not agreed, or
 *    another class, an LNA and a count (`violations`) only;
 *  - holding back, the pump's 0x2R/0x3R supersedes ours still waiting
 *    for its LNA when the far end has it: the pump saw its training
 *    (ln_acted), or it has gone twice (V(SA) moves on as if the LNA had
 *    come; that LNA would come on the forward channel the pump asks about
 *    again); else the request waits for the LNA or the next T401 [OURS];
 *  - holding forward, LTs out and credit given but N(R) still for 2 x
 *    T401 (the line not stopped): `recover` in the mailbox, once per 2 x
 *    T401 (the pump's recovery, spec "Rate changes") [OURS];
 *  - from an attention we send or grant until the pump's epoch changes
 *    (at most MNP_HST_HOLD_MAX_MS) no LT goes, the control frames do; at
 *    the epoch's change an LA goes first (the LAs in flight at a reversal's
 *    switch are lost, hst-facts.md `Reversal`);
 *  - holding the back channel, we ask for the forward one (0x1R at the
 *    current forward rate) when our backlog, the octets not yet
 *    transmitted (queued; the patent's "untransmitted data", not the
 *    unacknowledged ones: an LA lost at a switch would count them until
 *    T401 and ask for a needless reversal), is over MNP_HST_BACKLOG
 *    octets, the far end has held the forward channel MNP_HST_FAR_MS and no attention is outstanding (D14);
 *  - a repeated N(R) sends us back once per N(R) value: the LAs a single
 *    loss draws on the slow channel come one after another, each of them
 *    a go-back under A.7.3.5 a's literal reading; a loss of the frame
 *    sent again is T401's (A.7.3.5 b) [OURS];
 *  - T401 adds to Annex A's formula the far end's one LT ahead of its LA
 *    (an LT at rx_bps, part of Trt's "queuing delay") and the back
 *    receiver's hold-back when we receive the back channel (Trt's
 *    "remote processing"): MNP_HST_LAT_450_MS, _300_MS. T402 (T401/2)
 *    and T404 follow; our LAs go on the fast channel, or at 450/300 bit/s
 *    after the k/2 LTs Annex A asks for.
 * mnp_hst_poll exchanges all of it with the pump's mailbox (hst_link.h).
 *
 * The facts, each with its source, are in docs/history/hst-facts.md,
 * sections `MNP frames`, `LR and 0xC0`, `Establishment`, `Data and
 * attention`, `Timers`, `Reversal` and `Rate changes`. No allocation, no
 * callbacks, no global state; time arrives through mnp_tick. */
#ifndef V90D_MNP_H
#define V90D_MNP_H
#include <stdint.h>
#include <stddef.h>
#include "mnp_frame.h"
#include "v42_lapm.h"
#include "hst_link.h"

/* our window ceiling: k is 1..8 (spec "Status, end reasons, configuration") */
#define MNP_MAX_K   8
/* Trt of the T401 formula, the round trip with the far end's processing
 * (`Timers`) [OURS] */
#define MNP_TRT_MS  1000
/* establishment T401 (`Establishment`) [OURS] */
#define MNP_T401_EST_MS 2000
/* HST: the reversal policy, backlog and the far end's time on the forward
 * channel (`Reversal`, P1:416, P1:466, D14) [PUB] */
#define MNP_HST_BACKLOG     64
#define MNP_HST_FAR_MS      2000
/* HST: an attention's hold on our LTs at most this long, the longest a
 * call goes without a far signal (spec "Retrain, watchdogs") [OURS] */
#define MNP_HST_HOLD_MAX_MS 12000
/* HST: the back-channel receiver delivers its bits this late (its
 * decision delay, measured with the receiver of track B, Task 12) */
#define MNP_HST_LAT_450_MS  127
#define MNP_HST_LAT_300_MS  180

typedef struct {
    int k;            /* window offered, 1..MNP_MAX_K (8) */
    int n401;         /* information field offered, 1..256 (64); 256 needs opt bit 1 */
    int opt;          /* LR parameter 8 offered, 0..3 (2: fixed-field LT and LA) */
    int framing;      /* highest framing mode offered, 2..3 (3) */
    int t401_min_ms;  /* floor of the computed T401 (1500) */
    int n400;         /* tries before giving up, 1..255 (12) */
} mnp_cfg_t;

void mnp_cfg_default(mnp_cfg_t *c);

enum { MNP_S_IDLE = 0, MNP_S_CONFIRMING, MNP_S_CONNECTED, MNP_S_DISCONNECTED };

/* why the link ended: the same values as V42LAPM_R_* */
enum {
    MNP_R_NONE        = V42LAPM_R_NONE,
    MNP_R_LOCAL       = V42LAPM_R_LOCAL,        /* mnp_release (LD 255 sent) */
    MNP_R_PEER_DISC   = V42LAPM_R_PEER_DISC,    /* the far end's LD; peer_ld_reason */
    MNP_R_N400        = V42LAPM_R_N400,         /* tries ran out (LD 4; in establishment LD 1) */
    MNP_R_PROTOCOL    = V42LAPM_R_PROTOCOL,
    MNP_R_NEGOTIATION = V42LAPM_R_NEGOTIATION   /* the LR: LD 2 (constant 1) or 1 */
};

/* LN attention types outside HST (A.6.7): the breaks */
enum { MNP_LN_DESTRUCTIVE = 1, MNP_LN_EXPEDITED = 2, MNP_LN_INSEQ = 3 };
/* V42_BRK_* to an LN type and back; -1 for anything else */
int mnp_ln_of_brk(int brk_opt);
int mnp_brk_of_ln(int ln_type);

#define MNP_LNQ 4    /* LN types received, not yet taken */

typedef struct {
    mnp_cfg_t cfg;
    int state, reason;
    int peer_ld_reason;          /* the far end's LD, 0 none */

    /* agreed by the LRs */
    int framing, k, n401, opt, layout;
    int est_framing;             /* the framing the caller's LR came in */
    int tx_framing;              /* framing of the frame mnp_tx_frame just gave */
    mnp_lr_t lr;                 /* our reply LR */
    int lr_due, lr_tries;
    int est_fail;                /* T401 expiries and bad frames in establishment */
    int tx_bps, rx_bps, t401_ms, t404_ms;

    /* sender (A.7.3.1, A.7.3.4, A.7.3.5): stored LTs ack+1 .. vs-1 */
    uint8_t slot[MNP_MAX_K][MNP_MAX_N401];
    uint16_t slot_len[MNP_MAX_K];
    uint8_t vs;                  /* N(S) of the next new LT */
    uint8_t ack;                 /* the last N(R) received */
    uint8_t nxt;                 /* N(S) of the next LT to go out */
    uint8_t vh;                  /* one past the highest N(S) sent */
    int credit;                  /* N(k) of the last LA, at most k */
    int last_nr;                 /* N(R) of the last LA, -1 none */
    int retries;                 /* go-backs since N(R) last moved (A.7.3.6) */
    int stall;                   /* T401 expiries at credit 0 with no LA */

    /* receiver (A.7.3.2, A.7.3.3) */
    uint8_t vr;                  /* N(S) expected */
    int dup_seen;                /* the first N(S) = V(R) - 1 was ignored */
    int rx_unacked;              /* LTs taken, not yet acknowledged */
    int la_due;
    int adv_credit;              /* N(k) of our last LA */
    int busy;
    uint8_t hold[MNP_MAX_N401];  /* the information of one LT, for the user */
    int hold_len, hold_pos;

    /* attention (A.7.4) */
    int ln_code;                 /* our outstanding LN's type, -1 none */
    int ln_send, ln_tries;       /* ln_tries: sends of the outstanding LN */
    int brk_wait;                /* our destructive LN is out, its LNA not in */
    uint8_t vsa, vra;
    int lna_due; uint8_t lna_nr;
    int lnq[MNP_LNQ], lnq_n;
    int attn_tx, attn_rx;        /* the last LN type sent, received */

    /* the HST profile (above) */
    int hst_offer;               /* an HST call: 0xC0 offered (mnp_hst_offer) */
    uint8_t hst_offer_rates;     /* its rate mask */
    int hst, streamlined, back300;
    uint8_t hst_rates;           /* the agreed rate mask */
    int rx_layout;               /* mnp_hdr_parse's agreed_layout for what comes */
    int gb_nr;                   /* N(R) of the last go-back on a repeated LA, -1 none */
    int hst_role, hst_cap;       /* the pump's role and cap_up_code, as last polled */
    unsigned hst_epoch;          /* its epoch, as last polled */
    int la_told;                 /* the mailbox has la_rx */
    int attn_hold, attn_hold_ms; /* no LT until the epoch changes; ms held */
    int rev_grant;               /* we granted a 0x1R: its LNA not yet out */
    int rate_grant;              /* we granted a 0x2R/0x3R (its code): its LNA not yet out */
    int rev_lna_out;             /* that LNA (either grant's) has just been framed (for the endpoint) */
    int rev_wait;                /* the endpoint waits for that LNA's closing flag */
    unsigned long rev_frame0;    /* the HDLC frame count when it was framed */
    int rev_rx, rev_code;        /* our 0x1R's LNA came (for the mailbox); R */
    int rate_order;              /* 0x2R/0x3R for the mailbox, 0 none (holding back: the holder's
                                    own, or the LNA to ours) */
    int cap_due;                 /* our 0x2R at the cap: 1 at the next epoch, 2 now */
    int far_ms;                  /* holding back: ms the far end has held forward */
    uint8_t rec_ack;             /* holding forward: N(R) as it stood ... */
    int noack_ms;                /* ... and ms since, LTs out, the line not stopped (recover) */

    int ld_due, ld_reason, ld_layout;

    /* timers, ms left, 0 stopped; held while the line is stopped */
    int t401, t402, t404, ln_t, est_t;
    int stopped;

    /* framing mode 2, for the caller */
    mnp2_tx_t tx2;
    mnp2_rx_t rx2;

    /* events for the layer above: compare with a remembered copy */
    unsigned long resets;        /* destructive breaks that reset the link */

    /* statistics */
    unsigned long tx_lt, tx_retx, rx_lt, rx_dropped, tx_la, rx_la;
    unsigned long t401_expiries, rx_bad, rx_unknown, violations;
    int rx_unknown_id;
} mnp_t;

typedef char mnp_size_check[sizeof(mnp_t) <= 4096 ? 1 : -1];

void mnp_init(mnp_t *m, const mnp_cfg_t *c);
/* Responder: the caller's LR (a body; framing_in 2 or 3) arrived in
 * MNP_S_IDLE. Parse, agree, queue our LR: 0 and MNP_S_CONFIRMING, or the
 * MNP_R_* reason with an LD queued and MNP_S_DISCONNECTED. */
int  mnp_accept_lr(mnp_t *m, const uint8_t *f, int len, int framing_in);
/* a frame body with a good FCS */
void mnp_rx_frame(mnp_t *m, const uint8_t *f, int len);
/* a frame dropped by the framer (bad FCS, too long) in a framing heard */
void mnp_rx_bad(mnp_t *m);
/* the next frame body into f (MNP_MAX_BODY octets of room): its length,
 * 0 when nothing is due; its framing in m->tx_framing */
int  mnp_tx_frame(mnp_t *m, uint8_t *f);

/* the largest LT information a new mnp_send may take now, 0 none */
int  mnp_can_send(const mnp_t *m);
/* one LT of n octets (1..mnp_can_send): 0, or -1 if not allowed */
int  mnp_send(mnp_t *m, const uint8_t *d, int n);
/* LTs stored and not yet acknowledged */
int  mnp_unacked(const mnp_t *m);

/* the information of the last LT, not yet taken. The caller takes it
 * before the next frame arrives, or that LT is refused (no room) and
 * comes again. */
int  mnp_rx_avail(const mnp_t *m, const uint8_t **p);
void mnp_rx_consume(mnp_t *m, int n);
/* the user cannot take data: our credit is 0 until cleared */
void mnp_set_busy(mnp_t *m, int busy);

/* LN of this type (0..255; 1 to 3 the breaks, see above): 0, or -1 if
 * not connected or one outstanding */
int  mnp_send_ln(mnp_t *m, int code);
/* 1 and the type of the next LN received, once each; 0 none */
int  mnp_get_ln(mnp_t *m, int *code);

void mnp_tick(mnp_t *m, unsigned ms);
/* the modem's own pause (a retrain): every timer held from stopped to
 * restored, then resumed where it stood */
void mnp_line_stopped(mnp_t *m);
void mnp_line_restored(mnp_t *m);
/* LD reason 255, MNP_S_DISCONNECTED with MNP_R_LOCAL */
void mnp_release(mnp_t *m);
/* the line is gone: MNP_S_DISCONNECTED with MNP_R_LOCAL at once, no LD
 * (one already due still goes); nothing else is sent */
void mnp_abort(mnp_t *m);
/* our transmit and the far end's rate (bit/s): T401 and T404 again */
void mnp_set_rates(mnp_t *m, int tx_bps, int rx_bps);

/* The HST profile. On an HST call, before mnp_accept_lr: our 0xC0 offer
 * (L->offer_rates) and the pump's state as it stands. */
void mnp_hst_offer(mnp_t *m, const hst_link_t *L);
/* The mailbox, at every poll (hst_link.h): ms since the last (0 between
 * ticks), queued the octets written and not yet in an LT. Writes la_rx,
 * far_ld, rate_order, rev_lna_rx and rev_code; takes ln_req, role,
 * cap_up_code, fwd_code, epoch and the rates. The agreement and
 * rev_lna_sent are the endpoint's to write (agreed when the LRs are
 * exchanged; rev_lna_sent from rev_lna_out and its HDLC transmitter). */
void mnp_hst_poll(mnp_t *m, hst_link_t *L, unsigned ms, size_t queued);

/* T401 (A.7.5.1): 2 x ((k/2) Lb (Lf + Llt + N401) / tx_bps + Lb (Lf + Lla)
 * / rx_bps) + MNP_TRT_MS, rounded up to the ms, at least floor_ms. Lb, Lf
 * from the framing (2 or 3), Llt, Lla from the layout (`MNP frames`).
 * Rates below 75 bit/s count as 75. */
int  mnp_t401_ms(int framing, int layout, int k, int n401, int tx_bps, int rx_bps, int floor_ms);

#endif
