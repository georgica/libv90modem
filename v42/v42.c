/* V.42 endpoint: detection phase, LAPM over HDLC, V.42 bis, async fallback.
 * See v42.h. Clause numbers refer to ITU-T V.42 (03/2002) unless they say
 * V.42 bis. */
#include <string.h>
#include "v42.h"

#define PREAMBLE_FLAGS 16    /* flags sent ahead of the first frame (8.3.2.1 NOTE 2) */
#define FLAGS_SEEN     3     /* "continuous flags" that start the protocol phase */
#define ADP_MIN        10    /* 7.2.1.3: ADP at least ten times */
/* Originator, after the ADP: line bits of flags to send while the answerer
 * may still be finishing its ten ADPs and not yet listening for frames. Cut
 * short as soon as its flags arrive. Ten ADPs are at most 520 bits. */
#define ORIG_WAIT_BITS 600
/* MNP: the longest LR body the start-stop hunter takes (an LI is at most
 * 254, mnp_lr_parse reads no extended one), and the longest LT information
 * that fits an HDLC frame of v42_hdlc.h with the standard header */
#define LR_MAX_BODY    255
#define HDLC_MAX_LT    (V42HDLC_MAX_PAYLOAD - MNP_LT_STD_HDR)
/* MNP: busy below k x N401 octets free (mnp.h): never all the time */
typedef char v42ec_rxbuf_mnp[V42EC_RXBUF > MNP_MAX_K * MNP_MAX_N401 ? 1 : -1];

/* ------------------------------------------------------------------ rings */
static void ring_reset(v42ec_ring_t *r) { r->head = r->len = 0; }
static size_t ring_free(const v42ec_ring_t *r) { return r->size - r->len; }

static size_t ring_put(v42ec_ring_t *r, const uint8_t *p, size_t n)
{
    size_t i, w;
    if (n > ring_free(r)) n = ring_free(r);
    w = (r->head + r->len) % r->size;
    for (i = 0; i < n; i++) {
        r->buf[w] = p[i];
        if (++w == r->size) w = 0;
    }
    r->len += n;
    return n;
}

/* longest contiguous run at the head */
static const uint8_t *ring_peek(const v42ec_ring_t *r, size_t *n)
{
    size_t c = r->size - r->head;
    *n = r->len < c ? r->len : c;
    return r->buf + r->head;
}

static void ring_drop(v42ec_ring_t *r, size_t n)
{
    r->head = (r->head + n) % r->size;
    r->len -= n;
    if (r->len == 0) r->head = 0;
}

static size_t ring_get(v42ec_ring_t *r, uint8_t *p, size_t n)
{
    size_t done = 0;
    if (n > r->len) n = r->len;
    while (done < n) {
        size_t c; const uint8_t *s = ring_peek(r, &c);
        if (c > n - done) c = n - done;
        memcpy(p + done, s, c);
        ring_drop(r, c);
        done += c;
    }
    return n;
}

/* ------------------------------------------------------------------ setup */
void v42ec_cfg_default(v42ec_cfg_t *cfg, int originator)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->originator = originator ? 1 : 0;
    cfg->detect = 1;
    cfg->fallback = 1;
    cfg->t400_ms = 750;
    cfg->establish_ms = 30000;
    cfg->comp_policy = V42COMP_AUTO;
    v42lapm_cfg_default(&cfg->lapm);
    cfg->lapm.comp_tx = cfg->lapm.comp_rx = 1;
    cfg->lapm.comp_p1 = V42COMP_MAX_CODEWORDS < 2048 ? V42COMP_MAX_CODEWORDS : 2048;
    cfg->lapm.comp_p2 = 32;
    cfg->lapm.v44_tx = cfg->lapm.v44_rx = 1;
    cfg->lapm.v44_p1_tx = cfg->lapm.v44_p1_rx = V44COMP_MAX_CODEWORDS < 2048 ? V44COMP_MAX_CODEWORDS : 2048;
    cfg->lapm.v44_p2_tx = cfg->lapm.v44_p2_rx = V44COMP_MAX_STRING;
    cfg->lapm.v44_p3_tx = cfg->lapm.v44_p3_rx = V44COMP_MAX_HISTORY < 4096 ? V44COMP_MAX_HISTORY : 4096;
    cfg->mnp.enable = 0;
    mnp_cfg_default(&cfg->mnp.link);
}

/* --------------------------------------------- the compression procedures */
/* What the endpoint asks of the procedure in use (comp_alg): one entry for
 * V.42 bis and one for V.44, each on its member of the enc and dec unions */
typedef struct {
    int    (*enc_init)(v42ec_t *e);     /* C-INIT, our encoder, and its policy */
    int    (*dec_init)(v42ec_t *e);     /* C-INIT, our decoder */
    size_t (*encode)(v42ec_t *e, const uint8_t *in, size_t inlen,
                     uint8_t *out, size_t outmax, size_t *outlen);
    int    (*flush)(v42ec_t *e, uint8_t *out, size_t outmax, size_t *outlen);
    size_t (*enc_pending)(const v42ec_t *e);
    /* receive room a decode needs: V.42 bis one string (P2), V.44 any */
    size_t (*dec_room)(const v42ec_t *e);
    int    (*decode)(v42ec_t *e, const uint8_t *in, size_t inlen, size_t *used,
                     uint8_t *out, size_t outmax, size_t *outlen);
    int    (*dec_pending)(const v42ec_t *e);
    void   (*enc_counts)(const v42ec_t *e, unsigned long *in, unsigned long *out);
    void   (*dec_counts)(const v42ec_t *e, unsigned long *in, unsigned long *out);
} comp_ops_t;

/* V.42 bis */
static int bis_enc_init(v42ec_t *e)
{
    if (v42comp_enc_init(&e->enc.bis, e->l.lapm.comp_n2, e->l.lapm.comp_n7) != 0) return -1;
    v42comp_enc_policy(&e->enc.bis, e->cfg.comp_policy);
    return 0;
}
static int bis_dec_init(v42ec_t *e)
{
    return v42comp_dec_init(&e->dec.bis, e->l.lapm.comp_n2, e->l.lapm.comp_n7);
}
static size_t bis_encode(v42ec_t *e, const uint8_t *in, size_t inlen, uint8_t *out, size_t outmax, size_t *outlen)
{
    return v42comp_encode(&e->enc.bis, in, inlen, out, outmax, outlen);
}
static int bis_flush(v42ec_t *e, uint8_t *out, size_t outmax, size_t *outlen)
{
    return v42comp_flush(&e->enc.bis, out, outmax, outlen);
}
static size_t bis_enc_pending(const v42ec_t *e) { return v42comp_enc_pending(&e->enc.bis); }
static size_t bis_dec_room(const v42ec_t *e) { return e->dec.bis.d.n7; }
static int bis_decode(v42ec_t *e, const uint8_t *in, size_t inlen, size_t *used,
                      uint8_t *out, size_t outmax, size_t *outlen)
{
    return v42comp_decode(&e->dec.bis, in, inlen, used, out, outmax, outlen);
}
static int bis_dec_pending(const v42ec_t *e) { return v42comp_dec_pending(&e->dec.bis); }
static void bis_enc_counts(const v42ec_t *e, unsigned long *in, unsigned long *out)
{
    *in = e->enc.bis.in_chars; *out = e->enc.bis.out_octets;
}
static void bis_dec_counts(const v42ec_t *e, unsigned long *in, unsigned long *out)
{
    *in = e->dec.bis.in_octets; *out = e->dec.bis.out_chars;
}

/* V.44 */
static int v44_enc_init(v42ec_t *e)
{
    const v42lapm_t *l = &e->l.lapm;
    if (v44comp_enc_init(&e->enc.v44, l->v44_n2_tx, l->v44_n7_tx, l->v44_n8_tx) != 0) return -1;
    v44comp_enc_policy(&e->enc.v44, e->cfg.comp_policy);
    return 0;
}
static int v44_dec_init(v42ec_t *e)
{
    const v42lapm_t *l = &e->l.lapm;
    return v44comp_dec_init(&e->dec.v44, l->v44_n2_rx, l->v44_n7_rx, l->v44_n8_rx);
}
static size_t v44_encode(v42ec_t *e, const uint8_t *in, size_t inlen, uint8_t *out, size_t outmax, size_t *outlen)
{
    return v44comp_encode(&e->enc.v44, in, inlen, out, outmax, outlen);
}
static int v44_flush(v42ec_t *e, uint8_t *out, size_t outmax, size_t *outlen)
{
    return v44comp_flush(&e->enc.v44, out, outmax, outlen);
}
static size_t v44_enc_pending(const v42ec_t *e) { return v44comp_enc_pending(&e->enc.v44); }
static size_t v44_dec_room(const v42ec_t *e) { (void)e; return 1; }
static int v44_decode(v42ec_t *e, const uint8_t *in, size_t inlen, size_t *used,
                      uint8_t *out, size_t outmax, size_t *outlen)
{
    return v44comp_decode(&e->dec.v44, in, inlen, used, out, outmax, outlen);
}
static int v44_dec_pending(const v42ec_t *e) { return v44comp_dec_pending(&e->dec.v44); }
static void v44_enc_counts(const v42ec_t *e, unsigned long *in, unsigned long *out)
{
    *in = e->enc.v44.in_chars; *out = e->enc.v44.out_octets;
}
static void v44_dec_counts(const v42ec_t *e, unsigned long *in, unsigned long *out)
{
    *in = e->dec.v44.in_octets; *out = e->dec.v44.out_chars;
}

/* indexed by comp_alg - V42EC_COMP_V42BIS */
static const comp_ops_t comp_ops[2] = {
    { bis_enc_init, bis_dec_init, bis_encode, bis_flush, bis_enc_pending,
      bis_dec_room, bis_decode, bis_dec_pending, bis_enc_counts, bis_dec_counts },
    { v44_enc_init, v44_dec_init, v44_encode, v44_flush, v44_enc_pending,
      v44_dec_room, v44_decode, v44_dec_pending, v44_enc_counts, v44_dec_counts }
};
typedef char comp_ops_rows[(V42EC_COMP_V44 - V42EC_COMP_V42BIS + 1
                            == (int)(sizeof comp_ops / sizeof comp_ops[0])) ? 1 : -1];

/* the procedure in use; only while comp_alg is not V42EC_COMP_NONE */
static const comp_ops_t *ops(const v42ec_t *e)
{
    return &comp_ops[e->comp_alg - V42EC_COMP_V42BIS];
}

/* C-INIT of whichever procedure the XID exchange settled on (V.42 bis 5.2,
 * 5.3; V.44 7.5). LAPM never leaves both agreed. A direction agreed but
 * refused here (values past this build's arrays: v42ec_init keeps our
 * proposals within them, so only a bug gets here) releases the link: the
 * far end would compress, or expect compressed data, while we did not. */
static void comp_init(v42ec_t *e)
{
    const v42lapm_t *l = &e->l.lapm;
    int tx = 0, rx = 0, bad = 0;

    e->comp_tx = e->comp_rx = 0;
    e->comp_alg = V42EC_COMP_NONE;
    e->enc_dirty = 0;
    if (l->v44_tx || l->v44_rx) {
        e->comp_alg = V42EC_COMP_V44;
        tx = l->v44_tx; rx = l->v44_rx;
    } else if (l->comp_tx || l->comp_rx) {
        e->comp_alg = V42EC_COMP_V42BIS;
        tx = l->comp_tx; rx = l->comp_rx;
    }
    if (tx) {
        if (ops(e)->enc_init(e) == 0) e->comp_tx = 1;
        else bad = 1;
    }
    if (rx) {
        if (ops(e)->dec_init(e) == 0) e->comp_rx = 1;
        else bad = 1;
    }
    if (bad) {
        e->comp_tx = e->comp_rx = 0;
        e->reason_override = V42EC_R_COMPRESSION;
        v42lapm_release(&e->l.lapm);
    }
}

/* the encoder and decoder in use */
static int enc_pending(const v42ec_t *e)
{
    return e->comp_tx && ops(e)->enc_pending(e) != 0;
}

static int dec_pending(const v42ec_t *e)
{
    return e->comp_rx && ops(e)->dec_pending(e) != 0;
}

static void finish(v42ec_t *e, int reason)
{
    e->state = V42EC_DISCONNECTED;
    e->reason = reason;
    e->t400 = e->t_est = -1;
}

/* protocol establishment phase begins (7.2.2) */
static void start_protocol(v42ec_t *e)
{
    e->state = V42EC_ESTABLISH;
    e->protocol = V42EC_PROTO_LAPM;
    e->t400 = -1;
    if (e->cfg.originator) {
        e->hdlc_tx_on = 1;
        v42hdlc_tx_flags(&e->htx, PREAMBLE_FLAGS);
        if (e->cfg.detect) e->orig_wait = ORIG_WAIT_BITS;
        else v42lapm_start(&e->l.lapm);
    } else if (e->t_est < 0 && e->cfg.establish_ms > 0) {
        e->t_est = e->cfg.establish_ms;
    }
}

static void enter_async(v42ec_t *e)
{
    e->t400 = e->t_est = -1;
    if (!e->cfg.fallback) { finish(e, V42EC_R_NO_EC); return; }
    e->state = V42EC_ASYNC;
    e->protocol = V42EC_PROTO_ASYNC;
    e->hdlc_tx_on = 0;
    v42async_tx_init(&e->atx);
}

void v42ec_init(v42ec_t *e, const v42ec_cfg_t *cfg)
{
    memset(e, 0, sizeof *e);
    e->cfg = *cfg;
    e->cfg.originator = cfg->originator ? 1 : 0;
    /* never propose more than the arrays of v42_comp.h and v44_comp.h hold */
    if (e->cfg.lapm.comp_p1 > V42COMP_MAX_CODEWORDS) e->cfg.lapm.comp_p1 = V42COMP_MAX_CODEWORDS;
    if (e->cfg.lapm.v44_p1_tx > V44COMP_MAX_CODEWORDS) e->cfg.lapm.v44_p1_tx = V44COMP_MAX_CODEWORDS;
    if (e->cfg.lapm.v44_p1_rx > V44COMP_MAX_CODEWORDS) e->cfg.lapm.v44_p1_rx = V44COMP_MAX_CODEWORDS;
    if (e->cfg.lapm.v44_p3_tx > V44COMP_MAX_HISTORY) e->cfg.lapm.v44_p3_tx = V44COMP_MAX_HISTORY;
    if (e->cfg.lapm.v44_p3_rx > V44COMP_MAX_HISTORY) e->cfg.lapm.v44_p3_rx = V44COMP_MAX_HISTORY;
    e->txr.buf = e->txbuf; e->txr.size = sizeof e->txbuf;
    e->rxr.buf = e->rxbuf; e->rxr.size = sizeof e->rxbuf;
    e->t400 = e->t_est = -1;
    e->hst_win = -1;
    v42hdlc_tx_init(&e->htx);
    v42hdlc_rx_init(&e->hrx);
    v42async_tx_init(&e->atx);
    v42det_rx_init(&e->det_rx, e->cfg.originator);
    v42det_tx_init(&e->det_tx, e->cfg.originator ? V42DET_PAT_ODP : V42DET_PAT_ADP_V42, 8);
    v42lapm_init(&e->l.lapm, e->cfg.originator, &e->cfg.lapm);
    v42hdlc_rx_set_fcs(&e->hrx, e->l.lapm.rx_fcs_mode);
    e->cfg.mnp.enable = !e->cfg.originator && e->cfg.mnp.enable != 0;
    if (e->cfg.mnp.enable) mnp2_rx_init(&e->hunt2, LR_MAX_BODY);

    if (e->cfg.detect) {
        e->state = V42EC_DETECT;
        e->t400 = e->cfg.t400_ms > 0 ? e->cfg.t400_ms : 750;
    } else {
        e->hdlc_tx_on = 1;
        start_protocol(e);
    }
}

/* ------------------------------------------- follow what LAPM has done */
static void drain_rx(v42ec_t *e)
{
    for (;;) {
        const uint8_t *p;
        int n = v42lapm_rx_avail(&e->l.lapm, &p);
        size_t room = ring_free(&e->rxr);

        if (e->comp_rx) {
            uint8_t tmp[256];
            size_t used = 0, out = 0, max = room < sizeof tmp ? room : sizeof tmp;
            int rc;
            if (n == 0 && !dec_pending(e)) break;
            if (room < ops(e)->dec_room(e)) break;
            rc = ops(e)->decode(e, p, (size_t)n, &used, tmp, max, &out);
            ring_put(&e->rxr, tmp, out);
            if (used) v42lapm_rx_consume(&e->l.lapm, (int)used);
            if (rc != 0) {
                /* C-ERROR (V.42 bis 5.8, V.44 7.15). V.42 bis asks for
                 * "appropriate recovery action, including re-establishment";
                 * this implementation does not re-establish, it releases
                 * the connection so no doubtful data reaches the user. */
                if (n > (int)used) v42lapm_rx_consume(&e->l.lapm, n - (int)used);
                e->reason_override = V42EC_R_COMPRESSION;
                v42lapm_release(&e->l.lapm);
                break;
            }
            if (used == 0 && out == 0) break;
        } else {
            size_t c = (size_t)n < room ? (size_t)n : room;
            if (c == 0) break;
            ring_put(&e->rxr, p, c);
            v42lapm_rx_consume(&e->l.lapm, (int)c);
        }
    }
}

static void mnp_sync(v42ec_t *e);

static void sync(v42ec_t *e)
{
    const uint8_t *p;

    if (e->protocol == V42EC_PROTO_MNP) { mnp_sync(e); return; }

    if (e->state == V42EC_DETECT || e->state == V42EC_ASYNC || e->state == V42EC_DISCONNECTED)
        return;
    v42hdlc_rx_set_fcs(&e->hrx, e->l.lapm.rx_fcs_mode);

    if (e->l.lapm.established != e->seen_established) {
        /* L-ESTABLISH: C-INIT both compression functions (V.42 bis 5.2, 5.3;
         * V.44 7.5) and
         * drop whatever the detection phase left in the receive buffer */
        e->seen_established = e->l.lapm.established;
        e->seen_resets = e->l.lapm.resets;
        comp_init(e);
        ring_reset(&e->rxr);
        e->brk_have = 0; e->brk_tx_pend = 0;
        e->t_est = -1;
    }
    if (e->l.lapm.resets != e->seen_resets) {
        /* destructive break (8.13, V.42 bis 5.6; for V.44 by the same rule,
         * see v42/README.md): data in both directions is
         * discarded and the compression functions start again */
        e->seen_resets = e->l.lapm.resets;
        ring_reset(&e->txr); ring_reset(&e->rxr);
        comp_init(e);
        e->brk_tx_pend = 0; e->brk_have = 0;
    }

    switch (e->l.lapm.state) {
    case V42LAPM_CONNECTED:    e->state = V42EC_CONNECTED; break;
    case V42LAPM_RELEASING:    e->state = V42EC_RELEASING; break;
    case V42LAPM_DISCONNECTED:
        finish(e, e->reason_override ? e->reason_override : e->l.lapm.reason);
        return;
    default:                   e->state = V42EC_ESTABLISH; break;
    }
    if (e->state != V42EC_CONNECTED) return;

    drain_rx(e);
    if (e->l.lapm.state != V42LAPM_CONNECTED) { sync(e); return; }

    /* Own-receiver busy (8.4.7) is left to LAPM, which declares it when a
     * frame arrives and the previous one could not be moved out of its
     * hold buffer above. Declaring it earlier from the buffer fill would
     * only make it discard frames that still fit. */

    /* a break is handed up once everything received before it is in the
     * user's buffer */
    if (!e->brk_have && v42lapm_rx_avail(&e->l.lapm, &p) == 0
        && !dec_pending(e)
        && v42lapm_get_break(&e->l.lapm, &e->brk_opt, &e->brk_len)) {
        e->brk_have = 1;
        e->brk_after = e->brk_opt == V42_BRK_NONDESTRUCTIVE_INSEQ ? e->rxr.len : 0;
    }
}

/* -------------------------------------------------------------------- MNP */
/* MNP on: is this body an LR (complete, its FCS already checked)? An LAPM
 * frame cannot be one: its second octet is a control field */
static int is_lr(const uint8_t *f, int len)
{
    mnp_lr_t lr;
    return mnp_lr_parse(f, len, &lr) != -1;
}

/* in LAPM's establishment, an LR still leads to MNP: answerer, no ODP,
 * LAPM has taken nothing (V.42 7.2.1.3: flags first, then the frame) */
static int lr_still_taken(const v42ec_t *e)
{
    return e->cfg.mnp.enable && e->sub == 0 && e->state == V42EC_ESTABLISH
        && e->protocol == V42EC_PROTO_LAPM && e->l.lapm.state == V42LAPM_IDLE
        && e->l.lapm.established == 0;
}

/* MNP on, detection: the start-stop hunter is inside a frame that begins
 * as an LR does, its LI (2..254) and type 1 (A.6.2; is_lr's first test),
 * so T400 is held for it. A caller that sends no LR (typed text, SYN DLE
 * STX included) gets T400 as with MNP off. */
static int lr_partly_in(const v42ec_t *e)
{
    const mnp2_rx_t *h = &e->hunt2;
    return e->cfg.mnp.enable && e->sub == 0 && e->state == V42EC_DETECT && mnp2_rx_in_frame(h)
        && h->len >= 2 && h->buf[0] >= 2 && h->buf[0] <= 254 && h->buf[1] == 1;
}

/* Detection picks MNP: the link starts in LAPM's place with the caller's
 * LR (f is the hunter's or the HDLC receiver's, outside the union) and
 * answers in its framing fr */
static void mnp_pick(v42ec_t *e, const uint8_t *f, int len, int fr)
{
    e->protocol = V42EC_PROTO_MNP;
    e->state = V42EC_ESTABLISH;
    e->t400 = e->t_est = -1;
    e->want_hdlc = 0;
    e->adp_strip = 0;
    e->seen_resets = 0;
    e->mnp_brk = 0;
    ring_reset(&e->rxr);                 /* the LR's characters are not the user's */
    mnp_init(&e->l.mnp, &e->cfg.mnp.link);
    if (e->tx_bps > 0) mnp_set_rates(&e->l.mnp, e->tx_bps, e->rx_bps);    /* v42ec_set_rates */
    v42hdlc_rx_set_fcs(&e->hrx, V42HDLC_FCS16);
    if (e->hst_call) {
        /* the HST profile: our 0xC0 offer, from the pump in ANS_HST */
        int st;
        hst_link_t *L = e->link_get(e->link_ctx, &st);
        if (st == HST_LINK_ON && L) mnp_hst_offer(&e->l.mnp, L);
        /* kept for the reset at this call's CONNECTED: the LI and its LI
         * octets, at most V42EC_LR_CAP (is_lr: an LI of at most 254) */
        memcpy(e->lr_cap, f, (size_t)f[0] + 1);
        e->lr_cap_len = f[0] + 1; e->lr_cap_fr = fr;
    }
    /* a refusal: LD, then DISCONNECTED */
    if (mnp_accept_lr(&e->l.mnp, f, len, fr) == 0 && e->hst_call)
        e->agreed_due = 1;
    mnp_sync(e);
}

/* follow what the MNP link has done: resets, state, the LT it holds,
 * busy, LNs */
static void mnp_sync(v42ec_t *e)
{
    mnp_t *m = &e->l.mnp;
    const uint8_t *p;
    int code;

    if (e->state == V42EC_DISCONNECTED) return;
    if (m->resets != e->seen_resets) {
        /* a destructive break reset the link (Tables 4 and 5): data in both
         * directions is discarded, unless it is our own break's LNA, whose
         * data was discarded when it was asked for and what the user wrote
         * since goes now (mnp.h, Breaks). One frame resets at most once,
         * and this runs after every frame. */
        e->seen_resets = m->resets;
        if (e->mnp_brk) e->mnp_brk = 0;
        else {
            ring_reset(&e->txr); ring_reset(&e->rxr);
            e->brk_tx_pend = 0; e->brk_have = 0;
        }
    }
    switch (m->state) {
    case MNP_S_CONNECTED:    e->state = V42EC_CONNECTED; e->lr_cap_len = 0; break;
    case MNP_S_DISCONNECTED: finish(e, m->reason); return;
    default:                 e->state = V42EC_ESTABLISH; return;
    }
    /* the link holds one LT: into the ring before the next frame */
    for (;;) {
        int n = mnp_rx_avail(m, &p);
        size_t c = (size_t)n < ring_free(&e->rxr) ? (size_t)n : ring_free(&e->rxr);
        if (c == 0) break;
        ring_put(&e->rxr, p, c);
        mnp_rx_consume(m, (int)c);
    }
    /* our credit is k: busy while the ring cannot take k LTs */
    mnp_set_busy(m, ring_free(&e->rxr) < (size_t)m->k * (size_t)m->n401);
    /* a break is handed up once everything received before it is in the
     * user's buffer; LN types that are no break are dropped */
    while (!e->brk_have && mnp_rx_avail(m, &p) == 0 && mnp_get_ln(m, &code)) {
        int opt = mnp_brk_of_ln(code);
        if (opt < 0) continue;
        e->brk_have = 1; e->brk_opt = opt; e->brk_len = 0;
        e->brk_after = opt == V42_BRK_NONDESTRUCTIVE_INSEQ ? e->rxr.len : 0;
    }
}

/* queued user data into at most one new LT, and an in-sequence break
 * once the data written before it has gone */
static void feed_mnp(v42ec_t *e)
{
    mnp_t *m = &e->l.mnp;
    int room = mnp_can_send(m);

    if (room > 0) {
        uint8_t info[MNP_MAX_N401];
        size_t n, limit = e->txr.len;
        if (m->framing == 3 && room > HDLC_MAX_LT) room = HDLC_MAX_LT;
        if (e->brk_tx_pend && limit > e->brk_tx_after) limit = e->brk_tx_after;
        if (limit > (size_t)room) limit = (size_t)room;
        n = ring_get(&e->txr, info, limit);
        if (e->brk_tx_pend) e->brk_tx_after -= n;
        if (n > 0) (void)mnp_send(m, info, (int)n);
    }
    if (e->brk_tx_pend && e->brk_tx_after == 0 && mnp_send_ln(m, MNP_LN_INSEQ) == 0)
        e->brk_tx_pend = 0;
}

/* the link's next frame into the framer of its framing, switching the
 * transmitter between HDLC (flags first) and start-stop at a frame's
 * boundary */
static void mnp_next(v42ec_t *e)
{
    mnp_t *m = &e->l.mnp;
    uint8_t f[MNP_MAX_BODY];
    int n;
    if (e->state == V42EC_CONNECTED) feed_mnp(e);
    n = mnp_tx_frame(m, f);
    if (n <= 0) return;
    if (m->tx_framing == 3) {
        if (!e->hdlc_tx_on) { e->hdlc_tx_on = 1; v42hdlc_tx_flags(&e->htx, 2); }
        (void)v42hdlc_tx_frame(&e->htx, f, n, 0);
    } else {
        e->hdlc_tx_on = 0;
        (void)mnp2_tx_frame(&m->tx2, f, n);
    }
    if (m->rev_lna_out) {
        /* an LNA granting a reversal: its closing flag is waited for */
        m->rev_lna_out = 0;
        m->rev_wait = 1;
        m->rev_frame0 = e->htx.frames;
    }
    mnp_sync(e);
}

/* MNP's line bit: flags between HDLC frames, mark between start-stop
 * ones; no new frame while the line is stopped */
static int mnp_tx_bit(v42ec_t *e)
{
    mnp_t *m = &e->l.mnp;
    if (e->hdlc_tx_on) {
        if (!e->stopped && v42hdlc_tx_need_frame(&e->htx)) mnp_next(e);
        if (e->hdlc_tx_on) return v42hdlc_tx_bit(&e->htx);
    }
    if (v42async_tx_idle(&e->atx)) {
        int c = mnp2_tx_octet(&m->tx2);
        if (c < 0 && !e->stopped) {
            mnp_next(e);
            if (e->hdlc_tx_on) return v42hdlc_tx_bit(&e->htx);
            c = mnp2_tx_octet(&m->tx2);
        }
        if (c >= 0) v42async_tx_char(&e->atx, (uint8_t)c);
    }
    return v42async_tx_bit(&e->atx);
}

/* MNP's receiver: while the LRs are exchanged both framings are heard,
 * then the agreed one; a dropped frame counts in the framing in use only */
static void mnp_rx_bit(v42ec_t *e, int bit)
{
    mnp_t *m = &e->l.mnp;
    int both = m->state == MNP_S_CONFIRMING;
    int fr = m->state == MNP_S_CONNECTED ? m->framing : m->est_framing;
    int r;

    if (e->state == V42EC_DISCONNECTED) return;
    if (both || m->framing == 3) {
        r = v42hdlc_rx_bit(&e->hrx, bit);
        if (r > 0) { mnp_rx_frame(m, e->hrx.buf, r); mnp_sync(e); }
        else if ((r == V42HDLC_RX_BAD_FCS || r == V42HDLC_RX_LONG) && fr == 3) { mnp_rx_bad(m); mnp_sync(e); }
    }
    if ((both || m->framing == 2) && e->state != V42EC_DISCONNECTED) {
        int c = v42async_rx_bit(&e->det_rx.rx, bit);
        if (c >= 0) {
            r = mnp2_rx_octet(&m->rx2, (uint8_t)c);
            if (r == MNP2_RX_FRAME) { mnp_rx_frame(m, m->rx2.buf, m->rx2.len); mnp_sync(e); }
            else if (r == MNP2_RX_BAD && fr == 2) { mnp_rx_bad(m); mnp_sync(e); }
        }
    }
}

/* ----------------------------------------------------------- the HST link */
static void rx_line(v42ec_t *e, int bit);

/* HST, after each bit we transmit: ep_idle (between frames, at an octet
 * boundary), and rev_lna_sent once the LNA granting a reversal has gone
 * with its closing flag (the frame count passed it, and the flag's last
 * bit is out or the next frame has begun on it) */
static void hst_tx_after(v42ec_t *e, hst_link_t *L)
{
    mnp_t *m = &e->l.mnp;
    int idle = e->hdlc_tx_on ? !e->htx.in_frame && e->htx.nbits == 0
                             : !mnp2_tx_busy(&m->tx2) && v42async_tx_idle(&e->atx);
    L->ep_idle = idle;
    if (m->rev_wait) {
        unsigned long d = e->htx.frames - m->rev_frame0;
        int done = e->hdlc_tx_on ? d >= 2 || (d == 1 && (e->htx.nbits == 0 || e->htx.in_frame)) : idle;
        if (done) {
            m->rev_wait = 0;
            m->rev_grant = 0;
            L->rev_lna_sent = 1;
            L->rev_code = m->rev_code;
        }
    }
}

/* The modem's decision in *state (HST_LINK_NONE unlinked) and, on an HST
 * call, the mailbox: the pre-connect ring drained into the receive path,
 * the pump's rates taken for T401 when they change, the LRs' agreement
 * told, the HST profile's exchange (mnp_hst_poll; ms the tick's, 0 at a
 * bit). Called at the top of v42ec_tx_bit, v42ec_rx_bit and v42ec_tick
 * (hst_link.h). NONE before any ON is final for the call: get is not asked
 * again until the next init or reset. */
static hst_link_t *link_poll(v42ec_t *e, int *state, unsigned ms)
{
    hst_link_t *L;
    int b;

    *state = HST_LINK_NONE;
    if (!e->link_get || e->link_none) return NULL;
    L = e->link_get(e->link_ctx, state);
    if (*state != HST_LINK_ON || !L) {
        /* not (or no longer) in ANS_HST: nothing of the pump's is ours */
        e->lr_cap_len = 0;
        e->agreed_due = 0;
        if (!e->hst_call && *state != HST_LINK_UNDECIDED) {
            *state = HST_LINK_NONE;
            e->link_none = 1;
        }
        return NULL;
    }
    if (!e->hst_call) {
        e->hst_call = 1;                    /* HST to the call's end (D19) */
        e->t400 = -1;
    }
    while ((b = hst_pre_get(L)) >= 0) rx_line(e, b);
    e->link_r = L->pre_r;
    if (L->tx_bps > 0 && L->rx_bps > 0 && (L->tx_bps != e->tx_bps || L->rx_bps != e->rx_bps))
        (void)v42ec_set_rates(e, L->tx_bps, L->rx_bps);
    if (e->protocol == V42EC_PROTO_MNP) {
        mnp_t *m = &e->l.mnp;
        if (e->agreed_due) {
            /* the LRs exchanged: agreed_rates 0 is plain MNP, the pump
             * staying at its opening rate */
            e->agreed_due = 0;
            L->agreed = 1;
            L->agreed_rates = m->hst ? m->hst_rates : 0;
            L->back300 = m->hst ? m->back300 : 0;
        }
        mnp_hst_poll(m, L, ms, e->txr.len);
    }
    return L;
}

/* an HST call: an LR may still start MNP (detection, or LAPM's
 * establishment before LAPM has taken anything) */
static int hst_hunting(const v42ec_t *e)
{
    return e->protocol != V42EC_PROTO_MNP
        && (e->state == V42EC_DETECT
            || (e->state == V42EC_ESTABLISH && e->l.lapm.established == 0));
}

/* An HST call, T400's place (decision 19): from the tick that sees our
 * training out, V42EC_HST_WINDOW_MS for an LR, then NO_EC (no start-stop,
 * whatever cfg.fallback says). L NULL (the modem has left ANS_HST before
 * the window began): it begins at once. A window that has run out stays
 * at 0, so a reset cannot start it again. 1 when it has ended the
 * endpoint. */
static int hst_window(v42ec_t *e, const hst_link_t *L, unsigned ms)
{
    if (!hst_hunting(e)) return 0;
    if (e->hst_win < 0) {
        if (!L || L->trained) e->hst_win = V42EC_HST_WINDOW_MS;
        return 0;
    }
    e->hst_win -= (int)ms;
    if (e->hst_win > 0) return 0;
    e->hst_win = 0;
    if (e->protocol == V42EC_PROTO_LAPM) v42lapm_abort(&e->l.lapm, V42LAPM_R_PROTOCOL);
    finish(e, V42EC_R_NO_EC);
    return 1;
}

void v42ec_set_link(v42ec_t *e, hst_link_get_fn get, void *ctx)
{
    if (!e->cfg.mnp.enable) get = NULL;     /* MNP off or an originator */
    e->link_get = get;
    e->link_ctx = get ? ctx : NULL;
    /* no HST call in progress for this link yet */
    e->hst_call = 0;
    e->link_none = 0;
    e->hst_win = -1;
    e->link_r = 0;
    e->lr_cap_len = 0;
    e->agreed_due = 0;
}

int v42ec_linked(const v42ec_t *e) { return e->link_get != NULL; }

void v42ec_reset(v42ec_t *e, const v42ec_cfg_t *cfg)
{
    hst_link_get_fn get = e->link_get;
    void *ctx = e->link_ctx;
    int keep = 0, win = e->hst_win, n = e->lr_cap_len, fr = e->lr_cap_fr;
    unsigned r = e->link_r;
    uint8_t lr[V42EC_LR_CAP];

    /* the reset at this HST call's CONNECTED: ON now, nothing sent since
     * the last start, the same mailbox (its ring read count not gone back) */
    if (get && e->hst_call && !e->tx_seen) {
        int st;
        hst_link_t *L = get(ctx, &st);
        keep = st == HST_LINK_ON && L && L->pre_r - r < 0x80000000u;
    }
    if (keep && n > 0) memcpy(lr, e->lr_cap, (size_t)n);
    v42ec_init(e, cfg);
    if (!get) return;
    v42ec_set_link(e, get, ctx);
    if (!e->link_get || !keep) return;      /* MNP off: unlinked; or a new call */
    e->hst_call = 1;
    e->t400 = -1;
    e->hst_win = win;
    e->link_r = r;
    if (n > 0) mnp_pick(e, lr, n, fr);      /* answered again; kept again */
}

/* ------------------------------------------------------------ transmitter */
/* Turn queued user data into at most one new I frame. */
static void feed_lapm(v42ec_t *e)
{
    int room = v42lapm_can_send(&e->l.lapm);

    if (room > 0) {
        uint8_t info[V42_MAX_N401];
        size_t n = 0, limit = e->txr.len;

        if (e->brk_tx_pend && limit > e->brk_tx_after) limit = e->brk_tx_after;
        if (e->comp_tx) {
            for (;;) {
                size_t avail, c;
                const uint8_t *p = ring_peek(&e->txr, &avail);
                if (avail > limit) avail = limit;
                c = ops(e)->encode(e, p, avail, info, (size_t)room, &n);
                if (c) e->enc_dirty = 1;
                ring_drop(&e->txr, c);
                limit -= c;
                if (e->brk_tx_pend) e->brk_tx_after -= c;
                if (c == 0 || n >= (size_t)room) break;
            }
            /* nothing more to send for now: C-FLUSH so the far end gets
             * every character already accepted (V.42 bis 7.9, V.44 7.13) */
            if (limit == 0 && e->enc_dirty && n < (size_t)room
                && ops(e)->flush(e, info, (size_t)room, &n))
                e->enc_dirty = 0;
        } else {
            if (limit > (size_t)room) limit = (size_t)room;
            n = ring_get(&e->txr, info, limit);
            if (e->brk_tx_pend) e->brk_tx_after -= n;
        }
        if (n > 0) v42lapm_send(&e->l.lapm, info, (int)n);
    }
    if (e->brk_tx_pend && e->brk_tx_after == 0 && !e->enc_dirty
        && !enc_pending(e)
        && v42lapm_send_break(&e->l.lapm, e->brk_tx_opt, e->brk_tx_len) == 0)
        e->brk_tx_pend = 0;
}

/* before a line bit, when the originator still waits for its first frame
 * or the transmitter could take the next one */
static void hdlc_next(v42ec_t *e)
{
    if (e->orig_wait > 0 && (--e->orig_wait == 0 || e->hrx.flag_run >= FLAGS_SEEN)) {
        e->orig_wait = 0;
        v42lapm_start(&e->l.lapm);
    }
    if (!e->stopped && v42hdlc_tx_need_frame(&e->htx)) {
        uint8_t f[V42HDLC_MAX_PAYLOAD];
        int n, fcs32 = 0;
        if (e->state == V42EC_CONNECTED) feed_lapm(e);
        n = v42lapm_tx_frame(&e->l.lapm, f, &fcs32);
        if (n > 0) v42hdlc_tx_frame(&e->htx, f, n, fcs32);
    }
}

static int hdlc_bit(v42ec_t *e)
{
    if (e->orig_wait > 0 || v42hdlc_tx_need_frame(&e->htx)) hdlc_next(e);
    return v42hdlc_tx_bit(&e->htx);
}

int v42ec_tx_bit(v42ec_t *e)
{
    hst_link_t *L = NULL;
    e->tx_seen = 1;
    if (e->link_get) {
        int ls;
        L = link_poll(e, &ls, 0);           /* an LR in the ring is answered now */
    }
    if (e->protocol == V42EC_PROTO_MNP) {   /* also after release: the LD */
        int bit = mnp_tx_bit(e);
        if (L) hst_tx_after(e, L);
        return bit;
    }
    if (e->hdlc_tx_on) return hdlc_bit(e);      /* also after release: the last UA or DM */

    if (e->state == V42EC_ASYNC) {
        uint8_t c;
        if (v42async_tx_idle(&e->atx) && ring_get(&e->txr, &c, 1))
            v42async_tx_char(&e->atx, c);
        return v42async_tx_bit(&e->atx);
    }
    if (e->state == V42EC_DISCONNECTED) return 1;

    if (e->cfg.originator) {
        /* 7.2.1.2: ODP until the ADP is seen or T400 runs out; patterns are
         * always completed */
        if (e->state == V42EC_DETECT && v42det_tx_boundary(&e->det_tx)) {
            if (e->want_hdlc) { start_protocol(e); return hdlc_bit(e); }
            if (e->want_async) {
                enter_async(e);
                return 1;
            }
        }
        return v42det_tx_bit(&e->det_tx);
    }
    /* answerer, 7.2.1.3: mark, then the ADP once the ODP has been seen */
    if (e->sub == 0) {
        if (e->want_hdlc) { e->hdlc_tx_on = 1; return hdlc_bit(e); }
        return 1;
    }
    if (e->want_hdlc && e->det_tx.patterns >= ADP_MIN && v42det_tx_boundary(&e->det_tx)) {
        e->hdlc_tx_on = 1;
        return hdlc_bit(e);
    }
    return v42det_tx_bit(&e->det_tx);
}

void v42ec_tx_octets(v42ec_t *e, uint8_t *buf, size_t n)
{
    size_t i; int b;
    for (i = 0; i < n; i++) {
        unsigned o = 0;
        for (b = 0; b < 8; b++) o |= (unsigned)v42ec_tx_bit(e) << b;
        buf[i] = (uint8_t)o;
    }
}

/* --------------------------------------------------------------- receiver */
static void async_char(v42ec_t *e, int c)
{
    uint8_t ch = (uint8_t)c;
    if (c == V42ASYNC_FRAMING) { e->async_framing++; e->adp_strip = 0; return; }
    if (c < 0) return;
    if (e->adp_strip) {
        /* the answerer is still repeating "E NUL": not user data */
        int gap = e->det_rx.rx.gap;
        int want = e->adp_strip == 1 ? 'E' : 0;
        if (c == want && gap >= 8 && gap <= 16) { e->adp_strip ^= 3; return; }
        e->adp_strip = 0;
    }
    if (ring_put(&e->rxr, &ch, 1) == 0) e->async_overruns++;
}

/* one line bit into the receive path: the line's (v42ec_rx_bit) or the
 * pre-connect ring's (link_poll) */
static void rx_line(v42ec_t *e, int bit)
{
    int r;

    bit = bit ? 1 : 0;
    if (e->protocol == V42EC_PROTO_MNP) { mnp_rx_bit(e, bit); return; }
    if (e->state != V42EC_DETECT && e->state != V42EC_ASYNC && e->state != V42EC_DISCONNECTED) {
        /* HDLC alone: establishing, connected, releasing */
        r = v42hdlc_rx_bit(&e->hrx, bit);
        if (r > 0) {
            if (lr_still_taken(e) && is_lr(e->hrx.buf, r)) { mnp_pick(e, e->hrx.buf, r, 3); return; }
            v42lapm_rx_frame(&e->l.lapm, e->hrx.buf, r, e->hrx.last_fcs32);
            sync(e);
        }
        return;
    }
    switch (e->state) {
    case V42EC_DISCONNECTED:
        return;
    case V42EC_ASYNC:
        async_char(e, v42async_rx_bit(&e->det_rx.rx, bit));
        return;
    case V42EC_DETECT:
        r = v42det_rx_bit(&e->det_rx, bit);
        /* keep what arrives: it is the user's if we end up in start-stop
         * mode (Appendix I.3 b) */
        if (e->det_rx.last_char >= 0) async_char(e, e->det_rx.last_char);
        if (e->cfg.originator) {
            if (e->want_hdlc || e->want_async) return;
            /* an answerer without the detection phase (7.2.1) sends flags
             * instead of the ADP: they too say it does LAPM */
            v42hdlc_rx_bit(&e->hrx, bit);
            if (r == V42DET_ADP_V42 || e->hrx.flag_run >= FLAGS_SEEN) e->want_hdlc = 1;
            else if (r == V42DET_ADP_NOEC) {
                e->want_async = 1;
                ring_reset(&e->rxr);
                e->adp_strip = 1;
            }
            return;
        }
        if (r == V42DET_ODP && e->sub == 0) {
            e->sub = 1;
            e->protocol = V42EC_PROTO_LAPM;     /* the ODP wins over any LR */
            e->t400 = -1;
            ring_reset(&e->rxr);
            if (e->cfg.establish_ms > 0) e->t_est = e->cfg.establish_ms;
        }
        /* MNP on, no ODP: an LR in start-stop characters (framing mode 2) */
        if (e->cfg.mnp.enable && e->sub == 0 && e->det_rx.last_char >= 0
            && mnp2_rx_octet(&e->hunt2, (uint8_t)e->det_rx.last_char) == MNP2_RX_FRAME
            && is_lr(e->hunt2.buf, e->hunt2.len)) {
            mnp_pick(e, e->hunt2.buf, e->hunt2.len, 2);
            return;
        }
        /* T400 was held for a frame that has ended without being an LR */
        if (e->t400_held && e->t400 >= 0 && !lr_partly_in(e)) {
            enter_async(e);
            return;
        }
        /* flags or a frame start the protocol phase, with or without ODP;
         * a frame with no opening flag (after a run of ones, v42_hdlc.h)
         * only with an address LAPM takes (0x01, 0x03): a caller without
         * error control sends characters after idle mark */
        {
            unsigned long rs = e->hrx.resynced;
            r = v42hdlc_rx_bit(&e->hrx, bit);
            /* MNP on, no ODP: an LR in HDLC (mode 3), with or without its
             * opening flag (its FCS and its layout are the test) */
            if (r > 0 && e->cfg.mnp.enable && e->sub == 0 && is_lr(e->hrx.buf, r)) {
                mnp_pick(e, e->hrx.buf, r, 3);
                return;
            }
            if (r > 0 && e->hrx.resynced != rs && e->hrx.buf[0] != 0x01 && e->hrx.buf[0] != 0x03) r = 0;
        }
        if (r > 0 || e->hrx.flag_run >= FLAGS_SEEN) {
            e->want_hdlc = 1;
            start_protocol(e);
            if (r > 0) {
                v42lapm_rx_frame(&e->l.lapm, e->hrx.buf, r, e->hrx.last_fcs32);
                sync(e);
            }
        }
        return;
    }
}

void v42ec_rx_bit(v42ec_t *e, int bit)
{
    int ls;
    if (e->link_get) (void)link_poll(e, &ls, 0);   /* the ring's bits came first */
    rx_line(e, bit);
}

void v42ec_rx_octets(v42ec_t *e, const uint8_t *buf, size_t n)
{
    size_t i; int b;
    for (i = 0; i < n; i++)
        for (b = 0; b < 8; b++) v42ec_rx_bit(e, (buf[i] >> b) & 1);
}

/* ------------------------------------------------------------------- time */
void v42ec_line_stopped(v42ec_t *e)
{
    e->stopped = 1;
    e->stop_cut = e->hdlc_tx_on && e->htx.in_frame;
    e->stop_frames = e->htx.frames;
    if (e->protocol == V42EC_PROTO_MNP) mnp_line_stopped(&e->l.mnp);
}

void v42ec_line_restored(v42ec_t *e)
{
    if (e->stopped) {
        e->stopped = 0;
        if (e->hdlc_tx_on) {
            /* the frame the stop cut, if it is still going: abort it (an
             * I frame comes again through LAPM's poll); whole flags first */
            if (e->stop_cut && e->htx.in_frame && e->htx.frames == e->stop_frames)
                v42hdlc_tx_abort(&e->htx);
            v42hdlc_tx_flags(&e->htx, 2);
        }
    }
    if (e->protocol == V42EC_PROTO_MNP) mnp_line_restored(&e->l.mnp);
    else v42lapm_line_restored(&e->l.lapm);
}

void v42ec_tick(v42ec_t *e, unsigned ms)
{
    int ls;
    hst_link_t *L = link_poll(e, &ls, ms);

    if (e->state == V42EC_DISCONNECTED || e->state == V42EC_ASYNC) return;
    if (ms > 1000000u) ms = 1000000u;       /* as v42lapm_tick: past every timer, and an int */

    /* linked: T400 is held while the modem has not decided and counts
     * once it has decided against HST (HST_LINK_NONE, as unlinked); on an
     * HST call the window takes its place for the whole call */
    if (e->hst_call && hst_window(e, L, ms)) return;
    if (e->t400 >= 0 && !e->hst_call && ls != HST_LINK_UNDECIDED) {
        e->t400 -= (int)ms;
        if (e->t400 <= 0) {
            /* 7.2.1.2, 7.2.1.3: no error-correcting peer; MNP on, an LR
             * still arriving in start-stop is waited for, once and at most
             * V42EC_T400_LR_HOLD_MS (v42.h) */
            e->t400 = -1;
            if (!e->t400_held && lr_partly_in(e)) {
                e->t400_held = 1;
                e->t400 = V42EC_T400_LR_HOLD_MS;
                return;
            }
            if (e->cfg.originator) e->want_async = 1;   /* at the pattern boundary */
            else enter_async(e);
            return;
        }
    }
    if (e->t_est >= 0 && !(e->hst_call && hst_hunting(e))) {   /* HST: the window's */
        e->t_est -= (int)ms;
        if (e->t_est <= 0) {
            v42lapm_abort(&e->l.lapm, V42LAPM_R_PROTOCOL);
            finish(e, V42EC_R_NO_SABME);
            return;
        }
    }
    if (e->protocol == V42EC_PROTO_MNP) {
        mnp_tick(&e->l.mnp, ms);
        mnp_sync(e);
    } else if (e->state != V42EC_DETECT) {
        v42lapm_tick(&e->l.lapm, ms);
        sync(e);
    }
}

int v42ec_set_rates(v42ec_t *e, int tx_bps, int rx_bps)
{
    if (tx_bps <= 0 || rx_bps <= 0) return -1;
    e->tx_bps = tx_bps; e->rx_bps = rx_bps;
    if (e->protocol == V42EC_PROTO_MNP) mnp_set_rates(&e->l.mnp, tx_bps, rx_bps);
    return 0;
}

/* -------------------------------------------------------------- user side */
size_t v42ec_write_space(const v42ec_t *e)
{
    if (e->state == V42EC_RELEASING || e->state == V42EC_DISCONNECTED) return 0;
    return ring_free(&e->txr);
}

size_t v42ec_write(v42ec_t *e, const uint8_t *data, size_t len)
{
    if (e->state == V42EC_RELEASING || e->state == V42EC_DISCONNECTED) return 0;
    return ring_put(&e->txr, data, len);
}

size_t v42ec_read_avail(const v42ec_t *e)
{
    if (e->state == V42EC_DETECT || e->state == V42EC_ESTABLISH) return 0;
    if (e->brk_have && e->brk_after < e->rxr.len) return e->brk_after;
    return e->rxr.len;
}

size_t v42ec_read(v42ec_t *e, uint8_t *data, size_t max)
{
    size_t n = v42ec_read_avail(e);
    if (n > max) n = max;
    n = ring_get(&e->rxr, data, n);
    if (e->brk_have) e->brk_after -= n < e->brk_after ? n : e->brk_after;
    if (n) sync(e);                             /* room again: unblock the receiver */
    return n;
}

int v42ec_tx_busy(const v42ec_t *e)
{
    if (e->protocol == V42EC_PROTO_MNP) {
        /* an LD the link owes goes out at the next bit */
        if (e->l.mnp.ld_due) return 1;
        if (e->hdlc_tx_on) return v42hdlc_tx_busy(&e->htx);
        return mnp2_tx_busy(&e->l.mnp.tx2) || !v42async_tx_idle(&e->atx);
    }
    return e->hdlc_tx_on && v42hdlc_tx_busy(&e->htx);
}

int v42ec_tx_idle(const v42ec_t *e)
{
    if (e->txr.len != 0) return 0;
    if (e->state == V42EC_ASYNC) return v42async_tx_idle(&e->atx);
    if (e->protocol == V42EC_PROTO_MNP) return mnp_unacked(&e->l.mnp) == 0 && !e->brk_tx_pend;
    if (e->enc_dirty || enc_pending(e)) return 0;
    return v42lapm_unacked(&e->l.lapm) == 0 && !e->brk_tx_pend;
}

void v42ec_release(v42ec_t *e)
{
    switch (e->state) {
    case V42EC_DETECT:
    case V42EC_ASYNC:
        finish(e, V42LAPM_R_LOCAL);
        break;
    case V42EC_ESTABLISH:
    case V42EC_CONNECTED:
        if (e->protocol == V42EC_PROTO_MNP) mnp_release(&e->l.mnp);    /* LD 255 */
        else v42lapm_release(&e->l.lapm);
        sync(e);
        break;
    default:
        break;
    }
}

void v42ec_abort(v42ec_t *e)
{
    if (e->state == V42EC_DISCONNECTED) return;
    if (e->protocol == V42EC_PROTO_MNP) mnp_abort(&e->l.mnp);   /* only an LD owed still goes */
    else v42lapm_abort(&e->l.lapm, V42LAPM_R_LOCAL);   /* only a UA or DM owed still goes */
    e->orig_wait = 0;
    finish(e, V42LAPM_R_LOCAL);
}

int v42ec_send_break(v42ec_t *e, int opt, int len10ms)
{
    if (e->state != V42EC_CONNECTED || e->brk_tx_pend) return -1;
    if (e->protocol == V42EC_PROTO_MNP) {
        mnp_t *m = &e->l.mnp;
        int ln = mnp_ln_of_brk(opt);
        if (ln < 0 || m->ln_code >= 0 || m->hst) return -1;   /* HST: LN codes are attention */
        if (ln == MNP_LN_INSEQ) {
            /* after the data already written; feed_mnp() hands it on */
            e->brk_tx_pend = 1; e->brk_tx_opt = opt; e->brk_tx_len = len10ms;
            e->brk_tx_after = e->txr.len;
            return 0;
        }
        if (mnp_send_ln(m, ln) != 0) return -1;
        if (ln == MNP_LN_DESTRUCTIVE) {
            /* Table 4: what has not gone and what is not delivered is
             * discarded now; what the user writes from here is held until
             * the LNA, whose reset does not flush it */
            ring_reset(&e->txr); ring_reset(&e->rxr);
            e->brk_have = 0;
            e->mnp_brk = 1;
        }
        mnp_sync(e);
        return 0;
    }
    if (opt == V42_BRK_NONDESTRUCTIVE_INSEQ) {
        /* goes after the data already written; feed_lapm() hands it on */
        if (e->l.lapm.brk_tx != 0) return -1;
        e->brk_tx_pend = 1; e->brk_tx_opt = opt; e->brk_tx_len = len10ms;
        e->brk_tx_after = e->txr.len;
        return 0;
    }
    if (v42lapm_send_break(&e->l.lapm, opt, len10ms) != 0) return -1;
    sync(e);
    return 0;
}

int v42ec_get_break(v42ec_t *e, int *opt, int *len10ms)
{
    if (!e->brk_have || e->brk_after != 0) return 0;
    e->brk_have = 0;
    *opt = e->brk_opt; *len10ms = e->brk_len;
    sync(e);
    return 1;
}

int v42ec_state(const v42ec_t *e) { return e->state; }

void v42ec_get_status(const v42ec_t *e, v42ec_status_t *st)
{
    const v42lapm_t *l = &e->l.lapm;
    memset(st, 0, sizeof *st);
    st->state = e->state; st->reason = e->reason;
    st->protocol = e->protocol;
    st->tx_queued = e->txr.len;
    st->rx_avail = v42ec_read_avail(e);
    st->rx_frames = e->hrx.good;
    st->rx_frames_bad = e->hrx.bad_fcs + e->hrx.runts + e->hrx.aborts
                      + e->hrx.too_long + e->hrx.misaligned;
    st->async_overruns = e->async_overruns; st->async_framing = e->async_framing;
    if (e->protocol == V42EC_PROTO_MNP) {
        const mnp_t *m = &e->l.mnp;
        st->k_tx = st->k_rx = m->k;
        st->n401_tx = st->n401_rx = m->n401;
        st->unacked = mnp_unacked(m);
        st->rx_frames += m->rx2.frames;
        st->rx_frames_bad += m->rx2.bad;
        st->tx_i = m->tx_lt; st->tx_i_retx = m->tx_retx;
        st->rx_i = m->rx_lt; st->rx_i_dropped = m->rx_dropped;
        st->t401_expiries = m->t401_expiries; st->violations = m->violations;
        st->mnp_framing = m->framing; st->mnp_opt = m->opt;
        st->mnp_streamlined = m->layout == MNP_LAYOUT_STREAM;
        st->hst = m->hst;
        st->hst_rates = m->hst ? m->hst_rates : 0;
        st->hst_back300 = m->hst ? m->back300 : 0;
        st->attn_tx = m->attn_tx; st->attn_rx = m->attn_rx;
        st->rx_unknown = m->rx_unknown; st->rx_unknown_id = m->rx_unknown_id;
        st->peer_ld_reason = m->peer_ld_reason;
        return;
    }
    st->k_tx = l->k_tx; st->k_rx = l->k_rx;
    st->n401_tx = l->n401_tx; st->n401_rx = l->n401_rx;
    if (l->v44_tx || l->v44_rx) {
        st->comp_alg = V42EC_COMP_V44;
        st->comp_tx = l->v44_tx; st->comp_rx = l->v44_rx;
        st->v44_n2_tx = l->v44_n2_tx; st->v44_n7_tx = l->v44_n7_tx; st->v44_n8_tx = l->v44_n8_tx;
        st->v44_n2_rx = l->v44_n2_rx; st->v44_n7_rx = l->v44_n7_rx; st->v44_n8_rx = l->v44_n8_rx;
    } else {
        st->comp_alg = (l->comp_tx || l->comp_rx) ? V42EC_COMP_V42BIS : V42EC_COMP_NONE;
        st->comp_tx = l->comp_tx; st->comp_rx = l->comp_rx;
        st->comp_n2 = l->comp_n2; st->comp_n7 = l->comp_n7;
    }
    st->fcs32 = l->fcs32;
    st->unacked = v42lapm_unacked(l);
    st->tx_i = l->tx_i; st->tx_i_retx = l->tx_i_retx;
    st->rx_i = l->rx_i; st->rx_i_dropped = l->rx_i_dropped;
    st->tx_rej = l->tx_rej; st->rx_rej = l->rx_rej;
    st->t401_expiries = l->t401_expiries; st->violations = l->violations;
    if (e->comp_alg != V42EC_COMP_NONE) {
        if (e->comp_tx) ops(e)->enc_counts(e, &st->comp_in, &st->comp_out);
        if (e->comp_rx) ops(e)->dec_counts(e, &st->decomp_in, &st->decomp_out);
    }
}
