/* V.34 Phase 2 (probing / ranging), CALL modem: V.34 11.2.1.1, recovery
 * 11.2.2.1, and the Phase 2 part of a retrain (11.5.1).
 *
 *   INFO0c, Tone B ... (INFO0a and Tone A from the answerer) ... the Tone A
 *   reversal ... our Tone B reversal 40 ms after it at the line, 10 ms more,
 *   silence ... the second Tone A reversal gives the round-trip delay ...
 *   the answerer's L1 and L2 (analysed) ... Tone B ... Tone A, its reversal
 *   ... our reversal 40 ms after it, 10 ms more, L1, L2 until Tone A is
 *   heard under it ... INFO1c, silence ... INFO1a.
 *
 * INFO1c is where the call modem's receiver asks: for each symbol rate, the
 * carrier the answerer shall use, its pre-emphasis filter and the projected
 * answer -> call rate; once, the power reduction it shall make. All from the
 * analysis of the answerer's L2 and from what INFO0a says the answerer's
 * transmitter can do (p2call_decide). INFO1a, the answerer's choice, gives
 * our transmitter its symbol rate, carrier, filter and power reduction.
 *
 * Sample-driven: one received sample in, one transmit sample out, 8 kHz.
 * Fixed-point, no allocation. */
#ifndef V90D_MODEM_P2CALL_H
#define V90D_MODEM_P2CALL_H
#include <stdint.h>
#include "dpsk.h"
#include "probe.h"
#include "info.h"
#include "p2ans.h"          /* P2N_SR_*, v34_symbol_rate, v34_carrier */

typedef struct {
    int level;          /* nominal transmit power: RMS in 16-bit sample units */
    int io_delay;       /* round-trip delay of our own sample path to the line terminals
                           and back, samples (a driver's buffering) */
    int sr_mask;        /* symbol rates INFO1c may offer (P2N_SR_*); 0 = 2400, 3000 and 3200 */
    int max_rate;       /* ceiling for the projected answer -> call rate, bit/s; 0 = none */
} p2call_cfg;

typedef enum {
    P2L_INFO0,          /* INFO0c, then Tone B; waiting for INFO0a and Tone A */
    P2L_WAIT_REV1,      /* Tone B; waiting for the Tone A reversal */
    P2L_REPLY1,         /* timed Tone B reversal, 10 ms more, then silence */
    P2L_WAIT_REV2,      /* silent; waiting for the second Tone A reversal */
    P2L_RECOVER,        /* 11.2.2.1.3: silent until Tone A is heard again */
    P2L_PROBE_RX,       /* receiving L1 / L2 */
    P2L_WAIT_REV3,      /* Tone B; waiting for Tone A and its reversal */
    P2L_REPLY3,         /* timed Tone B reversal, 10 ms more */
    P2L_L1, P2L_L2,     /* probing signals; L2 until Tone A is heard */
    P2L_INFO1C,         /* sending INFO1c */
    P2L_WAIT_INFO1A,    /* silent; waiting for INFO1a */
    P2L_RT_SILENCE,     /* 11.5.1: 70 ms of silence before Tone B (a retrain begun or answered) */
    P2L_DONE,
    P2L_FAILED
} p2call_state;

enum { P2L_OK = 0,
       P2L_ERR_INFO0,       /* no INFO0a / Tone A */
       P2L_ERR_REV,         /* the Tone A phase reversals never came */
       P2L_ERR_INFO1A,      /* no INFO1a, retrains used up */
       P2L_ERR_NO_RATE };   /* INFO1a asks for what we do not do (or INFO1c had nothing to offer) */

#define P2L_MAX_RETRAINS 2      /* retrains p2call begins or answers by itself before giving up */

typedef struct {
    p2call_cfg   cfg;
    p2call_state state;
    int          error;
    int32_t      now, t;            /* sample clock; samples in the current state */
    dpsktx_t     tx; dpskrx_t rx;
    probetx_t    ptx; proberx_t prx;
    int          probing;           /* 0 none, 1 L1, 2 L2 */
    info0_t      info0a; int got0a, resend, n_info0c;
    info1c_t     info1c;            /* as sent */
    info1a_t     info1a;            /* as received */
    int32_t      act_at, b_rev_line, rev2_at, probe_from, l2_at, info1c_end;
    int32_t      rtd;               /* round-trip delay estimate RTDEc, samples */
    int          tone_blocks;
    int          retrains;          /* begun or answered by p2call itself (11.2.2.1.5, .6) */
    int          restarts;          /* returns to 11.2.1.1.3 (11.2.2.1.3) */
    int          rev3_timeouts;     /* 11.2.2.1.4: reversal sent without the answerer's */
    /* ---- the probe analysis (p2call_decide works on these) ---- */
    int          snr[PROBE_NBIN + 1];       /* per tone: repeating against non-repeating part, 0.1 dB */
    int          rx_level[PROBE_NBIN + 1];  /* per tone: RMS in sample units */
    int          sdr_db10;          /* the whole received L2 against the repeating part of the
                                       four empty bins, spread over 4 kHz: distortion, 0.1 dB */
    int          band_snr[6];       /* projected SNR per symbol rate index, as INFO1c asks, 0.1 dB */
    /* ---- the outcome, valid in P2L_DONE ---- */
    int          sym_idx;           /* symbol rate index for both directions: 0, 3 or 4 */
    int          sym_rate;          /* 2400, 3000 or 3200 */
    int          tx_hi_carrier;     /* call -> answer: INFO1a bit 25 */
    int          tx_preemph;        /* INFO1a bits 26:29, 0..10 */
    int          tx_pwr_red;        /* INFO1a bits 12:14, dB */
    int          rx_hi_carrier;     /* answer -> call: what our INFO1c asked at sym_idx */
    int          md_len;            /* the answerer's MD, units of 35 ms */
    /* sample times of events as this end saw them (tests) */
    int32_t      t_rev1, t_rev2, t_rev3, t_tone_a2, t_info1a;
} p2call_t;

void    p2call_init(p2call_t *p, const p2call_cfg *cfg);
/* Phase 2 of a retrain, after the 70 ms of silence (11.5.1.1, 11.5.1.2):
 * Tone B at once, no INFO0 exchange unless the answerer sends INFO0a.
 * info0a is the answerer's INFO0a of the first Phase 2. */
void    p2call_init_retrain(p2call_t *p, const p2call_cfg *cfg, const info0_t *info0a);
int16_t p2call_tick(p2call_t *p, int16_t rx);

/* The INFO1c decision (Table 15) from snr[], rx_level[], sdr_db10 and the
 * answerer's INFO0a: fills info1c and band_snr[]. Run by p2call_tick when
 * the answerer's L2 has been analysed; open to tests with made-up probe
 * results. */
void    p2call_decide(p2call_t *p);

#endif
