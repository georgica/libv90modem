/* V.92 short Phase 1, the digital modem's side (9.2.4.1, 9.2.4.3), once the
 * caller's QC1a has been taken (modem/v8ans V8A_RES_QC): QCA1d (Table 13)
 * on V.21(H), 75 ms of silence, QTS and QTS\ (8.3.6), then ANSpcm (8.3.1)
 * while TONEq (980 Hz, 8.2.5) is listened for; on TONEq, 75 ms of silence
 * and Phase 2 (QCD_TONEQ). No TONEq within 2 s of QCA1d's end: QCD_NO_TONEQ,
 * ANSam again and V.8 (the answerer's part). Values: docs/history/
 * qc-facts.md ("Short Phase 1", "QTS", "ANSpcm", "Timing", "Readings" 5).
 *
 * One G.711 octet out per received sample, 8000 samples/s. TONEq is taken
 * after TONEDET_BLOCKS blocks of 980 Hz (tonedet) held QCD_TONEQ_HOLD_MS
 * more: at least 100 ms, longer than any run of V.21(L) marks in a CM (a
 * caller that missed QCA1d goes on with CM). The 2 s deadline applies to
 * the start of a run: a run under way at the deadline is followed to its
 * end. Fixed point, no allocation; every wait bounded. */
#ifndef V90D_MODEM_QCD_H
#define V90D_MODEM_QCD_H
#include <stdint.h>
#include "v21.h"
#include "tonedet.h"

typedef enum {
    QCD_QCA,        /* QCA1d, 70 bits of V.21(H) */
    QCD_SIL,        /* 75 ms of silence (Ucode 0) */
    QCD_QTS,        /* QTS, QTS\ */
    QCD_ANSPCM,     /* ANSpcm, TONEq listened for */
    QCD_POST,       /* TONEq taken: 75 ms of silence */
    QCD_TONEQ,      /* done: Phase 2 follows */
    QCD_NO_TONEQ    /* no TONEq: ANSam again (9.2.4.3) */
} qcd_state;

#define QCD_SIL_MS        75
#define QCD_TONEQ_WAIT_MS 2000  /* from QCA1d's end */
#define QCD_TONEQ_HOLD_MS 40    /* after tonedet's TONEDET_BLOCKS blocks: >= 100 ms of 980 Hz */
#define QCD_QTS_SYMS      816   /* QTS 768 and QTS\ 48 */

/* t: samples in ANS_QC; since_qca: samples since QCA1d's last (1 on the
 * first after it); n: the state's own count (bits, samples, codewords);
 * ucode: QTS's UQTS; bits/nbits/pos: QCA1d's frame; tq: TONEq's detector
 * (from QCD_ANSPCM on), hold: samples of its run past TONEDET_BLOCKS;
 * qts_first: 1 on the sample of QTS's first symbol only; tq_ms: ms from
 * QCA1d's end to the start of the latest TONEq run (-1 none) */
typedef struct {
    qcd_state state;
    int32_t   t, since_qca, n;
    int       alaw, lm, ucode;
    v21mod_t  mod;
    uint8_t   bits[70];
    int       nbits, pos;
    int16_t   amp;
    tonedet_t tq;
    int32_t   hold;
    int       qts_first;
    int32_t   tq_ms;
} qcd_t;

/* alaw: the law; amp: QCA1d's amplitude (as our JM's); qc1a_oct: the
 * caller's QC1a information octet (its UQTS and P: our P mirrors it); lm:
 * ANSpcm's level (qc_lm), also in QCA1d */
void    qcd_init(qcd_t *q, int alaw, int16_t amp, uint8_t qc1a_oct, int lm);
/* one received sample in (linear), the octet to send out */
uint8_t qcd_tick(qcd_t *q, int16_t rx);

#endif
