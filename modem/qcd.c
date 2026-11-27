/* V.92 short Phase 1, the digital modem's side. See qcd.h. */
#include <string.h>
#include "qcd.h"
#include "qc.h"
#include "pcmenc.h"
#include "../v90/v90_pcm.h"

#define MS(ms) ((int32_t)(ms) * 8)

void qcd_init(qcd_t *q, int alaw, int16_t amp, uint8_t qc1a_oct, int lm)
{
    int wxyz = 0, lapm = 0;
    memset(q, 0, sizeof *q);
    (void)qc1a_parse(qc1a_oct, &wxyz, &lapm);
    q->state = QCD_QCA;
    q->alaw = alaw; q->lm = lm; q->amp = amp;
    q->ucode = qc_uqts_ucode(wxyz);
    if (q->ucode < 0) q->ucode = 0;     /* never: v8ans takes no WXYZ 1111 */
    v21mod_init(&q->mod, V21H_MARK, V21H_SPACE, 8000, 300, amp);
    q->nbits = qc_frame_bits(qca1d_octet(lm, lapm), 1, q->bits);
    tonedet_init(&q->tq, TONEDET_980);
    q->tq_ms = -1;
}

uint8_t qcd_tick(qcd_t *q, int16_t rx)
{
    uint8_t o = v90_pcm_octet(q->alaw, 0, 1);       /* silence: Ucode 0 */
    int done = 0;
    if (q->t < INT32_MAX) q->t++;
    q->qts_first = 0;
    if (q->state != QCD_QCA && q->since_qca < INT32_MAX) q->since_qca++;
    switch (q->state) {
    case QCD_QCA:                                   /* 9.2.4.1: QCA1d */
        o = pcm_encode(q->alaw, v21mod_tick(&q->mod, q->bits[q->pos], &done));
        if (done && ++q->pos >= q->nbits) { q->state = QCD_SIL; q->n = 0; q->since_qca = 0; }
        break;
    case QCD_SIL:                                   /* "silence for 75 +- 5 ms" */
        if (++q->n >= MS(QCD_SIL_MS)) { q->state = QCD_QTS; q->n = 0; }
        break;
    case QCD_QTS:                                   /* 8.3.6: QTS's first symbol is data frame interval 0 */
        q->qts_first = q->n == 0;
        o = qc_qts_octet(q->alaw, q->ucode, (int)q->n);
        if (++q->n >= QCD_QTS_SYMS) { q->state = QCD_ANSPCM; q->n = 0; tonedet_init(&q->tq, TONEDET_980); q->hold = 0; }
        break;
    case QCD_ANSPCM:
        /* 9.2.4.3: no TONEq during the 2 s after QCA1d; a run of 980 Hz
         * under way then is followed to its end (Readings 5) */
        if (q->since_qca >= MS(QCD_TONEQ_WAIT_MS) && q->tq.blocks == 0) { q->state = QCD_NO_TONEQ; break; }
        o = qc_anspcm_octet(q->alaw, q->lm, q->n);
        q->n++;
        /* tonedet says so on the last sample of each block: the run is
         * its count of blocks */
        (void)tonedet_tick(&q->tq, rx);
        if (q->tq.blocks >= TONEDET_BLOCKS) {
            /* the run began TONEDET_BLOCKS blocks ago */
            if (q->hold == 0) q->tq_ms = (q->since_qca - TONEDET_BLOCK * TONEDET_BLOCKS) / 8;
            if (++q->hold >= MS(QCD_TONEQ_HOLD_MS)) { q->state = QCD_POST; q->n = 0; }
        } else {
            q->hold = 0;
        }
        break;
    case QCD_POST:                                  /* TONEq: 75 +- 5 ms of silence, then Phase 2 */
        if (++q->n >= MS(QCD_SIL_MS)) q->state = QCD_TONEQ;
        break;
    case QCD_TONEQ: case QCD_NO_TONEQ:
        break;
    }
    return o;
}
