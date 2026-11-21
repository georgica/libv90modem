/* Test peer: a K56flex client calling our answerer, written from
 * docs/history/k56-facts.md ("V.8bis signals", "V.8bis messages", "NS
 * block", "Phase 2") as SM56 does it: it checks that our pieces fit
 * together; it does not prove K56flex.
 *
 *   V.8bis as the responding station: the initiator's CRe or MRe (1375 +
 *   2002 Hz held 100 ms, then its second segment) answered, 175 ms after
 *   it ends, with CRd (1529 + 2225 Hz 400 ms, 1900 Hz 100 ms); our CL
 *   received on V.21 channel 1 and its NS block checked with SM56's test
 *   (v8bis_cl_is_k56); 10 ms after it, SM56's MS on channel 2; then ACK(1)
 *   or NAK(1) received on channel 1. Per cfg.v8bis: the whole exchange, CRd
 *   and no MS (a V.92 modem), no V.8bis at all, or the MS with a bad FCS.
 *   V.8 as a caller (sim/v8call) from the start, as v34peer's own: CM with
 *   V.34, or with cm_pcm V.34 and the PCM octet of an analogue modem (SM56
 *   in automode). When JM selects V.34, sim/v34peer from Phase 2 on
 *   (skip_v8), exactly as v34peer goes on after its own V.8.
 *
 * The line is digital, as x2call's: one octet from the server in (mu-law,
 * as it left it), one linear sample back. Test side. */
#ifndef V90D_SIM_K56CALL_H
#define V90D_SIM_K56CALL_H
#include <stdint.h>
#include "v8call.h"
#include "v34peer.h"
#include "../modem/v21.h"
#include "../modem/v8bismsg.h"

#define K56C_V8BIS_FULL    0    /* CRd, MS with SM56's block */
#define K56C_V8BIS_NO_MS   1    /* CRd, no MS */
#define K56C_V8BIS_NONE    2    /* ignores CRe and MRe */
#define K56C_V8BIS_BAD_FCS 3    /* its MS with a bad FCS */

typedef struct {
    int level;              /* transmit RMS, sample units */
    int v8bis;              /* K56C_V8BIS_* */
    int cm_pcm;             /* CM offers PCM (V.90 analogue modem) as well */
    int  (*getbit)(void *user);
    void (*putbit)(void *user, int bit);
    void *user;
} k56call_cfg;

typedef enum {
    K56C_V8BIS,             /* V.8bis (and V.8 listening for ANSam all along) */
    K56C_V8,                /* V.8 under way */
    K56C_V34,               /* v34peer from Phase 2 on */
    K56C_IDLE,              /* V.8 selected something else: silent */
    K56C_ENDED
} k56c_state;

typedef struct {
    k56call_cfg cfg;
    k56c_state  state;
    int32_t     now, t;
    /* V.8bis */
    int         bst;                /* the responder's step */
    double      gr[2], gi[2], gph[2], gpw; int gn, gblocks;
    double      ph[3];
    int32_t     crd_at;             /* when its CRd goes out, -1 not yet */
    v21demod_t  dem; int64_t squelch; int level; int32_t clk;
    v8bis_rx_t  rx; uint8_t msg[V8B_MAXOCT];
    v21mod_t    mod; uint8_t bits[V8B_MAXBITS]; int nbits, pos; int32_t ms_at;
    int         got_cre;            /* the initiator's dual tone held 100 ms */
    int         got_cl, cl_ok;      /* a CL came; its NS block passed SM56's test */
    int         sent_ms;
    int         got_ack, got_nak;
    /* V.8 and V.34 */
    v8call_t    v8;
    int         v8_sel;             /* what JM selected (v8_selected), -1 before */
    v34peer_t   peer;
} k56call_t;

void       k56call_init(k56call_t *c, const k56call_cfg *cfg);
/* one octet from the server in (as it left it), one line sample out */
int16_t    k56call_tick(k56call_t *c, uint8_t octet_from_server);
k56c_state k56call_state(const k56call_t *c);

#endif
