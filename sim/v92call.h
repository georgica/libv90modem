/* Test peer: a whole V.92 analogue modem call with its line, as the digital
 * modem's G.711 interface sees it. One octet from the digital modem in, the
 * octet the network hands it out.
 *
 *   V.8 (sim/v8call, CM with the PCM category) -> Phase 2 (sim/p2a, INFO0a
 *   with or without the V.92 bit) -> by its own INFO1a either
 *     PCM upstream:  sim/v92a through sim/v92chan (V.92 Phases 3 and 4, data),
 *     V.34 upstream: sim/p34a through sim/channel (V.90 Phases 3 and 4, data).
 *   A retrain from either side (V.92 9.7.2): 70 ms of silence, Tone A, Phase 2
 *   again, and the same choice from what that INFO1d allows.
 *
 * So one object plays a V.92 modem with PCM upstream enabled or disabled
 * and a plain V.90 modem. V.8, Phase 2 and V.90 run over sim/channel (the
 * network is mu-law there; an A-law codec is put on it by converting at its
 * ports, as dce/test/harness does); the V.92 phases over sim/v92chan. Both
 * analogue modem models read the digital modem's octets as sent. Host only. */
#ifndef V90D_SIM_V92CALL_H
#define V90D_SIM_V92CALL_H
#include <stdint.h>
#include "v8call.h"
#include "p2a.h"
#include "p34a.h"
#include "v92a.h"
#include "v92chan.h"
#include "channel.h"
#include "qca.h"

typedef struct {
    int alaw;               /* law of the digital modem's codec */
    int rx_linear;          /* that codec's A/D is 16-bit linear (v92call_tick_lin); its D/A
                               still puts the G.711 levels of our octets on the line */
    double us_noise_dbfs;   /* sim/channel: upstream noise; 0 = -60 dBFS */
    double echo_gain_pcm;   /* hybrid of the V.92 line model; 0 = echo_gain / 2 */
    int v92;                /* INFO0a bit 26: a V.92 modem */
    int pcm_up;             /* select PCM upstream when INFO0d and INFO1d allow it */
    int level;              /* RMS of V.8, Phase 2 and the V.34 upstream; 0 = 4000 */
    int lapm;               /* CM carries the protocol category with LAPM */
    /* the line */
    double echo_gain;       /* hybrid, both line models; 0 = none */
    int    echo_delay;      /* samples before the echo begins (a driver's buffers) */
    int    shape;           /* v92chan line shape, 0..2 */
    double up_level;        /* v92chan: RMS of unit symbols at the codec; 0 = 4000 */
    double up_noise;        /* v92chan: line noise RMS; 0 = 3 */
    /* the V.92 modem */
    int dil_n;              /* DIL-segments asked for (0 = SCR instead) */
    uint32_t up_rates;      /* Ja: upstream rates, 0 = all */
    int mute_pcm;           /* fault: the first n PCM upstream start-ups send nothing at all */
    /* V.92 Quick Connect, the calling analogue modem (9.2.1.1, 9.2.1.3; sim/qca):
     * qc sends QC1a (Table 2: UQTS qc_wxyz, P qc_lapm) ahead of the first CM.
     * On QCA1d: silence, QTS and QTS\ and ANSpcm checked codeword by codeword,
     * TONEq once 20 ms of ANSpcm are in (toneq_delay_ms > 0: that long after
     * ANSpcm began instead), until ANSpcm stops; 75 ms; Phase 2. ANSam heard
     * for 1 s in place of any of that (9.2.4.3): CM, and V.8 as usual.
     * qc_lapm is QC1a's P; the CM's is lapm (a test that means one call's P
     * sets both). */
    int qc, qc_wxyz, qc_lapm;
    int short_p2;           /* INFO0a bit 27: short Phase 2 (9.4.2, sim/p2a) */
    int short_t19;          /* ... with Table 19's INFO1a (V.34 upstream), else Table 18's */
    int short_sr, short_hi; /* Table 19 bits 34:36 and 33 */
    /* faults: qc_deaf, QCA1d is not heard (CM goes on); qc_no_toneq, no TONEq;
     * short_no_rev and short_no_info1a as sim/p2a's */
    int qc_deaf, qc_no_toneq, toneq_delay_ms, short_no_rev, short_no_info1a;
    /* V.8 bis (Task 8): the MS sent when a CRe is heard; NULL = CRe ignored */
    const uint8_t *v8bis_ms; int v8bis_ms_len;
    int  (*getbit)(void *user);
    void (*putbit)(void *user, int bit);
    void *user;
} v92call_cfg;

typedef enum {
    VCL_V8,
    VCL_QC,                 /* short Phase 1 after QCA1d: QTS, ANSpcm, TONEq, 75 ms */
    VCL_P2,
    VCL_PCM,                /* V.92 Phases 3 and 4, data: PCM upstream */
    VCL_V90,                /* V.90 Phases 3 and 4, data: V.34 upstream */
    VCL_RTSIL,              /* the 70 ms of silence that begin a retrain */
    VCL_NO_PCM,             /* the JM had no PCM mode: nothing more is done */
    VCL_FAILED
} v92call_stage;

typedef struct {
    v92call_cfg cfg;
    v92call_stage stage;
    v8call_t  v8;
    p2a_t     p2;
    p34a_t    pa;
    v92a_t    va;
    channel_t ch;
    v92chan_t vch;
    double    ctx;              /* our line signal of the last sample */
    uint8_t   dly[32]; int dpos;    /* the octets on their way to the V.92 model */
    dpskrx_t  toneb;            /* Tone B in data mode asks for a retrain */
    int       rt_n;
    int       d_v92;            /* INFO0d bit 27 of the first Phase 2 */
    int       phase2s;          /* Phase 2 runs completed */
    int       pcm_starts, v90_starts;   /* times each Phase 3 was begun */
    int       retrains;         /* retrains begun (either side's) */
    int       retraining;
    int       hung;             /* on hook */
    long      n;
    int16_t   rx_lin;           /* what the network hands the digital modem, linear */
    /* Quick Connect (cfg.qc). Samples are n at our port: the digital modem's
     * octet handed to the n-th v92call_tick, or that tick's returned octet. */
    qca_rx_t  qrx;
    int       qs;               /* VCL_QC's step */
    long      qt;               /* octets in that step */
    int       qc_on;            /* QCA1d is listened for (until a fall-back) */
    int       qc_fallbacks;     /* times ANSam came back and CM was sent */
    long      qts_at;           /* QTS's first octet, -1 none */
    long      sd_at[4]; int n_sd;   /* the first Sd octet of each Phase 3 (the first four) */
    int       sd_seen;          /* this Phase 3's Sd is in sd_at */
    long      p3_n0;            /* n when this Phase 3 began */
    uint8_t   qca1d;            /* the QCA1d octet read, 0 none */
    int       qts_ok, anspcm_ok;    /* every codeword the sim's own */
    long      anspcm_n;         /* ANSpcm codewords that matched */
    long      toneq_on, toneq_off;  /* first sample of TONEq on our line and the first after it, -1 none */
    long      anspcm_end_at;    /* the first octet that was not ANSpcm, -1 none */
    long      info0d_at;        /* after ANSpcm, the first octet that is not a Ucode 0, -1 none */
    double    tph;              /* TONEq's phase */
    double    ai, aq, aph; int an, ahits;   /* ANSam (2100 Hz) in 20 ms blocks */
    int       d_short;          /* INFO0d bit 26 of the latest INFO0d */
    int       d_pcm_up;         /* INFO1d bit 70 of the latest full Phase 2 */
    int       v8bis_got;        /* ACK(1) or NAK octet received (Task 8) */
} v92call_t;

void    v92call_init(v92call_t *c, const v92call_cfg *cfg);
uint8_t v92call_tick(v92call_t *c, uint8_t ds_octet);
/* the same at a codec with a linear A/D (cfg.rx_linear): the received sample
 * on the 16-bit scale */
int16_t v92call_tick_lin(v92call_t *c, uint8_t ds_octet);
/* 1 while both directions carry user data */
int     v92call_data(const v92call_t *c);
/* from data mode: start a retrain (silence, then Tone A) */
void    v92call_retrain(v92call_t *c);
/* from data mode: a rate renegotiation (V.92 9.8.2 or V.90 9.6.2), or with
 * cleardown the orderly end (9.11, V.90 9.7). Returns 0, or -1 */
int     v92call_renegotiate(v92call_t *c, int cleardown);
/* go on-hook: silence from now on */
void    v92call_hangup(v92call_t *c);

#endif
