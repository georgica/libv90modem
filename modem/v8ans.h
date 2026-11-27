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
#include <stddef.h>
#include "ansam.h"
#include "v21.h"
#include "v8rx.h"

typedef enum { V8A_SILENCE, V8A_ANSAM, V8A_JM, V8A_POST, V8A_DONE } v8a_state;

#define V8A_RES_NONE  0
#define V8A_RES_OK    1   /* CM/JM/CJ completed: see jm, selected */
#define V8A_RES_NO_CM 2   /* ANSam ran its 5 s unanswered: caller is a pre-V.8
                             modem; continue as a legacy answerer (V.32bis Annex A) */
#define V8A_RES_NO_CJ 3   /* JM sent but CJ never seen */
#define V8A_RES_QC    4   /* V.92's QC1a heard with QC offered (v8ans_offer_qc):
                             the caller asks for Quick Connect; qc.oct */

#define V8A_SILENCE_MS 200
#define V8A_ANSAM_MS   5000
#define V8A_JM_MS      5000    /* our bound on JM (V.8 8.2.3 leaves it open) */
#define V8A_X2_FLIP_MS 2000    /* x2: the JM inverted only this long (see V8A_JM) */
#define V8A_POST_MS    75
/* the JM's bits: 15 octets need 20 + 10 x 15 = 170; 172 is kept because it
 * leaves v8ans_t its size once the 8-octet qc record is carved from the
 * tail of the 180 of V8_MAX_OCTETS (180 - 8). Our JM never exceeds 15
 * octets (7 at most: test/test_qc1a.c jm_fits); one that did would not be
 * framed, and V.8 ends with V8A_RES_NO_CM (v8ans_send_jm). */
#define V8A_MAXBITS    172

/* V.92 Quick Connect (docs/history/qc-facts.md), the answer side's record.
 * flags: */
#define V8A_QC_HEARD      0x01  /* a QC1a heard during ANSam (oct) */
#define V8A_QC_ACCEPTED   0x02
#define V8A_QC_TONEQ      0x04
#define V8A_QC_FALLBACK   0x08  /* ANSam again (v8ans_restart_ansam) */
#define V8A_QC_SHORT      0x10
#define V8A_QC_SHORT_FULL 0x20
#define V8A_QC_GRID       0x40
#define V8A_QC_BYPASS     0x80
/* aux: bits 0-2 the QC1a tracker (v8rx_tick_qc's *st), bits 3-4 LM, and: */
#define V8A_QCX_TRACK     0x07
#define V8A_QCX_LM_SHIFT  3
#define V8A_QCX_OFFER     0x20  /* v8ans_offer_qc: a QC1a is taken */
#define V8A_QCX_SPENT     0x40
#define V8A_QCX_ONHOLD    0x80  /* the QC1a heard had WXYZ 1111: ignored */
/* oct: the QC1a's information octet once V8A_QC_HEARD (before it, the
 * tracker's held copy); grid 0..5; tq_lo, tq_hi: TONEq's time in ms */
typedef struct { uint8_t flags, oct, aux, grid, tq_lo, tq_hi, spare[2]; } v8a_qc_t;

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
    v8a_qc_t   qc;
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
/* Between v8ans_init and the first tick: take a QC1a (on = 1) or not. In
 * V8A_ANSAM a QC1a is listened for either way (V8A_QC_HEARD, qc.oct); a
 * WXYZ of 1111 sets V8A_QCX_ONHOLD and is ignored; otherwise, offered,
 * V.8 ends at once: V8A_RES_QC, V8A_DONE (no V8A_POST). */
void v8ans_offer_qc(v8ans_t *v, int on);
/* ANSam again at once, and V.8 as before, but a QC1a no longer taken
 * (V8A_QCX_OFFER cleared, V8A_QC_FALLBACK set): V.92 9.2.4.3 */
void v8ans_restart_ansam(v8ans_t *v);
/* In V8A_ANSAM: send the JM of these n octets. Returns its bits, or 0 when
 * they do not fit (V8A_MAXBITS): then no JM, V8A_RES_NO_CM and V8A_POST.
 * v8ans_tick calls it with the JM it built. */
int  v8ans_send_jm(v8ans_t *v, const uint8_t *oct, int n);

typedef char v8ans_qc_check[sizeof(v8a_qc_t) == 8 && V8A_MAXBITS == 172
                            && offsetof(v8ans_t, qc) + sizeof(v8a_qc_t) == offsetof(v8ans_t, njm) ? 1 : -1];

#endif
