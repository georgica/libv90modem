/* The HST answering branch through the public API (spec "The answering
 * sequence"; modem/ans_hst.c): ANS_HST_WAIT after ANSam, ANS_HST to data,
 * the post-agreement retrain, the start-up end codes. Our end is the
 * library's answering modem with an MNP endpoint linked before the first
 * sample and reset at V90M_EV_CONNECTED, as docs/error-control.md tells a
 * host to (decision 18); it ticks the endpoint every ms from the first
 * sample on. The caller is sim/hstcall (written from the facts, not from
 * the pump) on sim/v32line (G.711 mu-law at our side, 6 dB loss, a mild
 * loop, 25 ms, our hybrid's echo 15 dB down, -60 dBm0 of noise).
 *
 *   rates       codes 2..6 offered by the caller (its mask up to the code)
 *               x back 450 and 300 (the LR at 450): CONNECTED as HST with
 *               tx_bps 9600 (the opening rate) and rx_bps 450; the
 *               post-agreement retrain stops data (DATA_STOPPED, then
 *               DATA_MOVING) and ends at the best agreed rate (RATES when
 *               it differs from 9600); rx_bps 300 once the caller's back
 *               channel moved (back 300); then 16 KiB each way through MNP
 *               (TEST_SHORT: 4 KiB forward, 1 KiB back), every octet
 *               checked; retrains 1, reversals 0. And a caller that starts
 *               its back channel at 300 (preflight F11): rx_bps 300 at
 *               CONNECTED
 *   hst96       the 1986 HST (rates 0x10, back 300): 9600 / 300; then
 *               v90m_retrain (holding forward: the short training at 9600,
 *               DATA_STOPPED and DATA_MOVING, retrains 2), and more data
 *   plain_mnp   the caller's LR without 0xC0: plain MNP over HST, data at
 *               the opening rate, no retrain
 *   unlinked    HST offered but no endpoint linked: no ANS_HST_WAIT; the
 *               caller hears exactly what it hears with HST not offered
 *               (our transmit hash over 8 s)
 *   max_down    max_down_bps 12000: our 0xC0 rates 0x3C, data at 12000
 *   lr_in_training  the caller's LR (lr_delay_ms 600 and 1000) leaves while
 *               our long training goes out (asserted from the timestamps:
 *               its first bit after ANS_HST began, its last before our
 *               training ended): sent once (no resend), MNP up
 *   no_lr       the caller never sends an LR: the endpoint ends NO_EC at
 *               our training's end + 5 s (+-10 ms); a host that clears
 *               down then gets V90M_END_CLEARDOWN_OURS; one that does not,
 *               V90M_END_HST_FAILED 15 s after ANS_HST began
 *   lost_fwd    the caller's carriers stop in data: V90M_END_LOST 5 s on
 *               (plus our carrier detect's release)
 *   legacy_unchanged  HST offered and linked against V.8 callers (V.90,
 *               V.34: the library's calling modem), a V.32bis caller that
 *               answers ANSam with AA (sim/v32call) and callers without
 *               V.8 (V.22bis: sim/callpeer; V.21 and Bell 103: plain FSK
 *               callers built here): the same modulation, rates and data as
 *               with HST not offered; the V.8 and AA callers sample for
 *               sample the same, the last three connected at most HST_W_MS
 *               later
 *   alaw        an A-law line (sim/v32line alaw), codes 6 / 450: data
 *   block_size  one 14400 / 450 call with v90m_process_* blocks of 1, 160
 *               and 4000 samples: the same transmitted samples (FNV-1a),
 *               the same events, the same endpoint status at the end. The
 *               line takes our samples 4000 samples late in all three, and
 *               the host here ticks the endpoint by bit time from its
 *               putbit (a fractional-ms accumulator), writes in its getbit
 *               and reads in its putbit (preflight F5: the claim is
 *               "identical for a host that ticks by bit time")
 *   rev_caller  (Task 21) the caller's backlog once our data is in: its
 *               0x1R, our LNA; 20 ms after it drained our forward carrier
 *               stops and our back transmitter starts at the agreed speed;
 *               the caller trains forward at the rate in force, the long
 *               training; RATES with tx and rx exchanged, data never
 *               stopped, reversals 1, retrains and renegotiations as
 *               before; the data both ways intact
 *   rev_ours    then our backlog, holding the back channel 2 s: our 0x16;
 *               at the caller's LNA our back transmitter stops, 200 ms, our
 *               short training, which the caller's receiver starts from its
 *               kept equaliser; reversals 2
 *   rev_back300 the same with the back channel at 300
 *   rev_many    both ends always behind: 20 reversals within 120 s (TEST_SHORT
 *               6), both ends counting the same; the caller's trainings
 *               long the first time, short after; then no more data, all
 *               delivered intact
 *   rev_lost    the caller takes no notice of our LNA: within 3 s of the
 *               switch we hold the forward channel again, a retrain and not
 *               a reversal (reversals 2: that switch and the caller's
 *               later one; the caller's 1; retrains 2); the call goes on
 *   rev_ours_la rev_ours against a caller that sends an LA first after each
 *               switch, as our endpoint does: the same 2 reversals (the
 *               other cases' caller does not: nothing says a Courier does)
 *   snr         holding the back channel, v90m_stats.snr_db10 is the
 *               forward receiver's, within 1 dB of the line's SNR
 * Rate changes (Task 22b; spec "Rate changes"):
 *   up          holding forward, the caller's 0x34: our switch by marks
 *               14400 -> 9600, no retrain; then its reversal: holding back
 *               at 9600 on a good line, our 0x35 10 s after the receiver
 *               came into data, then 0x36 10 s after that switch was in,
 *               each carried out by the caller's marks and followed by our
 *               receiver without a retrain (RATES rx 12000, 14400)
 *   down        holding back at 14400, frame errors (a received bit turned
 *               over every 1999): 10, then 11 s of none, no LN (the window
 *               opened by the first closed); then 15 within 10 s: our 0x25,
 *               the caller's short training, a retrain (DATA_STOPPED,
 *               DATA_MOVING, retrains 2); our next LN 0x36 no sooner than
 *               10 s after the downshift
 *   retrain_by_2R  holding forward, the caller's 0x25: our short training
 *               at 12000 once our LNA has gone, a retrain, RATES tx 12000;
 *               MNP's T401 held through it (the endpoint stopped, T401 where
 *               it stood or stopped by an LA, no expiry)
 *   max_up      max_up_bps 9600: the caller's 0x16 granted, then our 0x24
 *               once its training is in, its training at 9600; its 0x36: an
 *               LNA, a count (violations), our 0x24 again; never above 9600
 *   recovery    the caller carries 0x3R out by a training and listens for
 *               one (hstcall retrain_3r, BinModem's way) where we switch by
 *               marks: nothing acknowledged, its back channel alive; 2 x
 *               T401 on (within 1 s more) our short training at 12000, a
 *               retrain, the data moving again
 *   fallback    (Task 16's) holding back at 12000, white noise at our input
 *               to 15 dB SNR, where trainings at 12000 and 9600 fail: in
 *               data at 7200 within 30 s, holding back, at most 8 retrains,
 *               none and no LN in the last 15 s; the data intact
 *
 * Built with -DHST_PROFILE_BM (make check-hst-bm) the caller trains as
 * BinModem (V.17's long train) and, with no case named, only `rates` and
 * `rev_caller` run, the switch then 20 ms plus V.17's turn-off after the
 * LNA drained.
 *
 * V90M_END_HST_FAILED and v90m_stats.reversals are pinned. The output (the golden file) holds exact integers
 * and pass / fail lines; times only with HST_VERBOSE. Each case seeds its
 * own data. Linked with the development archive alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "v90modem.h"
#include "v90modem_priv.h"
#include "test.h"
#include "hst.h"
#include "pcmenc.h"
#include "v90_pcm.h"
#include "v21.h"
#include "v14.h"
#include "async.h"
#include "tonedet.h"
#include "hstcall.h"
#include "v32line.h"
#include "v32call.h"
#include "callpeer.h"

#define FS 8000
#define PI 3.14159265358979323846

static int verbose;
static void bound(const char *what, int ok)
{
    printf("  %s: %s\n", what, ok ? "pass" : "FAIL");
    CHECK(ok);
}
static int16_t sat16(double v)
{
    v = floor(v + 0.5);
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}
static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p; size_t i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
/* the data: octet i of a stream seeded by seed (a hash, so any octet can be checked alone) */
static uint8_t prbs(uint32_t seed, long i)
{
    uint32_t x = seed * 0x9E3779B9u ^ (uint32_t)i * 0x85EBCA6Bu;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return (uint8_t)x;
}

#ifdef TEST_SHORT
#define DATA_FWD 4096L
#define DATA_BACK 1024L
#else
#define DATA_FWD 16384L
#define DATA_BACK 16384L
#endif

/* ===================== our end ===================== */
#define NEV 256
typedef struct {
    v90m_t *m; v90m_ec_t *ec; v90m_ec_cfg ecfg;
    int linked;
    int raw;                     /* 1: no endpoint on the bits (legacy callers): PRBS out, a hash in */
    int bittime;                 /* the endpoint ticked by bit time from putbit, data in the callbacks */
    int clear_on_noec;           /* clear down when the endpoint ends with NO_EC */
    int alaw;
    int rx_bps;
    uint32_t bacc;               /* bit time not yet ticked, 1/900 ms */
    int nev, ev[NEV], ev_tx[NEV], ev_rx[NEV];
    long ev_ms[NEV];
    long get_ms;                 /* the last ms getbit was called in */
    long conn_ms, end_ms, wait_ms, hst_ms, v22_ms, trained_ms, noec_ms;
    int end, conn_tx, conn_rx, conn_mod;
    /* data, through the endpoint */
    long w, to_send, r, bad;
    uint32_t seed_out, seed_in;
    /* raw bits */
    uint32_t rawtx, rawh; long rawn;
    /* a bit of what we receive turned over every flip_every bits (0: none) */
    long flip_every, flip_n;
} our_t;
static our_t O;
static long now_ms;
static hstcall_t HC;

static void our_write(our_t *o)
{
    while (o->w < o->to_send && v90m_ec_state(o->ec) == V90M_EC_CONNECTED) {
        uint8_t b = prbs(o->seed_out, o->w);
        if (v90m_ec_write(o->ec, &b, 1) != 1) break;
        o->w++;
    }
}
static void our_read(our_t *o)
{
    uint8_t buf[64];
    size_t n, i;
    while ((n = v90m_ec_read(o->ec, buf, sizeof buf)) > 0)
        for (i = 0; i < n; i++, o->r++) if (buf[i] != prbs(o->seed_in, o->r)) o->bad++;
}
static int our_get(void *u)
{
    our_t *o = u;
    o->get_ms = now_ms;
    if (o->raw) { o->rawtx = o->rawtx * 1103515245u + 12345u; return (int)(o->rawtx >> 30) & 1; }
    if (o->bittime) our_write(o);
    return v90m_ec_tx_bit(o->ec);
}
static void our_put(void *u, int b)
{
    our_t *o = u;
    if (o->raw) { o->rawh = (o->rawh ^ (uint32_t)(b & 1)) * 16777619u; o->rawn++; return; }
    if (o->flip_every > 0 && ++o->flip_n >= o->flip_every) { o->flip_n = 0; b ^= 1; }
    v90m_ec_rx_bit(o->ec, b);
    if (o->bittime) {
        unsigned ms;
        our_read(o);
        o->bacc += o->rx_bps == 300 ? 3000u : 2000u;      /* 1000/300 and 1000/450 ms, in 1/900 */
        ms = o->bacc / 900u; o->bacc %= 900u;
        if (ms) v90m_ec_tick(o->ec, ms);
    }
}
static void our_event(void *u, v90m_event ev)
{
    our_t *o = u;
    v90m_state st;
    v90m_get_state(o->m, &st);
    if (o->nev < NEV) { o->ev[o->nev] = ev; o->ev_tx[o->nev] = st.tx_bps; o->ev_rx[o->nev] = st.rx_bps; o->ev_ms[o->nev] = now_ms; o->nev++; }
    switch (ev) {
    case V90M_EV_CONNECTED:
        o->conn_ms = now_ms; o->conn_tx = st.tx_bps; o->conn_rx = st.rx_bps; o->conn_mod = st.modulation;
        if (!o->raw) CHECK_EQ_I(v90m_ec_reset(o->ec, &o->ecfg), V90M_OK);
        /* fall through */
    case V90M_EV_RATES:
        o->rx_bps = st.rx_bps;
        if (!o->raw) CHECK_EQ_I(v90m_ec_set_rates(o->ec, st.tx_bps, st.rx_bps), V90M_OK);
        break;
    case V90M_EV_DATA_STOPPED: if (!o->raw) v90m_ec_line_stopped(o->ec); break;
    case V90M_EV_DATA_MOVING: if (!o->raw) v90m_ec_line_restored(o->ec); break;
    case V90M_EV_ENDED: o->end_ms = now_ms; o->end = st.end; break;
    }
}

typedef struct { unsigned mods; int link, raw, bittime, alaw, max_down, max_up; } our_cfg;
static void our_init(our_t *o, const our_cfg *c)
{
    v90m_cfg mc; int err;
    if (o->m) v90m_destroy(o->m);
    if (o->ec) v90m_ec_destroy(o->ec);
    memset(o, 0, sizeof *o);
    o->linked = c->link; o->raw = c->raw; o->bittime = c->bittime; o->alaw = c->alaw;
    o->conn_ms = o->end_ms = o->wait_ms = o->hst_ms = o->v22_ms = o->trained_ms = o->noec_ms = -1;
    o->rx_bps = 450;
    v90m_ec_cfg_default(&o->ecfg, 0);
    o->ecfg.mnp.enable = 1;
    o->ec = v90m_ec_create(&o->ecfg, &err);
    CHECK(o->ec != NULL);
    v90m_cfg_default(&mc);
    mc.role = V90M_ROLE_ANSWER; mc.modulations = c->mods; mc.alaw = c->alaw; mc.max_down_bps = c->max_down; mc.max_up_bps = c->max_up;
    mc.getbit = our_get; mc.putbit = our_put; mc.on_event = our_event; mc.user = o;
    o->m = v90m_create(&mc, &err);
    CHECK(o->m != NULL);
    if (c->link) CHECK_EQ_I(v90m_ec_link_hst(o->ec, o->m), V90M_OK);
}

static answer_t *ans(void) { return v90m__answer(O.m); }

/* the reversals as the test sees them, a ms at a time (Task 21): ours, each
 * switch of the role, with what went before it (the holder's 20 ms after
 * its LNA drained, the requester's LNA) and what our transmitters do at it;
 * the caller's forward trainings; whether its receiver started our
 * training from its kept equaliser */
#define NREV 64
typedef struct {
    int n;                       /* our role switches */
    long pre[NREV], sw[NREV];    /* ms: HST_REV_GRANT or HST_REV_GAP entered; the role switched */
    int to[NREV];                /* HST_ROLE_* switched to */
    int ftx_off[NREV], btx_on[NREV], btx_bps[NREV];   /* at the switch */
    long ones_ms[NREV];          /* to back: ms from the host's last forward bit to the switch (ones) */
    long tr[NREV]; int tr_len[NREV];   /* to forward: our training's start (ms), its S3 */
    int took_back[NREV];         /* to forward without a request: the 3 s bound */
    int last_rev, last_role; long pre_cur;
    int nhc; int hc_len[NREV], hc_code[NREV], hc_last;   /* the caller's forward trainings */
    int warm_n, warm_ok, hc_frx_last;                    /* its receiver on our later trainings */
} rv_t;
static rv_t RV;
static void rc_init(void);
static void rv_init(void) { memset(&RV, 0, sizeof RV); RV.last_role = HST_ROLE_FWD; rc_init(); }
static void rv_watch(void)
{
    answer_t *a = ans();
    hst_t *h;
    if (a->phase != ANS_HST) return;
    h = &a->u.hst;
    if (h->rev != RV.last_rev) {
        if (h->rev == HST_REV_GRANT || h->rev == HST_REV_GAP) RV.pre_cur = now_ms;
        if (h->rev == HST_REV_TRAIN && RV.n > 0) { RV.tr[RV.n - 1] = now_ms; RV.tr_len[RV.n - 1] = h->ftx.len[2]; }
        RV.last_rev = h->rev;
    }
    if (h->link.role != RV.last_role && RV.n < NREV) {
        int k = RV.n++;
        RV.sw[k] = now_ms; RV.pre[k] = RV.pre_cur; RV.to[k] = h->link.role;
        RV.ftx_off[k] = hst_ftx_seg(&h->ftx) == HST_SEG_OFF;
        RV.btx_on[k] = h->btx.mode != HST_BTX_SILENT; RV.btx_bps[k] = h->btx.bps;
        RV.took_back[k] = h->link.role == HST_ROLE_FWD && h->rev != HST_REV_GAP;
        RV.tr[k] = -1;
        RV.ones_ms[k] = now_ms - O.get_ms;
        RV.pre_cur = -1;
        RV.last_role = h->link.role;
        if (verbose) printf("  (%ld ms: to %s, after %ld; the caller's backlog %ld, we wrote %ld)\n", now_ms,
                            h->link.role == HST_ROLE_FWD ? "forward" : "back", RV.pre[k], hstcall_backlog(&HC), O.w);
    }
    /* the caller */
    if (HC.reversals != RV.hc_last) {
        if (HC.role == HC_ROLE_FWD && RV.nhc < NREV) { RV.hc_len[RV.nhc] = HC.ftx.lens[2]; RV.hc_code[RV.nhc] = HC.ftx.code; RV.nhc++; }
        RV.hc_last = HC.reversals;
    }
    if (HC.frx.state == HC_FRX_TRAIN && RV.hc_frx_last != HC_FRX_TRAIN && h->rev == HST_REV_TRAIN) {
        RV.warm_n++;
        RV.warm_ok += HC.frx.warm;
    }
    RV.hc_frx_last = HC.frx.state;
}

/* the host, once a ms (not with bittime): the endpoint's clock, the data,
 * and the cleardown after NO_EC; and what the test watches */
static void our_ms(our_t *o)
{
    answer_t *a = ans();
    if (o->wait_ms < 0 && a->phase == ANS_HST_WAIT) o->wait_ms = now_ms;
    if (o->hst_ms < 0 && a->phase == ANS_HST) o->hst_ms = now_ms;
    if (o->v22_ms < 0 && a->phase == ANS_V22) o->v22_ms = now_ms;
    if (o->trained_ms < 0 && a->phase == ANS_HST && a->u.hst.link.trained) o->trained_ms = now_ms;
    if (o->raw) return;
    if (!o->bittime) {
        v90m_ec_tick(o->ec, 1);
        our_write(o);
        our_read(o);
    }
    if (o->noec_ms < 0 && v90m_ec_state(o->ec) == V90M_EC_DISCONNECTED) {
        v90m_ec_status st;
        v90m_ec_get_status(o->ec, &st);
        if (st.reason == V90M_EC_R_NO_EC) {
            o->noec_ms = now_ms;
            if (o->clear_on_noec) CHECK_EQ_I(v90m_cleardown(o->m), V90M_OK);
        }
    }
}

/* ===================== the callers ===================== */
/* The profile this program is built for (-DHST_PROFILE_BM: make
 * check-hst-bm runs `rates` and `rev_caller`): the caller trains as
 * BinModem does, V.17's long train, when we do; the S3 the trainings show
 * (hstcall's lens[2], our len[2]: V.17's 64-symbol bridge then); and the
 * switch after our granting LNA: 20 ms, plus V.17's 32 symbols of ones
 * (13.3 ms) under the profile */
#if HST_LONG_V17
#define EXP_S3_LONG  HST_BM_BRIDGE
#define EXP_S3_SHORT HST_BM_BRIDGE
#define EXP_SW_LO    33
#define EXP_SW_HI    36
#define EXP_ONES_LO  13              /* ms of ones (no host bit) before the switch */
#define EXP_ONES_HI  15
#else
#define EXP_S3_LONG  HST_S3_LONG
#define EXP_S3_SHORT HST_S3_SHORT
#define EXP_SW_LO    20
#define EXP_SW_HI    22
#define EXP_ONES_LO  0
#define EXP_ONES_HI  1
#endif
static void hc_defaults(hstcall_cfg *c)
{
    hstcall_defaults(c);
    c->bm_training = HST_LONG_V17;
}

enum { K_HST, K_ORIG, K_AA, K_V22, K_V21, K_B103 };

/* A V.21 caller without V.8 (V.25 5, V.21 7 a)): its mark, 980 Hz, once the
 * answer tone has been gone 100 ms, then ours (1650 Hz) before data; and a
 * Bell 103 caller: its mark, 1270 Hz, once something has been in its
 * receive band for 150 ms. Start-stop through a V.14 terminal each. */
typedef struct {
    int bell;
    tonedet_t ans; v21mod_t mod; v21demod_t dem; arx_t uart; v14_t dte;
    long n, tone_last; int fbit, cd, on, carrier; int64_t thr;
} fsk_t;
#define ANS_2100 ((uint32_t)(((uint64_t)2100 << 32) / 8000))
static void fsk_init(fsk_t *v, int bell)
{
    memset(v, 0, sizeof *v);
    v->bell = bell;
    tonedet_init(&v->ans, ANS_2100);
    if (bell) {
        v21mod_init(&v->mod, BELL103_O_MARK, BELL103_O_SPACE, 8000, 300, 4243);
        v21demod_init(&v->dem, BELL103_A_MARK, BELL103_A_SPACE, 8000, 300);
    } else {
        v21mod_init(&v->mod, V21L_MARK, V21L_SPACE, 8000, 300, 4243);
        v21demod_init(&v->dem, V21H_MARK, V21H_SPACE, 8000, 300);
    }
    arx_init(&v->uart, 8000, 300);
    v14_init(&v->dte);
    v->thr = v21demod_energy_of(&v->dem, 1200); v->fbit = 1; v->tone_last = -1;
}
static int16_t fsk_tick(fsk_t *v, int16_t rx)
{
    int level, heard, c, i, done;
    int16_t out;
    v->n++;
    level = v21demod_tick(&v->dem, rx);
    heard = v->dem.energy >= v->thr;
    if (!v->on) {
        if (v->bell) { v->cd = heard ? v->cd + 1 : 0; if (v->cd > 150 * 8) v->on = 1; }
        else {
            if (tonedet_tick(&v->ans, rx)) v->tone_last = v->n;
            if (v->tone_last >= 0 && v->n - v->tone_last >= 100 * 8) v->on = 1;
        }
        if (!v->on) return 0;
        v->cd = 0;
    }
    v->cd = heard ? v->cd + 1 : 0;
    if (v->cd > 150 * 8) v->carrier = 1;
    if (v->carrier) {
        c = arx_tick(&v->uart, heard ? level : 1);
        if (c >= 0) {
            v14_putbit(&v->dte, 0);
            for (i = 0; i < 8; i++) v14_putbit(&v->dte, (c >> i) & 1);
            v14_putbit(&v->dte, 1);
        }
    }
    out = v21mod_tick(&v->mod, v->fbit, &done);
    if (done) v->fbit = v->carrier ? (v14_getbit(&v->dte) & 1) : 1;
    return out;
}

typedef struct {
    int kind;
    /* K_HST */
    long w, to_send, r, bad; uint32_t seed_out, seed_in;
    int after_fwd;               /* writes only once it has read all we send (a reversal's trigger) */
    long back_at, lr_start, lr_end, drop_at;
    /* K_ORIG */
    v90m_t *o; int16_t ox;
    /* K_AA */
    v32call_t vc;
    /* K_V22 */
    callpeer_t cp;
    /* K_V21, K_B103 */
    fsk_t f; int sent;
    /* raw bits, every kind but K_HST and the FSK callers */
    uint32_t rawtx, rawh; long rawn;
    int up;                      /* connected, by its own account */
} cal_t;
static cal_t C;
static v32line_t L;

static int cal_get(void *u) { cal_t *c = u; c->rawtx = c->rawtx * 22695477u + 1u; return (int)(c->rawtx >> 30) & 1; }
static void cal_put(void *u, int b) { cal_t *c = u; c->rawh = (c->rawh ^ (uint32_t)(b & 1)) * 16777619u; c->rawn++; }

static double peer(void *u, double rx)
{
    cal_t *c = u;
    int16_t x = sat16(rx), y = 0;
    switch (c->kind) {
    case K_HST: y = hstcall_tick(&HC, x); break;
    case K_ORIG: (void)v90m_process_linear(c->o, &x, &y, 1); break;
    case K_AA: return v32call_tick(&c->vc, rx);
    case K_V22: y = callpeer_tick(&c->cp, x); break;
    default: y = fsk_tick(&c->f, x); break;
    }
    return y;
}

static void line_init(int alaw, uint32_t seed)
{
    static const double loop4[] = { 0.3, 1.0, -0.5, 0.3 };
    v32line_cfg c;
    memset(&c, 0, sizeof c);
    c.mulaw = !alaw; c.alaw = alaw; c.loss_db = 6; c.delay = 200; c.taps = loop4; c.ntaps = 4;
    c.near_db = 15; c.c_near_db = 15; c.far_db = c.c_far_db = 300;
    c.noise_a_dbm0 = -60; c.noise_c_dbm0 = -60; c.seed = seed;
    v32line_init(&L, &c);
}

static void cal_init_hst(const hstcall_cfg *hc, long to_us, long to_hc, uint32_t seed)
{
    memset(&C, 0, sizeof C);
    C.kind = K_HST;
    C.to_send = to_us; C.seed_out = seed * 2u + 1u; C.seed_in = seed * 2u;
    O.to_send = to_hc; O.seed_out = C.seed_in; O.seed_in = C.seed_out;
    C.back_at = C.lr_start = C.lr_end = C.drop_at = -1;
    hstcall_init(&HC, hc);
}

/* the caller's side of a ms: the data, and when its LR left */
static void cal_ms(cal_t *c)
{
    if (c->kind == K_HST) {
        uint8_t buf[64];
        size_t n, i;
        if (c->back_at < 0 && HC.phase == HC_BACK) c->back_at = now_ms;
        if (HC.linked && (!c->after_fwd || c->r >= O.to_send)) {
            while (c->w < c->to_send) {
                uint8_t b = prbs(c->seed_out, c->w);
                if (hstcall_write(&HC, &b, 1) != 1) break;
                c->w++;
            }
        }
        while ((n = hstcall_read(&HC, buf, sizeof buf)) > 0)
            for (i = 0; i < n; i++, c->r++) if (buf[i] != prbs(c->seed_in, c->r)) c->bad++;
        if (c->drop_at < 0 && HC.phase == HC_DROPPED) c->drop_at = now_ms;
    }
}

/* ===================== the line, in blocks ===================== */
#define TXQ 8192
static int16_t txq[TXQ];
static long nsamp;               /* samples so far */
static long proc_err;            /* v90m_process_* calls that did not return V90M_OK */
static uint32_t txhash;
static void (*rx_tap)(int16_t x);      /* sees each sample we receive (the snr case) */
/* white noise added at our input (the fallback case): RMS xn_rms, 0 none;
 * and our input's power, summed while xn_meas */
static double xn_rms, xn_pw; static long xn_n; static int xn_meas;
static uint32_t xn_r = 1;
static double xn_uniform(void) { xn_r = xn_r * 1664525u + 1013904223u; return ((xn_r >> 8) + 0.5) / 16777216.0; }
static double xn_gauss(void) { return sqrt(-2 * log(xn_uniform())) * cos(2 * PI * xn_uniform()); }

/* one block of b samples, the line taking our samples d late (d >= b) */
static void block(int b, int d)
{
    static int16_t rx[4000], tx[4000];
    static uint8_t rx8[4000], tx8[4000];
    int i;
    for (i = 0; i < b; i++) {
        long s = nsamp + i;
        int16_t atx = s >= d ? txq[(s - d) & (TXQ - 1)] : 0;
        rx[i] = v32line_tick(&L, atx, peer, &C);
        if (xn_meas) { xn_pw += (double)rx[i] * rx[i]; xn_n++; }
        if (xn_rms > 0) rx[i] = sat16(rx[i] + xn_rms * xn_gauss());
        if (rx_tap) rx_tap(rx[i]);
        if (C.kind == K_HST && C.lr_start < 0 && HC.mnp.est_tries > 0) C.lr_start = s;
        if (C.kind == K_HST && C.lr_start >= 0 && C.lr_end < 0 && HC.mnp.line_framing == 0) C.lr_end = s;
        if ((s + 1) % 8 == 0) { now_ms = (s + 1) / 8; cal_ms(&C); }
    }
    if (O.alaw) {
        for (i = 0; i < b; i++) rx8[i] = pcm_encode(1, rx[i]);
        if (v90m_process_g711(O.m, rx8, tx8, (size_t)b) != V90M_OK) proc_err++;
        for (i = 0; i < b; i++) tx[i] = (int16_t)v90_octet_linear(1, tx8[i]);
    } else {
        if (v90m_process_linear(O.m, rx, tx, (size_t)b) != V90M_OK) proc_err++;
    }
    for (i = 0; i < b; i++) { txq[(nsamp + i) & (TXQ - 1)] = tx[i]; txhash = fnv(txhash, &tx[i], sizeof tx[i]); }
    nsamp += b;
    now_ms = nsamp / 8;
}

static void rc_watch(void);
static void run_start(void)
{
    xn_rms = 0; xn_meas = 0; xn_pw = 0; xn_n = 0; xn_r = 1;
    memset(txq, 0, sizeof txq);
    nsamp = 0; now_ms = 0; txhash = 2166136261u;
}

/* 1 ms blocks, until done() or ms_max */
static void run_ms(long ms_max, int (*done)(void))
{
    while (now_ms < ms_max) {
        block(8, 8);
        our_ms(&O);
        if (C.kind == K_HST) { rv_watch(); rc_watch(); }
        if (done && done()) break;
    }
}

static int data_done(void)
{
    /* each end has read all the other sends */
    return (C.r >= O.to_send && O.r >= C.to_send) || O.end_ms >= 0;
}
static int ended(void) { return O.end_ms >= 0; }

static int index_of(int ev, int from) { int i; for (i = from; i < O.nev; i++) if (O.ev[i] == ev) return i; return -1; }

/* ===================== rates ===================== */
static void hst_call(const hstcall_cfg *hc, const our_cfg *oc, long to_us, long to_hc, uint32_t seed, long ms_max)
{
    our_init(&O, oc);
    line_init(oc->alaw, seed);
    cal_init_hst(hc, to_us, to_hc, seed);
    rv_init();
    run_start();
    run_ms(ms_max, data_done);
}

static our_cfg hst_ours(void)
{
    our_cfg c;
    memset(&c, 0, sizeof c);
    c.mods = V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34 | V90M_MOD_V90 | V90M_MOD_HST;
    c.link = 1;
    return c;
}

static int rates_mask(int code) { return ((1 << (code + 1)) - 1) & 0x7C; }

/* the checks of a call that reached data at code, back speed back */
static void data_checks(int code, int back, int retrains)
{
    v90m_state st; v90m_stats ss; v90m_ec_status es;
    int i0, i1, i2;
    v90m_get_state(O.m, &st);
    v90m_get_stats(O.m, &ss);
    v90m_ec_get_status(O.ec, &es);
    printf("  connected: HST %d, tx %d rx %d; now tx %d rx %d; retrains %u, reversals %u; MNP hst %d rates 0x%02X back300 %d\n",
           O.conn_mod == V90M_CONN_HST, O.conn_tx, O.conn_rx, st.tx_bps, st.rx_bps, ss.retrains, ss.reversals,
           es.hst, es.hst_rates, es.hst_back300);
    printf("  data: %ld of %ld octets to the caller, %ld of %ld to us, %ld wrong\n", C.r, O.to_send, O.r, C.to_send, C.bad + O.bad);
    if (verbose) printf("  (at %ld ms)\n", now_ms);
    if (verbose) {
        int i;
        printf("  (connected at %ld ms, ANS_HST at %ld, trained at %ld; ended %ld; events:", O.conn_ms, O.hst_ms, O.trained_ms, O.end_ms);
        for (i = 0; i < O.nev; i++) printf(" %d@%ld %d/%d", O.ev[i], O.ev_ms[i], O.ev_tx[i], O.ev_rx[i]);
        printf(")\n");
    }
    bound("CONNECTED as HST at the opening rate", O.conn_mod == V90M_CONN_HST && O.conn_tx == 2400 * HST_OPEN_CODE);
    bound("in data at the best agreed rate and the back speed", st.connected && st.moving && st.tx_bps == 2400 * code && st.rx_bps == back);
    i0 = index_of(V90M_EV_CONNECTED, 0);
    i1 = index_of(V90M_EV_DATA_STOPPED, i0 < 0 ? 0 : i0);
    i2 = index_of(V90M_EV_DATA_MOVING, i1 < 0 ? 0 : i1);
    if (retrains) {
        bound("the post-agreement retrain: DATA_STOPPED, then DATA_MOVING", i0 >= 0 && i1 > i0 && i2 > i1);
        if (code != HST_OPEN_CODE) {
            int i, ok = 0;
            for (i = i1; i >= 0 && i < O.nev; i++) if (O.ev[i] == V90M_EV_RATES && O.ev_tx[i] == 2400 * code) ok = 1;
            bound("RATES with the best agreed rate", ok);
        }
    } else {
        bound("no retrain: data never stopped", i0 >= 0 && i1 < 0);
    }
    CHECK_EQ_I(ss.retrains, retrains);
    CHECK_EQ_I(ss.reversals, 0);
    CHECK_EQ_I(st.end, V90M_END_NONE);
    bound("the data both ways, intact", C.r == O.to_send && O.r == C.to_send && C.bad == 0 && O.bad == 0);
}

static void rates_one(int code, int back, int start, uint32_t seed)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    hc_defaults(&hc);
    hc.rates = (uint8_t)rates_mask(code); hc.back_bps = back; hc.start_bps = start;
    printf("rates: code %d, back %d, start %d\n", code, back, start ? start : 450);
    hst_call(&hc, &oc, DATA_BACK, DATA_FWD, seed, 1200000L);
    CHECK_EQ_I(O.conn_rx, start ? start : 450);
    data_checks(code, back, 1);
}

static void rates(void)
{
    int code;
    for (code = 2; code <= 6; code++) {
        rates_one(code, 450, 0, 100u + (uint32_t)code);
        rates_one(code, 300, 0, 200u + (uint32_t)code);
    }
    rates_one(6, 300, 300, 300u);
}

/* ===================== hst96, plain_mnp, max_down, alaw ===================== */
static void hst96(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    hc_defaults(&hc);
    hc.hst96 = 1; hc.back_bps = 300;
    printf("hst96: the 1986 HST, back 300\n");
    hst_call(&hc, &oc, DATA_BACK, DATA_FWD, 401u, 1200000L);
    data_checks(4, 300, 1);
    {
        /* v90m_retrain, holding the forward channel: the short training at
         * the rate in force, data stopped through it; then more data */
        v90m_stats ss;
        int n0 = O.nev, i1, i2;
        CHECK_EQ_I(v90m_retrain(O.m), V90M_OK);
        CHECK_EQ_I(v90m_retrain(O.m), V90M_ESTATE);         /* one at a time */
        O.to_send += 1024; C.to_send += 256;
        run_ms(now_ms + 600000L, data_done);
        v90m_get_stats(O.m, &ss);
        i1 = index_of(V90M_EV_DATA_STOPPED, n0);
        i2 = index_of(V90M_EV_DATA_MOVING, i1 < 0 ? n0 : i1);
        printf("  v90m_retrain: retrains %u; data %ld of %ld to the caller, %ld of %ld to us, %ld wrong\n",
               ss.retrains, C.r, O.to_send, O.r, C.to_send, C.bad + O.bad);
        bound("v90m_retrain: DATA_STOPPED, DATA_MOVING, still 9600 / 300", i1 >= n0 && i2 > i1 && O.ev_tx[i2] == 9600 && O.ev_rx[i2] == 300);
        CHECK_EQ_I(ss.retrains, 2);
        bound("then the data both ways, intact", C.r == O.to_send && O.r == C.to_send && C.bad == 0 && O.bad == 0);
    }
}

static void plain_mnp(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_ec_status es;
    hc_defaults(&hc);
    hc.rates = 0; hc.flags = 0;
    printf("plain_mnp: the caller's LR without 0xC0\n");
    hst_call(&hc, &oc, DATA_BACK, DATA_FWD, 402u, 1200000L);
    v90m_ec_get_status(O.ec, &es);
    CHECK_EQ_I(es.protocol, V90M_EC_PROTO_MNP);
    CHECK_EQ_I(es.hst, 0);
    data_checks(HST_OPEN_CODE, 450, 0);
}

static void max_down(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_ec_status es;
    hc_defaults(&hc);
    oc.max_down = 12000;
    printf("max_down: max_down_bps 12000\n");
    hst_call(&hc, &oc, DATA_BACK, DATA_FWD, 403u, 1200000L);
    v90m_ec_get_status(O.ec, &es);
    CHECK_EQ_I(ans()->u.hst.link.offer_rates, 0x3C);
    CHECK_EQ_I(es.hst_rates, 0x3C);
    CHECK_EQ_I(HC.mnp.hst_rates, 0x3C);
    data_checks(5, 450, 1);
}

static void alaw(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    hc_defaults(&hc);
    oc.alaw = 1;
    printf("alaw: an A-law line, codes 6 / 450\n");
    hst_call(&hc, &oc, DATA_BACK / 4, DATA_FWD, 404u, 1200000L);
    data_checks(6, 450, 1);
}

/* ===================== unlinked ===================== */
static void unlinked(void)
{
    our_cfg oc = hst_ours();
    hstcall_cfg hc;
    uint32_t h[2];
    long v22[2];
    int k, waited = 0;
    printf("unlinked: HST offered, no endpoint linked\n");
    hc_defaults(&hc);
    for (k = 0; k < 2; k++) {
        oc.link = 0;
        if (k == 1) oc.mods &= ~V90M_MOD_HST;
        our_init(&O, &oc);
        line_init(0, 405u);
        cal_init_hst(&hc, 0, 0, 405u);
        run_start();
        while (now_ms < 8000) {
            block(8, 8);
            our_ms(&O);
            if (ans()->phase == ANS_HST_WAIT || ans()->phase == ANS_HST) waited = 1;
        }
        h[k] = txhash; v22[k] = O.v22_ms;
    }
    printf("  our transmit over 8 s: %s; ANS_V22 entered %s\n", h[0] == h[1] ? "the same" : "different",
           v22[0] == v22[1] && v22[0] >= 0 ? "at the same sample" : "differently");
    bound("no ANS_HST_WAIT without the link", !waited);
    bound("the caller hears exactly what it hears with HST not offered", h[0] == h[1] && v22[0] == v22[1] && v22[0] >= 0);
}

/* ===================== lr_in_training ===================== */
static void lr_in_training(void)
{
    static const int delay[2] = { 600, 1000 };
    int k;
    for (k = 0; k < 2; k++) {
        hstcall_cfg hc;
        our_cfg oc = hst_ours();
        long lr0, lr1, hst0, tr1;
        hc_defaults(&hc);
        hc.lr_delay_ms = delay[k];
        printf("lr_in_training: lr_delay_ms %d\n", delay[k]);
        hst_call(&hc, &oc, DATA_BACK / 4, DATA_FWD / 4, 410u + (uint32_t)k, 600000L);
        lr0 = C.lr_start / 8; lr1 = C.lr_end / 8; hst0 = O.hst_ms; tr1 = O.trained_ms;
        if (verbose) printf("  (back channel at %ld ms, ANS_HST %ld, the LR %ld..%ld, our training out %ld)\n", C.back_at, hst0, lr0, lr1, tr1);
        bound("the LR left while our long training went out", hst0 >= 0 && tr1 >= 0 && lr0 >= hst0 && lr1 >= 0 && lr1 <= tr1);
        printf("  the LR sent %d time(s)\n", HC.mnp.est_tries);
        bound("no LR lost: sent once, MNP up", HC.mnp.est_tries == 1 && HC.linked);
        data_checks(6, 450, 1);
    }
}

/* ===================== no_lr ===================== */
static void no_lr(void)
{
    int k;
    for (k = 0; k < 2; k++) {
        hstcall_cfg hc;
        our_cfg oc = hst_ours();
        v90m_stats ss;
        hc_defaults(&hc);
        hc.no_lr = 1;
        our_init(&O, &oc);
        O.clear_on_noec = k == 0;
        line_init(0, 420u);
        cal_init_hst(&hc, 0, 0, 420u);
        run_start();
        run_ms(60000L, ended);
        v90m_get_stats(O.m, &ss);
        printf("no_lr: %s; the endpoint NO_EC %s; end %d\n", k == 0 ? "the host clears down" : "the host does not",
               O.noec_ms >= 0 ? "yes" : "no", O.end);
        if (verbose) printf("  (ANS_HST %ld ms, trained %ld, NO_EC %ld, ended %ld)\n", O.hst_ms, O.trained_ms, O.noec_ms, O.end_ms);
        bound("NO_EC at our training's end + 5 s (+-10 ms)", O.trained_ms >= 0 && O.noec_ms >= 0 && labs(O.noec_ms - O.trained_ms - 5000) <= 10);
        CHECK_EQ_I(ss.reversals, 0);
        if (k == 0) {
            CHECK_EQ_I(O.end, V90M_END_CLEARDOWN_OURS);
            bound("the host's cleardown ends the call at once", O.end_ms >= O.noec_ms && O.end_ms - O.noec_ms <= 1);
        } else {
            CHECK_EQ_I(O.end, V90M_END_HST_FAILED);
            bound("V90M_END_HST_FAILED 15 s after ANS_HST", O.hst_ms >= 0 && labs(O.end_ms - O.hst_ms - 15000) <= 1);
        }
    }
}

/* ===================== lost_fwd ===================== */
static void lost_fwd(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    long d;
    hc_defaults(&hc);
    hc.drop_at_ms = 12000;
    printf("lost_fwd: the caller's carriers stop at 12 s\n");
    hst_call(&hc, &oc, DATA_BACK / 16, DATA_FWD / 16, 430u, 600000L);
    run_ms(60000L, ended);
    d = O.end_ms - C.drop_at;
    if (verbose) printf("  (connected %ld ms, dropped %ld, ended %ld)\n", O.conn_ms, C.drop_at, O.end_ms);
    CHECK_EQ_I(O.end, V90M_END_LOST);
    bound("V90M_END_LOST 5 s after the back carrier went (+ its release, < 300 ms)", O.conn_ms >= 0 && C.drop_at > O.conn_ms && d >= 5000 && d < 5300);
}

/* ===================== legacy_unchanged ===================== */
typedef struct { int mod, tx, rx; long conn; uint32_t txh, oh, ch; long on, cn; int end; } leg_t;

static void leg_cal_init(int kind)
{
    memset(&C, 0, sizeof C);
    C.kind = kind;
    C.back_at = C.lr_start = C.lr_end = C.drop_at = -1;
    if (kind == K_ORIG) {
        v90m_cfg c; int err;
        v90m_cfg_default(&c);
        c.role = V90M_ROLE_ORIGINATE;
        c.modulations = V90M_MOD_V90 | V90M_MOD_V34 | V90M_MOD_V22;
        c.getbit = cal_get; c.putbit = cal_put; c.user = &C;
        C.o = v90m_create(&c, &err);
        CHECK(C.o != NULL);
    } else if (kind == K_AA) {
        vc_cfg cc;
        memset(&cc, 0, sizeof cc);
        cc.level = 3000; cc.rates = 0x1Fu; cc.trn_len = 1280;
        cc.getbit = cal_get; cc.putbit = cal_put; cc.user = &C;
        v32call_init(&C.vc, &cc);
    } else if (kind == K_V22) {
        callpeer_init(&C.cp, 0, V8_MOD_V22, 1, 3000);
        callpeer_set_pump(&C.cp, cal_get, cal_put, &C);
    } else {
        fsk_init(&C.f, kind == K_B103);
    }
}

static int cal_connected(void)
{
    switch (C.kind) {
    case K_ORIG: { v90m_state st; v90m_get_state(C.o, &st); return st.moving; }
    case K_AA: return C.vc.connected && C.vc.state == VCS_DATA;
    case K_V22: return C.cp.connected;
    default: return C.f.carrier;
    }
}

static void legacy_run(int kind, int v34, int hst, leg_t *r)
{
    our_cfg oc;
    long both = -1;
    memset(&oc, 0, sizeof oc);
    oc.mods = V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34 | V90M_MOD_V90 | V90M_MOD_BELL103;
    if (hst) oc.mods |= V90M_MOD_HST;
    oc.link = hst; oc.raw = 1;
    our_init(&O, &oc);
    line_init(0, 440u + (uint32_t)kind);
    leg_cal_init(kind);
    if (kind == K_ORIG && v34) {
        /* the V.34 caller: no V.90 */
        v90m_cfg c; int err;
        v90m_destroy(C.o);
        v90m_cfg_default(&c);
        c.role = V90M_ROLE_ORIGINATE; c.modulations = V90M_MOD_V34 | V90M_MOD_V22;
        c.getbit = cal_get; c.putbit = cal_put; c.user = &C;
        C.o = v90m_create(&c, &err);
    }
    run_start();
    while (now_ms < 40000) {
        block(8, 8);
        our_ms(&O);
        if (both < 0 && O.conn_ms >= 0 && cal_connected()) {
            both = now_ms;
            if (C.kind == K_V21 || C.kind == K_B103) { static const char msg[] = "the caller says hello"; v14_write(&C.f.dte, (const uint8_t *)msg, (int)strlen(msg)); }
            O.rawn = 0; O.rawh = 2166136261u; C.rawn = 0; C.rawh = 2166136261u;
        }
        if (both >= 0 && now_ms >= both + 3000) break;
        if (O.end_ms >= 0) break;
    }
    if (C.kind == K_V21 || C.kind == K_B103) {
        uint8_t buf[256]; int n = v14_read(&C.f.dte, buf, (int)sizeof buf);
        C.rawh = fnv(2166136261u, buf, n > 0 ? (size_t)n : 0); C.rawn = n;
    }
    r->mod = O.conn_mod; r->tx = O.conn_tx; r->rx = O.conn_rx; r->conn = O.conn_ms; r->txh = txhash;
    r->oh = O.rawh; r->on = O.rawn; r->ch = C.rawh; r->cn = C.rawn; r->end = O.end;
    if (C.kind == K_ORIG) { v90m_destroy(C.o); C.o = NULL; }
}

static void legacy_unchanged(void)
{
    static const struct { int kind, v34; const char *name; int same_samples; } cl[] = {
        { K_ORIG, 0, "V.90 (V.8)", 1 }, { K_ORIG, 1, "V.34 (V.8)", 1 }, { K_AA, 0, "V.32bis (AA)", 1 },
        { K_V22, 0, "V.22bis (no V.8)", 0 }, { K_V21, 0, "V.21 (no V.8)", 0 }, { K_B103, 0, "Bell 103", 0 }
    };
    unsigned i;
    for (i = 0; i < sizeof cl / sizeof cl[0]; i++) {
        leg_t off, on;
        char what[96];
        legacy_run(cl[i].kind, cl[i].v34, 0, &off);
        legacy_run(cl[i].kind, cl[i].v34, 1, &on);
        printf("legacy_unchanged: %s: modulation %d, %d / %d bit/s; with HST offered modulation %d, %d / %d bit/s; data %s; connected %s\n",
               cl[i].name, off.mod, off.tx, off.rx, on.mod, on.tx, on.rx,
               off.oh == on.oh && off.ch == on.ch && off.on == on.on && off.cn == on.cn ? "the same" : "different",
               on.conn == off.conn ? "at the same ms" : on.conn > off.conn && on.conn - off.conn <= HST_W_MS ? "later, within W" : "otherwise");
        if (verbose) printf("  (connected at %ld / %ld ms, %ld bits in, %ld bits out)\n", off.conn, on.conn, off.on, off.cn);
        snprintf(what, sizeof what, "%s: the same modulation, rates and data", cl[i].name);
        bound(what, off.conn >= 0 && off.mod == on.mod && off.tx == on.tx && off.rx == on.rx && off.on > 0 && off.cn > 0
                    && off.oh == on.oh && off.ch == on.ch && off.on == on.on && off.cn == on.cn && off.end == on.end);
        if (cl[i].same_samples) {
            snprintf(what, sizeof what, "%s: sample for sample the same", cl[i].name);
            bound(what, off.txh == on.txh && off.conn == on.conn);
        } else {
            snprintf(what, sizeof what, "%s: connected at most HST_W_MS later", cl[i].name);
            bound(what, on.conn >= off.conn && on.conn - off.conn <= HST_W_MS);
        }
    }
}

/* ===================== block_size ===================== */
typedef struct { uint32_t txh; int nev, ev[NEV], tx[NEV], rx[NEV]; v90m_ec_status es; v90m_state st; long got_us, got_hc, bad; } bs_t;
static void bs_run(int b, bs_t *r)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    long total = 30L * FS;
    hc_defaults(&hc);
    oc.bittime = 1;
    our_init(&O, &oc);
    line_init(0, 450u);
    cal_init_hst(&hc, 1024, 8192, 450u);
    run_start();
    while (nsamp < total) {
        int n = total - nsamp < b ? (int)(total - nsamp) : b;
        block(n, 4000);
        our_ms(&O);
    }
    memset(r, 0, sizeof *r);
    r->txh = txhash; r->nev = O.nev;
    memcpy(r->ev, O.ev, sizeof r->ev); memcpy(r->tx, O.ev_tx, sizeof r->tx); memcpy(r->rx, O.ev_rx, sizeof r->rx);
    memset(&r->es, 0, sizeof r->es);
    v90m_ec_get_status(O.ec, &r->es);
    v90m_get_state(O.m, &r->st);
    r->got_us = O.r; r->got_hc = C.r; r->bad = O.bad + C.bad;
}
static int status_same(const v90m_ec_status *a, const v90m_ec_status *b)
{
    return a->state == b->state && a->reason == b->reason && a->protocol == b->protocol && a->hst == b->hst
        && a->hst_rates == b->hst_rates && a->hst_back300 == b->hst_back300 && a->tx_queued == b->tx_queued
        && a->rx_avail == b->rx_avail && a->unacked == b->unacked && a->rx_frames == b->rx_frames
        && a->rx_frames_bad == b->rx_frames_bad && a->tx_i == b->tx_i && a->tx_i_retx == b->tx_i_retx
        && a->rx_i == b->rx_i && a->rx_i_dropped == b->rx_i_dropped && a->t401_expiries == b->t401_expiries
        && a->violations == b->violations && a->attn_tx == b->attn_tx && a->attn_rx == b->attn_rx;
}
static void block_size(void)
{
    static bs_t r[3];
    static const int bs[3] = { 1, 160, 4000 };
    int k, same = 1;
    for (k = 0; k < 3; k++) {
        bs_run(bs[k], &r[k]);
        printf("block_size %d: %d events, in data %d at %d / %d; %ld octets to the caller, %ld to us, %ld wrong\n",
               bs[k], r[k].nev, r[k].st.moving, r[k].st.tx_bps, r[k].st.rx_bps, r[k].got_hc, r[k].got_us, r[k].bad);
        if (k > 0)
            same &= r[k].txh == r[0].txh && r[k].nev == r[0].nev && !memcmp(r[k].ev, r[0].ev, sizeof r[0].ev)
                    && !memcmp(r[k].tx, r[0].tx, sizeof r[0].tx) && !memcmp(r[k].rx, r[0].rx, sizeof r[0].rx)
                    && status_same(&r[k].es, &r[0].es) && r[k].got_us == r[0].got_us && r[k].got_hc == r[0].got_hc;
    }
    bound("in data at 14400 / 450, data intact", r[0].st.moving && r[0].st.tx_bps == 14400 && r[0].st.rx_bps == 450 && r[0].bad == 0 && r[0].got_hc > 0 && r[0].got_us > 0);
    bound("blocks of 1, 160 and 4000: the same samples, events and endpoint status", same);
}

/* ===================== reversals (Task 21) ===================== */
static int index_at(int ev, int from, long ms_lo, long ms_hi)
{
    int i;
    for (i = from; i < O.nev; i++) if (O.ev[i] == ev && O.ev_ms[i] >= ms_lo && O.ev_ms[i] <= ms_hi) return i;
    return -1;
}
static int count_ev(int ev, int from) { int i, n = 0; for (i = from; i < O.nev; i++) n += O.ev[i] == ev; return n; }

/* the switch k, to the back channel (we granted the caller's 0x1R): 20 ms
 * after the LNA drained (+1 for the ms grid, and the frame that may have
 * begun in them) our forward carrier stops and our back transmitter
 * starts at the agreed speed; RATES there with tx and rx exchanged */
static void rev_to_back(int k, int code, int back)
{
    int ie;
    long d = RV.sw[k] - RV.pre[k];
    if (verbose) printf("  (switch %d at %ld ms, %ld ms after the LNA drained, %ld ms after the host's last bit)\n", k, RV.sw[k], d, RV.ones_ms[k]);
    bound(HST_LONG_V17 ? "to the back channel 20 ms and V.17's turn-off after our LNA drained"
                       : "to the back channel 20 ms after our LNA drained",
          RV.to[k] == HST_ROLE_BACK && RV.pre[k] >= 0 && d >= EXP_SW_LO && d <= EXP_SW_HI);
    bound("ones, no host bit, from the cut to the switch", RV.ones_ms[k] >= EXP_ONES_LO && RV.ones_ms[k] <= EXP_ONES_HI);
    bound("there our forward carrier stops, our back transmitter starts at the agreed speed",
          RV.ftx_off[k] && RV.btx_on[k] && RV.btx_bps[k] == back);
    ie = index_at(V90M_EV_RATES, 0, RV.sw[k], RV.sw[k]);
    bound("RATES with tx and rx exchanged", ie >= 0 && O.ev_tx[ie] == back && O.ev_rx[ie] == 2400 * code);
}
/* the switch k, to the forward channel (the caller granted our 0x1R): our
 * back transmitter stopped at the LNA, our short training 200 ms on */
static void rev_to_fwd(int k, int code, int back)
{
    int ie;
    if (verbose) printf("  (switch %d at %ld ms, our training at %ld, S3 %d)\n", k, RV.sw[k], RV.tr[k], RV.tr_len[k]);
    bound("to the forward channel at the caller's LNA: our back transmitter stopped",
          RV.to[k] == HST_ROLE_FWD && !RV.took_back[k] && RV.pre[k] == RV.sw[k] && !RV.btx_on[k]);
    bound("200 ms on, our forward training, the short one",
          RV.tr[k] >= 0 && RV.tr[k] - RV.sw[k] >= 200 && RV.tr[k] - RV.sw[k] <= 201 && RV.tr_len[k] == EXP_S3_SHORT);
    ie = index_at(V90M_EV_RATES, 0, RV.sw[k], RV.sw[k]);
    bound("RATES with tx and rx exchanged", ie >= 0 && O.ev_tx[ie] == 2400 * code && O.ev_rx[ie] == back);
}

/* what every reversal case checks at its end: no data stopped after the
 * post-agreement retrain, the counters, the data */
static void rev_end_checks(unsigned nrev, int moved_at)
{
    v90m_stats ss; v90m_state st;
    v90m_get_stats(O.m, &ss);
    v90m_get_state(O.m, &st);
    printf("  reversals %u (the caller's %d), retrains %u, renegotiations %u; data %ld of %ld to the caller, %ld of %ld to us, %ld wrong\n",
           ss.reversals, HC.reversals, ss.retrains, ss.renegotiations, C.r, O.to_send, O.r, C.to_send, C.bad + O.bad);
    bound("data never stopped after the post-agreement retrain", moved_at >= 0 && count_ev(V90M_EV_DATA_STOPPED, moved_at) == 0 && st.moving);
    CHECK_EQ_I(ss.reversals, nrev);
    CHECK_EQ_I((unsigned)HC.reversals, nrev);
    CHECK_EQ_I(ss.retrains, 1);
    CHECK_EQ_I(ss.renegotiations, 0);
    CHECK_EQ_I(st.end, V90M_END_NONE);
    bound("the data both ways, intact", C.r == O.to_send && O.r == C.to_send && C.bad == 0 && O.bad == 0);
}

static int moved_index(void)
{
    int i0 = index_of(V90M_EV_DATA_STOPPED, 0);
    return i0 < 0 ? -1 : index_of(V90M_EV_DATA_MOVING, i0);
}

/* the caller's reversal: it asks once our data is in (its backlog over
 * 64, we have held the forward channel 2 s); then, with `ours`, our
 * backlog for one back; `back` the agreed back speed */
static void rev_run(int ours, int back, int la, uint32_t seed)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    long t0;
    hc_defaults(&hc);
    hc.back_bps = back; hc.rev_backlog = 64; hc.la_after_rev = la;
    our_init(&O, &oc);
    line_init(0, seed);
    cal_init_hst(&hc, 2048, 2048, seed);
    C.after_fwd = 1;
    rv_init();
    run_start();
    run_ms(300000L, data_done);
    if (ours) {
        /* holding the back channel: our backlog over 64 for 2 s */
        t0 = now_ms;
        O.to_send += 2048;
        run_ms(t0 + 300000L, data_done);
    }
}

static void rev_caller(void)
{
    printf("rev_caller: the caller's backlog: its 0x1R, our LNA, its training\n");
    rev_run(0, 450, 0, 501u);
    printf("  switches %d; the caller's training S3 %d at code %d\n", RV.n, RV.nhc ? RV.hc_len[0] : 0, RV.nhc ? RV.hc_code[0] : 0);
    bound("one reversal: to the back channel", RV.n == 1);
    if (RV.n >= 1) rev_to_back(0, 6, 450);
    bound("the caller trains forward at the rate in force, the long training", RV.nhc == 1 && RV.hc_code[0] == 6 && RV.hc_len[0] == EXP_S3_LONG);
    rev_end_checks(1, moved_index());
}

static void rev_ours_one(int back, int la, uint32_t seed)
{
    v90m_ec_status es;
    rev_run(1, back, la, seed);
    v90m_ec_get_status(O.ec, &es);
    printf("  switches %d; our 0x1R 0x%02X; the caller's receiver from its kept equaliser on %d of %d of our trainings\n",
           RV.n, es.attn_tx, RV.warm_ok, RV.warm_n);
    bound("two reversals: to the back channel and back", RV.n == 2);
    if (RV.n >= 2) { rev_to_back(0, 6, back); rev_to_fwd(1, 6, back); }
    bound("our 0x1R at the forward rate in force", es.attn_tx == 0x16);
    bound("the caller's receiver started from its kept equaliser", RV.warm_n == 1 && RV.warm_ok == 1);
    rev_end_checks(2, moved_index());
}

static void rev_ours(void)
{
    printf("rev_ours: then our backlog: our 0x1R, its LNA, our training\n");
    rev_ours_one(450, 0, 502u);
}

static void rev_back300(void)
{
    printf("rev_back300: the same, the back channel at 300\n");
    rev_ours_one(300, 0, 503u);
}

/* rev_ours against a caller that acknowledges anew after each switch, as
 * our endpoint does (hstcall la_after_rev): the same two reversals */
static void rev_ours_la(void)
{
    printf("rev_ours_la: rev_ours, the caller sending an LA first after each switch\n");
    rev_ours_one(450, 1, 507u);
}

/* both ends always behind: reversals back and forth, 20 within 120 s;
 * then no more data, the backlogs drained */
#ifdef TEST_SHORT
#define MANY 6
#else
#define MANY 20
#endif
static int many_done(void) { v90m_stats ss; v90m_get_stats(O.m, &ss); return ss.reversals >= MANY || O.end_ms >= 0; }
static void rev_many(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_stats ss;
    unsigned at;
    int k, hc_at, shorts = 0;
    long t_at;
    hc_defaults(&hc);
    hc.rev_backlog = 64;
    printf("rev_many: both ends behind, %d reversals\n", MANY);
    our_init(&O, &oc);
    line_init(0, 504u);
    cal_init_hst(&hc, 100000000L, 100000000L, 504u);
    rv_init();
    run_start();
    run_ms(120000L, many_done);
    v90m_get_stats(O.m, &ss);
    at = ss.reversals; hc_at = HC.reversals; t_at = now_ms;
    /* no more data: what is written is delivered */
    C.to_send = C.w; O.to_send = O.w;
    run_ms(t_at + 300000L, data_done);
    for (k = 1; k < RV.nhc; k++) shorts += RV.hc_len[k] == EXP_S3_SHORT;
    printf("  %u reversals (the caller's %d)%s; the caller's trainings %d, the long one first %d, short after %d\n",
           at, hc_at, at >= MANY ? " within 120 s" : "", RV.nhc, RV.nhc > 0 && RV.hc_len[0] == EXP_S3_LONG, shorts == RV.nhc - 1);
    if (verbose) printf("  (the %dth at %ld ms)\n", MANY, t_at);
    CHECK_EQ_I(at, MANY);
    CHECK_EQ_I(hc_at, MANY);
    bound("the caller's trainings: long the first time, short after", RV.nhc > 1 && RV.hc_len[0] == EXP_S3_LONG && shorts == RV.nhc - 1);
    v90m_get_stats(O.m, &ss);
    printf("  then: reversals %u (the caller's %d); data %ld octets to the caller, %ld to us, %ld wrong\n",
           ss.reversals, HC.reversals, C.r, O.r, C.bad + O.bad);
    bound("then the data delivered, intact; both ends count the same", ss.reversals == (unsigned)HC.reversals
          && C.r == O.to_send && O.r == C.to_send && C.bad == 0 && O.bad == 0 && O.to_send > 0 && C.to_send > 0);
    bound("data never stopped after the post-agreement retrain", count_ev(V90M_EV_DATA_STOPPED, moved_index()) == 0);
}

/* the caller takes no notice of our LNA (as if lost): it never trains;
 * within 3 s of the switch we hold the forward channel again (the
 * watchdogs; Task 23 adds the end) */
static void rev_lost(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    long d;
    hc_defaults(&hc);
    hc.rev_backlog = 64; hc.ignore_lna = 1;
    printf("rev_lost: the caller never trains after our LNA\n");
    our_init(&O, &oc);
    line_init(0, 505u);
    cal_init_hst(&hc, 2048, 2048, 505u);
    C.after_fwd = 1;
    rv_init();
    run_start();
    run_ms(300000L, data_done);
    d = RV.n >= 2 ? RV.sw[1] - RV.sw[0] : -1;
    printf("  switches %d: to the back channel, then the forward one taken back %s\n", RV.n,
           d >= 0 && d <= 3000 ? "within 3 s" : "not within 3 s");
    if (verbose) printf("  (switches at %ld and %ld ms)\n", RV.sw[0], RV.n >= 2 ? RV.sw[1] : -1L);
    bound("the forward channel taken back within 3 s of the switch", RV.n >= 2 && RV.to[0] == HST_ROLE_BACK
          && RV.to[1] == HST_ROLE_FWD && RV.took_back[1] && d >= 0 && d <= 3000);
    bound("then the data both ways, intact", C.r == O.to_send && O.r == C.to_send && C.bad == 0 && O.bad == 0 && O.end_ms < 0);
    {
        /* the taking back is a retrain, not a reversal (the caller never
         * switched): ours the switch to back and the caller's later one */
        v90m_stats ss;
        v90m_get_stats(O.m, &ss);
        printf("  reversals %u (the caller's %d), retrains %u\n", ss.reversals, HC.reversals, ss.retrains);
        CHECK_EQ_I(ss.reversals, 2);
        CHECK_EQ_I(HC.reversals, 1);
        CHECK_EQ_I(ss.retrains, 2);
    }
}

/* holding the back channel, the SNR is the forward receiver's: within 1 dB
 * of the line's (the caller's signal in 500-3100 Hz at our input, the
 * noise in the 2400 Hz of a symbol), the line flat (the loop's equaliser
 * noise costs some 0.6 dB more), noise -40 dBm0 at our end, 9600 bit/s
 * (the forward receiver in data throughout); snr_db10
 * read every 100 ms over 2 s, averaged (the receiver's figure is over 64
 * symbols or so) */
#define SNR_TAPS 255
static double snr_bp[SNR_TAPS], snr_xh[SNR_TAPS], snr_ps;
static long snr_n, snr_xp;
static double snr_rd; static long snr_nrd;     /* snr_db10 read every 100 ms: their sum, their number */
static void snr_tap(int16_t x)
{
    double v = 0;
    long i;
    if (snr_n % 800 == 799) { v90m_stats ss; v90m_get_stats(O.m, &ss); snr_rd += ss.snr_db10; snr_nrd++; }
    snr_xh[snr_xp] = x; snr_xp = (snr_xp + 1) % SNR_TAPS;
    for (i = 0; i < SNR_TAPS; i++) v += snr_bp[i] * snr_xh[(snr_xp + i) % SNR_TAPS];
    snr_ps += v * v; snr_n++;
}
static void snr(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v32line_cfg c;
    double ps, nrms = v32line_rms(-40), sline;
    long n, k;
    v90m_stats ss;
    int ok;
    printf("snr: holding the back channel, the forward receiver's SNR\n");
    hc_defaults(&hc);
    hc.rev_backlog = 64; hc.rates = (uint8_t)rates_mask(4);        /* 9600: well inside 21 dB */
    our_init(&O, &oc);
    memset(&c, 0, sizeof c);
    c.mulaw = 1; c.loss_db = 6; c.delay = 200;
    c.near_db = 15; c.c_near_db = 15; c.far_db = c.c_far_db = 300;
    c.noise_a_dbm0 = -40; c.noise_c_dbm0 = -60; c.seed = 506u;
    v32line_init(&L, &c);
    cal_init_hst(&hc, 1000000L, 2048, 506u);
    C.after_fwd = 1;
    rv_init();
    run_start();
    /* until we hold the back channel, the caller's training in; 1 s of its data */
    while (now_ms < 300000L && !(RV.n == 1 && ans()->u.hst.rev == HST_REV_NONE) && O.end_ms < 0) run_ms(now_ms + 1, NULL);
    run_ms(now_ms + 1000, ended);
    /* 500-3100 Hz, windowed sinc (Blackman) */
    for (k = 0; k < SNR_TAPS; k++) {
        double m = k - (SNR_TAPS - 1) / 2.0, h;
        h = m == 0 ? (3100.0 - 500.0) * 2 / FS : (sin(2 * PI * 3100 * m / FS) - sin(2 * PI * 500 * m / FS)) / (PI * m);
        snr_bp[k] = h * (0.42 - 0.5 * cos(2 * PI * k / (SNR_TAPS - 1)) + 0.08 * cos(4 * PI * k / (SNR_TAPS - 1)));
    }
    /* 2 s of our input, filtered */
    snr_ps = 0; snr_n = 0; snr_xp = 0; snr_rd = 0; snr_nrd = 0;
    memset(snr_xh, 0, sizeof snr_xh);
    rx_tap = snr_tap;
    run_ms(now_ms + 2000, ended);
    rx_tap = NULL;
    ps = snr_ps; n = snr_n;
    ps = n > 0 ? ps / n : 0;
    /* the noise in the band, and in a symbol's 2400 Hz */
    sline = 10 * log10((ps - nrms * nrms * 2600 / 4000) / (nrms * nrms * 2400 / 4000));
    v90m_get_stats(O.m, &ss);
    ok = RV.n == 1 && ans()->u.hst.link.role == HST_ROLE_BACK && hst_frx_state(&ans()->u.hst.frx) == HST_FRX_DATA && snr_nrd > 0 && fabs(snr_rd / snr_nrd / 10.0 - sline) <= 1.0;
    printf("  holding the back channel: %s\n", RV.n == 1 && ans()->u.hst.link.role == HST_ROLE_BACK ? "yes" : "no");
    if (verbose) printf("  (switches %d, the caller wrote %ld, at %ld ms, end %d)\n", RV.n, C.w, now_ms, O.end);
    if (verbose) printf("  (snr_db10 %d at the end, %.1f on average over %ld readings; the line's %.2f dB)\n", ss.snr_db10,
                        snr_nrd ? snr_rd / snr_nrd : 0.0, snr_nrd, sline);
    bound("v90m_stats.snr_db10 within 1 dB of the line's SNR", ok);
}

/* ===================== rate changes (Task 22b) ===================== */
/* what the rate cases watch, a ms at a time: our LNs (as our endpoint
 * puts them out), our forward trainings and switches by marks, the
 * pump's epochs, the forward receiver's code while we hold the back
 * channel, and MNP's T401 through every retrain (held: the endpoint
 * stopped, T401 where it stood) */
#define NRC 64
typedef struct {
    int nln; int ln[NRC]; long ln_ms[NRC]; unsigned long ln_bad[NRC];   /* our LNs; frame errors then */
    int ntr; long tr_ms[NRC]; int tr_len[NRC], tr_code[NRC];            /* our trainings */
    int nmk; long mk_ms[NRC]; int mk_from[NRC], mk_to[NRC];             /* our switches by marks */
    int nep; long ep_ms[NRC];                                           /* epochs */
    int nrx; long rx_ms[NRC]; int rx_code[NRC], rx_trained[NRC];        /* the forward receiver's code, holding back */
    int last_ln, last_seg, last_mk, last_rx, left_data, last_st, last_rc, last_hseg, last_hfrx; unsigned last_ep;
    int rt_on, rt_t401; unsigned long rt_exp; long rt_ticks, rt_bad, rt_running;              /* T401 through the retrains */
} rcw_t;
static rcw_t RC;
static void rc_init(void) { memset(&RC, 0, sizeof RC); RC.last_ln = -1; RC.last_rx = -1; }
static mnp_t *our_mnp(void)
{
    v42ec_t *e = v90m__ec(O.ec);
    return e->protocol == V42EC_PROTO_MNP ? &e->l.mnp : NULL;
}
static void rc_watch(void)
{
    answer_t *a = ans();
    hst_t *h;
    mnp_t *m = our_mnp();
    int seg, mk;
    if (a->phase != ANS_HST) return;
    h = &a->u.hst;
    if (m && m->ln_code != RC.last_ln) {
        if (m->ln_code >= 0 && RC.nln < NRC) {
            RC.ln[RC.nln] = m->ln_code; RC.ln_ms[RC.nln] = now_ms; RC.ln_bad[RC.nln] = m->rx_bad; RC.nln++;
            if (verbose) printf("  (%ld ms: our LN 0x%02X)\n", now_ms, m->ln_code);
        }
        RC.last_ln = m->ln_code;
    }
    seg = hst_ftx_seg(&h->ftx);
    if (seg == HST_SEG_S1 && RC.last_seg != HST_SEG_S1 && RC.ntr < NRC) {
        RC.tr_ms[RC.ntr] = now_ms; RC.tr_len[RC.ntr] = h->ftx.len[2]; RC.tr_code[RC.ntr] = h->ftx.code; RC.ntr++;
        if (verbose) printf("  (%ld ms: our training, code %d, S3 %d)\n", now_ms, (int)h->ftx.code, (int)h->ftx.len[2]);
    }
    RC.last_seg = seg;
    mk = hst_ftx_marking(&h->ftx) > 0;
    if (mk && !RC.last_mk && RC.nmk < NRC) {
        RC.mk_ms[RC.nmk] = now_ms; RC.mk_from[RC.nmk] = h->ftx.code; RC.mk_to[RC.nmk] = h->ftx.mcode; RC.nmk++;
        if (verbose) printf("  (%ld ms: our marks, %d to %d)\n", now_ms, (int)h->ftx.code, (int)h->ftx.mcode);
    }
    RC.last_mk = mk;
    if (h->link.epoch != RC.last_ep) {
        if (RC.nep < NRC) RC.ep_ms[RC.nep++] = now_ms;
        if (verbose) printf("  (%ld ms: epoch %u)\n", now_ms, h->link.epoch);
        RC.last_ep = h->link.epoch;
    }
    if (verbose && h->link.role == HST_ROLE_BACK && hst_frx_state(&h->frx) != RC.last_st)
        printf("  (%ld ms: the forward receiver's state %d, code %d, armed %d; rc %d)\n", now_ms, hst_frx_state(&h->frx),
               (int)h->frx.code, hst_frx_armed(&h->frx), (int)h->rc);
    RC.last_st = h->link.role == HST_ROLE_BACK ? hst_frx_state(&h->frx) : -1;
    if (verbose && (int)h->rc != RC.last_rc)
        printf("  (%ld ms: rc %d, order 0x%02X, rct %d, left %d, fails %d, retraining %d)\n", now_ms, (int)h->rc, (int)h->rc_order,
               (int)h->rct, (int)h->rc_left, (int)h->fl_n, a->retraining);
    RC.last_rc = h->rc;
    if (verbose && HC.ftx.seg != RC.last_hseg) printf("  (%ld ms: the caller's forward segment %d, code %d)\n", now_ms, HC.ftx.seg, HC.ftx.code);
    RC.last_hseg = HC.ftx.seg;
    if (verbose && HC.frx.state != RC.last_hfrx) printf("  (%ld ms: the caller's forward receiver %d, code %d, next %d)\n", now_ms, HC.frx.state, HC.frx.d.code, HC.frx.next_code);
    RC.last_hfrx = HC.frx.state;
    if (h->link.role == HST_ROLE_BACK) {
        if (hst_frx_state(&h->frx) != HST_FRX_DATA) RC.left_data = 1;
        else if ((int)h->frx.code != RC.last_rx) {
            if (RC.nrx < NRC) { RC.rx_ms[RC.nrx] = now_ms; RC.rx_code[RC.nrx] = h->frx.code; RC.rx_trained[RC.nrx] = RC.left_data; RC.nrx++; }
            if (verbose) printf("  (%ld ms: the forward receiver in data at code %d%s)\n", now_ms, (int)h->frx.code, RC.left_data ? ", trained" : "");
            RC.last_rx = h->frx.code; RC.left_data = 0;
        } else RC.left_data = 0;
    } else RC.last_rx = -1;
    /* T401 through a retrain: from the tick after DATA_STOPPED (the host's
     * line_stopped) to DATA_MOVING, never counted down, never expired */
    if (m && a->retraining && m->state == MNP_S_CONNECTED) {
        if (!RC.rt_on) { RC.rt_on = 1; RC.rt_t401 = m->t401; RC.rt_exp = m->t401_expiries; RC.rt_running += m->t401 > 0; }
        else {
            /* held: where it stood, or stopped by an LA (the back channel goes on) */
            int bad = !m->stopped || (m->t401 != RC.rt_t401 && m->t401 != 0) || m->t401_expiries != RC.rt_exp;
            RC.rt_ticks++; RC.rt_bad += bad;
            if (verbose && bad)
                printf("  (%ld ms: retraining, the endpoint stopped %d, T401 %d, %d at the start)\n", now_ms, m->stopped, m->t401, RC.rt_t401);
        }
    } else RC.rt_on = 0;
}
static int rc_ln_index(int code, int from) { int i; for (i = from; i < RC.nln; i++) if (RC.ln[i] == code) return i; return -1; }
/* the first RATES at or after ms with rx (rx_bps; 0 any) and tx (0 any) */
static int rates_at(long ms, int tx, int rx)
{
    int i;
    for (i = 0; i < O.nev; i++)
        if (O.ev[i] == V90M_EV_RATES && O.ev_ms[i] >= ms && (!tx || O.ev_tx[i] == tx) && (!rx || O.ev_rx[i] == rx)) return i;
    return -1;
}
static void rc_data_line(void)
{
    v90m_stats ss; v90m_ec_status es;
    v90m_get_stats(O.m, &ss);
    v90m_ec_get_status(O.ec, &es);
    printf("  retrains %u, reversals %u; LNs ours %d, last 0x%02X, the caller's last 0x%02X, violations %lu; data %ld to the caller, %ld to us, %ld wrong\n",
           ss.retrains, ss.reversals, RC.nln, es.attn_tx, es.attn_rx, es.violations, C.r, O.r, C.bad + O.bad);
}
/* the caller's data to us, unbounded: runs end on time */
static void rc_call(const hstcall_cfg *hc, const our_cfg *oc, long to_us, long to_hc, uint32_t seed)
{
    our_init(&O, oc);
    line_init(0, seed);
    cal_init_hst(hc, to_us, to_hc, seed);
    rv_init();
    run_start();
}
static int holding_back_data(void)
{
    answer_t *a = ans();
    return (a->phase == ANS_HST && a->u.hst.link.role == HST_ROLE_BACK && a->u.hst.rev == HST_REV_NONE
            && hst_frx_state(&a->u.hst.frx) == HST_FRX_DATA) || O.end_ms >= 0;
}

/* up: holding forward, the caller's 0x34: our switch by marks 6 -> 4, no
 * retrain; then its reversal at 9600: we hold the back channel at 4 on a
 * good line, and every 10 s our 0x3R to the next agreed rate, 0x35 then
 * 0x36, each carried out by the caller's marks and followed by our
 * receiver without a retrain */
static void up(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    long t_back, t0;
    int i35, i36, ok_t35, ok_t36, e35, e36, k;
    hc_defaults(&hc);
    hc.rev_backlog = 64; hc.force_kind = 0x30; hc.force_rate_code = 4; hc.force_rate_at_ms = 6000;
    printf("up: our switch by marks to 9600 as holder; then holding back, 10 s good: our 0x35, then 0x36\n");
    rc_call(&hc, &oc, 100000000L, 2048, 2201u);
    C.after_fwd = 1;
    run_ms(60000L, holding_back_data);
    t_back = now_ms;
    if (verbose) printf("  (holding the back channel in data at %ld ms)\n", t_back);
    run_ms(t_back + 32000L, ended);
    printf("  our switches by marks %d: %d to %d; our trainings %d; switches %d\n", RC.nmk, RC.nmk ? RC.mk_from[0] : 0,
           RC.nmk ? RC.mk_to[0] : 0, RC.ntr, RV.n);
    bound("holding forward, the caller's 0x34: our switch by marks, 14400 to 9600, RATES tx 9600",
          RC.nmk == 1 && RC.mk_from[0] == 6 && RC.mk_to[0] == 4 && rates_at(RC.mk_ms[0], 9600, 0) >= 0);
    bound("then its reversal: we hold the back channel at 9600", RV.n == 1 && RV.to[0] == HST_ROLE_BACK && rates_at(RV.sw[0], 0, 9600) >= 0);
    i35 = rc_ln_index(0x35, 0); i36 = rc_ln_index(0x36, i35 < 0 ? 0 : i35);
    printf("  our LNs %d: 0x35 %s, 0x36 %s\n", RC.nln, i35 >= 0 ? "sent" : "not sent", i36 >= 0 ? "sent" : "not sent");
    /* the epochs: the reversal's (the receiver in data), then each switch's */
    e35 = -1; e36 = -1;
    for (k = 0; k < RC.nep; k++) {
        if (RC.ep_ms[k] <= t_back + 1) e35 = k;
        if (i35 >= 0 && RC.ep_ms[k] > RC.ln_ms[i35] && e36 < 0) e36 = k;
    }
    t0 = e35 >= 0 ? RC.ep_ms[e35] : -1;
    ok_t35 = i35 >= 0 && t0 >= 0 && RC.ln_ms[i35] - t0 >= 10000 && RC.ln_ms[i35] - t0 <= 10050;
    ok_t36 = i36 >= 0 && e36 >= 0 && RC.ln_ms[i36] - RC.ep_ms[e36] >= 10000 && RC.ln_ms[i36] - RC.ep_ms[e36] <= 10050;
    if (verbose) printf("  (0x35 at %ld, %ld ms after the reversal's epoch; 0x36 at %ld, %ld after 0x35's)\n",
                        i35 >= 0 ? RC.ln_ms[i35] : -1L, i35 >= 0 && t0 >= 0 ? RC.ln_ms[i35] - t0 : -1L,
                        i36 >= 0 ? RC.ln_ms[i36] : -1L, i36 >= 0 && e36 >= 0 ? RC.ln_ms[i36] - RC.ep_ms[e36] : -1L);
    bound("our 0x35 10 s after the receiver came into data, our first LN", ok_t35 && i35 == 0);
    bound("our 0x36 10 s after the switch to 12000 was in", ok_t36 && i36 == 1);
    bound("the receiver switched by marks to 12000, then 14400, untrained; RATES rx 12000, rx 14400",
          RC.nrx == 3 && RC.rx_code[0] == 4 && RC.rx_code[1] == 5 && RC.rx_code[2] == 6 && !RC.rx_trained[1] && !RC.rx_trained[2]
          && i35 >= 0 && rates_at(RC.ln_ms[i35], 0, 12000) >= 0 && i36 >= 0 && rates_at(RC.ln_ms[i36], 0, 14400) >= 0);
    rc_data_line();
    bound("data never stopped after the post-agreement retrain", count_ev(V90M_EV_DATA_STOPPED, moved_index()) == 0);
    bound("no more LNs (14400 is the top); the data both ways intact, ours all in", RC.nln == 2 && C.bad == 0 && O.bad == 0
          && C.r == O.to_send && O.r > 20000 && O.end_ms < 0);
}

/* down: holding back at 14400, frame errors (a bit of what we receive
 * turned over now and then): 10 of them, then nothing for 11 s, no
 * downshift (the window opened by the first has closed); then 15 in a new
 * window: our 0x25, the caller's short training at 12000, a retrain
 * (DATA_STOPPED, DATA_MOVING, retrains + 1); our next LN, 0x36, no sooner
 * than 10 s after the 0x25 */
static void down(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    mnp_t *m;
    unsigned long b0, b1;
    long t_a, t_b, t_first = -1;
    int i25, i36, n0, ok;
    v90m_stats ss;
    hc_defaults(&hc);
    hc.rev_backlog = 64;
    printf("down: holding back at 14400, 15 frame errors in 10 s: our 0x25\n");
    rc_call(&hc, &oc, 100000000L, 2048, 2202u);
    C.after_fwd = 1;
    run_ms(60000L, holding_back_data);
    run_ms(now_ms + 2000, ended);
    m = our_mnp();
    /* 10 frame errors, then a pause */
    b0 = m->rx_bad; t_a = now_ms;
    O.flip_every = 1999;
    while (now_ms < t_a + 20000 && m->rx_bad < b0 + 10 && O.end_ms < 0) run_ms(now_ms + 1, NULL);
    O.flip_every = 0;
    if (verbose) printf("  (%lu frame errors from %ld to %ld ms)\n", m->rx_bad - b0, t_a, now_ms);
    run_ms(now_ms + 11000, ended);
    n0 = RC.nln;
    bound("10 frame errors, then 11 s: no LN", n0 == 0 && m->rx_bad - b0 >= 10 && m->rx_bad - b0 <= 11);
    /* again until our LN */
    b1 = m->rx_bad; t_b = now_ms;
    O.flip_every = 1999;
    while (now_ms < t_b + 20000 && RC.nln == n0 && O.end_ms < 0) {
        run_ms(now_ms + 1, NULL);
        if (t_first < 0 && m->rx_bad > b1) t_first = now_ms;
    }
    O.flip_every = 0;
    i25 = rc_ln_index(0x25, 0);
    printf("  then frame errors until our LN: %lu; our first LN 0x%02X\n", RC.nln ? RC.ln_bad[0] - b1 : 0UL, RC.nln ? RC.ln[0] : 0);
    if (verbose) printf("  (the first at %ld ms, our LN at %ld)\n", t_first, RC.nln ? RC.ln_ms[0] : -1L);
    bound("15 frame errors within 10 s of the first: our 0x25", i25 == 0 && RC.ln_bad[0] - b1 == 15 && RC.ln_ms[0] - t_first <= 10000);
    n0 = O.nev;
    run_ms(now_ms + 30000, ended);
    v90m_get_stats(O.m, &ss);
    i36 = rc_ln_index(0x36, 1);
    ok = i25 == 0 && index_of(V90M_EV_DATA_STOPPED, n0) >= 0 && index_of(V90M_EV_DATA_MOVING, index_of(V90M_EV_DATA_STOPPED, n0)) >= 0;
    bound("the caller's training at 12000: a retrain, DATA_STOPPED then DATA_MOVING, RATES rx 12000, retrains 2",
          ok && RC.nrx >= 2 && RC.rx_code[1] == 5 && RC.rx_trained[1] && rates_at(RC.ln_ms[0], 0, 12000) >= 0 && ss.retrains == 2);
    if (verbose) printf("  (0x36 at %ld, %ld ms after the 0x25)\n", i36 >= 0 ? RC.ln_ms[i36] : -1L, i36 >= 0 ? RC.ln_ms[i36] - RC.ln_ms[0] : -1L);
    bound("our next LN 0x36, no sooner than 10 s after the downshift; back at 14400 by marks",
          i36 == 1 && RC.ln_ms[1] - RC.ln_ms[0] >= 10000 && RC.nrx == 3 && RC.rx_code[2] == 6 && !RC.rx_trained[2]);
    rc_data_line();
    bound("the data both ways intact", C.bad == 0 && O.bad == 0 && C.r == O.to_send && O.r > 20000 && O.end_ms < 0);
}

/* retrain_by_2R: holding forward at 14400, the caller's 0x25: our short
 * training at 12000 once our LNA has gone, a retrain (DATA_STOPPED,
 * DATA_MOVING, retrains + 1, RATES tx 12000), MNP's T401 held through it */
static void retrain_by_2R(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_stats ss;
    int n0, i1, i2, k = -1, i;
    hc_defaults(&hc);
    hc.force_kind = 0x20; hc.force_rate_code = 5; hc.force_rate_at_ms = 12000;
    printf("retrain_by_2R: holding forward, the caller's 0x25: our short training at 12000\n");
    rc_call(&hc, &oc, 512, 100000000L, 2203u);
    run_ms(11900, ended);
    n0 = O.nev;
    run_ms(now_ms + 15000, ended);
    v90m_get_stats(O.m, &ss);
    i1 = index_of(V90M_EV_DATA_STOPPED, n0); i2 = i1 < 0 ? -1 : index_of(V90M_EV_DATA_MOVING, i1);
    for (i = 0; i < RC.ntr; i++) if (RC.tr_ms[i] >= 11900) { k = i; break; }
    printf("  our trainings after 12 s: %d, code %d, S3 %d; retrains %u\n", k >= 0 ? RC.ntr - k : 0, k >= 0 ? RC.tr_code[k] : 0,
           k >= 0 ? RC.tr_len[k] : 0, ss.retrains);
    if (verbose) printf("  (training at %ld ms; STOPPED %ld, MOVING %ld; T401 over %ld ticks, %ld moved, running at the stop %ld)\n",
                        k >= 0 ? RC.tr_ms[k] : -1L, i1 >= 0 ? O.ev_ms[i1] : -1L, i2 >= 0 ? O.ev_ms[i2] : -1L, RC.rt_ticks, RC.rt_bad, RC.rt_running);
    bound("one short training at 12000", k >= 0 && RC.ntr - k == 1 && RC.tr_code[k] == 5 && RC.tr_len[k] == EXP_S3_SHORT);
    bound("a retrain: DATA_STOPPED, then DATA_MOVING at tx 12000; retrains 2", i1 >= 0 && i2 > i1 && O.ev_tx[i2] == 12000 && ss.retrains == 2);
    bound("MNP's T401 held through both retrains (running at this one's start)", RC.rt_ticks > 0 && RC.rt_bad == 0 && RC.rt_running >= 1);
    bound("the caller's receiver in data at 12000", HC.frx.state == HC_FRX_DATA && HC.frx.d.code == 5);
    rc_data_line();
    bound("the data both ways intact", C.bad == 0 && O.bad == 0 && O.r == C.to_send && C.r > 8000 && O.end_ms < 0);
}

/* max_up: max_up_bps 9600. The caller's reversal at 14400 (0x16): our LNA,
 * its training, then our 0x24 and its training at 9600; its 0x36 later:
 * an LNA, a count (violations), our 0x24 again */
static void max_up(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_ec_status es;
    int i1, i2, ok, k, hi = 0;
    oc.max_up = 9600;
    hc_defaults(&hc);
    hc.rev_backlog = 64;
    hc.force_more[0].kind = 0x30; hc.force_more[0].code = 6; hc.force_more[0].at_ms = 30000;
    printf("max_up: max_up_bps 9600; the caller's 0x16, then its 0x36\n");
    rc_call(&hc, &oc, 100000000L, 2048, 2204u);
    C.after_fwd = 1;
    run_ms(29000, ended);
    i1 = rc_ln_index(0x24, 0);
    bound("the caller's 0x16 granted: we hold back at 14400 (RATES rx 14400)", RV.n == 1 && RV.to[0] == HST_ROLE_BACK && rates_at(RV.sw[0], 0, 14400) >= 0);
    if (verbose) printf("  (switch at %ld ms; epochs:%s", RV.sw[0], RC.nep ? "" : " none");
    if (verbose) { for (k = 0; k < RC.nep; k++) printf(" %ld", RC.ep_ms[k]); printf("; our 0x24 at %ld)\n", i1 >= 0 ? RC.ln_ms[i1] : -1L); }
    ok = i1 == 0 && RC.nep >= 1 && RC.ln_ms[i1] >= RC.ep_ms[0] && RC.ln_ms[i1] - RC.ep_ms[0] <= 100;
    bound("once its training is in, our 0x24, our first LN", ok);
    bound("its training at 9600: our receiver in data there, RATES rx 9600", RC.nrx >= 2 && RC.rx_code[0] == 6 && RC.rx_code[1] == 4
          && RC.rx_trained[1] && rates_at(RV.sw[0] + 1, 0, 9600) >= 0);
    run_ms(45000, ended);
    v90m_ec_get_status(O.ec, &es);
    i2 = rc_ln_index(0x24, i1 + 1);
    for (k = 2; k < RC.nrx; k++) hi |= RC.rx_code[k] != 4;
    printf("  the caller's last LN 0x%02X; violations %lu; our LNs %d\n", es.attn_rx, es.violations, RC.nln);
    bound("its 0x36: an LNA, a count, our 0x24 again; never above 9600", es.attn_rx == 0x36 && es.violations == 1 && i2 == 1
          && RC.nln == 2 && !hi && RC.rx_code[RC.nrx - 1] == 4);
    rc_data_line();
    bound("the data both ways intact", C.bad == 0 && O.bad == 0 && C.r == O.to_send && O.r > 10000 && O.end_ms < 0);
}

/* recovery: holding forward at 14400, the caller's 0x35, which it carries
 * out by a training (BinModem's way) where we switch by marks: its
 * receiver lost, nothing we send acknowledged while its back channel
 * goes on; 2 x T401 on, our short training at 12000, a retrain */
static void recovery(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_stats ss;
    mnp_t *m;
    int t401 = 0, n0, i1, i2, k = -1, i, n_tr;
    long d = -1, c_at, o_at;
    hc_defaults(&hc);
    hc.retrain_3r = 1; hc.force_kind = 0x30; hc.force_rate_code = 5; hc.force_rate_at_ms = 12000;
    printf("recovery: the caller expects a training where we switch by marks; 2 x T401: our short training\n");
    rc_call(&hc, &oc, 100000000L, 100000000L, 2205u);
    run_ms(11900, ended);
    m = our_mnp();
    n0 = O.nev;
    while (now_ms < 30000 && RC.nmk == 0 && O.end_ms < 0) run_ms(now_ms + 1, NULL);
    t401 = m ? m->t401_ms : 0;
    /* to 1 s after our next training, then 10 s more: the data moving again */
    n_tr = RC.ntr;
    while (now_ms < 60000 && RC.ntr == n_tr && O.end_ms < 0) run_ms(now_ms + 1, NULL);
    run_ms(now_ms + 1000, ended);
    c_at = C.r; o_at = O.r;
    run_ms(now_ms + 10000, ended);
    v90m_get_stats(O.m, &ss);
    for (i = 0; i < RC.ntr; i++) if (RC.nmk && RC.tr_ms[i] >= RC.mk_ms[0]) { k = i; break; }
    if (k >= 0) d = RC.tr_ms[k] - RC.mk_ms[0];
    i1 = index_of(V90M_EV_DATA_STOPPED, n0); i2 = i1 < 0 ? -1 : index_of(V90M_EV_DATA_MOVING, i1);
    printf("  our switch by marks: %d to %d; then our trainings %d, code %d, S3 %d\n", RC.nmk ? RC.mk_from[0] : 0, RC.nmk ? RC.mk_to[0] : 0,
           k >= 0 ? RC.ntr - k : 0, k >= 0 ? RC.tr_code[k] : 0, k >= 0 ? RC.tr_len[k] : 0);
    if (verbose) printf("  (marks at %ld ms, our training %ld ms later; T401 %d ms)\n", RC.nmk ? RC.mk_ms[0] : -1L, d, t401);
    bound("the caller's 0x35: our switch by marks to 12000, no retrain there", RC.nmk == 1 && RC.mk_from[0] == 6 && RC.mk_to[0] == 5
          && (i1 < 0 || O.ev_ms[i1] > RC.mk_ms[0] + 1000));
    bound("2 x T401 on (within 1 s more), our short training at 12000", k >= 0 && RC.ntr - k == 1 && RC.tr_code[k] == 5
          && RC.tr_len[k] == EXP_S3_SHORT && t401 > 0 && d >= 2 * t401 && d <= 2 * t401 + 1000);
    bound("a retrain: DATA_STOPPED, DATA_MOVING; retrains 2; the caller's receiver at 12000",
          i1 >= 0 && i2 > i1 && ss.retrains == 2 && HC.frx.state == HC_FRX_DATA && HC.frx.d.code == 5);
    rc_data_line();
    if (verbose) printf("  (then in 10 s: %ld octets to the caller, %ld to us)\n", C.r - c_at, O.r - o_at);
    bound("the data both ways intact, and moving again", C.bad == 0 && O.bad == 0 && C.r - c_at > 2000 && O.r - o_at > 200 && O.end_ms < 0);
}

/* fallback (Task 16's): holding back at 12000, the forward channel at our
 * input falls to 15 dB SNR, where trainings at 9600 fail: the policy ends
 * at 7200 (frame errors and failed retrains), in data there, and stays:
 * no retrain, no LN in the last 15 s */
static void fallback(void)
{
    hstcall_cfg hc;
    our_cfg oc = hst_ours();
    v90m_stats ss0, ss;
    answer_t *a;
    long t_deg, n_ln, t_last, t_stop = -1;
    int k;
    hc_defaults(&hc);
    hc.rev_backlog = 64; hc.rates = (uint8_t)rates_mask(5);
    printf("fallback: holding back at 12000, the line at 15 dB SNR: down to 7200, and staying there\n");
    rc_call(&hc, &oc, 100000000L, 2048, 2206u);
    C.after_fwd = 1;
    run_ms(60000L, holding_back_data);
    xn_meas = 1;
    run_ms(now_ms + 1000, ended);
    xn_meas = 0;
    xn_rms = sqrt(xn_pw / (xn_n ? xn_n : 1)) * pow(10, -15.0 / 20);
    t_deg = now_ms;
    v90m_get_stats(O.m, &ss0);
    run_ms(t_deg + 45000L, ended);
    v90m_get_stats(O.m, &ss);
    a = ans();
    n_ln = 0; t_last = -1;
    for (k = 0; k < RC.nln; k++) { n_ln++; t_last = RC.ln_ms[k]; }
    printf("  our LNs %ld:", n_ln);
    for (k = 0; k < RC.nln; k++) printf(" 0x%02X", RC.ln[k]);
    printf("; retrains %u\n", ss.retrains - ss0.retrains);
    if (verbose) { printf("  (LNs at"); for (k = 0; k < RC.nln; k++) printf(" %ld", RC.ln_ms[k] - t_deg); printf(" ms after the noise)\n"); }
    bound("in data at 7200, holding back", a->phase == ANS_HST && a->u.hst.link.role == HST_ROLE_BACK
          && hst_frx_state(&a->u.hst.frx) == HST_FRX_DATA && a->u.hst.frx.code == 3 && a->rx_bitrate == 7200);
    for (k = 0; k < O.nev; k++) if (O.ev[k] == V90M_EV_DATA_STOPPED) t_stop = O.ev_ms[k];
    bound("no loop: at most 8 retrains, none and no LN in the last 15 s", ss.retrains - ss0.retrains <= 8 && n_ln <= 10
          && t_last >= 0 && t_last < t_deg + 30000 && t_stop < t_deg + 30000);
    rc_data_line();
    bound("the data intact", C.bad == 0 && O.bad == 0 && O.end_ms < 0);
}

#define NCASES 24
int main(int argc, char **argv)
{
    int only;
    if (argc > 1 && !strcmp(argv[1], "--cases")) { printf("%d\n", NCASES); return 0; }
    only = argc > 1 ? atoi(argv[1]) : 0;
#if HST_LONG_V17
    /* the BinModem profile's build (make check-hst-bm): `rates` and
     * `rev_caller`, unless a case is named */
    if (!only) {
        printf("test_answer_hst: the HST answering branch through the API, the BinModem profile (W %d ms)\n", HST_W_MS);
        rates();
        rev_caller();
        CHECK_EQ_I(proc_err, 0);
        if (O.m) v90m_destroy(O.m);
        if (O.ec) v90m_ec_destroy(O.ec);
        T_REPORT("test_answer_hst");
    }
#endif
    verbose = getenv("HST_VERBOSE") != NULL;
    printf("test_answer_hst: the HST answering branch through the API (W %d ms)\n", HST_W_MS);
    CHECK_EQ_I(V90M_END_HST_FAILED, V90M_END_K56_FAILED + 1);
    if (!only || only == 1) rates();
    if (!only || only == 2) hst96();
    if (!only || only == 3) plain_mnp();
    if (!only || only == 4) unlinked();
    if (!only || only == 5) max_down();
    if (!only || only == 6) lr_in_training();
    if (!only || only == 7) no_lr();
    if (!only || only == 8) lost_fwd();
    if (!only || only == 9) legacy_unchanged();
    if (!only || only == 10) alaw();
    if (!only || only == 11) block_size();
    if (!only || only == 12) rev_caller();
    if (!only || only == 13) rev_ours();
    if (!only || only == 14) rev_back300();
    if (!only || only == 15) rev_many();
    if (!only || only == 16) rev_lost();
    if (!only || only == 17) snr();
    if (!only || only == 18) rev_ours_la();
    if (!only || only == 19) up();
    if (!only || only == 20) down();
    if (!only || only == 21) retrain_by_2R();
    if (!only || only == 22) max_up();
    if (!only || only == 23) recovery();
    if (!only || only == 24) fallback();
    CHECK_EQ_I(proc_err, 0);
    if (O.m) v90m_destroy(O.m);
    if (O.ec) v90m_ec_destroy(O.ec);
    T_REPORT("test_answer_hst");
}
