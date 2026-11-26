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

/* 16 bytes, so that answer_t keeps its size on every target (a check
 * below; v90m_mem_size is pinned within 0.7). The counts stop at 65535
 * (some 18 hours of retrains back to back) */
#define CEV_LAST_RT 1u      /* flags: the last begun was a retrain */
#define CEV_HOST_DUE 2u     /* flags: the host asked for a retrain not counted yet */
typedef struct {
    uint32_t now;           /* samples since the call's first (the API's clock: wraps after 6.2 days) */
    uint32_t last_at;       /* the clock where the last counted one began */
    uint16_t reneg_far;     /* renegotiations counted that the far end began */
    uint16_t rt_far;        /* retrains begun by the far end */
    uint16_t rt_host;       /* ... by us, asked for by the host (v90m_retrain) */
    uint8_t  last;          /* CEV_* of the last begun, CEV_NONE before the first */
    uint8_t  flags;         /* CEV_LAST_RT, CEV_HOST_DUE */
} callev_t;
typedef char callev_size_check[sizeof(callev_t) == 16 ? 1 : -1];

static inline void callev_inc(uint16_t *n) { if (*n != 0xFFFFu) (*n)++; }

static inline void callev_note(callev_t *e, int cev, int rt, uint32_t at)
{
    /* the one begun last (a renegotiation that became a retrain is counted
     * after the retrain, but began before it) */
    if (e->last != CEV_NONE && (int32_t)(at - e->last_at) < 0) return;
    e->last = (uint8_t)cev; e->last_at = at;
    e->flags = (uint8_t)((e->flags & ~CEV_LAST_RT) | (rt ? CEV_LAST_RT : 0u));
}

/* a retrain that began at the clock at (why: WHY_*). The host's request is
 * settled by any retrain counted after it: ours is the request, the far
 * end's took its place */
static inline void callev_retrain_at(callev_t *e, int why, uint32_t at)
{
    int c = CEV_OF(why);
    if (c == CEV_FAR) callev_inc(&e->rt_far);
    else if (e->flags & CEV_HOST_DUE) { c = CEV_HOST; callev_inc(&e->rt_host); }
    e->flags &= (uint8_t)~CEV_HOST_DUE;
    callev_note(e, c, 1, at);
}
/* ... beginning now */
static inline void callev_retrain(callev_t *e, int why) { callev_retrain_at(e, why, e->now); }

/* the API counts a renegotiation that stopped the data at the clock at (cev: CEV_*) */
static inline void callev_reneg(callev_t *e, int cev, uint32_t at)
{
    if (cev == CEV_FAR) callev_inc(&e->reneg_far);
    callev_note(e, cev, 0, at);
}

#endif
