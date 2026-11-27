/* V.90 Phase 2 (probing / ranging), digital modem side: V.90 9.2.1.
 *
 *   INFO0d, tone B ... reversal 40 ms after tone A's ... silence, receive
 *   L1/L2 ... tone B, reversal 40 ms after tone A's, L1, L2 ... INFO1d ...
 *   receive INFO1a.
 *
 * Sample-driven: one received sample in, one transmit sample out. */
#ifndef V90D_MODEM_P2D_H
#define V90D_MODEM_P2D_H
#include <stdint.h>
#include "dpsk.h"
#include "probe.h"
#include "info.h"

typedef struct {
    int tx_dbm0;        /* nominal Phase 2 power, -6 .. -21 dBm0 */
    int max_tx_code;    /* INFO0d bits 33:37: maximum power, 0.5 dB steps below -0.5 dBm0 */
    int alaw;           /* PCM law of our codec */
    int io_delay;       /* round-trip delay of our own sample path to the line terminals */
    int up_sr_mask;     /* upstream symbol rates our receiver offers: bit n = rate index n
                           (0 = 2400 ... 5 = 3429); V.90 needs 3, 4 or 5 */
    int up_hi_carrier;  /* bit n = use the high carrier for rate index n */
} p2d_cfg;

typedef enum {
    P2D_INFO0,          /* sending INFO0d (+ tone B), waiting for INFO0a and tone A */
    P2D_WAIT_REV1,      /* tone B; waiting for the first tone A reversal */
    P2D_REPLY1,         /* timed tone B reversal, 10 ms more, then silence */
    P2D_WAIT_REV2,      /* silent; waiting for the second tone A reversal */
    P2D_RECOVER,        /* 9.2.1.2.3: silent, waiting for tone A again */
    P2D_PROBE_RX,       /* receiving L1 / L2 */
    P2D_WAIT_REV3,      /* tone B; waiting for tone A and its reversal */
    P2D_REPLY3,         /* timed tone B reversal, 10 ms more */
    P2D_L1, P2D_L2,     /* sending the probing signals; L2 until tone A is heard */
    P2D_INFO1D,
    P2D_WAIT_INFO1A,    /* silent; INFO1a, or INFO0a again (9.2.1.2.1: back to INFO0) */
    P2D_DONE,
    P2D_FAILED,
    /* V.92 short Phase 2 (9.4.1), after the INFO0 exchange when both INFO0s
     * ask for it; no probing and no INFO1d */
    P2D_SHORT_REV,      /* Tone A heard, Tone B >= 50 ms: reverse it, 10 ms more, then silence */
    P2D_SHORT_WAIT_REV, /* silent; waiting for the caller's Tone A reversal (RTDEd) */
    P2D_SHORT_INFO1A,   /* silent; INFO1a of Table 18 or 19/V.92 */
    P2D_NSTATES         /* the count, not a state: a new state goes above it
                           (tools/trace.c's p2n[] is checked against it) */
} p2d_state;

enum { P2D_OK = 0, P2D_ERR_INFO0 = 1, P2D_ERR_REV, P2D_ERR_TONE_A, P2D_ERR_INFO1A,
       P2D_ERR_PCM_UP,      /* INFO1a selects PCM upstream where Table 18/V.92 "shall not" be used */
       P2D_ERR_TIME };      /* Phase 2 ran past P2D_EXCHANGE_MS */

/* Bounds on a caller that keeps Phase 2 going without end: INFO0a repeated
 * (each one takes WAIT_REV1 back to INFO0), or Tone A reversed once but
 * never twice (WAIT_REV2 -> RECOVER -> WAIT_REV1 ...). Far above a normal
 * Phase 2 (3 to 5 s). */
#define P2D_EXCHANGE_MS 20000   /* from p2d_init / p2d_init_retrain to P2D_DONE */
#define P2D_MAX_RECOVER 4       /* entries into P2D_RECOVER (9.2.1.2.3); the next one fails */
/* INFO0a again once the INFO0 exchange is behind us (9.2.1.2.1): from
 * WAIT_REV2 or RECOVER, or after our INFO1d (a caller that has lost it and
 * starts Phase 2 over); each takes us back to INFO0 with INFO0d, bit 28 set */
#define P2D_MAX_RESTART 2       /* such returns to INFO0; a later INFO0a is ignored */

/* how the call goes on after P2D_DONE (p2d_t.mode) */
enum { P2D_MODE_V34 = 0,    /* INFO1a of Table 11/V.90: V.34 (V.90 9.2.1.1.8) */
       P2D_MODE_V90 = 1,    /* PCM downstream, V.34 upstream: Phase 3 of V.90 */
       P2D_MODE_V92 = 2 };  /* PCM both ways: Phase 3 of V.92 (9.5/V.92) */

typedef struct {
    p2d_cfg   cfg;
    p2d_state state;
    int       error;
    int32_t   now, t;               /* sample clock; samples in the current state */
    int32_t   t_x;                  /* samples in this Phase 2 (P2D_EXCHANGE_MS) */
    int       n_recover;            /* entries into P2D_RECOVER (P2D_MAX_RECOVER) */
    int32_t   level;
    dpsktx_t  tx; dpskrx_t rx;
    probetx_t ptx; proberx_t prx;
    int       probing;              /* 0 none, 1 L1, 2 L2 */
    info0_t   info0a; int got0a, resend;
    int       n_restart;            /* returns to INFO0 on INFO0a again (P2D_MAX_RESTART) */
    int       n_info0d;             /* INFO0d sequences sent */
    info1d_t  info1d;
    info1a_t  info1a;
    int32_t   act_at;               /* scheduled reversal command */
    int32_t   b_rev_line;           /* when our first reversal was on the line */
    int32_t   rev2_at;              /* second tone A reversal (receive time) */
    int32_t   probe_from, l2_from;
    int32_t   rtd;                  /* round-trip delay estimate RTDEd, samples */
    int       tone_blocks;
    int       snr[PROBE_NBIN + 1];  /* probing result per 150 Hz bin, 0.1 dB */
    int       rx_level[PROBE_NBIN + 1];
    /* V.92 (9.3/V.92) */
    int       v92;                  /* P2D_V92_x bits; ON: we indicate V.92 in INFO0d bit 27 */
    int       pcm_up;               /* INFO1d bit 70 when both sides are V.92 */
    int       peer_v92;             /* INFO0a bit 26 (or the owner's word, in a retrain) */
    int       mode;                 /* P2D_MODE_x, valid in P2D_DONE */
} p2d_t;

/* p2d_t.v92 */
#define P2D_V92_ON          1   /* V.92 indicated (INFO0d bit 27): p2d_offer_v92, p2d_retrain_v92 */
#define P2D_V92_SHORT       2   /* short Phase 2 asked for (INFO0d bit 26): p2d_offer_short */
#define P2D_V92_SHORT_RAN   4   /* short Phase 2 ended with INFO1a (no probing, no INFO1d) */
#define P2D_V92_SHORT_FULL  8   /* short Phase 2 began and went to the full Phase 2 (9.4.1.2.2, .3) */
/* 9.4.1.2.2, 9.4.1.2.3: no Tone A reversal, or no INFO1a, this long after
 * our Tone B reversal: the full Phase 2 */
#define P2D_SHORT_WAIT_MS 2500

void    p2d_init(p2d_t *p, const p2d_cfg *cfg);
/* Phase 2 of a retrain (9.5.1), after the 70 ms of silence: Tone B at once,
 * no INFO0 exchange, waiting for the Tone A phase reversal (9.2.1.1.3) */
void    p2d_init_retrain(p2d_t *p, const p2d_cfg *cfg);
/* Optional, right after p2d_init() and before the first tick: indicate
 * V.92 capability in INFO0d (bit 27 of Table 15/V.92). If INFO0a has its
 * bit 26 set as well, INFO1d takes the form of Table 17/V.92 with bit 70 =
 * pcm_up ("the channel supports PCM upstream"), and INFO1a may then select
 * PCM upstream (mode P2D_MODE_V92). Without this call the modem is a V.90
 * digital modem exactly as before. */
void    p2d_offer_v92(p2d_t *p, int pcm_up);
/* The same for the Phase 2 of a retrain, right after p2d_init_retrain():
 * there is no INFO0 exchange, so the owner says that both sides indicated
 * V.92 when the call began ("any subsequent retrains shall use Phase 2 of
 * ITU-T V.92", 9.3/V.92). */
void    p2d_retrain_v92(p2d_t *p, int pcm_up);
/* Optional, after p2d_offer_v92() and before the first tick: ask for the
 * short Phase 2 of V.92 (9.4.1): INFO0d with bit 26 as well as bit 27
 * (replacing the queued one, as p2d_offer_v92 does). When the INFO0a has
 * both its bits 26 (V.92) and 27 (short Phase 2), Tone B is reversed once
 * Tone A is heard, the caller's reversal gives RTDEd, and its INFO1a (Table
 * 18: P2D_MODE_V92; Table 19: P2D_MODE_V90) ends Phase 2 with
 * P2D_V92_SHORT_RAN set; there is no probing and no INFO1d. No Tone A
 * reversal or no INFO1a we can serve within P2D_SHORT_WAIT_MS of our
 * reversal: the full Phase 2, P2D_V92_SHORT_FULL set. An INFO1a we cannot
 * serve counts as none (9.4.1.2.3): Table 19 at a symbol rate not in
 * up_sr_mask, or Table 18 when PCM upstream is not on offer here (pcm_up
 * 0; the full Phase 2's INFO1d then says so in bit 70). An INFO0a without bit
 * 27 gives the full Phase 2 with neither flag. */
void    p2d_offer_short(p2d_t *p);
int16_t p2d_tick(p2d_t *p, int16_t rx);
/* nominal level (RMS, sample units) for a power in dBm0 */
int     p2_level(int dbm0);

#endif
