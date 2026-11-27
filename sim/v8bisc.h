/* Test peer: V.8bis's responding station (the calling modem) as far as a
 * test of our initiating station needs it, written from
 * docs/history/k56-facts.md ("V.8bis signals", "V.8bis messages"), not from
 * modem/v8bis or modem/v8bismsg: its own frame writer, receiver and FCS, so
 * that the two check each other. It uses the V.21 modulator and demodulator
 * the other peers use (modem/v21).
 *
 *   the initiator's CRe or MRe: 1375 + 2002 Hz held 100 ms, then its second
 *     segment until a quiet 10 ms block;
 *   175 ms later, CRd: 1529 + 2225 Hz 400 ms, 1900 Hz 100 ms;
 *   the initiator's CL on V.21 channel 1 (980 / 1180 Hz);
 *   10 ms after it, the MS of the given octets on channel 2 (1650 / 1850
 *     Hz): 40 ones, four flags, the octets and the FCS with zeros inserted,
 *     two flags (the real client's framing);
 *   then ACK(1) or a NAK on channel 1, for 2 s at most: v8bisc_got.
 * Host only. */
#ifndef V90D_SIM_V8BISC_H
#define V90D_SIM_V8BISC_H
#include <stdint.h>
#include "../modem/v21.h"

#define V8BISC_MAXOCT 64                /* V.8bis 8.6: the longest information field */

typedef struct {
    uint8_t ms[V8BISC_MAXOCT]; int ms_len;
    double  level;          /* RMS of each of its signals */
    int     st;             /* its step */
    long    now, at;        /* samples; when the step's next thing happens */
    /* the initiator's dual tone, in 10 ms blocks */
    double  gr[2], gi[2], gph[2], gpw; int gn, gblocks;
    double  ph[3];
    /* V.21 channel 1, and its own frame receiver */
    v21demod_t dem; int64_t squelch; int level1; int32_t clk;
    uint8_t rbuf[V8BISC_MAXOCT + 2]; int rbits, rones, rin;
    /* its MS */
    v21mod_t mod; uint8_t bits[(V8BISC_MAXOCT + 2) * 10 + 80]; int nbits, pos;
    int     got_cre, got_cl, sent_ms, got;
} v8bisc_t;

/* ms: the MS's octets (n of them, 1 .. V8BISC_MAXOCT), its FCS not
 * included; level: the RMS of its signals */
void   v8bisc_init(v8bisc_t *b, const uint8_t *ms, int n, double level);
/* one line sample heard in, its own out */
double v8bisc_tick(v8bisc_t *b, int16_t x);
/* the one-octet message received after its MS: 0x14 (ACK(1)), a NAK's
 * octet (types 8 to 11), 0 none */
int    v8bisc_got(const v8bisc_t *b);

#endif
