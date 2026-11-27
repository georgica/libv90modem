#include <string.h>
#include "v8rx.h"
#include "qc.h"

#define PREAMBLE_BITS 8    /* >= this much mark before a character = new sequence
                              (10 ONEs + half a stop bit are sent; margin for timing) */

void v8rx_init(v8rx_t *r, int fs)
{
    arx_init(&r->u, fs, 300);
    r->ncur = r->in_msg = r->overflow = 0;
    r->nmsg = 0; r->matches = 0; r->zeros = 0;
    r->jm_flip = 0; r->carry = 0;
}

static int finalize(v8rx_t *r)
{
    r->in_msg = 0;
    if (r->ncur == 0 || r->overflow) return 0;
    if (r->ncur == r->nmsg && !memcmp(r->cur, r->msg, (size_t)r->ncur)) {
        r->matches++;
    } else {
        memcpy(r->msg, r->cur, (size_t)r->ncur);
        r->nmsg = r->ncur;
        r->matches = 1;
    }
    return r->matches >= 2 ? V8RX_MENU : 0;
}

/* one sample: the events; *ch the character taken (ARX_NONE when none or
 * a jm_flip mark, ARX_FRAMEERR), *pre_out the mark bits before it */
static int step(v8rx_t *r, int level, int *ch, int *pre_out)
{
    int ev = 0;
    int was_idle_bits = arx_idle_bits(&r->u) + r->carry;
    int c = arx_tick(&r->u, level), pre;

    *ch = c; *pre_out = 0;
    if (c == ARX_FRAMEERR) { r->in_msg = 0; r->zeros = 0; r->carry = 0; return 0; }
    if (c == ARX_NONE) {
        /* the mark run just reached preamble length: the sequence has ended */
        if (r->in_msg && was_idle_bits < PREAMBLE_BITS && arx_idle_bits(&r->u) + r->carry >= PREAMBLE_BITS)
            ev |= finalize(r);
        return ev;
    }
    pre = arx_pre_bits(&r->u) + r->carry;
    if (r->jm_flip && c == 0xFF) {              /* mark, not a character (see v8rx.h) */
        *ch = ARX_NONE;
        r->carry = pre + 10;
        r->zeros = 0;
        if (r->in_msg && pre < PREAMBLE_BITS && r->carry >= PREAMBLE_BITS) ev |= finalize(r);
        return ev;
    }
    r->carry = 0;
    *pre_out = pre;
    if (c == 0) { if (++r->zeros == 3) ev |= V8RX_CJ; } else r->zeros = 0;

    if (pre >= PREAMBLE_BITS) {
        if (r->in_msg) ev |= finalize(r);
        r->in_msg = (c == V8_SYNC_CM_JM);
        r->ncur = 0; r->overflow = 0;
    } else if (r->in_msg) {
        if (r->ncur < V8_MAX_OCTETS) r->cur[r->ncur++] = (uint8_t)c;
        else r->overflow = 1;
    }
    return ev;
}

int v8rx_tick(v8rx_t *r, int level)
{
    int c, pre;
    return step(r, level, &c, &pre);
}

/* *st: bits 0-1 where in a copy, bit 2 one copy held (v8rx.h) */
#define QS_SYNC 1
#define QS_INFO 2
#define QS_HELD 4

int v8rx_tick_qc(v8rx_t *r, int level, uint8_t *st, uint8_t *oct)
{
    int c, pre, w, p;
    int ev = step(r, level, &c, &pre);
    int s = *st & 3, held = *st & QS_HELD;

    if (c == ARX_NONE) return ev;
    if (c == ARX_FRAMEERR) { *st = 0; return ev; }
    if (pre >= PREAMBLE_BITS) {                 /* a character after a preamble */
        if (s == QS_INFO) held = QS_HELD;       /* the first copy, ended by marks */
        else held = 0;                          /* a 0x55 without its character */
        s = c == QC_SYNC ? QS_SYNC : 0;
        if (!s) held = 0;
    } else if (s == QS_SYNC) {                  /* the information character */
        if (qc1a_parse((uint8_t)c, &w, &p) < 0) s = held = 0;
        else if (!held) { *oct = (uint8_t)c; s = QS_INFO; }
        else {
            if (c == *oct) ev |= V8RX_QC1A;
            s = held = 0;
        }
    } else {                                    /* a second character before the marks */
        s = held = 0;
    }
    *st = (uint8_t)(s | held);
    return ev;
}
