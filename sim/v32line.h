/* A 2-wire telephone connection between an answering modem (network side, at
 * the 8 kHz network clock, linear or G.711 mu-law) and a calling modem (on
 * its own sample clock), for echo-cancelling duplex modems. x86 test side.
 *
 *   answerer tx -+-> [codec] -> loop filter -> delay -> gain, frequency shift -+-> caller rx
 *                |                                                             |
 *            near echo (answerer's hybrid)             caller's hybrid: far echo of the
 *                |                                     answerer, near echo of the caller
 *   answerer rx <+- [codec] <- noise <- loop filter <- delay <- gain, shift <--+-- caller tx
 *
 * With mulaw set, the network side (codec both ways, loop filters, noise and
 * the answerer's hybrid echo) is sim/channel.c; without it the same in linear
 * arithmetic with a dispersive hybrid.
 *
 * The band's lower edge (optional; edge_* in the config): what long loops,
 * channel banks and ATAs' high-pass filters do below about 1000 Hz, on the
 * way to the answerer (before its codec), and with edge_both on the way to
 * the caller too (on the network clock, after the frequency shift). From
 * filter theory alone:
 * - edge_hp_hz: a Butterworth high-pass of order edge_hp_order (even, 2..10;
 *   0: 4) cornered there, by the bilinear transform at 8000 Hz with its
 *   corner prewarped: sections s^2 / (s^2 + 2 zeta s + 1), zeta =
 *   sin((2k + 1) pi / 2n). Its gain and its phase (group delay at the corner
 *   and above it falling with the frequency);
 * - edge_gd_ms: phase alone, a second-order all-pass with its poles at
 *   550 Hz, radius r: group delay (1 - r^2) / |1 - r e^(j(th - w))|^2 for
 *   each pole, r chosen so that the delay at 600 Hz is edge_gd_ms over the
 *   delay at 1800 Hz; it falls to a few percent of that by 1000 Hz (at 3 ms:
 *   700 Hz 0.85, 1000 Hz 0.12 ms; the gain is 1 everywhere). */
#ifndef V90D_SIM_V32LINE_H
#define V90D_SIM_V32LINE_H
#include <stdint.h>
#include "channel.h"
#include "resamp.h"

#define V32LINE_MAXDELAY 16384
#define V32LINE_HILB 63

typedef struct {
    int      mulaw;            /* G.711 mu-law at the answerer's interface, through sim/channel */
    int      alaw;             /* (mulaw 0) G.711 A-law at the answerer's interface: what it sends
                                  and what it receives each through A-law, on the linear line */
    double   loss_db;          /* line loss each way */
    const double *taps; int ntaps;   /* loop filter each way (0: flat) */
    int      delay;            /* one-way delay, samples */
    double   near_db;          /* answerer's hybrid: echo this far below its transmit level (>= 200: none) */
    double   far_db;           /* caller's hybrid: the answerer's signal reflected, dB below what arrives there */
    double   c_near_db;        /* caller's own echo from its hybrid */
    double   c_far_db;         /* answerer's hybrid reflecting the caller's signal back to it */
    double   noise_a_dbm0;     /* noise at the answerer's receiver (<= -200: none) */
    double   noise_c_dbm0;     /* noise at the caller's receiver */
    double   freq_offset;      /* Hz, carrier shift each way */
    double   ppm;              /* the caller's sample clock against the network's */
    uint32_t seed;
    double   edge_hp_hz;       /* the band edge's high-pass corner, Hz (0: none) */
    int      edge_hp_order;    /* its order: even, 2..10 (0: 4) */
    double   edge_gd_ms;       /* the all-pass's group delay at 600 Hz over 1800's, ms (0: none) */
    int      edge_both;        /* the edge on the way to the caller as well */
} v32line_cfg;

/* the band edge, one way: up to 5 high-pass sections and the all-pass */
#define V32LINE_EDGE_SEC 6
typedef struct {
    int    n;
    double b[V32LINE_EDGE_SEC][3], a[V32LINE_EDGE_SEC][2], z[V32LINE_EDGE_SEC][2];
} v32line_edge;

typedef double (*v32line_peer_fn)(void *user, double rx);

typedef struct {
    v32line_cfg cfg;
    channel_t  ch; channel_cfg chcfg;
    fir_t      fa, fc; double za[CH_MAXTAPS], zc[CH_MAXTAPS];
    double     flat[1];
    rng_t      rng;
    double     gain, g_near, g_far, g_cnear, g_cfar;
    double     d_ac[V32LINE_MAXDELAY], d_ca[V32LINE_MAXDELAY]; int dpos;
    double     a_hist[64]; int a_pos;          /* answerer's tx, for its hybrid */
    double     c_hist[64]; int c_pos;          /* caller's tx, for its hybrid */
    double     hil[V32LINE_HILB];
    double     h_ac[V32LINE_HILB], h_ca[V32LINE_HILB]; int hpos;
    double     ph;
    sresamp_t  r_ac, r_ca;
    double     fifo[16]; int fifo_n, fifo_run;
    double     last_c_arrive;                  /* what reached the caller's side of the line last */
    double     last_a_arrive;
    /* for the tests' arithmetic, with no clock offset: total delay from the
     * answerer's output to the caller's input, and back, in samples; and how
     * far the caller's sample count trails the network's (the resampler's
     * start-up) */
    int        oneway, oneway_back, peer_lag;
    v32line_edge e_ca, e_ac;                   /* the band edge, to the answerer and to the caller */
} v32line_t;

void    v32line_init(v32line_t *l, const v32line_cfg *cfg);
/* one network sample: the answerer sends a_tx and receives the return value;
 * the caller is run through `peer` as its own clock demands (0, 1 or 2 times) */
int16_t v32line_tick(v32line_t *l, int16_t a_tx, v32line_peer_fn peer, void *user);
/* dBm0 <-> RMS in 16-bit sample units (0 dBm0 is 3.17 dB below a full-scale sine of 32124) */
double  v32line_rms(double dbm0);

#endif
