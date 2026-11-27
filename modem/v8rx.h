/* V.8 sequence receiver: assembles CM/JM sequences from a line-level stream and
 * reports (a) two identical consecutive sequences, (b) CJ (three zero octets). */
#ifndef V90D_MODEM_V8RX_H
#define V90D_MODEM_V8RX_H
#include "async.h"
#include "v8msg.h"

#define V8RX_MENU 1   /* >= 2 identical sequences received; see msg/nmsg */
#define V8RX_CJ   2
#define V8RX_QC1A 4   /* v8rx_tick_qc: V.92's QC1a (Table 2) heard, twice the same */

typedef struct {
    arx_t   u;
    uint8_t cur[V8_MAX_OCTETS]; int ncur, in_msg, overflow;
    uint8_t msg[V8_MAX_OCTETS]; int nmsg;    /* last completed sequence */
    int     matches;                         /* consecutive identical count */
    int     zeros;
    /* jm_flip (set after v8rx_init, by a calling modem): an x2 server inverts
     * its carrier after each JM (docs/history/x2-facts.md "Phase 1"). Across
     * that 180 degree step the V.21 correlators see space for longer than
     * half a bit, and the receiver takes a character of all ones out of the
     * ten ONEs before the next JM. No V.8 octet is FF (its low bits are
     * neither a category nor an extension), so with jm_flip such a character
     * counts as the mark it is made of. */
    int     jm_flip;
    int     carry;                           /* mark bits that FF characters stood for */
} v8rx_t;

void v8rx_init(v8rx_t *r, int fs);
/* one sample of line level; returns a V8RX_* event mask (0 = nothing) */
int  v8rx_tick(v8rx_t *r, int level);
/* The same receiver, the same events and state, and beside it a tracker of
 * V.92's QC1a (Table 2: twice, ten ONEs, the character 0x55 and one
 * information character; docs/history/qc-facts.md "Short Phase 1" and
 * "Readings" 2). Its state is the caller's: *st (bits 0-1: 0 idle, 1 after
 * a 0x55 that followed >= PREAMBLE_BITS of mark, 2 its information
 * character taken; bit 2: one copy held, in *oct) and *oct, both 0 to
 * begin. V8RX_QC1A is returned with the second copy's information
 * character when it equals the first; *oct is then that octet and *st 0.
 * A framing error, a second character before the marks, any character
 * after a preamble but 0x55, an information character qc1a_parse refuses
 * (QC1d, for one) or a copy unlike the first sets *st to 0. The tracker
 * never writes r->msg, r->matches or r->zeros (the receiver does, as in
 * v8rx_tick). */
int  v8rx_tick_qc(v8rx_t *r, int level, uint8_t *st, uint8_t *oct);

#endif
