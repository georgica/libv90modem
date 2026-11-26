/* K56flex's Phase 1 on the answering side: V.8bis with us as the initiating
 * station (V.8bis 9.3), in front of V.8 (docs/history/k56-facts.md "V.8bis
 * signals", "V.8bis messages", "MS test"):
 *   at least 400 ms of silence after answering (10.2.2; ruling C6);
 *   CRe as the real server sent it (ruling C1): 1375 + 2002 Hz for 290 ms
 *     (the shortened first segment, 7.1.3), 400 Hz for 100 ms, 14.6 dB below
 *     our messages;
 *   the client's CRd (1529 + 2225 Hz, then 1900 Hz): its pair must have
 *     begun within V8B_CRD_DEADLINE_MS of CRe's end, and a CRd begun is
 *     received to its end;
 *   V8B_CL_GAP_MS after CRd, our CL (modem/v8bismsg) on V.21 channel 1
 *     (980 / 1180 Hz);
 *   the client's MS on channel 2 (1650 / 1850 Hz), until V8B_MS_WAIT_MS
 *     after our CL ended; V8B_ACK_GAP_MS after it, ACK(1) to an MS that
 *     carries the K56flex block (v8bis_ms_is_k56), NAK(1) to an invalid
 *     frame (7.2.9, 9.8; ruling C6), nothing to a valid frame without it;
 *     NAK(3) (9.5: a mode we do not support or have disabled) to an MS
 *     whose block selects version 1 alone when our CL offered it among
 *     others and accept_v1 is clear (docs/history/k56-facts.md
 *     "Versions": we run version 2's start-up only).
 * The reverse of v8ans' V.21 setup: here we send on the low channel and
 * hear the high one. Done in every case; V.8 follows (v8ans, from its own
 * silence), with K56flex agreed or not.
 * Sample-driven, fixed-point, 8000 samples/s, no allocation. */
#ifndef V90D_MODEM_V8BIS_H
#define V90D_MODEM_V8BIS_H
#include <stdint.h>
#include "v21.h"
#include "v8bismsg.h"

typedef enum {
    V8B_SIL,        /* silence after answering */
    V8B_CRE,        /* our CRe (ruling C1: CRe, not MRe) */
    V8B_WAIT_CRD,   /* listening for the client's CRd */
    V8B_CRD,        /* its pair held: until its 1900 Hz ends */
    V8B_GAP_CL, V8B_CL,
    V8B_WAIT_MS,    /* listening for its MS on channel 2 */
    V8B_GAP_ACK, V8B_ACK,   /* ACK(1), NAK(1) to an invalid frame, NAK(3) to version 1 */
    V8B_DONE
} v8b_state;

#define V8B_RES_NONE   0
#define V8B_RES_AGREED 1    /* its MS carried the K56flex block; we sent ACK(1) */
#define V8B_RES_NO_CRD 2
#define V8B_RES_NO_MS  3    /* none, a valid frame without the block, or an invalid frame */
#define V8B_RES_DECLINED 4  /* its MS carried the block but selected version 1: NAK(3) */

#define V8B_SIL_MS          400
#define V8B_CRE_DUAL_MS     290
#define V8B_CRE_TONE_MS     100
#define V8B_CRD_DEADLINE_MS 1000    /* from the end of CRe: CRd's pair begun by then */
#define V8B_CRD_MAX_MS      1000    /* from the pair held to its 1900 Hz ended */
#define V8B_CL_GAP_MS       120     /* from the end of CRd */
#define V8B_MS_WAIT_MS      1500    /* from the end of our CL */
#define V8B_ACK_GAP_MS      180     /* from the end of its MS */

/* the CRd detector: 10 ms blocks, the share of each block's power at 1529,
 * 2225 and 1900 Hz */
#define V8B_CRD_NONE 0
#define V8B_CRD_TONE 1      /* the 1529 + 2225 Hz pair held V8B_CRD_HOLD blocks */
#define V8B_CRD_END  2      /* then its 1900 Hz ended: one block ago */
#define V8B_CRD_BLOCK 80
#define V8B_CRD_HOLD  15    /* 150 ms (k56-facts.md "V.8bis signals": D-Modem's) */

typedef struct {
    uint32_t ph[3];
    int32_t  re[3], im[3];
    int64_t  pw;
    int      n;
    int      run;           /* blocks in a row with the pair */
    int      phase;         /* 0 looking, 1 the pair held, 2 in its 1900 Hz */
    int      miss;          /* blocks since the pair without 1900 Hz */
    int      pair_now;      /* the last whole block had the pair */
} v8bis_crd_t;

void v8bis_crd_init(v8bis_crd_t *d);
/* one received sample; V8B_CRD_TONE and V8B_CRD_END once each, at the end
 * of the block that decided it */
int  v8bis_crd_tick(v8bis_crd_t *d, int16_t x);

typedef struct {
    v8b_state   state;
    int32_t     t;              /* samples in this state */
    int16_t     amp;            /* the messages' peak */
    int16_t     a_dual, a_tone; /* CRe's, each tone's peak */
    uint32_t    ph1, ph2;
    v8bis_crd_t crd;
    v21mod_t    mod;
    v21demod_t  dem;
    int64_t     squelch;
    int         level;          /* the demodulator's last line level */
    int32_t     clk;            /* bit clock: + 300 a sample, a bit at 8000 */
    v8bis_rx_t  rx;
    uint8_t     bits[V8B_MAXBITS];
    int         nbits, pos;
    uint8_t     msg[V8B_MAXOCT];
    int         ns_form;
    int         vers;           /* the versions our CL offers (V8B_VERS_*) */
    int         accept_v1;      /* an MS selecting version 1 is acknowledged too */
    int         cl_sent;        /* our CL went out */
    int         ms_ver;         /* the version octet of its MS's K56flex block, -1 none */
    int         result;
} v8bis_t;

/* amp: the peak of our messages (as v8ans' JM); ns_form: V8B_NS9 or
 * V8B_NS7; vers: the versions our CL offers (V8B_VERS_BOTH, or V8B_VERS_2
 * as the real server and 0.7.1 did); accept_v1: acknowledge an MS that
 * selects version 1 (else, with version 1 in vers, it gets NAK(3)) */
void      v8bis_init(v8bis_t *b, int16_t amp, int ns_form, int vers, int accept_v1);
/* one sample in, one out; the state after this sample */
v8b_state v8bis_tick(v8bis_t *b, int16_t rx, int16_t *tx);
/* V8B_RES_*: final once the state is V8B_DONE */
int       v8bis_result(const v8bis_t *b);

#endif
