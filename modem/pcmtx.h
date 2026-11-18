/* V.92 analogue modem: the PCM upstream on our own D/A clock.
 *
 * The upstream symbol rate is 8000 symbol/s "derived from the digital
 * network" (6.2/V.92): the far codec samples our signal on the network
 * clock, and each of its samples must find one of our symbols. Our D/A runs
 * on our own clock, so the symbols, made on the network clock, are carried
 * over to ours by a fractional-delay interpolator (modem/pcmtx_tab.h, made
 * by tools/gen_pcmtx_tab.py): 64 taps and a low-pass at 3700 Hz, so that
 * what reaches the far codec is one fixed response of the symbols, to -78
 * dB, however the two clocks stand (a PCM upstream reaches to the band edge,
 * where no interpolator of reasonable length is right).
 *
 * The network clock as we know it is the downstream receiver's
 * (modem/v90arx): its interpolator makes one network sample every
 * `step + slew` of our samples (Q32). The transmitter runs the same
 * accumulator with the same increments, so it emits exactly one symbol for
 * every network sample the receiver makes, and its timing follows every
 * correction of the receiver's timing loop: the round trip between the two
 * is a constant. Before the receiver has measured the clock (the first
 * TRN1u, Ja) the step is one sample, our own clock, as in a real modem whose
 * loop has not locked yet.
 *
 * Each of our samples:
 *   pcmtx_clock(&t);
 *   while (pcmtx_due(&t)) pcmtx_put(&t, next_symbol(), step);
 *   out = pcmtx_out(&t);
 * The output is the symbols' band-limited signal 33 samples after the
 * symbol instants (PCMTX_DELAY). pcmtx_slip() delays every symbol from the
 * next one on by a fraction of a symbol (the half symbol of the first Su-bar
 * and the fraction of Jp, 9.5.2.1.7 and 9.5.2.1.8).
 *
 * Symbols are linear values on the 16-bit scale. Integer only, no
 * allocation. */
#ifndef V90D_MODEM_PCMTX_H
#define V90D_MODEM_PCMTX_H
#include <stdint.h>

#define PCMTX_RING   128        /* symbols kept (>= the bank's 64 taps + 2) */
#define PCMTX_DELAY  33         /* samples from a symbol's instant to its centre in the output */

typedef struct {
    int16_t ring[PCMTX_RING];
    int     pos;                /* next write position */
    int64_t tt;                 /* time to the next symbol instant, Q32 of our samples */
    int64_t since;              /* time since the newest symbol's instant, Q32 */
    uint32_t nsym;              /* symbols put */
    int     wide;               /* see pcmtx_wide() */
} pcmtx_t;

void    pcmtx_init(pcmtx_t *t);
/* A new sample of ours begins. */
void    pcmtx_clock(pcmtx_t *t);
/* 1 while a symbol instant has passed that has no symbol yet. */
int     pcmtx_due(const pcmtx_t *t);
/* The symbol for that instant; step: our samples to the next instant, Q32
 * (the receiver's step + slew; 1 << 32 for our own clock). */
void    pcmtx_put(pcmtx_t *t, int16_t sym, int64_t step);
/* The D/A sample for the sample that began with pcmtx_clock(). */
int16_t pcmtx_out(const pcmtx_t *t);
/* on: the whole band, without the low-pass (modem/v34_tabs.h's bank, of the
 * same length and delay), for an echo canceller that learns on it: the band
 * edge, where the low-pass is half way down, is where a hybrid's echo is
 * strongest, and NLMS learns slowly what little is sent there. What that
 * bank gets wrong near 4 kHz changes with the clocks' phase, which does not
 * matter for training signals of two levels. off: the low-pass bank. */
void    pcmtx_wide(pcmtx_t *t, int on);
/* Delay the symbols from the next one on by frac / 65536 of a symbol. */
void    pcmtx_slip(pcmtx_t *t, uint32_t frac_q16);

#endif
