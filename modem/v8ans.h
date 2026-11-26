/* V.8 answer-side start-up (V.8 8.2, V.90 9.1.3):
 *   >= 0.2 s silence -> ANSam (with phase reversals) while listening for CM on
 *   V.21(L) -> on >= 2 identical CM: JM on V.21(H) until CJ -> 75 ms silence.
 * x2 (v8ans_offer_x2): to a caller whose CM offers V.34 and no PCM, on a
 * mu-law line, the JM of an x2 server (docs/history/x2-facts.md "Phase 1"):
 * the octets as V.34's, the PSTN access octet returned with "DCE on digital
 * network" when the CM has one, and the carrier inverted after each
 * complete JM for the first V8A_X2_FLIP_MS, then plain (a strict V.8
 * receiver never takes the inverted JM and goes on sending CM).
 * Sample-driven, fixed-point, no allocation. */
#ifndef V90D_MODEM_V8ANS_H
#define V90D_MODEM_V8ANS_H
#include "ansam.h"
#include "v21.h"
#include "v8rx.h"

typedef enum { V8A_SILENCE, V8A_ANSAM, V8A_JM, V8A_POST, V8A_DONE } v8a_state;

#define V8A_RES_NONE  0
#define V8A_RES_OK    1   /* CM/JM/CJ completed: see jm, selected */
#define V8A_RES_NO_CM 2   /* ANSam ran its 5 s unanswered: caller is a pre-V.8
                             modem; continue as a legacy answerer (V.32bis Annex A) */
#define V8A_RES_NO_CJ 3   /* JM sent but CJ never seen */

#define V8A_SILENCE_MS 200
#define V8A_ANSAM_MS   5000
#define V8A_JM_MS      5000    /* our bound on JM (V.8 8.2.3 leaves it open) */
#define V8A_X2_FLIP_MS 2000    /* x2: the JM inverted only this long (see V8A_JM) */
#define V8A_POST_MS    75
#define V8A_MAXBITS    (20 + 10 * V8_MAX_OCTETS)

typedef struct {
    v8a_state  state;
    int32_t    fs, t;              /* t = samples spent in the current state */
    ansam_t    ansam;
    v21mod_t   mod;
    v21demod_t dem;
    v8rx_t     rx;
    int64_t    squelch;
    v8_menu_t  local, cm, jm;
    uint8_t    jmbits[V8A_MAXBITS];
    int        njm, jmpos;
    int        result, selected;
    int        x2_offer;           /* v8ans_offer_x2 on a mu-law line */
    int        x2;                 /* this JM is x2's (see v8ans_x2) */
    int16_t    jm_sign;            /* +1, -1: the carrier's sign in this JM */
} v8ans_t;

void v8ans_init(v8ans_t *v, int fs, int16_t amp, const v8_menu_t *local);
/* one sample in, one sample out; returns the state after this sample */
v8a_state v8ans_tick(v8ans_t *v, int16_t rx, int16_t *tx);
/* Between v8ans_init and the first tick: offer x2. Nothing is offered on an
 * A-law line (alaw != 0): no known x2 client completed it. */
void v8ans_offer_x2(v8ans_t *v, int alaw);
/* 1 when x2 was offered and the CM selected V.34 without PCM: our JM is x2's,
 * and Phase 2 is to offer x2 too (p2ans_cfg.x2) */
int  v8ans_x2(const v8ans_t *v);

#endif
