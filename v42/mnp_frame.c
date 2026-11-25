/* MNP framing mode 2 and the frame layouts (mnp_frame.h). */
#include <string.h>
#include "mnp_frame.h"

#define SYN 0x16
#define DLE 0x10
#define STX 0x02
#define ETX 0x03

uint16_t mnp_crc16(uint16_t crc, const uint8_t *p, size_t n)
{
    unsigned c = crc;
    size_t i;
    int b;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (b = 0; b < 8; b++)
            c = (c & 1u) ? (c >> 1) ^ 0xA001u : c >> 1;
    }
    return (uint16_t)c;
}

/* the mode 2 FCS of a body and its ETX, in line order (MNP_FCS2) */
static void fcs2(const uint8_t *body, int len, uint8_t out[2])
{
    static const uint8_t etx = ETX;
    int ffff = MNP_FCS2 == MNP_FCS2_FFFF_LO || MNP_FCS2 == MNP_FCS2_FFFF_HI;
    int hi_first = MNP_FCS2 == MNP_FCS2_ARC_HI || MNP_FCS2 == MNP_FCS2_FFFF_HI;
    uint16_t c = mnp_crc16(ffff ? 0xFFFFu : 0u, body, (size_t)len);
    c = mnp_crc16(c, &etx, 1);
    out[hi_first] = (uint8_t)(c & 0xFFu);
    out[!hi_first] = (uint8_t)(c >> 8);
}

/* ------------------------------------------------------------- mode 2 send */
enum { TX_SYN = 0, TX_DLE1, TX_STX, TX_BODY, TX_DLE2, TX_ETX, TX_FCS0, TX_FCS1 };

void mnp2_tx_init(mnp2_tx_t *t)
{
    memset(t, 0, sizeof *t);
}

int mnp2_tx_frame(mnp2_tx_t *t, const uint8_t *body, int len)
{
    if (t->len != 0 || len < 1 || len > MNP_MAX_BODY) return -1;
    memcpy(t->buf, body, (size_t)len);
    t->len = len;
    t->pos = 0;
    t->step = TX_SYN;
    t->dle_owed = 0;
    fcs2(t->buf, len, t->fcs);
    return 0;
}

int mnp2_tx_busy(const mnp2_tx_t *t)
{
    return t->len != 0;
}

int mnp2_tx_octet(mnp2_tx_t *t)
{
    int c;
    if (t->len == 0) return -1;
    switch (t->step) {
    case TX_SYN:  t->step = TX_DLE1; return SYN;
    case TX_DLE1: t->step = TX_STX;  return DLE;
    case TX_STX:  t->step = TX_BODY; return STX;
    case TX_BODY:
        if (t->dle_owed) {
            t->dle_owed = 0;
            c = DLE;
        } else {
            c = t->buf[t->pos++];
            t->dle_owed = c == DLE;
        }
        if (t->pos == t->len && !t->dle_owed) t->step = TX_DLE2;
        return c;
    case TX_DLE2: t->step = TX_ETX;  return DLE;
    case TX_ETX:  t->step = TX_FCS0; return ETX;
    case TX_FCS0: t->step = TX_FCS1; return t->fcs[0];
    default:
        t->len = 0;
        t->step = TX_SYN;
        return t->fcs[1];
    }
}

/* ---------------------------------------------------------- mode 2 receive */
enum { RX_HUNT = 0, RX_SYN, RX_SYN_DLE, RX_BODY, RX_BODY_DLE, RX_FCS0, RX_FCS1 };

void mnp2_rx_init(mnp2_rx_t *r, int max_len)
{
    memset(r, 0, sizeof *r);
    if (max_len < 1) max_len = 1;
    if (max_len > MNP_MAX_BODY) max_len = MNP_MAX_BODY;
    r->max_len = max_len;
    r->state = RX_HUNT;
}

static int rx_bad(mnp2_rx_t *r)
{
    r->bad++;
    r->state = RX_HUNT;
    r->len = 0;
    return MNP2_RX_BAD;
}

static int rx_take(mnp2_rx_t *r, uint8_t c)
{
    if (r->len >= r->max_len) return rx_bad(r);
    r->buf[r->len++] = c;
    r->state = RX_BODY;
    return MNP2_RX_NONE;
}

int mnp2_rx_in_frame(const mnp2_rx_t *r)
{
    return r->state >= RX_BODY;
}

int mnp2_rx_octet(mnp2_rx_t *r, uint8_t c)
{
    uint8_t want[2];
    switch (r->state) {
    case RX_HUNT:
        if (c == SYN) r->state = RX_SYN;
        return MNP2_RX_NONE;
    case RX_SYN:
        r->state = c == DLE ? RX_SYN_DLE : c == SYN ? RX_SYN : RX_HUNT;
        return MNP2_RX_NONE;
    case RX_SYN_DLE:
        if (c == STX) {
            r->len = 0;
            r->state = RX_BODY;
        } else {
            r->state = c == SYN ? RX_SYN : RX_HUNT;
        }
        return MNP2_RX_NONE;
    case RX_BODY:
        if (c == DLE) {
            r->state = RX_BODY_DLE;
            return MNP2_RX_NONE;
        }
        return rx_take(r, c);
    case RX_BODY_DLE:
        if (c == DLE) return rx_take(r, c);
        if (c == ETX && r->len > 0) {
            r->state = RX_FCS0;
            return MNP2_RX_NONE;
        }
        if (c == STX) {
            /* a start flag: the frame under way lost its end (DLE STX
             * cannot occur in a body, its DLE would be doubled) */
            rx_bad(r);
            r->state = RX_BODY;
            return MNP2_RX_BAD;
        }
        rx_bad(r);
        if (c == SYN) r->state = RX_SYN;  /* the next start flag may follow */
        return MNP2_RX_BAD;
    case RX_FCS0:
        r->fcs0 = c;
        r->state = RX_FCS1;
        return MNP2_RX_NONE;
    default:
        fcs2(r->buf, r->len, want);
        r->state = RX_HUNT;
        if (r->fcs0 != want[0] || c != want[1]) return rx_bad(r);
        r->frames++;
        return MNP2_RX_FRAME;
    }
}

/* ----------------------------------------------------------- frame headers */
static int byte_ok(int v)
{
    return v >= 0 && v <= 255;
}

/* HST's streamlined layouts: the type first, no LI, no parameter octets */
static int build_stream(uint8_t *out, int max, const mnp_hdr_t *h)
{
    uint8_t b[3];
    int n = 0;
    b[n++] = (uint8_t)h->type;
    switch (h->type) {
    case MNP_LT:
        if (!byte_ok(h->ns) || h->dlen < 1 || h->dlen > MNP_MAX_N401 || h->data == NULL) return -1;
        if (2 + h->dlen > max) return -1;
        out[0] = MNP_LT;
        out[1] = (uint8_t)h->ns;
        memcpy(out + 2, h->data, (size_t)h->dlen);
        return 2 + h->dlen;
    case MNP_LA:
        if (!byte_ok(h->nr) || !byte_ok(h->nk)) return -1;
        b[n++] = (uint8_t)h->nr; b[n++] = (uint8_t)h->nk;
        break;
    case MNP_LN:
        if (!byte_ok(h->ns) || !byte_ok(h->code)) return -1;
        b[n++] = (uint8_t)h->ns; b[n++] = (uint8_t)h->code;
        break;
    case MNP_LNA:
        if (!byte_ok(h->nr)) return -1;
        b[n++] = (uint8_t)h->nr;
        break;
    case MNP_LD:
        if (!byte_ok(h->reason) || h->code != 0) return -1;
        b[n++] = (uint8_t)h->reason;
        break;
    default:
        return -1;
    }
    if (n > max) return -1;
    memcpy(out, b, (size_t)n);
    return n;
}

int mnp_hdr_build(uint8_t *out, int max, int layout, const mnp_hdr_t *h)
{
    uint8_t b[8];
    int n = 0, opt = layout == MNP_LAYOUT_OPT;

    if (layout == MNP_LAYOUT_STREAM) return build_stream(out, max, h);
    if (layout != MNP_LAYOUT_STD && layout != MNP_LAYOUT_OPT) return -1;
    switch (h->type) {
    case MNP_LT:
        if (!byte_ok(h->ns) || h->dlen < 1 || h->dlen > MNP_MAX_N401 || h->data == NULL) return -1;
        if (opt) { b[n++] = 2; b[n++] = MNP_LT; b[n++] = (uint8_t)h->ns; }
        else { b[n++] = 4; b[n++] = MNP_LT; b[n++] = 1; b[n++] = 1; b[n++] = (uint8_t)h->ns; }
        if (n + h->dlen > max) return -1;
        memcpy(out, b, (size_t)n);
        memcpy(out + n, h->data, (size_t)h->dlen);
        return n + h->dlen;
    case MNP_LA:
        if (!byte_ok(h->nr) || !byte_ok(h->nk)) return -1;
        if (opt) {
            b[n++] = 3; b[n++] = MNP_LA; b[n++] = (uint8_t)h->nr; b[n++] = (uint8_t)h->nk;
        } else {
            b[n++] = 7; b[n++] = MNP_LA; b[n++] = 1; b[n++] = 1; b[n++] = (uint8_t)h->nr;
            b[n++] = 2; b[n++] = 1; b[n++] = (uint8_t)h->nk;
        }
        break;
    case MNP_LN:
        if (!byte_ok(h->ns) || !byte_ok(h->code)) return -1;
        b[n++] = 7; b[n++] = MNP_LN; b[n++] = 1; b[n++] = 1; b[n++] = (uint8_t)h->ns;
        b[n++] = 2; b[n++] = 1; b[n++] = (uint8_t)h->code;
        break;
    case MNP_LNA:
        if (!byte_ok(h->nr)) return -1;
        b[n++] = 4; b[n++] = MNP_LNA; b[n++] = 1; b[n++] = 1; b[n++] = (uint8_t)h->nr;
        break;
    case MNP_LD:
        if (!byte_ok(h->reason) || !byte_ok(h->code)) return -1;
        b[n++] = h->code ? 7 : 4; b[n++] = MNP_LD; b[n++] = 1; b[n++] = 1; b[n++] = (uint8_t)h->reason;
        if (h->code) { b[n++] = 2; b[n++] = 1; b[n++] = (uint8_t)h->code; }
        break;
    default:
        return -1;
    }
    if (n > max) return -1;
    memcpy(out, b, (size_t)n);
    return n;
}

/* the one-octet values of variable parameters 1 and 2 (types 1..2) of a
 * header's p[0..n); have[t] says whether parameter t came. Returns -1 if a
 * parameter runs past the header. Others, and ones of another length,
 * are skipped. */
static int params12(const uint8_t *p, int n, int v[3], int have[3])
{
    int i = 0;
    have[1] = have[2] = 0;
    v[1] = v[2] = 0;
    while (i < n) {
        int t, l;
        if (i + 2 > n) return -1;
        t = p[i]; l = p[i + 1];
        if (i + 2 + l > n) return -1;
        if ((t == 1 || t == 2) && l == 1) { v[t] = p[i + 2]; have[t] = 1; }
        i += 2 + l;
    }
    return 0;
}

/* Annex A's reading: standard and optimised layouts */
static int parse_annex(const uint8_t *f, int len, mnp_hdr_t *h)
{
    int li, type, v[3], have[3];

    memset(h, 0, sizeof *h);
    h->data = NULL;
    if (len < 2) return -1;
    li = f[0];
    type = f[1];
    if (li < 1 || li == 255 || li + 1 > len) return -1;
    h->type = type;
    h->layout = MNP_LAYOUT_STD;

    if (type == MNP_LR) {
        h->data = f;
        h->dlen = len;
        return 0;
    }
    /* optimised LT `02 04 N(S)` and LA `03 05 N(R) N(k)` */
    if (type == MNP_LT && li == 2) {
        if (len < 4) return -1;
        h->layout = MNP_LAYOUT_OPT;
        h->ns = f[2];
        h->data = f + 3;
        h->dlen = len - 3;
        return 0;
    }
    if (type == MNP_LA && li == 3) {
        h->layout = MNP_LAYOUT_OPT;
        h->nr = f[2];
        h->nk = f[3];
        return 0;
    }
    if (type != MNP_LT && type != MNP_LA && type != MNP_LN && type != MNP_LNA && type != MNP_LD)
        return MNP_PARSE_UNKNOWN;

    if (params12(f + 2, li - 1, v, have) < 0 || !have[1]) return -1;
    switch (type) {
    case MNP_LT:
        if (len - (li + 1) < 1) return -1;
        h->ns = v[1];
        h->data = f + li + 1;
        h->dlen = len - (li + 1);
        break;
    case MNP_LA:
        if (!have[2]) return -1;
        h->nr = v[1];
        h->nk = v[2];
        break;
    case MNP_LN:
        if (!have[2]) return -1;
        h->ns = v[1];
        h->code = v[2];
        break;
    case MNP_LNA:
        h->nr = v[1];
        break;
    default: /* MNP_LD */
        h->reason = v[1];
        h->code = have[2] ? v[2] : 0;
        break;
    }
    return 0;
}

/* a streamlined frame's first octet */
static int stream_type(int c)
{
    return c == MNP_LD || c == MNP_LT || c == MNP_LA || c == MNP_LN || c == MNP_LNA;
}

/* the streamlined reading: 0, or -1 when the length is not its layout's */
static int parse_stream(const uint8_t *f, int len, mnp_hdr_t *h)
{
    memset(h, 0, sizeof *h);
    h->data = NULL;
    h->type = f[0];
    h->layout = MNP_LAYOUT_STREAM;
    switch (f[0]) {
    case MNP_LT:
        if (len < 3 || len - 2 > MNP_MAX_N401) return -1;
        h->ns = f[1]; h->data = f + 2; h->dlen = len - 2;
        return 0;
    case MNP_LA:
        if (len != 3) return -1;
        h->nr = f[1]; h->nk = f[2];
        return 0;
    case MNP_LN:
        if (len != 3) return -1;
        h->ns = f[1]; h->code = f[2];
        return 0;
    case MNP_LNA:
        if (len != 2) return -1;
        h->nr = f[1];
        return 0;
    case MNP_LD:
        if (len != 2) return -1;
        h->reason = f[1];
        return 0;
    default:
        return -1;
    }
}

int mnp_hdr_parse(const uint8_t *f, int len, int agreed_layout, int expect_ns, mnp_hdr_t *h)
{
    mnp_hdr_t a, s;
    int ra, rs;

    if (agreed_layout == MNP_LAYOUT_STD || agreed_layout == MNP_LAYOUT_OPT) return parse_annex(f, len, h);
    ra = parse_annex(f, len, &a);
    if (len < 2) { *h = a; return -1; }
    if (ra == 0 && a.type == MNP_LR) {
        mnp_lr_t lr;
        /* an LR always comes standard: its LI is no streamlined type; with
         * one that is (2..7), too short for an LR's parameters, it is only
         * if it reads as one */
        if (!stream_type(f[0]) || mnp_lr_parse(f, len, &lr) != -1) { *h = a; return 0; }
        ra = -1;
    }
    rs = stream_type(f[0]) ? parse_stream(f, len, &s) : -1;
    if (ra == 0 && rs == 0) {
        int stream;
        if (agreed_layout == MNP_LAYOUT_STREAM) stream = 1;
        else if (agreed_layout == MNP_LAYOUT_ANY)
            stream = expect_ns >= 0 && s.type == MNP_LT && s.ns == expect_ns
                  && !(a.type == MNP_LT && a.ns == expect_ns);
        else stream = 0;
        *h = stream ? s : a;
        return 0;
    }
    if (ra == 0) { *h = a; return 0; }
    if (rs == 0) { *h = s; return 0; }
    *h = a;
    if (stream_type(f[0])) return -1;                      /* a streamlined frame, wrong length */
    if (ra == MNP_PARSE_UNKNOWN) return MNP_PARSE_UNKNOWN;
    /* a consistent LI and a known type, its header malformed */
    if (f[0] >= 1 && f[0] != 255 && f[0] + 1 <= len) return -1;
    return MNP_PARSE_UNKNOWN;                              /* an id we do not know */
}

/* ------------------------------------------------------------ link request */
int mnp_lr_build(uint8_t *out, int max, const mnp_lr_t *lr)
{
    static const uint8_t p1[] = { 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF };
    uint8_t b[40];
    int n = 0;

    if (!byte_ok(lr->const1) || !byte_ok(lr->framing) || !byte_ok(lr->k)
        || lr->n401 < 0 || lr->n401 > 0xFFFF || (lr->have_opt && !byte_ok(lr->opt)))
        return -1;
    b[n++] = 0;                       /* LI, below */
    b[n++] = MNP_LR;
    b[n++] = (uint8_t)lr->const1;
    memcpy(b + n, p1, sizeof p1);
    n += (int)sizeof p1;
    b[n++] = 2; b[n++] = 1; b[n++] = (uint8_t)lr->framing;
    b[n++] = 3; b[n++] = 1; b[n++] = (uint8_t)lr->k;
    b[n++] = 4; b[n++] = 2; b[n++] = (uint8_t)(lr->n401 & 0xFF); b[n++] = (uint8_t)(lr->n401 >> 8);
    if (lr->have_opt) { b[n++] = 8; b[n++] = 1; b[n++] = (uint8_t)lr->opt; }
    if (lr->have_c0) { b[n++] = 0xC0; b[n++] = 2; b[n++] = lr->c0_flags; b[n++] = lr->c0_rates; }
    b[0] = (uint8_t)(n - 1);
    if (n > max) return -1;
    memcpy(out, b, (size_t)n);
    return n;
}

int mnp_lr_parse(const uint8_t *f, int len, mnp_lr_t *lr)
{
    int li, i, end, have_c2 = 0, have_framing = 0, have_k = 0;

    memset(lr, 0, sizeof *lr);
    lr->n401 = 64;
    if (len < 3) return -1;
    li = f[0];
    if (f[1] != MNP_LR || li < 2 || li == 255 || li + 1 > len) return -1;
    lr->const1 = f[2];
    end = li + 1;
    for (i = 3; i < end; ) {
        int t, l;
        const uint8_t *v;
        if (i + 2 > end) return -1;
        t = f[i]; l = f[i + 1]; v = f + i + 2;
        if (i + 2 + l > end) return -1;
        switch (t) {
        case 2: if (l == 1) { lr->framing = v[0]; have_framing = 1; } break;
        case 3: if (l == 1) { lr->k = v[0]; have_k = 1; } break;
        case 1: have_c2 = 1; break;
        case 4:
            if (l == 2) lr->n401 = v[0] | (v[1] << 8);
            else if (l == 1) lr->n401 = v[0];
            else return -1;
            break;
        case 8: if (l == 1) { lr->opt = v[0] & 3; lr->have_opt = 1; } break;
        case 0xC0:
            if (l == 2) { lr->c0_flags = v[0]; lr->c0_rates = v[1]; lr->have_c0 = 1; }
            break;
        default: break;   /* the unknown */
        }
        i += 2 + l;
    }
    if (!have_c2 || !have_framing || !have_k) return -1;
    return lr->const1 == 2 ? 0 : MNP_LR_BAD_CONST1;
}

static int imin(int a, int b)
{
    return a < b ? a : b;
}

int mnp_c0_best(unsigned rates)
{
    int n;
    for (n = 7; n >= 2; n--)
        if ((rates >> n) & 1u) return n;
    return 0;
}

int mnp_lr_merge(const mnp_lr_t *ours, const mnp_lr_t *theirs, int hst_profile, mnp_lr_t *reply)
{
    memset(reply, 0, sizeof *reply);
    reply->const1 = 2;
    reply->framing = imin(ours->framing, theirs->framing);
    reply->k = imin(ours->k, theirs->k);
    reply->n401 = imin(imin(ours->n401, theirs->n401), MNP_MAX_N401);
    if (theirs->have_opt) {
        reply->have_opt = 1;
        reply->opt = (ours->have_opt ? ours->opt : 0) & theirs->opt & 3;
    }
    /* USR's 0xC0: HST needs bit 0 from both, 300 if either asks, the
     * rates in common less 16800 (`LR and 0xC0`) */
    if (hst_profile && ours->have_c0 && theirs->have_c0 && (theirs->c0_flags & MNP_C0_HST)) {
        reply->have_c0 = 1;
        reply->c0_flags = (uint8_t)((ours->c0_flags & theirs->c0_flags & MNP_C0_HST)
                                    | ((ours->c0_flags | theirs->c0_flags) & MNP_C0_BACK300));
        reply->c0_rates = (uint8_t)(ours->c0_rates & theirs->c0_rates & MNP_C0_RATES);
    }
    if (reply->framing < 2 || reply->k < 1 || reply->n401 < 1) return -1;
    return 0;
}
