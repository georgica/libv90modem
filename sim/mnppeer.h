/* Test peer: an MNP error-correcting entity (ITU-T V.42 Annex A, the
 * alternative procedure) on a bit stream, for either end of a call.
 *
 * Written from V.42 (03/93) Annex A and docs/history/hst-facts.md only,
 * independently of the library's MNP code: it has its own CRCs (CRC-16/X.25 for framing
 * mode 3, the mode 2 FCS of `MNP framing`), its own HDLC bit layer, its own
 * start-stop characters, its own header code and its own link procedure,
 * so that it checks our MNP endpoint against the Recommendation and not
 * against itself (the sim/v32call.h rule). It includes no library header.
 *
 * Use: mnppeer_init, then once a bit time mnppeer_tx_bit (the bit it puts
 * on the line) and mnppeer_rx_bit (the bit it hears), and mnppeer_tick
 * as the milliseconds pass. Data in with mnppeer_write, out with
 * mnppeer_read; breaks (LN) with mnppeer_send_ln and mnppeer_get_ln;
 * mnppeer_release sends LD reason 255 and stops.
 *
 * What it does (Annex A):
 *  - The originator sends its LR (after `lr_delay_ms`; after T400 = 750 ms
 *    of ODP with `odp_first`) in framing mode `framing`: 2, start-stop with
 *    SYN DLE STX ... DLE ETX FCS; 3, HDLC. It sends it again every T401 and
 *    on a frame with a bad FCS (A.7.1.2 b), up to N400 times, then stops without an LD (A.7.2.2). On the responder's
 *    LR it counts as connected and confirms with an LA, N(R) 0 and N(k) its
 *    credit, in the layout that LR set (A.6.4.2) and the establishment
 *    framing; it confirms again on a repeated LR, which it hears until the
 *    responder is heard in the agreed framing.
 *  - The responder listens in both framings, negotiates (A.7.1.5: the lower
 *    framing mode, the lower k and N401; parameter 8 the AND, in the reply
 *    only if the LR had it and the AND agrees a facility), replies in the framing the LR came in (after
 *    `lr_delay_ms`), sends its LR again on T401, a bad frame or a repeated
 *    LR, once (A.7.1.3), and counts as connected on the LA or on an LT.
 *    LD reason 2 for constant parameter 1 not 2. For parameters that fail
 *    negotiation (and for an LR without constant parameter 2, framing mode,
 *    k or N401) the originator sends 3; the responder sends 1, since code 3
 *    is the initiator's only (Table A.5 Note), as it does when its LR tries
 *    run out [OURS]. The responder takes the confirming LA in either layout. Parameter 8 bit 1 agreed makes N401 256. After the LRs
 *    each end hears only the agreed framing.
 *  - Data phase (A.7.3): LT with V(S) from 1, at most S(k) beyond the last
 *    N(R); LA on an invalid frame, an out-of-sequence LT (not the first
 *    N(S) = V(R) - 1), an LT without credit, T404, and for received LTs not
 *    yet acknowledged by the k/2, no-data and T402 rules; go-back-N on an LA
 *    repeating the last N(R) or on T401; N400 tries, then LD reason 4.
 *    Credit is the room in the receive ring in maximum-length frames, at
 *    most k, zero for `zero_credit_ms` after connecting (an LA with N(k) 0
 *    goes out at once, and one with the credit when it ends). An LT beyond
 *    every N(R) + N(k) we have sent (at the start, the LRs' k) is counted in
 *    `rx_over_credit` and handled as one without credit.
 *  - LN and LNA (A.7.4): one LN outstanding, V(SA) from 1, sent again every
 *    T401, N400 tries; a received LN with N(SA) = V(RA) is delivered once;
 *    every valid LN gets its LNA, except one that finds the receive queue
 *    full: it is counted (`ln_rx_full`), not taken and not acknowledged, so
 *    the sender brings it again. mnppeer_send_ln says when its queue is
 *    full. The LN's type octet is passed through as `code`. Type 1, the
 *    destructive break, follows Tables 4 and 5 and their note a: asked
 *    for (mnppeer_send_ln), the write and read queues are emptied, no LT
 *    goes and what comes is discarded until its LNA, then every LT/LA
 *    state variable goes back to its start; received, the same queues
 *    are emptied and the state reset when the LN is taken, then the LNA,
 *    except during our own destructive break, whose LNA resets once.
 *    `resets` counts them.
 *    Type 3, the non-expedited break, goes once the data written before
 *    it has gone in LTs and every LT is acknowledged, before any data
 *    written after it, which it holds until its LNA (A.7.4.1, Table 4).
 *    Type 2 does nothing to data.
 *
 * Layouts: standard, or Annex A's optimised LT and LA when both LRs carry
 * parameter 8 and its AND has bit 2 (A.6.4.1.8); `layout_rx_any` takes
 * either in any case (and, on HST, the streamlined ones too).
 *
 * HST mode (hst-facts.md `MNP frames`, `LR and 0xC0`, `Data and
 * attention`), with `c0`: the LR carries USR's `C0 02 flags rates`. The
 * originator takes HST as agreed when the reply's 0xC0 has bit 0 (and so
 * did ours) and a non-zero rate mask; the responder replies with the merge
 * (bit 0 from both, 300 if either asks, the rates' AND, AND 0xFC [BM]),
 * only to an LR whose 0xC0 has bit 0. Agreed, both ends send the
 * streamlined layouts (no LI: LT `04 N(S) data`, LA `05 N(R) N(k)`, LN `06
 * N(SA) code`, LNA `07 N(RA)`, LD `02 reason`) when both LRs carry a
 * non-zero parameter 8, or always with `stream_always` (the patent
 * streamlines every frame; the Courier's LR has no parameter 8). The
 * originator's confirming LA stays standard [BM]. Received in the
 * streamlined layout: a frame whose first octet is a type with the length
 * of its layout; an LR by its second octet 0x01 (its first, an LI, is no
 * type); any other first octet is counted in `rx_unknown`. LN codes are
 * attention codes, passed through: none is a break. `unknown_ids` N: every
 * Nth new LT goes once with first octet 0x3F (`3F N(S) data`, counted in
 * `tx_unknown`), and when it is sent again, as an ordinary LT (the patent's
 * reformat).
 *
 * T401 in the data phase: `t401_ms`, or with it 0 and `tx_bps`, `rx_bps`
 * given, Annex A's formula (A.7.5.1) for the agreed framing, layout, k and
 * N401: 2 ((k/2) Lb (Lf + Llt + N401) / tx + Lb (Lf + Lla) / rx) + Trt, Trt
 * 1000 ms, at least 1500 ms (`Timers`), rounded up to the ms; else 4000.
 *
 * Every frame this peer sends is lost with probability `lose_ppm` per
 * million and has one bit inverted with probability `corrupt_ppm` (after
 * its FCS), from a generator seeded with `seed`. No allocation, no global
 * state; two peers in a process are independent. */
#ifndef V90D_SIM_MNPPEER_H
#define V90D_SIM_MNPPEER_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int originator;      /* 1: send the first LR (the caller); 0: answer one */
    int framing;         /* framing mode offered: 2 or 3 (0: 3) */
    int k;               /* window offered, 1..MNPP_MAX_K (0: 8) */
    int n401;            /* information field offered, 1..MNPP_MAX_N401 (0: 64) */
    int opt;             /* LR parameter 8's value, when send_opt */
    int send_opt;        /* 1: the LR carries parameter 8 */
    int c0, c0_flags, c0_rates;   /* HST: the 0xC0 parameter, sent when c0 (see above) */
    int layout_rx_any;   /* 1: take LT and LA in either Annex A layout */
    int lose_ppm, corrupt_ppm;    /* impairments of the frames we send */
    uint32_t seed;
    int zero_credit_ms;  /* credit 0 for this long after connecting */
    int lr_delay_ms;     /* our LR goes out no earlier than this (the responder: after the caller's) */
    int odp_first;       /* originator: V.42's ODP for T400 before the LR */
    int unknown_ids;     /* HST: every Nth new LT once with first octet 0x3F (0: none) */
    int never_lr;        /* 1: never send an LR */
    int t401_ms;         /* T401 (0: 2000 ms in establishment, 4000 ms in data) */
    int n400;            /* N400 (0: 12) */
    int confirm_la_standard;  /* originator: the confirming LA in the standard
                                 layout even when bit 2 is agreed, as BinModem
                                 sends it [BM] (Annex A A.6.4.2 says A.3b) */
    int stream_always;   /* HST agreed: streamlined layouts even without parameter 8 */
    int tx_bps, rx_bps;  /* our transmit rate, the far end's: T401 from the formula */
    int drop_ln;         /* the first N LN frames received are lost: neither taken nor
                            acknowledged (a peer deaf to attention; the final review's m2) */
    int ln_nsa_fixed;    /* HST: V(SA) does not step on an LNA, so every LN goes with the
                            first's N(SA) (1), a new attention told only by its code, as a
                            Courier sent 0x35, 0x36 and 0x37 (hst-facts.md `Rate changes`, [REC]) */
} mnp_peer_cfg;

enum { MNPP_IDLE = 0, MNPP_CONNECTED, MNPP_DISCONNECTED };
/* layouts, for last_layout_rx */
enum { MNPP_LAYOUT_STD = 0, MNPP_LAYOUT_OPT, MNPP_LAYOUT_STREAM };

#define MNPP_MAX_K    16
#define MNPP_MAX_N401 256
#define MNPP_RING     4096
#define MNPP_BODY     (5 + MNPP_MAX_N401)               /* standard LT header + data */
#define MNPP_LINE     (2 * (MNPP_BODY + 3) + 8)          /* a mode 2 frame, every octet a DLE */
#define MNPP_LNQ      8

typedef struct {
    mnp_peer_cfg cfg;
    int state;                    /* MNPP_* */
    int phase;                    /* establishment step (mnppeer.c) */
    uint32_t now_ms;              /* since init */
    uint32_t connect_ms;          /* now_ms when connected */
    /* agreed (after the LRs) */
    int framing, k, n401, layout;
    int n400;
    int est_framing;              /* establishment frames go in this framing */
    int est_listen;               /* data phase: still hear est_framing too */
    uint8_t lr_opt; int lr_have_opt; /* our reply's parameter 8 (responder) */
    int est_tries; int est_t;     /* LR sends so far; T401 left (ms, 0 off) */
    int wait_t;                   /* responder: time left to wait for an LR */
    int lr_due;                   /* responder: ms until the reply LR may go */

    /* the frame on the line */
    uint8_t line[MNPP_LINE];
    int line_n, line_i, line_bit; /* octets, the current one, its bit (-1: start bit, 8: stop) */
    int line_framing;             /* 2 or 3; 0: nothing being sent */
    int tx_ph;                    /* mode 3: 1 opening flag, 2 data, 3 closing flag */
    int tx_ones;                  /* mode 3: ones in a row, for zero insertion */
    int gap;                      /* mode 2: mark bits after each octet (the ODP) */
    int gap_left;
    int sent_hdlc;                /* an HDLC frame has gone: idle is flags */
    int flag_i;                   /* idle flag's bit */

    /* receivers: HDLC */
    int h_ones, h_in, h_bits; uint8_t h_cur;
    uint8_t h_buf[MNPP_BODY + 4]; int h_n;
    /* receivers: start-stop characters and mode 2 frames */
    int s_state, s_bits; unsigned s_c;
    int m_state; uint8_t m_prev, m_f0;
    uint8_t m_buf[MNPP_BODY + 4]; int m_n;

    /* sender (A.7.3.1, A.7.3.4, A.7.3.5) */
    uint8_t txq[MNPP_RING]; size_t txq_h, txq_n;
    uint8_t sto[MNPP_MAX_K][MNPP_MAX_N401]; int sto_n[MNPP_MAX_K];
    uint8_t vs;                   /* V(S): N(S) of the next new LT */
    uint8_t ack;                  /* last N(R) received */
    uint8_t nxt;                  /* N(S) of the next LT to go (ack+1 .. vs) */
    int credit;                   /* N(k) of the last LA */
    int last_nr;                  /* N(R) of the last LA, -1 none */
    int t401, retries;

    /* receiver (A.7.3.2, A.7.3.3) */
    uint8_t rxq[MNPP_RING]; size_t rxq_h, rxq_n;
    uint8_t vr;                   /* V(R): N(S) expected */
    int dup_seen;                 /* N(S) = V(R) - 1 seen once */
    int unacked;                  /* LTs accepted, not yet acknowledged */
    int la_now;                   /* an LA is due at the next frame */
    int conf_la;                  /* originator: the confirming LA is due */
    int t402, t404;
    int adv_credit;               /* N(k) of our last LA */
    uint8_t adv_edge;             /* the furthest N(S) our LAs (or LR's k) allowed */
    int zc_left;

    /* attention (A.7.4) */
    int lnq[MNPP_LNQ], lnq_n;     /* codes to send */
    size_t lnq_ahead[MNPP_LNQ];   /* octets written before each (type 3: they go first) */
    int ln_code;                  /* the outstanding LN's code, -1 none */
    int ln_send;                  /* it is due on the line */
    int ln_t, ln_tries;
    uint8_t vsa, vra;
    int lna_due; uint8_t lna_nr;
    int lnr[MNPP_LNQ], lnr_n;     /* codes received */
    int brk_wait;                 /* our destructive LN is out, its LNA not in */

    /* disconnect */
    int ld_due, ld_reason;

    uint32_t rng;

    /* counters */
    int rx_lt;                    /* valid LT frames received (any N(S)) */
    int tx_lt;                    /* LT frames sent, first time */
    int tx_retx;                  /* LT frames sent again */
    int rx_ld_reason;             /* reason of the LD received, 0 none */
    int last_layout_rx;           /* layout of the last LT or LA received, -1 none */
    int rx_bad;                   /* frames failing their FCS, or malformed */
    int rx_over_credit;           /* LTs beyond any credit we gave (N(R) + N(k)) */
    int ln_rx_full;               /* LNs not taken (and not acknowledged): queue full */
    int ln_dropped;               /* LNs lost by drop_ln */
    int rx_unknown;               /* valid frames of an unknown type */
    int tx_unknown;               /* HST: LTs sent with first octet 0x3F */
    /* HST (agreed by the LRs' 0xC0) */
    int hst, hst_rates, back300;
    uint8_t c0_reply_flags, c0_reply_rates; int c0_reply;   /* responder: our reply's 0xC0 */
    int unk_n;                    /* new LTs counted for unknown_ids */
    uint8_t unk[MNPP_MAX_K];      /* the stored LT went with 0x3F, not yet again */
    int syn;                      /* the frame being handled came streamlined */
    int t401_f;                   /* data phase T401 from the formula, 0 none */
    int resets;                   /* destructive breaks that reset the link */
    int tx_frames, rx_frames;
} mnppeer_t;

void   mnppeer_init(mnppeer_t *p, const mnp_peer_cfg *c);
int    mnppeer_tx_bit(mnppeer_t *p);
void   mnppeer_rx_bit(mnppeer_t *p, int bit);
void   mnppeer_tick(mnppeer_t *p, unsigned ms);
size_t mnppeer_write(mnppeer_t *p, const uint8_t *d, size_t n);
size_t mnppeer_read(mnppeer_t *p, uint8_t *d, size_t max);
int    mnppeer_send_ln(mnppeer_t *p, int code);   /* 1 queued, 0 queue full */
int    mnppeer_get_ln(mnppeer_t *p);   /* the next LN code received, -1 none */
void   mnppeer_release(mnppeer_t *p);
int    mnppeer_state(const mnppeer_t *p);

#endif
