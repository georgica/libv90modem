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
 * acknowledgement"), and flushes nothing it took after the reset.
 *
 * The facts, each with its source, are in docs/history/hst-facts.md,
 * sections `MNP frames`, `LR and 0xC0`, `Establishment`, `Data and
 * attention` and `Timers`. No allocation, no callbacks, no global state;
 * time arrives through mnp_tick. */
#ifndef V90D_MNP_H
#define V90D_MNP_H
#include <stdint.h>
#include "mnp_frame.h"
#include "v42_lapm.h"

/* our window ceiling: k is 1..8 (spec "Status, end reasons, configuration") */
#define MNP_MAX_K   8
/* Trt of the T401 formula, the round trip with the far end's processing
 * (`Timers`) [OURS] */
#define MNP_TRT_MS  1000
/* establishment T401 (`Establishment`) [OURS] */
#define MNP_T401_EST_MS 2000

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

    int ld_due, ld_reason;

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
/* our transmit and the far end's rate (bit/s): T401 and T404 again */
void mnp_set_rates(mnp_t *m, int tx_bps, int rx_bps);

/* T401 (A.7.5.1): 2 x ((k/2) Lb (Lf + Llt + N401) / tx_bps + Lb (Lf + Lla)
 * / rx_bps) + MNP_TRT_MS, rounded up to the ms, at least floor_ms. Lb, Lf
 * from the framing (2 or 3), Llt, Lla from the layout (`MNP frames`).
 * Rates below 75 bit/s count as 75. */
int  mnp_t401_ms(int framing, int layout, int k, int n401, int tx_bps, int rx_bps, int floor_ms);

#endif
