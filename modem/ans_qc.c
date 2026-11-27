/* The answering modem's V.92 short Phase 1 (modem/ans_scheme.h: ans_qc,
 * ANS_QC): V.8 took the caller's QC1a (V8A_RES_QC); modem/qcd sends QCA1d,
 * QTS and ANSpcm and listens for TONEq. On TONEq, Phase 2 (9.2.4.3); with
 * none, ANSam again and V.8 with the QC1a no longer taken. The record of
 * it is a->v8.qc (modem/v8ans.h): ACCEPTED, BYPASS (both P bits, 9.2.5),
 * LM in aux, GRID and grid (QTS's first symbol is data frame interval 0,
 * 8.3.6; answer_tick runs the grid on), TONEQ and its time, FALLBACK. */
#include "ans_scheme.h"
#include "qc.h"
#include "../v90/v90_pcm.h"

void ans_start_qc(answer_t *a)
{
    v8a_qc_t *r = &a->v8.qc;
    int wxyz = 0, lapm = 0;
    int lm = qc_lm(a->p2cfg.tx_dbm0, a->p2cfg.max_tx_code);
    (void)qc1a_parse(r->oct, &wxyz, &lapm);
    qcd_init(&a->u.qc, a->p2cfg.alaw, (int16_t)(a->cfg.level * 181 / 128), r->oct, lm);
    /* our P mirrors the caller's, as our JM mirrors its CM's LAPM: both
     * set, V.42's ODP/ADP is bypassed (9.2.5) */
    r->flags |= V8A_QC_ACCEPTED;
    if (lapm) r->flags |= V8A_QC_BYPASS;
    r->aux = (uint8_t)((r->aux & ~(3u << V8A_QCX_LM_SHIFT)) | (unsigned)lm << V8A_QCX_LM_SHIFT);
    ans_enter(a, ANS_QC); a->t = 0;
}

static int16_t tick_qc(answer_t *a, int16_t rx)
{
    qcd_t *q = &a->u.qc;
    v8a_qc_t *r = &a->v8.qc;
    a->pcm = qcd_tick(q, rx);
    if (q->qts_first) { r->grid = 0; r->flags |= V8A_QC_GRID; }
    if (q->state == QCD_TONEQ) {
        int32_t ms = q->tq_ms < 0 ? 0 : q->tq_ms > 0xFFFF ? 0xFFFF : q->tq_ms;
        r->flags |= V8A_QC_TONEQ;
        r->tq_lo = (uint8_t)(ms & 0xFF); r->tq_hi = (uint8_t)(ms >> 8);
        ans_start_p2(a);
    } else if (q->state == QCD_NO_TONEQ) {
        /* 9.2.4.3: ANSam from the next sample, V.8 as for any caller; a V.8
         * start-up has no grid of QTS's */
        r->flags &= (uint8_t)~V8A_QC_GRID;
        v8ans_restart_ansam(&a->v8);
        ans_enter(a, ANS_V8); a->t = 0;
    }
    return (int16_t)v90_octet_linear(a->p2cfg.alaw, a->pcm);
}

const ans_scheme_t ans_qc = { tick_qc, NULL, NULL, NULL, NULL, NULL, NULL, 1 };
