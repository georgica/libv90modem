/* Calling modem, from the moment the far end has answered (dialling, tones
 * and call progress are the owner's) to data mode:
 *
 *   V.8 (modem/v8orig: answer tone, CI, CM / JM / CJ) ->
 *     JM selects V.32bis / V.32 -> V.32bis 6.1, calling modem (v32_init_call)
 *     JM selects V.22bis / V.22 -> V.22bis 6.3.1.1, calling modem (v22m)
 *     JM selects V.21           -> V.21 channel 1 out (980 / 1180 Hz),
 *                                  channel 2 in (1650 / 1850 Hz)
 *   the answer tone is ANS (a pre-V.8 answerer, V.25):
 *     V.32bis on offer: V.32bis Annex A, the calling automode modem (A.2.1):
 *       AA once ANS has been heard for 1 s, while it lasts and after it;
 *       AC with a phase reversal                  -> V.32bis 6.1
 *       the unscrambled binary 1 of V.22bis for 155 ms: AA stops; ANS
 *         had lasted more than 800 ms from the start of AA -> 456 ms of
 *         silence, then V.22bis 6.3.1.1 (A.2.1.3); otherwise its timer Tc
 *         of 3.1 s runs first, AC still welcome (A.2.1.2)
 *       neither, 1.5 s after the end of ANS, V.21 on offer: AA stops (its
 *         1800 Hz would mask channel 2), a steady channel 2 mark -> V.21;
 *         AC from then on is still answered with AA (A.2.1.1)
 *     otherwise:
 *       its unscrambled binary 1 at 2400 Hz -> V.22bis 6.3.1.1
 *       a steady mark on V.21 channel 2     -> V.21
 *
 * Data: a bit pump, as in modem/answer.h. V.21 carries start-stop characters
 * (handed on as start, 8 data, stop bits); V.22bis and V.32bis a synchronous
 * stream. One sample in, one sample out, fixed point, no allocation. */
#ifndef V90D_MODEM_ORIG_H
#define V90D_MODEM_ORIG_H
#include "v8orig.h"
#include "v22bis.h"
#include "v21.h"
#include "async.h"
#include "../v32/v32.h"

typedef enum {
    ORIG_V8,          /* V.8: waiting for the answer tone, CM / JM */
    ORIG_LEGACY,      /* ANS heard: waiting for V.32bis, V.22bis or V.21 from the answerer */
    ORIG_V22,         /* V.22bis / V.22, start-up and data */
    ORIG_V21,         /* V.21 */
    ORIG_FAILED,
    ORIG_V32          /* V.32bis / V.32, start-up and data */
} orig_phase;

/* the modulation of a connection: the numbers of ANS_MOD_* (modem/answer.h) */
#define ORIG_MOD_NONE   0
#define ORIG_MOD_V21    1
#define ORIG_MOD_V22    2
#define ORIG_MOD_V22BIS 3
#define ORIG_MOD_V32BIS 6

/* why a call ended (orig_t.end) */
#define ORIG_END_NONE       0
#define ORIG_END_NO_TONE    1   /* no answer tone */
#define ORIG_END_NO_JM      2   /* ANSam, but no JM came back to CM */
#define ORIG_END_NO_COMMON  3   /* JM: no modulation in common (or one not built yet); V.32bis: no rate */
#define ORIG_END_NO_CARRIER 4   /* the modulation's start-up failed */
#define ORIG_END_LOST       5   /* the carrier went in data mode */
#define ORIG_END_CLEARDOWN  6   /* the answerer called for a cleardown (V.32bis R3 or E) */

typedef struct {
    int           level;     /* transmit RMS, 16-bit sample units */
    unsigned      modes;     /* V8_MOD_V22 | V8_MOD_V21 ... what we offer */
    int           send_ci;   /* CI before the answer tone (V.8 7.1) */
    int           max_bps;   /* V.32bis rate ceiling, bit/s; 0 = none */
    v22_getbit_fn getbit;
    v22_putbit_fn putbit;
    void         *user;
} orig_cfg;

typedef struct {
    orig_cfg   cfg;
    orig_phase phase;
    int32_t    t;            /* samples in this phase */
    int        used_v8;      /* the answerer spoke V.8 */
    v8orig_t   v8;
    v22bis_t   v22;
    v32_t      v32;
    v21mod_t   fmod; v21demod_t fdem; arx_t uart;
    int        fbit; int32_t cd_run; int64_t cd_thresh;
    int16_t    bpf[128]; int bpos;  /* V.21 channel 2 receive band-pass history */
    int32_t    mark_run;     /* ORIG_LEGACY: how long a V.21 channel 2 mark has been heard */
    /* ORIG_LEGACY with V.32bis on offer (Annex A): the answer tone, and AA */
    uint32_t   at_ph;        /* 2100 Hz detector */
    int32_t    at_i1, at_q1, at_i2, at_q2;
    int32_t    at_on, at_off;      /* samples the tone has been there / away */
    int        aa;           /* AA goes out */
    int32_t    t_aa;         /* samples since AA began (-1: not yet) */
    int32_t    ans_rest;     /* ANS heard from the start of AA to its end (-1: not ended) */
    int32_t    t_quiet;      /* samples since ANS ended (-1: not ended) */
    int32_t    tc;           /* A.2.1.2: samples since the unscrambled binary 1 was first heard (-1: not) */
    int        connected, modulation, bitrate;
    int        end;          /* ORIG_END_* once phase is ORIG_FAILED */
} orig_t;

void    orig_init(orig_t *o, const orig_cfg *cfg);
int16_t orig_tick(orig_t *o, int16_t rx);
/* 1 while user data moves both ways: connected and not retraining */
int     orig_moving(const orig_t *o);
/* the receiver's signal to error ratio in data mode, 0.1 dB (0..600), as
 * answer_snr_db10; -1 where there is none (V.21) */
int     orig_snr_db10(const orig_t *o);
/* V.22bis at 2400 bit/s (6.4) or V.32bis (7.1) in data mode: start a
 * retrain. 0, or -1 */
int     orig_retrain(orig_t *o);
/* V.32bis: the round trip measured in the start-up (NT less the answerer's
 * 64 T), samples; -1 where there is none */
int     orig_rtd(const orig_t *o);

#endif
