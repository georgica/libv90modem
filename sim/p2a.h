/* Test peer: V.90 Phase 2, analogue modem side (V.90 9.2.2). Built from the
 * same tone / INFO / probing blocks as the digital side. */
#ifndef V90D_SIM_P2A_H
#define V90D_SIM_P2A_H
#include <stdint.h>
#include "../modem/dpsk.h"
#include "../modem/probe.h"
#include "../modem/info.h"

typedef struct {
    int level;          /* nominal RMS */
    int v90;            /* INFO1a asks for V.90 (else V.34) */
    int up_sr;          /* upstream symbol rate index requested, -1 = best offered */
    int uinfo;
    info0_t info0a;
    /* V.34 in INFO1a (Table 11) only: what it asks of the digital modem's
     * transmitter. The symbol rate is the same in both directions. */
    int v34_hi_carrier; /* bit 25 */
    int v34_preemph;    /* bits 26:29 */
    int v34_max_rate;   /* bits 30:33, x 2400 bit/s; 0 = 12 (28800) */
    /* V.92: INFO0a bit 26 is info0a.v92. pcm_up: select PCM upstream with
     * the INFO1a of Table 18/V.92 when INFO0d and INFO1d allow it (else what
     * v90 says); pcm_force: send that INFO1a whatever they say */
    int pcm_up, pcm_force;
    int sections, ltot_code, lmax_code, md_len;     /* Table 18 bits 12:24 */
    /* misbehaving callers, for the Phase 2 bounds of the digital modem:
     * info0_repeat: once INFO0 is exchanged, send INFO0a (acknowledged) this
     * many times more before the Tone A reversal; -1 = for ever. no_rev2:
     * never send the second Tone A reversal (the answer to the digital
     * modem's Tone B reversal); keep Tone A and start again from INFO0 2500 ms
     * later. 0 in both is the well-behaved caller. */
    int info0_repeat, no_rev2;
    /* V.92 short Phase 2 (9.4.2), when INFO0d bit 26 asks for it too:
     * short_p2 sets INFO0a bit 27. After INFO0 the caller sends Tone A and
     * waits for the digital modem's Tone B reversal; its own reversal is at
     * its line terminals 40 ms after that one is, Tone A goes on 10 ms, then
     * INFO1a: Table 18 (PCM upstream, the fields above), or with short_t19
     * Table 19 (V.34 upstream at symbol rate short_sr 3..5, bit 33 short_hi),
     * written by p2a_t19_build(). Faults: short_no_rev, the reversal is never
     * answered; short_no_info1a, it is, and no INFO1a follows. Either way the
     * caller keeps Tone A, and when Tone B comes again (9.4.1.2.2, .3) goes
     * on as in the full Phase 2. So does a caller in P2A_DONE after its short
     * INFO1a when Tone B comes (the digital modem could not use the INFO1a,
     * 9.4.1.2.3): Tone A, the full Phase 2. */
    int short_p2, short_t19, short_sr, short_hi, short_no_rev, short_no_info1a;
} p2a_cfg;

typedef enum {
    P2A_INFO0, P2A_WAIT_BREV1, P2A_REPLY, P2A_L1, P2A_L2, P2A_TONE50, P2A_REV_TAIL,
    P2A_WAIT_BREV2, P2A_PROBE_RX, P2A_WAIT_INFO1D, P2A_INFO1A, P2A_DONE, P2A_FAILED,
    P2A_SHORT_WAIT,         /* short Phase 2: Tone A, waiting for the Tone B reversal */
    P2A_SHORT_REPLY         /* ... its own reversal 40 ms on, 10 ms of Tone A, INFO1a */
} p2a_state;

typedef struct {
    p2a_cfg   cfg;
    p2a_state state;
    int32_t   now, t;
    dpsktx_t  tx; dpskrx_t rx;
    probetx_t ptx; proberx_t prx;
    int       probing;
    info0_t   info0d; int got0d, resend, n_info0a;
    info1d_t  info1d;
    info1a_t  info1a;
    int32_t   act_at, a_rev_line, probe_from, tone_since;
    int32_t   rtd;
    int       tone_blocks;
    int32_t   done_at;
    int       rep_left;     /* cfg.info0_repeat still to send */
    int32_t   restart_at;   /* cfg.no_rev2: INFO0 again at this sample; 0 = none */
    int       full;         /* short Phase 2 given up: the rest is the full Phase 2 */
    int       short_run;    /* this run ended by short Phase 2's INFO1a (no INFO1d) */
} p2a_t;

void    p2a_init(p2a_t *p, const p2a_cfg *cfg);
/* Phase 2 of a retrain (9.5.2), after the 70 ms of silence: Tone A at once,
 * no INFO0 exchange, then as 9.2.2.1.3 */
void    p2a_init_retrain(p2a_t *p, const p2a_cfg *cfg);
int16_t p2a_tick(p2a_t *p, int16_t rx);
/* INFO1a of Table 19/V.92 (V.34 upstream after short Phase 2), bit by bit
 * from the table: md_len (x 35 ms), uinfo, hi (bit 33), sr (3..5, bits
 * 34:36), the digital modem's 6 (37:39), freq_off (40:49, -512 = ignore), the
 * CRC of 10.1.2.3.2/V.34 (info_crc) and the fill. Returns 70. */
int     p2a_t19_build(int md_len, int uinfo, int hi, int sr, int freq_off, uint8_t *bits);

#endif
