/* Test channel for the V.90 PCM downstream: what lies between the digital
 * modem's octets and the analogue modem's A/D converter.
 *
 *   octet on the network clock -> optional digital loss pad (decode, scale,
 *   encode again, as a network's pad table does) -> optional robbed-bit
 *   signalling (the octet's least significant bit forced to 1 in one network
 *   sample of every 6) -> the far codec's D/A (Table 1 / G.711 levels) ->
 *   codec receive filter, subscriber loop, the analogue modem's DAA and A/D
 *   filter (one tabulated pulse) -> + white noise -> sampled on the analogue
 *   modem's own clock, 8000 (1 + ppm 1e-6) Hz, at an arbitrary phase ->
 *   + the echo of the analogue modem's own transmit samples through its
 *   hybrid -> 16-bit A/D.
 *
 * The analogue response is built in the frequency domain: an 8th order
 * Chebyshev low-pass (0.1 dB ripple, edge 3550 Hz: G.712's 14 dB at 4 kHz and
 * 32 dB at 4.6 kHz) for each codec, the line card's transformer (2nd order
 * high-pass at 150 Hz), the DAA's coupling (1st order high-pass at 50 Hz),
 * and loop_km of 0.4 mm cable (R 280 ohm/km, L 0.65 mH/km, C 50 nF/km,
 * matched). The pulse is normalised to the gain `level_db` at 1 kHz, so the
 * loop only adds its slope and delay distortion.
 *
 * The channel pulls the digital modem's octets as it needs them: one A/D
 * sample per dschan_tick(), 0, 1 or 2 octets taken from the source for it.
 * Host test code only (double precision). */
#ifndef V90D_SIM_DSCHAN_H
#define V90D_SIM_DSCHAN_H
#include <stdint.h>
#include "noise.h"

#define DSCH_OS    32           /* pulse points per network sample */
#define DSCH_LEN   128          /* pulse length, network samples */
#define DSCH_PRE   8            /* of which before t = 0 */
#define DSCH_HIST  4096         /* octets kept for dschan_octet() */
#define DSCH_ENTAP 48           /* hybrid echo FIR, A/D samples */

typedef uint8_t (*dschan_src_fn)(void *ctx);   /* the digital modem's next octet */

typedef struct {
    int    alaw;
    double pad_db;              /* digital loss pad, 0 = none */
    int    rbs;                 /* robbed-bit signalling on */
    int    rbs_phase;           /* network sample n is robbed when n % 6 == rbs_phase */
    double loop_km;             /* 0.4 mm cable */
    double level_db;            /* gain at 1 kHz, Table 1 units -> A/D units */
    double noise_dbm0;          /* white noise at the A/D, dB re the level a 0 dBm0
                                   1 kHz tone has there; <= -200: none */
    double ppm;                 /* A/D clock 8000 (1 + ppm 1e-6) Hz */
    double phase;               /* network time of A/D sample 0, in network samples (0..1) */
    double echo_db;             /* hybrid: gain from our transmit samples to the A/D at
                                   1 kHz, dB; <= -200: none */
    uint32_t seed;
} dschan_cfg;

typedef struct {
    dschan_cfg cfg;
    dschan_src_fn src; void *ctx;
    double pulse[DSCH_LEN * DSCH_OS + 4];   /* p(t), t = i / OS - PRE - 1/OS ... see .c */
    double a[256];              /* network samples at the far D/A, by n & 255 */
    long   n;                   /* network samples taken */
    long   k;                   /* A/D samples made */
    double r;                   /* network samples per A/D sample */
    double nrms;                /* noise RMS, A/D units */
    double echo[DSCH_ENTAP];    /* hybrid FIR */
    int16_t up[2 * DSCH_ENTAP]; int upos;
    uint8_t sent[DSCH_HIST], got[DSCH_HIST];  /* octets as sent and as they reached the D/A */
    double tone_rms;            /* RMS at the A/D of a 0 dBm0 1 kHz tone */
    double echo_last;           /* the echo in the last sample (for tests) */
    rng_t  rng;
} dschan_t;

void    dschan_init(dschan_t *c, const dschan_cfg *cfg, dschan_src_fn src, void *ctx);
/* One A/D sample. up: what the analogue modem sends at this tick (its D/A
 * runs on the same clock as its A/D). */
int16_t dschan_tick(dschan_t *c, int16_t up);
/* Network sample n (one of the last DSCH_HIST): octet as the digital modem
 * sent it, and as it reached the far codec (after pad and robbed bit). */
uint8_t dschan_sent(const dschan_t *c, long n);
uint8_t dschan_got(const dschan_t *c, long n);
/* The pulse (A/D units for a Table 1 unit at the D/A) at network time t. */
double  dschan_pulse(const dschan_t *c, double t);
/* |H(f)| in dB (the response the pulse was built from, with level_db) */
double  dschan_gain_db(const dschan_cfg *cfg, double f);

#endif
