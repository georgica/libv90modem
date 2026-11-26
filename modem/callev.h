/* Who began a call's retrains and rate renegotiations, and why: what the
 * API's v90m_get_restarts reports (include/v90modem.h). One per modem, in
 * answer_t and orig_t; the retrains are counted where they begin
 * (callev_retrain), the renegotiations by the API where it counts them
 * (callev_reneg). Read only for the report: nothing here changes what the
 * modem does. */
#ifndef CALLEV_H
#define CALLEV_H
#include <stdint.h>

/* why a retrain or renegotiation began, as an engine records it (its
 * retrain_ours and the like: 0 the far end's, else ours, why) */
#define WHY_FAR       0     /* the far modem's */
#define WHY_OURS      1     /* ours: a step of a training, retrain or renegotiation failed or
                               ran out of time, or the engine says no more */
#define WHY_RX_ERRORS 2     /* ours: our receiver's errors too many for the rate */
#define WHY_RX_LOST   3     /* ours: our receiver lost the far end's data or its signal */

/* the report's causes: V90M_CAUSE_* (the API checks them), WHY_* + 1 and
 * the host's */
#define CEV_NONE  0
#define CEV_FAR   (WHY_FAR + 1)
#define CEV_HOST  (WHY_RX_LOST + 2)
#define CEV_OF(why) ((why) >= WHY_FAR && (why) <= WHY_RX_LOST ? (why) + 1 : WHY_OURS + 1)

typedef struct {
    uint32_t now;           /* samples since the call's first (the API's clock: wraps after 6.2 days) */
    uint32_t last_at;       /* the clock where the last counted one began */
    uint32_t pend_at;       /* ... where the API's pending renegotiation stopped the data */
    uint32_t reneg_far;     /* renegotiations counted that the far end began */
    uint32_t rt_far;        /* retrains begun by the far end */
    uint32_t rt_host;       /* ... by us, asked for by the host (v90m_retrain) */
    uint8_t  last;          /* CEV_* of the last begun, CEV_NONE before the first */
    uint8_t  last_rt;       /* it was a retrain */
    uint8_t  host;          /* the host asked for a retrain not counted yet: the next of ours is its */
} callev_t;

static inline void callev_note(callev_t *e, int cev, int rt, uint32_t at)
{
    /* the one begun last (a renegotiation that became a retrain is counted
     * after the retrain, but began before it) */
    if (e->last != CEV_NONE && (int32_t)(at - e->last_at) < 0) return;
    e->last = (uint8_t)cev; e->last_rt = (uint8_t)rt; e->last_at = at;
}

/* a retrain begins (why: WHY_*) */
static inline void callev_retrain(callev_t *e, int why)
{
    int c = CEV_OF(why);
    if (c == CEV_FAR) e->rt_far++;
    else if (e->host) { c = CEV_HOST; e->rt_host++; e->host = 0; }
    callev_note(e, c, 1, e->now);
}

/* the API counts a renegotiation that stopped the data at pend_at (cev: CEV_*) */
static inline void callev_reneg(callev_t *e, int cev)
{
    if (cev == CEV_FAR) e->reneg_far++;
    callev_note(e, cev, 0, e->pend_at);
}

#endif
