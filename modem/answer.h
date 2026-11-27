/* Answering modem: V.8 start-up, then the selected modulation.
 *   V.8 (ANSam, CM/JM/CJ) -> V.92, V.90, V.34, V.22bis / V.22 or V.21 (V.34 when
 *   V8_MOD_V34 is in the modes and the caller has no PCM mode, or V.90 is not
 *   offered; also when a V.90 caller selects V.34 at the end of V.90
 *   Phase 2, V.90 9.2.1.1.8, if V8_MOD_V34 is in the modes), or, when the caller is a
 *   pre-V.8 modem (no CM during ANSam), the legacy V.22bis answer sequence
 *   with a later fall-back to V.21.
 * V.92 (ANS_MODE_V92 next to ANS_MODE_V90): Phase 1 is V.90's and V.8 has no
 * way to say V.92 (V.92 9.1 and its NOTE), so JM is the same. Phase 2 says
 * it: INFO0d bit 27 (Table 15/V.92). If the caller's INFO0a has its bit 26
 * (Table 16), INFO1d takes the form of Table 17 with bit 70 = PCM upstream
 * on offer, and the caller's INFO1a decides (9.3/V.92): PCM upstream (Table
 * 18: Phases 3 and 4 and data mode of V.92, ../v92/v92_p34d), V.34 upstream
 * (V.90 as before) or V.34. No short Phase 2, no modem on hold.
 * V.92 Quick Connect (with ANS_MODE_V90 and ANS_MODE_V92, not after a
 * K56flex agreement, not with ANS_MODE_NO_QC): a QC1a heard during ANSam
 * (modem/v8ans) ends V.8 at once; ANS_QC (modem/ans_qc.c, modem/qcd) sends
 * QCA1d (our P the caller's, LM from our Phase 2 power), QTS and ANSpcm,
 * and on TONEq goes to Phase 2 (9.2.4.1); with no TONEq within 2 s, ANSam
 * again and V.8 as above (9.2.4.3), the QC1a no longer taken. The data
 * frame grid of every Phase 3 of such a call is QTS's (8.3.6); V.42's
 * ODP/ADP is skipped when both P bits are set (9.2.5).
 * x2 (ANS_MODE_X2, with V8_MOD_V34, on a mu-law line): offered to a caller
 * whose CM offers V.34 and no PCM, in its JM (modem/v8ans) and its INFO0a
 * (modem/p2ans). A client that sends the x2 marker after our L1 makes the
 * call x2 (ANS_X2: modem/x2d, PCM downstream, V.34 upstream); one that does
 * not still gets V.34, but sees that JM (carrier inverted after each JM)
 * and INFO0a, not those it would get without x2.
 * After the marker there is no way back to V.34: an x2 start-up that does
 * not reach data mode, or a client's rate change that does not get back to
 * it, ends the call with ANS_END_X2_FAILED.
 * K56flex (ANS_MODE_K56, with V8_MOD_V34, on a mu-law line): V.8bis comes
 * first (ANS_V8BIS: modem/v8bis), then V.8 as above whatever came of it. A
 * client whose MS carries the K56flex block is agreed (k56): x2 is
 * then not offered, and V.34 is told (v34ans_cfg.k56). V.34's Phase 2 then
 * flags our INFO1a when the client's INFO1c takes K56flex up and the symbol
 * rate is 3200 (modem/p2ans): the point of no return, ANS_K56: modem/k56d
 * (PCM downstream, V.34 upstream) to data mode. A K56flex start-up that
 * does not reach data mode ends the call with ANS_END_K56_FAILED; in data
 * mode the upstream's loss ends it with ANS_END_LOST, our cleardown with
 * ANS_END_CLEARDOWN_OURS.
 * The client's retrain (its tone B in data mode) is ANS_K56RT: Phase 2 of
 * a retrain with INFO1a flagged again, then ANS_K56 from the start
 * (retraining until data mode); anything but our flagged INFO1a, or a
 * third retrain in a row (none forgotten until data mode holds 2 s), ends
 * the call with ANS_END_K56_FAILED. We never retrain K56flex.
 * HST (ANS_MODE_HST, and ANS_MODE_HST_LINKED when ANSam ends: an MNP
 * endpoint is linked): a caller that sent neither CM nor AA during ANSam
 * meets ANS_HST_WAIT, silence while the back channel's detector listens
 * for up to W (modem/hst.h); heard, ANS_HST (modem/ans_hst.c), the point
 * of no return: our forward channel's long training at the opening rate,
 * both back receivers, connected once the training is out and a back
 * receiver locked; the caller's bits reach the endpoint through the
 * mailbox until then (v42/hst_link.h). The caller's confirming LA (MNP up)
 * starts the retrain at the best agreed rate. No MNP link within 15 s ends
 * the call with ANS_END_HST_FAILED; no back carrier for 5 s in data mode,
 * ANS_END_LOST. Not heard within W: V.22bis or V.21 as without HST.
 * One sample in, one sample out. The data interface is a bit pump: getbit is
 * called for every transmitted line bit, putbit for every received one, once
 * the connection is up. Characters travel start-stop framed in that bit stream
 * (modem/v14.h); an error-control layer can take the same pump over. */
#ifndef V90D_MODEM_ANSWER_H
#define V90D_MODEM_ANSWER_H
#include "v8ans.h"
#include "v22bis.h"
#include "v21.h"
#include "tonedet.h"
#include "async.h"
#include "p2d.h"
#include "p34d.h"
#include "v34ans.h"
#include "x2d.h"
#include "k56d.h"
#include "v8bis.h"
#include "../v32/v32.h"
#include "../v92/v92_p34d.h"
#include "../v42/hst_link.h"
#include "hst.h"
#include "qcd.h"
#include "callev.h"

typedef enum {
    ANS_V8, ANS_V22, ANS_V21, ANS_FAILED,
    ANS_P2,              /* V.90 Phase 2 (probing / ranging) */
    ANS_P3,              /* V.90 Phases 3 and 4: training */
    ANS_V90,             /* V.90 data mode */
    ANS_RETRAIN,         /* retrain after V.90 Phase 2: 70 ms of silence, then ANS_P2 again */
    ANS_V34,             /* V.34: Phase 2 to data mode, its retrains included (modem/v34ans);
                            or V.34 from Phase 3 on as the call modem, when a V.90 caller
                            selected it in INFO1a (v34.from_v90; its retrains: ANS_RETRAIN) */
    ANS_V32,             /* V.32bis / V.32 from AC on (v32/) */
    ANS_V32_SIL,         /* a caller without V.8 sent AA during our answer tone: 75 ms of silence, then ANS_V32 */
    ANS_P3X,             /* V.92 Phases 3 and 4 with PCM upstream: training (v92/v92_p34d) */
    ANS_V92,             /* V.92 data mode, its rate renegotiations included; retrains: ANS_RETRAIN */
    ANS_X2,              /* x2: the client sent the marker; its start-up and data mode (u.x2) */
    ANS_V8BIS,           /* K56flex offered: V.8bis before V.8 (u.v8b), then ANS_V8 */
    ANS_K56,             /* K56flex: from our flagged INFO1a on (u.k56), its data mode included */
    ANS_K56RT,           /* K56flex: the client's retrain (its tone B in data mode): 70 ms of
                            silence, then V.34's Phase 2 of a retrain (v34.p2, INFO1a flagged
                            again), then ANS_K56 from the start */
    ANS_HST_WAIT,        /* HST offered and linked, a caller without V.8: silent after ANSam,
                            listening for its back channel (u.hst.det) for W, then ANS_V22 or
                            ANS_V21 as without HST */
    ANS_HST,             /* HST: from the detection on (u.hst), data mode included */
    ANS_QC,              /* V.92 short Phase 1 after the caller's QC1a (u.qc): QCA1d, QTS,
                            ANSpcm, TONEq (modem/qcd); then ANS_P2, or ANSam and ANS_V8 again */
    ANS_NPHASES          /* the number of phases (not a phase): a new one goes above */
} ans_phase;

/* in answer_cfg.modes, next to the V8_MOD_ bits (which fit 16 bits): offer
 * V.90 as the digital modem */
#define ANS_MODE_V90 0x10000u
/* with ANS_MODE_V90: indicate V.92 in Phase 2 and offer PCM upstream to a
 * caller that indicates it too (use answer_tick_pcm, as for V.90) */
#define ANS_MODE_V92 0x20000u
/* answer a caller without V.8 that sends the Bell 103 mark (1270 Hz) in
 * Bell 103: 300 bit/s FSK like V.21, on that system's tones */
#define ANS_MODE_BELL103 0x40000u
/* with V8_MOD_V22: when the V.22bis answer sequence has gone unanswered by
 * a caller without V.8, send the Bell answer tone (2225 Hz) for a while:
 * a Bell 212A caller replies with 1200 bit/s as a V.22 caller would */
#define ANS_MODE_BELL212A 0x80000u
/* with V8_MOD_V34: offer x2 (U.S. Robotics, 1997) as its server to a caller
 * whose CM offers V.34 and no PCM; only on a mu-law line */
#define ANS_MODE_X2 0x100000u
/* with V8_MOD_V34: offer K56flex as its server in V.8bis before V.8
 * (modem/v8bis); only on a mu-law line */
#define ANS_MODE_K56 0x200000u
/* offer HST (U.S. Robotics' asymmetric modem) to a caller without V.8;
 * only with ANS_MODE_HST_LINKED too when the modem decides (the end of
 * ANSam) */
#define ANS_MODE_HST 0x400000u
/* an MNP endpoint is linked for HST (v90m_ec_link_hst): set and cleared by
 * the API layer, in this word so that answer_t does not grow */
#define ANS_MODE_HST_LINKED 0x800000u
/* V.34: our MP's transmit ceiling capped at the caller's INFO1c / INFO1a
 * projection (v34ans_cfg.tx_cap) */
#define ANS_MODE_V34_TX_CAP 0x1000000u
/* with ANS_MODE_K56: our CL offers K56flex version 2 alone (the version
 * octet 0x42, as 0.7.1 and the real server did) instead of versions 1 and 2
 * (0x43), and every MS with the block is acknowledged, as in 0.7.1 */
#define ANS_MODE_K56_V2_ONLY 0x2000000u
/* with ANS_MODE_K56 (experimental): an MS that selects version 1 is
 * acknowledged and K56flex's start-up (version 2's, modem/k56d) runs;
 * without it such an MS gets NAK(3) and the call goes on as without K56flex */
#define ANS_MODE_K56_V1 0x4000000u
/* with ANS_MODE_V92: a caller's V.92 QC1a is not taken (V.8 as in 0.7.3,
 * the QC1a ignored): V90M_OPT_NO_QC */
#define ANS_MODE_NO_QC 0x8000000u

#define ANS_MOD_NONE   0
#define ANS_MOD_V21    1
#define ANS_MOD_V22    2     /* 1200 bit/s */
#define ANS_MOD_V22BIS 3     /* 2400 bit/s */
#define ANS_MOD_V90    4     /* PCM downstream, V.34 upstream */
#define ANS_MOD_V34    5
#define ANS_MOD_V32BIS 6     /* V.32bis or V.32: 4800 to 14400 bit/s */
#define ANS_MOD_V92    7     /* PCM both ways: 24000 to 48000 bit/s upstream */
#define ANS_MOD_BELL103 8    /* 300 bit/s, in ANS_V21 */
#define ANS_MOD_BELL212A 9   /* 1200 bit/s, in ANS_V22: V.22 begun after our 2225 Hz */
#define ANS_MOD_X2     10    /* x2: PCM downstream, V.34 upstream */
#define ANS_MOD_K56    11    /* K56flex: PCM downstream, V.34 upstream */
#define ANS_MOD_HST    12    /* HST: a fast forward channel, a 450 / 300 bit/s back channel */

/* why a call ended (answer_t.end, answer_end): set where it fails */
enum {
    ANS_END_NONE,
    ANS_END_FAILED,         /* no connection was made (start-up failed) */
    ANS_END_LOST,           /* the connection was lost */
    ANS_END_CLEARDOWN_FAR,  /* the caller cleared down */
    ANS_END_CLEARDOWN_OURS, /* we cleared down (answer_cleardown) */
    ANS_END_NO_COMMON,      /* V.8: nothing in common with the caller */
    ANS_END_RETRAIN_FAILED, /* V.90, V.92, V.34: a retrain due past the limit in a row;
                               x2: the client asked for a retrain (its Tone A) */
    ANS_END_X2_FAILED,      /* the x2 start-up, or a rate change of the client's, did not complete */
    ANS_END_K56_FAILED,     /* past the point of no return, the K56flex start-up, a rate change
                               or retrain did not complete, or the third retrain in a row */
    ANS_END_HST_FAILED      /* an HST call had no MNP link within its limit */
};

typedef struct {
    int           level;     /* transmit RMS, 16-bit sample units */
    unsigned      modes;     /* V8_MOD_V22 | V8_MOD_V21 ... what we offer */
    v22_getbit_fn getbit;
    v22_putbit_fn putbit;
    void         *user;
    /* bench settings; 0 in both is how it always was */
    int           rx_linear; /* the receive samples are not companded (a codec run in 16-bit
                                linear, answer_tick_lin): V.92 then designs the upstream
                                constellation without the G.711 quantiser's share of the noise */
    int           max_tx_code; /* INFO0d bits 33:37, the most power the caller may ask our
                                downstream to have: -0.5 dBm0 less 0.5 dB per step, 1..31;
                                0 = the default, ANS_MAX_TX_CODE (-12 dBm0) */
} answer_cfg;

#define ANS_MAX_TX_CODE 23

struct ans_scheme;            /* modem/ans_scheme.h */

typedef struct {
    /* the members are in an order that leaves no holes between them on
     * a 64-bit host nor on 32-bit ARM, for callev (at the end): answer_t
     * and v90m_mem_size keep 0.7.0's sizes */
    answer_cfg cfg;
    ans_phase  phase;
    int        used_v8;      /* the caller spoke V.8 */
    const struct ans_scheme *s;  /* the scheme of the phase: set with it */
    int32_t    t;            /* samples in this phase (saturates) */
    int32_t    t_phase;      /* the backstop's clock (saturating): samples in this phase, or
                                in V.34's Phase 2 within ANS_V34 since it began */
    v8ans_t    v8;
    v22bis_t   v22;
    p2d_cfg    p2cfg;
    int        echo_delay, max_down, max_up;    /* answer_v90_line */
    info0_t    info0a;       /* the caller's INFO0a from V.90 Phase 2 (a retrain's has none) */
    p2d_t      p2;
    p34d_t     p34;
    v34ans_t   v34;
    /* V.32bis, V.92, x2 and K56flex never meet in one call (V.92 needs the
     * PCM mode of V.8, x2 a CM without it and V.34 selected, K56flex the
     * V.8bis agreement, which turns x2 off; V.32 is what V.8 selects without
     * V.34 or a caller without V.8 asks for with AA, and no phase leads back
     * to V.8 but ANS_QC's fall-back, before any of them): they share their storage. u.v92 holds a call's V.92 engine
     * from ANS_P3X on, u.x2 the x2 engine from ANS_X2 on; u.v8b K56flex's
     * V.8bis, during ANS_V8BIS only (before any of the others), u.k56 the
     * K56flex engine from ANS_K56 on; u.hst the HST engine from
     * ANS_HST_WAIT on (a caller without V.8, after ANSam: none of the
     * others; V.22 and V.21, which may follow it, keep their own storage).
     * u.qc: short Phase 1 of V.92's Quick Connect, during ANS_QC only
     * (after V.8, before Phase 2: none of the others is in use then). */
    union { v32_t v32; v92d_t v92; x2d_t x2; v8bis_t v8b; k56d_t k56; hst_t hst; qcd_t qc; } u;
    v32_aadet_t aadet;       /* 1800 Hz from the caller while the answer tone runs */
    v21mod_t   fmod;
    int        bpos;         /* the place in bpf */
    v21demod_t fdem; arx_t uart;
    int        fbit, fsk_mod, via_bell; int32_t cd_run; int64_t cd_thresh;
    int16_t    bpf[128];     /* V.21 / Bell 103 receive band-pass history (bpos above) */
    tonedet_t  bell;                 /* the Bell 103 caller's mark while we wait in ANS_V22 */
    tonedet_t  v21mark;              /* the V.21 caller's mark (980 Hz) while we wait in ANS_V22 */
    const int16_t *bpf_taps;         /* v21_bpf or bell103_bpf */
    int        connected, modulation, bitrate;
    int        rx_bitrate;   /* receive direction, where it differs (V.90 upstream) */
    int        end;          /* ANS_END_*: why the call ended (ANS_FAILED) */
    int        retrains;     /* V.90 retrains begun */
    int        retraining;   /* from there until data mode is back: connected stays 1, no data
                                moves (a rate renegotiation is not a retrain: 0) */
    int        rt_recent;    /* retrains lately: one is forgotten per minute of data mode */
    int32_t    rt_clock;
    /* V.92 */
    int        v92_both;     /* both INFO0 sequences indicated V.92 (9.3/V.92): the Phase 2 of
                                every retrain is V.92's, and V.42 skips ODP / ADP (9.3.1/V.92) */
    int        pcm_up;       /* PCM upstream is on offer: bit 70 of the next INFO1d. Cleared
                                for the rest of the call when a V.92 start-up failed or the
                                line did not carry the lowest PCM upstream rate */
    int        v92_data;     /* this V.92 engine has reached data mode */
    uint8_t    pcm;          /* the codeword of this sample while V.90 runs */
    /* x2 and K56flex: flags of one octet each, so that the API's modem can
     * hold its HST endpoint's address without growing (v90m_mem_size) */
    uint8_t    x2_offered;   /* ANS_MODE_X2 with V.34, on a mu-law line */
    uint8_t    k56_offered;  /* ANS_MODE_K56 with V.34, on a mu-law line: ANS_V8BIS first */
    uint8_t    k56;          /* K56flex's V.8bis, ANS_K56_* below: ANS_K56_AGREED when the
                                client's MS carried the K56flex block and we sent ACK(1)
                                (x2 is then not offered in this call); the rest is read
                                only for the report (v90m_get_k56) */
    callev_t   ev;           /* who began the retrains and renegotiations (v90m_get_restarts);
                                in the holes the order above took away (16 bytes) */
} answer_t;

/* answer_t.k56: one octet, so that answer_t keeps its size on every target */
#define ANS_K56_AGREED   0x01u  /* we sent ACK(1): K56flex agreed */
#define ANS_K56_CL       0x02u  /* our CL went out */
#define ANS_K56_MS       0x04u  /* the client's MS carried the K56flex block */
#define ANS_K56_DECLINED 0x08u  /* ... selecting version 1: we sent NAK(3) */
#define ANS_K56_VER_SHIFT 4     /* bits 4 to 6: the version its MS selected (v8bis_version_of), 0..5 */
#define ANS_K56_VER_MASK 0x70u
#define ANS_K56_AGREED_OF(a) (((a)->k56 & ANS_K56_AGREED) != 0)

/* the HST engine shares the union and fits in it (the spec's "HST pump":
 * no growth of answer_t) */
typedef char hst_size_check[sizeof(hst_t) <= sizeof(v92d_t) ? 1 : -1];
/* and so does short Phase 1's */
typedef char qcd_size_check[sizeof(qcd_t) <= sizeof(v92d_t) ? 1 : -1];

void    answer_init(answer_t *a, const answer_cfg *cfg);
/* The same modem at the G.711 interface: one received codeword in, one out
 * (law from the V.90 configuration, mu-law by default). This is the interface
 * to use with a codec: V.90 needs its exact codewords on the line. The linear
 * answer_tick() is equivalent for everything else and returns the linear
 * value of the codeword while V.90 runs. */
uint8_t answer_tick_pcm(answer_t *a, uint8_t rx);
/* The same for a codec that receives in 16-bit linear and still sends G.711
 * levels (the box's SLIC in linear mode, fw/drv/pump.h): rx is the received
 * sample on the 16-bit scale of V.90 Table 1 (what v90_octet_linear gives for
 * a codeword, so a codec value needs no scaling), the result is the codeword
 * to send. Everything the modem receives it receives as linear samples
 * anyway; this entry only leaves out the G.711 decoder, and with it the
 * quantiser. Set cfg.rx_linear with it. */
uint8_t answer_tick_lin(answer_t *a, int16_t rx);
/* optional, between answer_init and the first tick: Phase 2 parameters other
 * than the defaults (-12 dBm0, mu-law, no I/O delay, 3200 and 3000 symbols/s);
 * after the first tick it changes nothing */
void    answer_v90_cfg(answer_t *a, const p2d_cfg *cfg);
/* optional, likewise (V.92, V.90 and V.34; for V.34 max_down is the ceiling of
 * what we send, max_up of what we receive): echo_delay = samples from handing a codeword to the
 * line until its echo starts to come back (a driver's buffering; see
 * p34d.h; -1 = not known, V.90 and V.92 measure it in every start-up);
 * max_down / max_up = rate ceilings in bit/s, 0 = none. The PCM upstream of
 * V.92 goes up to 48000 bit/s: a max_up of 33600 holds it at 33333. */
void    answer_v90_line(answer_t *a, int echo_delay, int max_down, int max_up);
int16_t answer_tick(answer_t *a, int16_t rx);
/* Tell the calling modem that we are hanging up, where the modulation has a
 * way to say so (V.90 9.7, V.92 9.11). Returns 0 when that has begun: the
 * phase goes to ANS_FAILED by itself within a fraction of a second, ending
 * ANS_END_CLEARDOWN_OURS. V.92: the cleardown is a rate sequence (CPd) with
 * data rate 0 (drn = 0); a caller that does not acknowledge it, or does not
 * answer the renegotiation at all, is given up to 3.5 s.
 * Returns -1 when there is nothing to send: just stop. */
int     answer_cleardown(answer_t *a);
/* What the receiver makes of the caller's signal in data mode: signal to
 * error ratio at the decision point in units of 0.1 dB, clipped to 0..600.
 * -1 where the modulation's receiver has no such figure (V.32bis, V.21) or
 * there is no carrier. V.92: the mean square of the constellation points we
 * dictated in CPd over the decoder's noise estimate (v92_dec_quality); -1
 * while a V.92 call retrains. */
int     answer_snr_db10(const answer_t *a);
/* Round-trip delay of the connection as the handshake measured it, in
 * samples; -1 where the modulation does not measure one. */
int     answer_rtd(const answer_t *a);
/* V.92, V.90 or V.34 data mode: start a retrain. Returns 0, or -1. */
int     answer_retrain(answer_t *a);
/* 1 while the modem carries user data in both directions: connected, not
 * retraining, and (V.90, V.92) not inside a rate renegotiation. */
int     answer_moving(const answer_t *a);
/* 1 when both modems indicated V.92 in Phase 2 (V.92 9.3) and LAPM in V.8:
 * the caller then skips the ODP / ADP exchange of V.42 (9.3.1/V.92). After
 * a Quick Connect start (short Phase 1 taken, no fall-back to V.8): 1 when
 * both P bits asked for LAPM (9.2.5). */
int     answer_odp_bypass(const answer_t *a);
/* Why the call ended: ANS_END_*, ANS_END_NONE until the phase is ANS_FAILED. */
int     answer_end(const answer_t *a);
/* v90m_get_restarts: on the sample data stops for a rate renegotiation, who
 * began it and why (CEV_* of callev.h) */
int     answer_reneg_cev(const answer_t *a);
/* The modem's side of the HST mailbox (v42/hst_link.h), for the MNP
 * endpoint linked to it: *state HST_LINK_UNDECIDED until the modem is
 * either on an HST call (HST_LINK_ON in ANS_HST, the mailbox returned) or
 * connected or ended on another path (HST_LINK_NONE, from the sample that
 * raises connected on); ANS_HST_WAIT is UNDECIDED. NULL but with ON. No
 * side effects: it only reports. */
hst_link_t *answer_hst_link(answer_t *a, int *state);
/* HST: the channel reversals of this call (v90m_stats.reversals); 0 on
 * any other call */
unsigned answer_hst_reversals(const answer_t *a);
/* 1 once the modem has decided whether to offer HST (it has left ANS_V8
 * and ANS_V8BIS, or took a QC1a: still 1 after a fall-back to V.8): an
 * endpoint can no longer be linked for this call. */
int     answer_hst_decided(const answer_t *a);
/* The echo delay in samples the running engine measured or was given (V.90,
 * V.92, x2, K56flex): the engine of the phase; between engines (a retrain,
 * the end) the modulation's. -1 where there is none. */
int     answer_echo_delay(const answer_t *a);
/* V.34 (and V.34 selected by a V.90 caller's INFO1a), our transmit
 * direction, from the last Phase 2 and Phase 4 of the call (kept after the
 * end, until the next answer_init): *proj_bps the caller's own projection
 * at the symbol rate in use (2400 x INFO1c / INFO1a bits 30:33), *ask_bps
 * its MP's maximum (0 until its MP came; after our or its cleardown, the
 * one data mode last began with), *capped 1 when ANS_MODE_V34_TX_CAP
 * lowered our MP's offer below what it would have been without it. All 0
 * when no V.34 training began this call (x2 and K56flex included). */
void    answer_v34_tx_cap(const answer_t *a, int *proj_bps, int *ask_bps, int *capped);

#endif
