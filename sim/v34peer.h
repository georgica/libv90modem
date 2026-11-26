/* Test peer: a V.34 CALLING modem, from the V.8 CM to data mode and through
 * retrains: sim/v8call (CM without PCM modulation modes: a V.34-only
 * caller), sim/p2c (Phase 2, call side) and modem/v34dx in the call role.
 * Test side; the signal path is the modem's own fixed-point code. */
#ifndef V90D_SIM_V34PEER_H
#define V90D_SIM_V34PEER_H
#include <stdint.h>
#include "v8call.h"
#include "p2c.h"
#include "../modem/v34dx.h"

typedef struct {
    int level;
    int skip_v8;            /* start with Phase 2 at once */
    int cm_access;          /* CM carries the PSTN access octet (analogue, as the Courier's 0d) */
    uint16_t modes;         /* V.8 modulation modes of CM; 0 = V.34 only */
    p2c_cfg p2;             /* INFO0c / INFO1c: what this modem can do and asks for */
    v34dx_cfg dx;           /* the ask_* fields, ceilings, getbit / putbit / user are used */
    int s_after_j_ms;       /* Phase 3: our S this long after the far J is heard (0: at
                               once, as v34dx). Real modems take longer: two WebTV boxes
                               began S 649-970 ms after the answerer's J began. The whole
                               call-side engine waits, silent */
    int k56;                /* a K56flex client (k56-facts.md "Phase 2"): after an INFO1a with
                               the frequency offset 0x001 it stops in VP_K56 (the server's PCM
                               training would follow; this peer does not go on) */
} v34peer_cfg;

typedef enum { VP_V8, VP_PHASE2, VP_TRAINING, VP_DATA, VP_RETRAIN, VP_ENDED,
               VP_K56 } v34peer_state;      /* VP_K56: silent, from the flagged INFO1a on */

typedef struct {
    v34peer_cfg cfg;
    v34peer_state state;
    int32_t t;
    v8call_t v8;
    p2c_t p2;
    v34dx_t dx;
    int retrains, connects;
    int p2_failed;
    int32_t hold;           /* samples left of an s_after_j_ms wait */
} v34peer_t;

void    v34peer_init(v34peer_t *p, const v34peer_cfg *cfg);
int16_t v34peer_tick(v34peer_t *p, int16_t rx);
/* a p2c configuration that offers everything and lets the probing decide */
void    v34peer_default(v34peer_cfg *cfg, int level);

#endif
