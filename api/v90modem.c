/* libv90modem: the public API (include/v90modem.h) over the modem, V.14 and
 * V.42 code. A thin layer: it maps the configuration, runs the modem one
 * sample at a time exactly as the code underneath expects, and reads its
 * state after every sample to report changes. Nothing here touches the
 * signal path. */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#pragma GCC visibility push(default)
#include "../include/v90modem.h"
#pragma GCC visibility pop
#include "v90modem_priv.h"
#include "v90modem_fields.h"
#include "v90modem_diag.h"
#include "../modem/pcmenc.h"
#include "../modem/v14.h"
#include "../modem/qc.h"
#include "../v90/v90_pcm.h"

/* the public numbers are the internal ones (checked at compile time) */
#define SAME(a, b) ((int)(a) == (int)(b))
typedef char v90m_check_conn[(SAME(V90M_CONN_V21, ANS_MOD_V21) && SAME(V90M_CONN_V22, ANS_MOD_V22) && SAME(V90M_CONN_V22BIS, ANS_MOD_V22BIS)
                              && SAME(V90M_CONN_V90, ANS_MOD_V90) && SAME(V90M_CONN_V34, ANS_MOD_V34) && SAME(V90M_CONN_V32BIS, ANS_MOD_V32BIS)
                              && SAME(V90M_CONN_V92, ANS_MOD_V92) && SAME(V90M_CONN_BELL103, ANS_MOD_BELL103)
                              && SAME(V90M_CONN_BELL212A, ANS_MOD_BELL212A) && SAME(V90M_CONN_X2, ANS_MOD_X2)
                              && SAME(V90M_CONN_K56FLEX, ANS_MOD_K56) && SAME(V90M_CONN_HST, ANS_MOD_HST)) ? 1 : -1];
typedef char v90m_check_ec[(SAME(V90M_EC_DETECT, V42EC_DETECT) && SAME(V90M_EC_ESTABLISH, V42EC_ESTABLISH) && SAME(V90M_EC_CONNECTED, V42EC_CONNECTED)
                            && SAME(V90M_EC_ASYNC, V42EC_ASYNC) && SAME(V90M_EC_RELEASING, V42EC_RELEASING) && SAME(V90M_EC_DISCONNECTED, V42EC_DISCONNECTED)
                            && SAME(V90M_EC_R_LOCAL, V42LAPM_R_LOCAL) && SAME(V90M_EC_R_NEGOTIATION, V42LAPM_R_NEGOTIATION)
                            && SAME(V90M_EC_R_NO_EC, V42EC_R_NO_EC) && SAME(V90M_EC_R_NO_SABME, V42EC_R_NO_SABME)
                            && SAME(V90M_EC_COMP_V42BIS, V42EC_COMP_V42BIS) && SAME(V90M_EC_COMP_V44, V42EC_COMP_V44)
                            && SAME(V90M_COMP_AUTO, V42COMP_AUTO) && SAME(V90M_COMP_ALWAYS, V42COMP_ALWAYS) && SAME(V90M_COMP_NEVER, V42COMP_NEVER)
                            && SAME(V90M_BRK_DESTRUCTIVE_EXPEDITED, V42_BRK_DESTRUCTIVE_EXPEDITED)
                            && SAME(V90M_BRK_NONDESTRUCTIVE_EXPEDITED, V42_BRK_NONDESTRUCTIVE_EXPEDITED)) ? 1 : -1];

/* the strictest alignment any member can need: where a v90m_align lands
 * after a char */
typedef struct { char c; v90m_align u; } v90m_align_probe;
#define V90M_ALIGN offsetof(v90m_align_probe, u)
static int aligned(const void *p) { return ((uintptr_t)p % V90M_ALIGN) == 0; }
static void seterr(int *err, int e) { if (err) *err = e; }

/* the header's numbers as "MAJOR.MINOR.PATCH" (make check-version holds
 * them to the Makefile's VERSION) */
#define V90M_STR_(x) #x
#define V90M_STR(x)  V90M_STR_(x)
const char *v90m_version(void)
{
    return V90M_STR(V90M_VERSION_MAJOR) "." V90M_STR(V90M_VERSION_MINOR) "." V90M_STR(V90M_VERSION_PATCH);
}

uint8_t v90m_g711_encode(int alaw, int16_t linear) { return pcm_encode(alaw != 0, linear); }
int16_t v90m_g711_decode(int alaw, uint8_t codeword) { return (int16_t)v90_octet_linear(alaw != 0, codeword); }

/* ------------------------------------------------------------------ modem */
struct v90m {
    v90m_cfg cfg;
    uint8_t  owned;             /* from v90m_create: v90m_destroy frees it */
    neg_track_t neg;            /* v90m_get_negotiation. With owned, the 4 octets an int owned took:
                                   struct v90m must not grow (v90m_mem_size() is pinned) */
    /* what the last sample left, to report changes */
    uint8_t  was_connected, was_moving, was_ended;
    uint8_t  busy;              /* inside v90m_process_*: a callback may be running */
    uint8_t  dead;              /* v90m_destroy came from a callback: stop after this sample */
    uint8_t  reneg_pending;     /* data stopped, not in a retrain: counts when it moves again, becomes
                                   a retrain, or the call ends other than by a cleardown; 0 none,
                                   else who began it (CEV_*, modem/callev.h: never 0) */
    uint8_t  meas_tick;         /* samples since the SNR and round trip were last kept */
    uint8_t  was_mod;           /* V90M_CONN_* (they fit 8 bits) */
    int      was_tx, was_rx;
    unsigned renegotiations;
    int16_t  last_snr, last_rtd;    /* the last measured while data moves; -1 before the first */
    diag_track_t diag;          /* v90m_get_diag: as the last sample before the end left it */
    uint8_t  data_reached;      /* data moved both ways this call (v90m_get_diag) */
    v90m_ec_t *hst_ec;          /* the endpoint linked to us for HST (v90m_ec_link_hst), NULL none:
                                   the link is two-way, so either side can end it */
    uint32_t reneg_at;          /* v90m_get_restarts' clock where reneg_pending's stop came (last
                                   before u: on 32-bit targets in the pad before it) */
    union { answer_t a; orig_t o; } u;     /* by cfg.role */
};

#define ORIGINATING(m) ((m)->cfg.role == V90M_ROLE_ORIGINATE)
#define V90M_MOD_ALL  (V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34 | V90M_MOD_V90 | V90M_MOD_V92 \
                       | V90M_MOD_BELL103 | V90M_MOD_BELL212A | V90M_MOD_X2 | V90M_MOD_K56 | V90M_MOD_HST)
/* the defaults: all but V.92, Bell (which would change what a caller
 * without V.8 hears from us after 4 s), x2, K56flex and HST */
#define V90M_MOD_DEFAULT (V90M_MOD_ALL & ~(V90M_MOD_V92 | V90M_MOD_BELL103 | V90M_MOD_BELL212A | V90M_MOD_X2 | V90M_MOD_K56 \
                                          | V90M_MOD_HST))
/* what the calling side can do: all but x2, K56flex and HST (V.90 and V.92
 * as their analogue modem) */
#define V90M_MOD_ORIG (V90M_MOD_ALL & ~(V90M_MOD_X2 | V90M_MOD_K56 | V90M_MOD_HST))
/* the option bits (bits 16 and up) */
#define V90M_OPTS (V90M_OPT_V34_TX_CAP | V90M_OPT_K56_V2_ONLY | V90M_OPT_K56_V1 | V90M_OPT_NO_QC \
                   | V90M_OPT_NEG_EVENTS)

/* V.8's modes and PCM bits go out as the engines have them */
typedef char v90m_check_v8[(SAME(V90M_V8_V34, V8_MOD_V34) && SAME(V90M_V8_V34HD, V8_MOD_V34HD) && SAME(V90M_V8_V32, V8_MOD_V32)
                            && SAME(V90M_V8_V22, V8_MOD_V22) && SAME(V90M_V8_V17, V8_MOD_V17) && SAME(V90M_V8_V29, V8_MOD_V29)
                            && SAME(V90M_V8_V27TER, V8_MOD_V27TER) && SAME(V90M_V8_V26TER, V8_MOD_V26TER)
                            && SAME(V90M_V8_V26BIS, V8_MOD_V26BIS) && SAME(V90M_V8_V23, V8_MOD_V23)
                            && SAME(V90M_V8_V23HD, V8_MOD_V23HD) && SAME(V90M_V8_V21, V8_MOD_V21)
                            && SAME(V90M_V8_PCM_ANALOG, V8_PCM_ANALOG) && SAME(V90M_V8_PCM_DIGITAL, V8_PCM_DIGITAL)
                            && SAME(V90M_V8_PCM_V91, V8_PCM_V91)) ? 1 : -1];

typedef char v90m_check_orig[(SAME(ORIG_MOD_V21, ANS_MOD_V21) && SAME(ORIG_MOD_V22, ANS_MOD_V22) && SAME(ORIG_MOD_V22BIS, ANS_MOD_V22BIS)
                              && SAME(ORIG_MOD_V32BIS, ANS_MOD_V32BIS) && SAME(ORIG_MOD_V34, ANS_MOD_V34)
                              && SAME(ORIG_MOD_V90, ANS_MOD_V90) && SAME(ORIG_MODE_V90, ANS_MODE_V90)
                              && SAME(ORIG_MOD_V92, ANS_MOD_V92) && SAME(ORIG_MODE_V92, ANS_MODE_V92)
                              && SAME(ORIG_MOD_BELL103, ANS_MOD_BELL103) && SAME(ORIG_MODE_BELL103, ANS_MODE_BELL103)
                              && SAME(ORIG_MOD_BELL212A, ANS_MOD_BELL212A) && SAME(ORIG_MODE_BELL212A, ANS_MODE_BELL212A)) ? 1 : -1];

void v90m_cfg_default(v90m_cfg *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof *cfg);
    cfg->role = V90M_ROLE_ANSWER;
    cfg->modulations = V90M_MOD_DEFAULT;
    cfg->tx_level = 3000;
    cfg->echo_delay = -1;
}

static int check_cfg(const v90m_cfg *c)
{
    if (c->role != V90M_ROLE_ANSWER && c->role != V90M_ROLE_ORIGINATE) return V90M_EINVAL;
    if (c->modulations & ~(V90M_MOD_ALL | V90M_OPTS)) return V90M_EINVAL;
    if (c->max_tx_code < 0 || c->max_tx_code > 31 || c->tx_level < 0 || c->tx_level > V90M_TX_LEVEL_MAX
        || c->max_down_bps < 0 || c->max_up_bps < 0 || c->echo_delay < -1)
        return V90M_EINVAL;
    if (!(c->modulations & V90M_MOD_ALL)) return V90M_EINVAL;
    /* V.92 is V.90's start-up with PCM upstream on top, in either role */
    if ((c->modulations & V90M_MOD_V92) && !(c->modulations & V90M_MOD_V90)) return V90M_ENOTSUP;
    /* x2's upstream is V.34, and only its server (answering) side is here */
    if ((c->modulations & V90M_MOD_X2) && !(c->modulations & V90M_MOD_V34)) return V90M_ENOTSUP;
    /* K56flex's upstream is V.34 too, and only its server side is here */
    if ((c->modulations & V90M_MOD_K56) && !(c->modulations & V90M_MOD_V34)) return V90M_ENOTSUP;
    if (c->role == V90M_ROLE_ORIGINATE) {
        if (!(c->modulations & V90M_MOD_ORIG)) return V90M_ENOTSUP;
        if (c->modulations & ~(V90M_MOD_ORIG | V90M_OPTS)) return V90M_ENOTSUP;     /* the options: ignored */
        /* V.92 is V.90's start-up with PCM upstream on top */
        if ((c->modulations & V90M_MOD_V92) && !(c->modulations & V90M_MOD_V90)) return V90M_ENOTSUP;
    } else {
        /* answering, Bell 212A is the fallback of the V.22 answerer */
        if ((c->modulations & V90M_MOD_BELL212A) && !(c->modulations & V90M_MOD_V22)) return V90M_ENOTSUP;
        /* x2's and K56flex's upstream is V.34 from 2400 bit/s: a lower ceiling
         * leaves the client no rate after the point of no return */
        if ((c->modulations & (V90M_MOD_X2 | V90M_MOD_K56)) && c->max_up_bps > 0 && c->max_up_bps < 2400) return V90M_EINVAL;
    }
    return V90M_OK;
}

static unsigned modes_of(unsigned mods)
{
    unsigned m = 0;
    if (mods & V90M_MOD_V21) m |= V8_MOD_V21;
    if (mods & V90M_MOD_V22) m |= V8_MOD_V22;
    if (mods & V90M_MOD_V32BIS) m |= V8_MOD_V32;
    if (mods & V90M_MOD_V34) m |= V8_MOD_V34;
    if (mods & V90M_MOD_V90) m |= ANS_MODE_V90;
    if (mods & V90M_MOD_V92) m |= ANS_MODE_V92;
    if (mods & V90M_MOD_BELL103) m |= ANS_MODE_BELL103;
    if (mods & V90M_MOD_BELL212A) m |= ANS_MODE_BELL212A;
    if (mods & V90M_MOD_X2) m |= ANS_MODE_X2;
    if (mods & V90M_MOD_K56) m |= ANS_MODE_K56;
    if (mods & V90M_MOD_HST) m |= ANS_MODE_HST;
    return m;
}

/* a checked cfg into m, whatever m held but owned */
static void setup(v90m_t *m, const v90m_cfg *cfg)
{
    uint8_t owned = m->owned;
    /* the code underneath expects zeroed memory before answer_init */
    memset(m, 0, sizeof *m);
    m->owned = owned;
    m->cfg = *cfg;
    m->last_snr = m->last_rtd = -1;
    if (ORIGINATING(m)) {
        orig_cfg oc;
        memset(&oc, 0, sizeof oc);
        oc.level = cfg->tx_level;
        oc.modes = modes_of(cfg->modulations);
        oc.send_ci = cfg->send_ci != 0;
        /* V.32bis: one rate both ways, the lower ceiling */
        oc.max_bps = cfg->max_down_bps;
        if (cfg->max_up_bps > 0 && (oc.max_bps <= 0 || cfg->max_up_bps < oc.max_bps)) oc.max_bps = cfg->max_up_bps;
        /* V.34: max_down caps what we send, max_up what we receive (as answering);
         * V.90 likewise: max_down the V.34 upstream, max_up the PCM downstream */
        oc.max_tx_bps = cfg->max_down_bps; oc.max_rx_bps = cfg->max_up_bps;
        oc.echo_delay = cfg->echo_delay;
        /* V.34 sends codec values, as the answering side's does */
        oc.law = cfg->alaw ? 2 : 1;
        oc.getbit = cfg->getbit;
        oc.putbit = cfg->putbit;
        oc.user = cfg->user;
        orig_init(&m->u.o, &oc);
    } else {
        answer_cfg ac;
        memset(&ac, 0, sizeof ac);
        ac.level = cfg->tx_level;
        ac.modes = modes_of(cfg->modulations);
        /* HST under a ceiling below its lowest rate (4800): not offered,
         * the modem answers as without it (include/v90modem.h) */
        if (cfg->max_down_bps > 0 && cfg->max_down_bps < 4800) ac.modes &= ~ANS_MODE_HST;
        if (cfg->modulations & V90M_OPT_V34_TX_CAP) ac.modes |= ANS_MODE_V34_TX_CAP;
        if (cfg->modulations & V90M_OPT_K56_V2_ONLY) ac.modes |= ANS_MODE_K56_V2_ONLY;
        if (cfg->modulations & V90M_OPT_K56_V1) ac.modes |= ANS_MODE_K56_V1;
        if (cfg->modulations & V90M_OPT_NO_QC) ac.modes |= ANS_MODE_NO_QC;
        ac.getbit = cfg->getbit;
        ac.putbit = cfg->putbit;
        ac.user = cfg->user;
        ac.rx_linear = cfg->codec_linear_rx != 0;
        ac.max_tx_code = cfg->max_tx_code;
        answer_init(&m->u.a, &ac);
        if (cfg->alaw) {
            /* the Phase 2 defaults with the law changed */
            p2d_cfg pc = m->u.a.p2cfg;
            pc.alaw = 1;
            answer_v90_cfg(&m->u.a, &pc);
        }
        answer_v90_line(&m->u.a, cfg->echo_delay, cfg->max_down_bps, cfg->max_up_bps);
    }
}

static void hst_drop_ec(v90m_t *m);

int v90m_reset(v90m_t *m, const v90m_cfg *cfg)
{
    int r;
    if (!m || !cfg) return V90M_EINVAL;
    /* from a callback: the block under way runs on the modem as it is */
    if (m->busy) return V90M_ESTATE;
    if ((r = check_cfg(cfg)) != V90M_OK) return r;
    hst_drop_ec(m);             /* a new call: the endpoint links again */
    setup(m, cfg);
    return V90M_OK;
}

size_t v90m_mem_size(void) { return sizeof(struct v90m); }

v90m_t *v90m_init(void *mem, size_t len, const v90m_cfg *cfg, int *err)
{
    v90m_t *m = mem;
    int r;
    if (!mem || !cfg || !aligned(mem)) { seterr(err, V90M_EINVAL); return NULL; }
    if (len < sizeof *m) { seterr(err, V90M_ENOMEM); return NULL; }
    if ((r = check_cfg(cfg)) != V90M_OK) { seterr(err, r); return NULL; }
    m->owned = 0;
    setup(m, cfg);
    seterr(err, V90M_OK);
    return m;
}

v90m_t *v90m_create(const v90m_cfg *cfg, int *err)
{
    v90m_t *m;
    int r;
    if (!cfg) { seterr(err, V90M_EINVAL); return NULL; }
    if ((r = check_cfg(cfg)) != V90M_OK) { seterr(err, r); return NULL; }
    if (!(m = malloc(sizeof *m))) { seterr(err, V90M_ENOMEM); return NULL; }
    m->owned = 1;
    setup(m, cfg);
    seterr(err, V90M_OK);
    return m;
}

void v90m_destroy(v90m_t *m)
{
    if (!m) return;
    hst_drop_ec(m);             /* the endpoint keeps no address of ours */
    /* from a callback: v90m_process_* ends the block and frees it */
    if (m->busy) { m->dead = 1; return; }
    if (m->owned) free(m);
}

/* the modem's state, on the side orig says (ORIGINATING(m)): a constant
 * where the role is known, as in after_answer and after_orig */
static int failed_of(const v90m_t *m, int orig) { return orig ? m->u.o.phase == ORIG_FAILED : m->u.a.phase == ANS_FAILED; }
static int connected_of(const v90m_t *m, int orig) { return orig ? m->u.o.connected : m->u.a.connected; }
static int moving_of(const v90m_t *m, int orig) { return orig ? orig_moving(&m->u.o) : answer_moving(&m->u.a); }
static int retraining_of(const v90m_t *m, int orig)
{
    if (orig) return orig_retraining(&m->u.o);
    return m->u.a.retraining;
}
static int mod_of(const v90m_t *m, int orig) { return orig ? m->u.o.modulation : m->u.a.modulation; }
static int tx_of(const v90m_t *m, int orig) { return orig ? m->u.o.bitrate : m->u.a.bitrate; }
static int rx_of(const v90m_t *m, int orig)
{
    if (orig) return m->u.o.rx_bitrate > 0 ? m->u.o.rx_bitrate : m->u.o.bitrate;
    return m->u.a.rx_bitrate > 0 ? m->u.a.rx_bitrate : m->u.a.bitrate;
}

static v90m_end end_of(const v90m_t *m)
{
    if (ORIGINATING(m)) {
        switch (m->u.o.phase == ORIG_FAILED ? m->u.o.end : ORIG_END_NONE) {
        case ORIG_END_NONE: return V90M_END_NONE;
        case ORIG_END_NO_TONE: return V90M_END_NO_ANSWER_TONE;
        case ORIG_END_NO_JM: return V90M_END_NO_JM;
        case ORIG_END_NO_COMMON: return V90M_END_NO_COMMON;
        case ORIG_END_LOST: return V90M_END_LOST;
        case ORIG_END_CLEARDOWN: return V90M_END_CLEARDOWN_FAR;
        case ORIG_END_HANGUP: return V90M_END_CLEARDOWN_OURS;
        case ORIG_END_RETRAIN: return V90M_END_RETRAIN_FAILED;
        default: return V90M_END_FAILED;
        }
    } else {
        /* ANS_END_* in order */
        static const v90m_end ans_end[] = {
            V90M_END_NONE, V90M_END_FAILED, V90M_END_LOST, V90M_END_CLEARDOWN_FAR, V90M_END_CLEARDOWN_OURS,
            V90M_END_NO_COMMON, V90M_END_RETRAIN_FAILED, V90M_END_X2_FAILED, V90M_END_K56_FAILED,
            V90M_END_HST_FAILED
        };
        int e = answer_end(&m->u.a);
        return e >= 0 && e < (int)(sizeof ans_end / sizeof ans_end[0]) ? ans_end[e] : V90M_END_FAILED;
    }
}

static void event(v90m_t *m, v90m_event ev) { if (m->cfg.on_event && !m->dead) m->cfg.on_event(m->cfg.user, ev); }

/* the SNR and the round trip as the engine says (-1: not known now) */
static int snr_of(const v90m_t *m, int orig)
{
    return orig ? orig_snr_db10(&m->u.o) : m->u.a.connected ? answer_snr_db10(&m->u.a) : -1;
}
static int rtd_of(const v90m_t *m, int orig)
{
    return orig ? orig_rtd(&m->u.o) : m->u.a.connected ? answer_rtd(&m->u.a) : -1;
}
/* keep what is measured while connected, for the status after the end of the call */
static void keep_measures(v90m_t *m, int orig)
{
    int v = snr_of(m, orig);
    if (v >= 0) m->last_snr = v;
    v = rtd_of(m, orig);
    if (v >= 0) m->last_rtd = v;
}

/* the diagnostics after a sample that did not end the call */
static uint8_t iphase_of(const v90m_t *m, int orig) { return (uint8_t)(orig ? m->u.o.phase : m->u.a.phase); }
static void diag_track(v90m_t *m, int orig)
{
    if (orig) {
        diag_track_orig(&m->diag, &m->u.o);
        neg_track_orig(&m->neg, &m->u.o, m->diag.sel);
    } else {
        diag_track_answer(&m->diag, &m->u.a);
        neg_track_answer(&m->neg, &m->u.a, m->diag.sel);
    }
}

/* the diagnostics after a sample, and V90M_EV_NEGOTIATION where the host
 * asked for it and something it reports changed before data mode (reached:
 * data_reached as it was before this sample) */
static inline void track(v90m_t *m, int orig, int reached)
{
    diag_track_t was;
    neg_track_t on;
    if (reached || !(m->cfg.modulations & V90M_OPT_NEG_EVENTS)) { diag_track(m, orig); return; }
    was = m->diag; on = m->neg;
    diag_track(m, orig);
    if (m->diag.phase != was.phase || m->diag.sel != was.sel || m->neg.trying != on.trying
        || m->neg.basis != on.basis || m->neg.flags != on.flags)
        event(m, V90M_EV_NEGOTIATION);
}

/* after each sample: only reads the modem, on the side orig says */
static inline void after(v90m_t *m, int orig)
{
    int moving, reached, rx, connected = connected_of(m, orig), failed = failed_of(m, orig);
    callev_t *ev = orig ? &m->u.o.ev : &m->u.a.ev;
    ev->now++;                  /* v90m_get_restarts' clock */
    if (!connected && !m->was_connected && !failed) {          /* start-up: nothing to report */
        track(m, orig, 0);
        return;
    }
    moving = moving_of(m, orig);
    reached = m->data_reached;
    /* the diagnostics keep the last sample before the end; while data
     * moves in the same phase of the modem they cannot change (the phase
     * reached is data mode, and a retrain, which may stay in that phase of
     * the modem, stops the data first) */
    if (moving) m->data_reached = 1;
    if (!failed && !(moving && m->diag.phase == V90M_PHASE_DATA && m->diag.iphase == iphase_of(m, orig)))
        track(m, orig, reached);
    rx = rx_of(m, orig);
    /* kept only while data moves: a retrain's figures are not the call's */
    if (moving && connected && !failed && (uint8_t)(++m->meas_tick & 127) == 0) keep_measures(m, orig);
    if (connected && !m->was_connected) {
        m->was_connected = 1;
        m->was_mod = (uint8_t)mod_of(m, orig); m->was_tx = tx_of(m, orig); m->was_rx = rx;
        event(m, V90M_EV_CONNECTED);
    } else if (m->was_connected && (mod_of(m, orig) != m->was_mod || tx_of(m, orig) != m->was_tx || rx != m->was_rx)) {
        m->was_mod = (uint8_t)mod_of(m, orig); m->was_tx = tx_of(m, orig); m->was_rx = rx;
        event(m, V90M_EV_RATES);
    }
    if (moving != m->was_moving) {
        /* a stop that is neither a retrain nor the end is a rate renegotiation;
         * it counts when data moves again, or when it becomes a retrain or
         * ends the call (below), unless that end is a cleardown, ours or the
         * far end's, which stops data for good: a cleardown never counts */
        if (!moving) {
            m->reneg_pending = connected && !retraining_of(m, orig) && !failed
                               ? (uint8_t)(orig ? orig_reneg_cev(&m->u.o) : answer_reneg_cev(&m->u.a)) : 0;
            m->reneg_at = ev->now - 1;  /* the clock the engines saw in this sample */
        } else if (m->reneg_pending) {
            m->renegotiations++;
            callev_reneg(ev, m->reneg_pending, m->reneg_at);
            m->reneg_pending = 0;
        }
        m->was_moving = moving;
        event(m, moving ? V90M_EV_DATA_MOVING : V90M_EV_DATA_STOPPED);
    }
    if (m->reneg_pending) {
        if (failed) {
            v90m_end e = end_of(m);
            if (e != V90M_END_CLEARDOWN_OURS && e != V90M_END_CLEARDOWN_FAR) {
                m->renegotiations++;
                callev_reneg(ev, m->reneg_pending, m->reneg_at);
            }
            m->reneg_pending = 0;
        } else if (retraining_of(m, orig)) {
            m->renegotiations++;     /* the renegotiation became a retrain: counted in both */
            callev_reneg(ev, m->reneg_pending, m->reneg_at);
            m->reneg_pending = 0;
        }
    }
    if (failed && !m->was_ended) {
        m->was_ended = 1;
        event(m, V90M_EV_ENDED);
    }
}
/* one per role, for the loops of v90m_process_*, which know theirs */
static void after_answer(v90m_t *m) { after(m, 0); }
static void after_orig(v90m_t *m) { after(m, 1); }

/* The end of a block: when a callback destroyed the modem after sample i,
 * the rest of the block is silence and the modem is freed (when owned). */
static int block_end(v90m_t *m, size_t i, size_t n, uint8_t *tx8, int16_t *tx16)
{
    m->busy = 0;
    if (!m->dead) return V90M_OK;
    if (tx8) { if (n > i + 1) memset(tx8 + i + 1, m->cfg.alaw ? 0xD5 : 0xFF, n - i - 1); }
    else for (i++; i < n; i++) tx16[i] = 0;
    if (m->owned) free(m);
    return V90M_OK;
}

int v90m_process_g711(v90m_t *m, const uint8_t *rx, uint8_t *tx, size_t n)
{
    size_t i;
    if (!m || (n && (!rx || !tx))) return V90M_EINVAL;
    m->busy = 1;
    if (ORIGINATING(m)) {
        int alaw = m->cfg.alaw != 0;
        for (i = 0; i < n; i++) {
            tx[i] = pcm_encode(alaw, orig_tick(&m->u.o, (int16_t)v90_octet_linear(alaw, rx[i])));
            after_orig(m);
            if (m->dead) break;
        }
    } else
        for (i = 0; i < n; i++) { tx[i] = answer_tick_pcm(&m->u.a, rx[i]); after_answer(m); if (m->dead) break; }
    return block_end(m, i, n, tx, NULL);
}

int v90m_process_lin_rx(v90m_t *m, const int16_t *rx, uint8_t *tx, size_t n)
{
    size_t i;
    if (!m || (n && (!rx || !tx))) return V90M_EINVAL;
    m->busy = 1;
    if (ORIGINATING(m)) {
        int alaw = m->cfg.alaw != 0;
        for (i = 0; i < n; i++) { tx[i] = pcm_encode(alaw, orig_tick(&m->u.o, rx[i])); after_orig(m); if (m->dead) break; }
    } else
        for (i = 0; i < n; i++) { tx[i] = answer_tick_lin(&m->u.a, rx[i]); after_answer(m); if (m->dead) break; }
    return block_end(m, i, n, tx, NULL);
}

int v90m_process_linear(v90m_t *m, const int16_t *rx, int16_t *tx, size_t n)
{
    size_t i;
    if (!m || (n && (!rx || !tx))) return V90M_EINVAL;
    m->busy = 1;
    if (ORIGINATING(m))
        for (i = 0; i < n; i++) { tx[i] = orig_tick(&m->u.o, rx[i]); after_orig(m); if (m->dead) break; }
    else
        for (i = 0; i < n; i++) { tx[i] = answer_tick(&m->u.a, rx[i]); after_answer(m); if (m->dead) break; }
    return block_end(m, i, n, NULL, tx);
}

int v90m_retrain(v90m_t *m)
{
    callev_t *ev;
    uint8_t was;
    int r;
    if (!m) return V90M_EINVAL;
    /* the next retrain of ours counted is this one (v90m_get_restarts): some
     * engines count it before they return; refused, as it was (one asked
     * for before may still be to count) */
    ev = ORIGINATING(m) ? &m->u.o.ev : &m->u.a.ev;
    was = ev->flags; ev->flags |= CEV_HOST_DUE;
    r = ORIGINATING(m) ? orig_retrain(&m->u.o) : answer_retrain(&m->u.a);
    if (r != 0) { ev->flags = (uint8_t)((ev->flags & ~CEV_HOST_DUE) | (was & CEV_HOST_DUE)); return V90M_ESTATE; }
    return V90M_OK;
}

int v90m_cleardown(v90m_t *m)
{
    if (!m) return V90M_EINVAL;
    /* V.34 (11.7), V.90 (9.7), V.92 (9.11), either role; V.21, V.22, V.22bis,
     * V.32bis and Bell have none in data mode */
    if (ORIGINATING(m)) return orig_cleardown(&m->u.o) == 0 ? V90M_OK : V90M_ESTATE;
    return answer_cleardown(&m->u.a) == 0 ? V90M_OK : V90M_ESTATE;
}

void v90m_get_state(const v90m_t *m, v90m_state *st)
{
    int orig;
    if (!m || !st) return;
    orig = ORIGINATING(m);
    st->connected = connected_of(m, orig);
    st->moving = moving_of(m, orig);
    st->retraining = retraining_of(m, orig);
    st->modulation = (v90m_conn)mod_of(m, orig);
    st->tx_bps = tx_of(m, orig);
    st->rx_bps = rx_of(m, orig);
    st->used_v8 = orig ? m->u.o.used_v8 : m->u.a.used_v8;
    st->odp_bypass = orig ? 0 : answer_odp_bypass(&m->u.a);
    st->end = end_of(m);
}

void v90m_get_stats(const v90m_t *m, v90m_stats *st)
{
    int orig;
    if (!m || !st) return;
    orig = ORIGINATING(m);
    st->modulation = (v90m_conn)mod_of(m, orig);
    st->tx_bps = tx_of(m, orig);
    st->rx_bps = rx_of(m, orig);
    st->renegotiations = m->renegotiations;
    st->reversals = ORIGINATING(m) ? 0u : answer_hst_reversals(&m->u.a);
    /* the last measured while connected stays after the call, until the next one */
    if (orig) {
        const orig_t *o = &m->u.o;
        st->snr_db10 = orig_snr_db10(o);
        st->rtd = orig_rtd(o);
        st->echo_delay = -1;
        st->retrains = (unsigned)orig_retrains(o);
    } else {
        const answer_t *a = &m->u.a;
        st->snr_db10 = a->connected ? answer_snr_db10(a) : -1;
        st->rtd = a->connected ? answer_rtd(a) : -1;
        st->echo_delay = answer_echo_delay(a);
        st->retrains = (unsigned)a->retrains;
    }
    if (st->snr_db10 < 0) st->snr_db10 = m->last_snr;
    if (st->rtd < 0) st->rtd = m->last_rtd;
}

void v90m_get_diag(const v90m_t *m, v90m_diag *d)
{
    v90m_end e;
    if (!m || !d) return;
    d->phase = (v90m_phase)m->diag.phase;
    d->selected = (v90m_conn)m->diag.sel;
    d->data_reached = m->data_reached;
    /* an orderly cleardown after data mode is no failure; otherwise the
     * cause, read from the engine that ran last (nothing changes it after
     * the end) */
    e = end_of(m);
    if (m->data_reached && (e == V90M_END_CLEARDOWN_OURS || e == V90M_END_CLEARDOWN_FAR)) d->error = 0;
    else d->error = ORIGINATING(m) ? diag_err_orig(&m->u.o, &m->diag) : diag_err_answer(&m->u.a, &m->diag);
}

void v90m_get_v34_cap(const v90m_t *m, v90m_v34_cap *c)
{
    if (!m || !c) return;
    c->proj_bps = c->ask_bps = c->capped = 0;
    if (!ORIGINATING(m)) answer_v34_tx_cap(&m->u.a, &c->proj_bps, &c->ask_bps, &c->capped);
}

typedef char v90m_check_cause[(SAME(V90M_CAUSE_NONE, CEV_NONE) && SAME(V90M_CAUSE_FAR, CEV_FAR)
                               && SAME(V90M_CAUSE_OURS, CEV_OF(WHY_OURS)) && SAME(V90M_CAUSE_RX_ERRORS, CEV_OF(WHY_RX_ERRORS))
                               && SAME(V90M_CAUSE_RX_LOST, CEV_OF(WHY_RX_LOST)) && SAME(V90M_CAUSE_HOST, CEV_HOST)
                               && WHY_RX_ERRORS == 2) ? 1 : -1];      /* (v32/'s rt_why says 2) */

void v90m_get_restarts(const v90m_t *m, v90m_restarts *r)
{
    const callev_t *ev;
    unsigned rt, rt_ours;
    if (!m || !r) return;
    if (ORIGINATING(m)) {
        ev = &m->u.o.ev;
        rt_ours = (unsigned)m->u.o.rt_ours;
    } else {
        ev = &m->u.a.ev;
        rt = (unsigned)m->u.a.retrains;
        rt_ours = rt - ev->rt_far;
    }
    r->reneg_far = ev->reneg_far;
    r->reneg_ours = m->renegotiations - ev->reneg_far;
    r->retrain_far = ev->rt_far;
    r->retrain_ours = rt_ours;
    r->retrain_host = ev->rt_host;
    r->last = (v90m_cause)ev->last;
    r->last_retrain = (ev->flags & CEV_LAST_RT) != 0;
    r->last_ms = ev->last != CEV_NONE ? ev->last_at / 8u : 0u;
}

/* v90m_k56 from answer_t.k56, one octet (answer_t and v90m_mem_size keep
 * 0.7.1's sizes): every version v8bis_version_of gives fits its field, the
 * flags below it and ANS_K56_MS_V8 (0.7.4) above it; offered is V.8bis's own mask; the option bits are
 * options (bits 16 and up), apart */
typedef char v90m_check_k56[(sizeof(((answer_t *)0)->k56) == 1
                             && (ANS_K56_VER_MASK >> ANS_K56_VER_SHIFT) >= 5 && (ANS_K56_VER_MASK & 0x8fu) == 0
                             && ((ANS_K56_AGREED | ANS_K56_CL | ANS_K56_MS | ANS_K56_DECLINED) & ANS_K56_VER_MASK) == 0
                             && ((ANS_K56_AGREED | ANS_K56_CL | ANS_K56_MS | ANS_K56_DECLINED | ANS_K56_VER_MASK)
                                 & ANS_K56_MS_V8) == 0 && ANS_K56_MS_V8 <= 0xffu
                             && V8B_VERS_1 == 1 && V8B_VERS_2 == 2 && V8B_VER_MASK == 0x1f
                             && (V90M_OPTS & V90M_MOD_ALL) == 0 && (V90M_OPTS & 0xffffu) == 0
                             && (ANS_MODE_K56_V2_ONLY & ANS_MODE_K56_V1) == 0) ? 1 : -1];

void v90m_get_k56(const v90m_t *m, v90m_k56 *k)
{
    unsigned f;
    if (!m || !k) return;
    k->offered = k->exchanged = k->chosen = k->declined = k->agreed = 0;
    if (ORIGINATING(m)) return;
    f = m->u.a.k56;
    /* the mask the CL carried (modem/answer.c's k56_offer and V8B_NS_DEFAULT) */
    if (f & ANS_K56_CL)
        k->offered = v8bis_cl_versions(V8B_NS_DEFAULT, (m->u.a.cfg.modes & ANS_MODE_K56_V2_ONLY) ? V8B_VERS_2 : V8B_VERS_BOTH);
    k->exchanged = (f & ANS_K56_MS) != 0;
    k->chosen = (int)((f & ANS_K56_VER_MASK) >> ANS_K56_VER_SHIFT);
    k->declined = (f & ANS_K56_DECLINED) != 0;
    k->agreed = (f & ANS_K56_AGREED) != 0;
}

/* v90m_qc from answer_t.v8.qc (modem/v8ans.h, 8 octets carved in v8ans_t)
 * and answer_t.k56: no state of its own. qc.oct is the QC1a's octet once
 * V8A_QC_HEARD (before it, the tracker's candidate copy); V.8 tracks no
 * QC1a after that (v8ans_tick), so it is the one acted on. LM lives in aux
 * bits 3-4, TONEq's ms in tq_lo / tq_hi. */
typedef char v90m_check_qc[(sizeof(((answer_t *)0)->v8.qc) == 8 && V8A_QCX_LM_SHIFT + 2 <= 8
                            && (V8A_QCX_TRACK & (3u << V8A_QCX_LM_SHIFT)) == 0
                            && ((V8A_QCX_OFFER | V8A_QCX_SPENT | V8A_QCX_ONHOLD) & (3u << V8A_QCX_LM_SHIFT)) == 0
                            && (ANS_K56_MS_V8 & 0xffu) == ANS_K56_MS_V8) ? 1 : -1];

void v90m_get_qc(const v90m_t *m, v90m_qc *q)
{
    const answer_t *a;
    unsigned f;
    int wxyz = 0, lapm = 0;
    if (!m || !q) return;
    memset(q, 0, sizeof *q);
    if (ORIGINATING(m)) return;
    a = &m->u.a;
    q->v8bis_ack = (a->k56 & ANS_K56_MS_V8) != 0;
    f = a->v8.qc.flags;
    if (!(f & V8A_QC_HEARD)) return;
    q->heard = 1;
    if (qc1a_parse(a->v8.qc.oct, &wxyz, &lapm) == 0) {
        q->lapm = lapm;
        q->uqts = qc_uqts_ucode(wxyz);          /* -1 for 1111 */
    } else q->uqts = -1;
    q->accepted = (f & V8A_QC_ACCEPTED) != 0;
    if (q->accepted) q->level_db10 = qc_lm_db10((int)((a->v8.qc.aux >> V8A_QCX_LM_SHIFT) & 3u));
    if (f & V8A_QC_TONEQ) q->toneq_ms = a->v8.qc.tq_lo | a->v8.qc.tq_hi << 8;
    q->fallback = (f & V8A_QC_FALLBACK) != 0;
    /* the bypass the call used (F9: after a fallback, V.8's 9.3.1) */
    q->bypass = answer_odp_bypass(a);
    q->short_p2 = (f & V8A_QC_SHORT_FULL) ? 2 : (f & V8A_QC_SHORT) ? 1 : 0;
}

const char *v90m_cause_name(v90m_cause c)
{
    static const char *const names[] = { "none", "far", "ours", "rx_errors", "rx_lost", "host" };
    return (unsigned)c < sizeof names / sizeof names[0] ? names[c] : "?";
}

answer_t *v90m__answer(v90m_t *m) { return &m->u.a; }
orig_t   *v90m__orig(v90m_t *m) { return &m->u.o; }

/* ------------------------------------------------------------ start-stop */
struct v90m_async { int owned; v14_t v; };

size_t v90m_async_mem_size(void) { return sizeof(struct v90m_async); }
void v90m_async_reset(v90m_async_t *a) { int o; if (!a) return; o = a->owned; memset(a, 0, sizeof *a); a->owned = o; v14_init(&a->v); }
v90m_async_t *v90m_async_init(void *mem, size_t len, int *err)
{
    v90m_async_t *a = mem;
    if (!mem || !aligned(mem)) { seterr(err, V90M_EINVAL); return NULL; }
    if (len < sizeof *a) { seterr(err, V90M_ENOMEM); return NULL; }
    a->owned = 0; v90m_async_reset(a); seterr(err, V90M_OK);
    return a;
}
v90m_async_t *v90m_async_create(int *err)
{
    v90m_async_t *a = malloc(sizeof *a);
    if (!a) { seterr(err, V90M_ENOMEM); return NULL; }
    a->owned = 1; v90m_async_reset(a); seterr(err, V90M_OK);
    return a;
}
void v90m_async_destroy(v90m_async_t *a) { if (a && a->owned) free(a); }
int  v90m_async_getbit(void *a) { return v14_getbit(&((v90m_async_t *)a)->v); }
void v90m_async_putbit(void *a, int bit) { v14_putbit(&((v90m_async_t *)a)->v, bit); }
size_t v90m_async_write(v90m_async_t *a, const uint8_t *buf, size_t n)
{
    if (!a) return 0;
    return (size_t)v14_write(&a->v, buf, n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n);
}
size_t v90m_async_read(v90m_async_t *a, uint8_t *buf, size_t max)
{
    if (!a) return 0;
    return (size_t)v14_read(&a->v, buf, max > 0x7FFFFFFF ? 0x7FFFFFFF : (int)max);
}
size_t v90m_async_tx_pending(const v90m_async_t *a) { return a ? (size_t)v14_tx_pending(&a->v) : 0; }
unsigned long v90m_async_frame_errors(const v90m_async_t *a) { return a ? (unsigned long)a->v.rx_frame_errs : 0; }

/* ------------------------------------------------------------------ V.42 */
struct v90m_ec { int owned; v42ec_t e; };

/* check_ec_cfg, cfg_in, cfg_out and status_out: api/v90modem_fields.h,
 * from its lists of the fields */

void v90m_ec_cfg_default(v90m_ec_cfg *c, int originator)
{
    v42ec_cfg_t i;
    if (!c) return;
    v42ec_cfg_default(&i, originator);
    cfg_out(c, &i);
}

/* ---- the HST link (v90m_ec_link_hst), two-way: the endpoint's get has
 * the modem as its context, and the modem has the endpoint in hst_ec.
 * Every change goes through hst_pair and hst_unpair, which keep the two
 * consistent, so neither side ever holds the address of an object that
 * is gone: the modem's destroy and reset end the link from its side. */
static hst_link_t *api_hst_get(void *ctx, int *state)
{
    v90m_t *m = ctx;
    if (ORIGINATING(m)) { *state = HST_LINK_NONE; return NULL; }   /* not reached: a reset unlinks */
    return answer_hst_link(&m->u.a, state);
}

/* the modem e is linked to, NULL for none */
static v90m_t *linked_modem(const v90m_ec_t *e)
{
    return v42ec_linked(&e->e) && e->e.link_get == api_hst_get ? (v90m_t *)e->e.link_ctx : NULL;
}

/* end e's link, from either side: the modem's bit goes while it has not
 * decided (it would offer HST with nobody to answer the caller's LR);
 * after that the decision is made and the call keeps it */
static void hst_release(v90m_t *m, const v90m_ec_t *e)
{
    if (!m) return;
    if (m->hst_ec == e) m->hst_ec = NULL;
    if (!ORIGINATING(m) && !answer_hst_decided(&m->u.a)) m->u.a.cfg.modes &= ~ANS_MODE_HST_LINKED;
}

static void hst_unpair(v90m_ec_t *e)
{
    hst_release(linked_modem(e), e);
    v42ec_set_link(&e->e, NULL, NULL);
}

/* the modem's side: its endpoint, if still linked to it, lets go */
static void hst_drop_ec(v90m_t *m)
{
    v90m_ec_t *e = m->hst_ec;
    m->hst_ec = NULL;
    if (e && linked_modem(e) == m) hst_unpair(e);
}

/* a checked configuration: v42ec_reset keeps the HST link (decision 18),
 * so a host's reset at V90M_EV_CONNECTED keeps an LR taken during our
 * training; with MNP off it unlinks */
int v90m_ec_reset(v90m_ec_t *e, const v90m_ec_cfg *cfg)
{
    v42ec_cfg_t c;
    v90m_t *m;
    if (!e || !cfg || check_ec_cfg(cfg) != V90M_OK) return V90M_EINVAL;
    m = linked_modem(e);
    cfg_in(&c, cfg);
    v42ec_reset(&e->e, &c);
    if (m && !v42ec_linked(&e->e)) hst_release(m, e);   /* MNP off: the link ends both ways */
    return V90M_OK;
}

/* a clean start, on memory of any content: no link */
static void ec_start(v90m_ec_t *e, int owned, const v90m_ec_cfg *cfg)
{
    v42ec_cfg_t c;
    cfg_in(&c, cfg);
    memset(e, 0, sizeof *e);
    e->owned = owned;
    v42ec_init(&e->e, &c);
}

size_t v90m_ec_mem_size(void) { return sizeof(struct v90m_ec); }

v90m_ec_t *v90m_ec_init(void *mem, size_t len, const v90m_ec_cfg *cfg, int *err)
{
    v90m_ec_t *e = mem;
    if (!mem || !cfg || !aligned(mem) || check_ec_cfg(cfg) != V90M_OK) { seterr(err, V90M_EINVAL); return NULL; }
    if (len < sizeof *e) { seterr(err, V90M_ENOMEM); return NULL; }
    ec_start(e, 0, cfg); seterr(err, V90M_OK);
    return e;
}

v90m_ec_t *v90m_ec_create(const v90m_ec_cfg *cfg, int *err)
{
    v90m_ec_t *e;
    if (!cfg || check_ec_cfg(cfg) != V90M_OK) { seterr(err, V90M_EINVAL); return NULL; }
    if (!(e = malloc(sizeof *e))) { seterr(err, V90M_ENOMEM); return NULL; }
    ec_start(e, 1, cfg); seterr(err, V90M_OK);
    return e;
}

void v90m_ec_destroy(v90m_ec_t *e)
{
    if (!e) return;
    hst_unpair(e);              /* the modem keeps no address of ours */
    if (e->owned) free(e);
}

int v90m_ec_link_hst(v90m_ec_t *e, v90m_t *m)
{
    /* an originator's endpoint has MNP off (v42ec_init) */
    if (!e || !m || !e->e.cfg.mnp.enable || ORIGINATING(m)) return V90M_EINVAL;
    if (answer_hst_decided(&m->u.a)) return V90M_ESTATE;
    hst_unpair(e);              /* from the modem it had, if any */
    hst_drop_ec(m);             /* one endpoint per modem: the one it had lets go */
    m->hst_ec = e;
    m->u.a.cfg.modes |= ANS_MODE_HST_LINKED;
    v42ec_set_link(&e->e, api_hst_get, m);
    return V90M_OK;
}
int    v90m_ec_tx_bit(v90m_ec_t *e) { return v42ec_tx_bit(&e->e); }
void   v90m_ec_rx_bit(v90m_ec_t *e, int bit) { v42ec_rx_bit(&e->e, bit); }
/* NULL (the header): every function checks its object but the two per-bit ones */
void   v90m_ec_tick(v90m_ec_t *e, unsigned ms) { if (e) v42ec_tick(&e->e, ms); }
void   v90m_ec_line_stopped(v90m_ec_t *e) { if (e) v42ec_line_stopped(&e->e); }
void   v90m_ec_line_restored(v90m_ec_t *e) { if (e) v42ec_line_restored(&e->e); }
int    v90m_ec_set_rates(v90m_ec_t *e, int tx_bps, int rx_bps)
{
    if (!e || v42ec_set_rates(&e->e, tx_bps, rx_bps) != 0) return V90M_EINVAL;
    return V90M_OK;
}
size_t v90m_ec_write(v90m_ec_t *e, const uint8_t *d, size_t n) { return e ? v42ec_write(&e->e, d, n) : 0; }
size_t v90m_ec_write_space(const v90m_ec_t *e) { return e ? v42ec_write_space(&e->e) : 0; }
size_t v90m_ec_read(v90m_ec_t *e, uint8_t *d, size_t max) { return e ? v42ec_read(&e->e, d, max) : 0; }
size_t v90m_ec_read_avail(const v90m_ec_t *e) { return e ? v42ec_read_avail(&e->e) : 0; }
int    v90m_ec_tx_idle(const v90m_ec_t *e) { return e ? v42ec_tx_idle(&e->e) : V90M_EINVAL; }
int    v90m_ec_tx_busy(const v90m_ec_t *e) { return e ? v42ec_tx_busy(&e->e) : V90M_EINVAL; }
void   v90m_ec_release(v90m_ec_t *e) { if (e) v42ec_release(&e->e); }
void   v90m_ec_abort(v90m_ec_t *e) { if (e) v42ec_abort(&e->e); }
int    v90m_ec_send_break(v90m_ec_t *e, int opt, int len10ms)
{
    if (!e || (opt != V90M_BRK_NONDESTRUCTIVE_INSEQ && opt != V90M_BRK_NONDESTRUCTIVE_EXPEDITED
               && opt != V90M_BRK_DESTRUCTIVE_EXPEDITED)) return V90M_EINVAL;
    return v42ec_send_break(&e->e, opt, len10ms) == 0 ? V90M_OK : V90M_ESTATE;
}
int    v90m_ec_get_break(v90m_ec_t *e, int *opt, int *len10ms) { return e ? v42ec_get_break(&e->e, opt, len10ms) : V90M_EINVAL; }
int    v90m_ec_state(const v90m_ec_t *e) { return e ? v42ec_state(&e->e) : V90M_EINVAL; }

void v90m_ec_get_status(const v90m_ec_t *e, v90m_ec_status *st)
{
    v42ec_status_t s;
    if (!e || !st) return;
    v42ec_get_status(&e->e, &s);
    status_out(st, &s, &e->e);
}

v42ec_t *v90m__ec(v90m_ec_t *e) { return &e->e; }

/* the bounds in the header hold on this platform */
typedef char v90m_check_mem[sizeof(struct v90m) <= V90M_MEM_SIZE ? 1 : -1];
typedef char v90m_check_ec_mem[sizeof(struct v90m_ec) <= V90M_EC_MEM_SIZE ? 1 : -1];
typedef char v90m_check_async_mem[sizeof(struct v90m_async) <= V90M_ASYNC_MEM_SIZE ? 1 : -1];

/* the menus as the engine that ran V.8 has them (nothing changes them
 * after V.8) */
void v90m_get_negotiation(const v90m_t *m, v90m_negotiation *n)
{
    const v8_menu_t *cm, *jm;
    if (!m || !n) return;
    memset(n, 0, sizeof *n);
    n->trying = (v90m_conn)m->neg.trying;
    n->basis = (v90m_basis)m->neg.basis;
    if (ORIGINATING(m)) {
        if (!m->u.o.v8.jm_seen) return;
        cm = &m->u.o.v8.cfg.local; jm = &m->u.o.v8.jm;
    } else {
        if (m->u.a.v8.njm <= 0) return;
        cm = &m->u.a.v8.cm; jm = &m->u.a.v8.jm;
    }
    n->far_menu = 1;
    n->cm_modes = cm->modes; n->jm_modes = jm->modes;
    /* the three bits sent (V8_PCM_NO_V34_DATA is a local menu's own) */
    n->cm_pcm = cm->has_pcm ? cm->pcm & 7u : 0; n->jm_pcm = jm->has_pcm ? jm->pcm & 7u : 0;
}
