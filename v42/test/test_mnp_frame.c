/* MNP framing mode 2 and the frame layouts (v42/mnp_frame.h), against
 * octets written out by hand from docs/history/hst-facts.md (sections
 * `MNP framing`, `MNP frames`, `LR and 0xC0`) and a bit-at-a-time CRC
 * written here. One function per group of checks; each prints one line. */
#include <stdint.h>
#include "mnp_frame.h"
#include "v42_hdlc.h"
#include "test.h"

/* x^16 + x^15 + x^2 + 1, reflected, one bit at a time */
static uint16_t ref_crc(uint16_t crc, const uint8_t *d, int n)
{
    int i, b;
    for (i = 0; i < n; i++)
        for (b = 0; b < 8; b++) {
            unsigned x = (crc ^ (unsigned)(d[i] >> b)) & 1u;
            crc = (uint16_t)(x ? (crc >> 1) ^ 0xA001u : crc >> 1);
        }
    return crc;
}

/* the mode 2 FCS of body + ETX, as MNP_FCS2 says it goes on the line */
static void ref_fcs2(const uint8_t *body, int n, uint8_t out[2])
{
    static const uint8_t etx = 0x03;
    uint16_t c = (MNP_FCS2 == MNP_FCS2_FFFF_LO || MNP_FCS2 == MNP_FCS2_FFFF_HI) ? 0xFFFFu : 0;
    int hi_first = MNP_FCS2 == MNP_FCS2_ARC_HI || MNP_FCS2 == MNP_FCS2_FFFF_HI;
    c = ref_crc(c, body, n);
    c = ref_crc(c, &etx, 1);
    out[hi_first ? 1 : 0] = (uint8_t)(c & 0xFFu);
    out[hi_first ? 0 : 1] = (uint8_t)(c >> 8);
}

static int same(const uint8_t *a, const uint8_t *b, int n)
{
    return memcmp(a, b, (size_t)n) == 0;
}

static void crc_vectors(void)
{
    static const uint8_t s[] = "123456789";
    uint16_t a = mnp_crc16(0, s, 9);
    uint16_t x = (uint16_t)(v42_fcs16(V42_FCS16_INIT, s, 9) ^ 0xFFFFu);
    CHECK_EQ_I(a, 0xBB3D);
    CHECK_EQ_I(ref_crc(0, s, 9), 0xBB3D);
    CHECK_EQ_I(x, 0x906E);
    /* the good-frame residue: the register after "123456789" and 6E 90 */
    {
        uint8_t fr[11];
        memcpy(fr, s, 9);
        fr[9] = (uint8_t)(x & 0xFFu); fr[10] = (uint8_t)(x >> 8);
        CHECK_EQ_I(v42_fcs16(V42_FCS16_INIT, fr, 11), 0xF0B8);
    }
    /* in pieces = whole */
    CHECK_EQ_I(mnp_crc16(mnp_crc16(0xFFFFu, s, 4), s + 4, 5), ref_crc(0xFFFFu, s, 9));
    printf("crc_vectors: CRC-16/ARC %04X, FCS-16 sent %02X %02X\n",
           (unsigned)a, (unsigned)(x & 0xFFu), (unsigned)(x >> 8));
}

/* feed n octets to r; the last result, and how many FRAME and BAD there were */
static int feed(mnp2_rx_t *r, const uint8_t *p, int n, int *frames, int *bads)
{
    int i, res = MNP2_RX_NONE;
    *frames = *bads = 0;
    for (i = 0; i < n; i++) {
        res = mnp2_rx_octet(r, p[i]);
        if (res == MNP2_RX_FRAME) (*frames)++;
        if (res == MNP2_RX_BAD) (*bads)++;
    }
    return res;
}

static void mode2_frame(void)
{
    static const uint8_t body[] = { 0x04, 0x04, 0x01, 0x01, 0x05, 0x41, 0x10, 0x42 };
    uint8_t want[32], line[64], noisy[80];
    int nw = 0, nl = 0, i, c, frames, bads, res;
    mnp2_tx_t t;
    mnp2_rx_t r;

    want[nw++] = 0x16; want[nw++] = 0x10; want[nw++] = 0x02;
    for (i = 0; i < (int)sizeof body; i++) {
        want[nw++] = body[i];
        if (body[i] == 0x10) want[nw++] = 0x10;
    }
    want[nw++] = 0x10; want[nw++] = 0x03;
    ref_fcs2(body, (int)sizeof body, want + nw);
    nw += 2;

    mnp2_tx_init(&t);
    CHECK_EQ_I(mnp2_tx_busy(&t), 0);
    CHECK_EQ_I(mnp2_tx_octet(&t), -1);
    CHECK_EQ_I(mnp2_tx_frame(&t, body, (int)sizeof body), 0);
    CHECK_EQ_I(mnp2_tx_busy(&t), 1);
    CHECK_EQ_I(mnp2_tx_frame(&t, body, (int)sizeof body), -1);   /* still loaded */
    while ((c = mnp2_tx_octet(&t)) >= 0 && nl < (int)sizeof line) line[nl++] = (uint8_t)c;
    CHECK_EQ_I(nl, 16);
    CHECK_EQ_I(nl, nw);
    CHECK(same(line, want, nw));
    CHECK_EQ_I(mnp2_tx_busy(&t), 0);
    CHECK_EQ_I(mnp2_tx_frame(&t, body, 0), -1);
    CHECK_EQ_I(mnp2_tx_frame(&t, body, MNP_MAX_BODY + 1), -1);

    /* back to the body */
    mnp2_rx_init(&r, MNP_MAX_BODY);
    res = feed(&r, line, nl, &frames, &bads);
    CHECK_EQ_I(res, MNP2_RX_FRAME);
    CHECK_EQ_I(frames, 1);
    CHECK_EQ_I(bads, 0);
    CHECK_EQ_I(r.len, (int)sizeof body);
    CHECK(same(r.buf, body, (int)sizeof body));

    /* one flipped bit anywhere after the start flag: BAD, never a frame */
    {
        int k, b, nbad = 0, nframe = 0;
        for (k = 3; k < nl; k++)
            for (b = 0; b < 8; b++) {
                uint8_t m[64];
                memcpy(m, line, (size_t)nl);
                m[k] = (uint8_t)(m[k] ^ (1u << b));
                mnp2_rx_init(&r, MNP_MAX_BODY);
                feed(&r, m, nl, &frames, &bads);
                nframe += frames;
                /* the flipped stream ends in BAD, or (a flag octet hit)
                 * leaves the receiver hunting: never a frame */
                if (bads > 0) nbad++;
            }
        CHECK_EQ_I(nframe, 0);
        printf("mode2_frame: %d single-bit errors, %d end in BAD, 0 frames\n", (nl - 3) * 8, nbad);
        /* the first body octet flipped: plainly BAD */
        memcpy(noisy, line, (size_t)nl);
        noisy[3] ^= 0x01;
        mnp2_rx_init(&r, MNP_MAX_BODY);
        CHECK_EQ_I(feed(&r, noisy, nl, &frames, &bads), MNP2_RX_BAD);
    }

    /* noise before SYN DLE STX, including a lone SYN and a SYN DLE */
    {
        static const uint8_t pre[] = { 0xFF, 0x41, 0x16, 0x41, 0x10, 0x02, 0x16, 0x10, 0x41, 0x03, 0x10 };
        int n = (int)sizeof pre;
        memcpy(noisy, pre, (size_t)n);
        memcpy(noisy + n, line, (size_t)nl);
        mnp2_rx_init(&r, MNP_MAX_BODY);
        res = feed(&r, noisy, n + nl, &frames, &bads);
        CHECK_EQ_I(res, MNP2_RX_FRAME);
        CHECK_EQ_I(frames, 1);
        CHECK_EQ_I(bads, 0);
        CHECK(r.len == (int)sizeof body && same(r.buf, body, (int)sizeof body));
    }

    /* a body longer than max_len: BAD */
    mnp2_rx_init(&r, 4);
    CHECK_EQ_I(feed(&r, line, nl, &frames, &bads), MNP2_RX_NONE);
    CHECK_EQ_I(frames, 0);
    CHECK_EQ_I(bads, 1);

    /* a frame cut off (its end lost), then a good one, with and without
     * idle FF between: the next start flag ends the cut one (one BAD) and
     * starts the good one (one FRAME) */
    {
        int idle, cut;
        for (idle = 0; idle <= 3; idle += 3)
            for (cut = 4; cut <= 12; cut += 8) {
                int n = 0;
                memcpy(noisy, line, (size_t)cut);
                n = cut;
                for (i = 0; i < idle; i++) noisy[n++] = 0xFF;
                memcpy(noisy + n, line, (size_t)nl);
                n += nl;
                mnp2_rx_init(&r, MNP_MAX_BODY);
                res = feed(&r, noisy, n, &frames, &bads);
                CHECK_EQ_I(res, MNP2_RX_FRAME);
                CHECK_EQ_I(frames, 1);
                CHECK_EQ_I(bads, 1);
                CHECK(r.len == (int)sizeof body && same(r.buf, body, (int)sizeof body));
            }
        /* cut just after a body DLE: the DLE then SYN DLE STX */
        memcpy(noisy, line, 10);
        memcpy(noisy + 10, line, (size_t)nl);
        mnp2_rx_init(&r, MNP_MAX_BODY);
        res = feed(&r, noisy, 10 + nl, &frames, &bads);
        CHECK_EQ_I(res, MNP2_RX_FRAME);
        CHECK_EQ_I(frames, 1);
        CHECK_EQ_I(bads, 1);
        printf("mode2_frame: a cut-off frame then a good one: 1 BAD, 1 FRAME\n");
    }

    /* two frames back to back */
    memcpy(noisy, line, (size_t)nl);
    memcpy(noisy + nl, line, (size_t)nl);
    mnp2_rx_init(&r, MNP_MAX_BODY);
    feed(&r, noisy, 2 * nl, &frames, &bads);
    CHECK_EQ_I(frames, 2);
    CHECK_EQ_I(bads, 0);

    printf("mode2_frame: %d line octets:", nl);
    for (i = 0; i < nl; i++) printf(" %02X", line[i]);
    printf("\n");
}

/* build h in layout, check the octets, parse them back to the same fields */
static void one_layout(const char *name, int layout, const mnp_hdr_t *h,
                       const uint8_t *want, int nwant)
{
    uint8_t out[300];
    mnp_hdr_t p;
    int n = mnp_hdr_build(out, (int)sizeof out, layout, h), i, ok;
    CHECK_EQ_I(n, nwant);
    ok = n == nwant && same(out, want, nwant);
    CHECK(ok);
    CHECK_EQ_I(mnp_hdr_build(out, nwant - 1, layout, h), -1);     /* no room */
    memset(&p, 0x5A, sizeof p);
    CHECK_EQ_I(mnp_hdr_parse(want, nwant, layout, -1, &p), 0);
    CHECK_EQ_I(p.type, h->type);
    CHECK_EQ_I(p.layout, layout);
    CHECK_EQ_I(p.ns, h->ns);
    CHECK_EQ_I(p.nr, h->nr);
    CHECK_EQ_I(p.nk, h->nk);
    CHECK_EQ_I(p.code, h->code);
    CHECK_EQ_I(p.reason, h->reason);
    CHECK_EQ_I(p.dlen, h->dlen);
    CHECK(h->dlen == 0 || (p.data != NULL && same(p.data, h->data, h->dlen)));
    printf("layouts: %-7s", name);
    for (i = 0; i < nwant; i++) printf(" %02X", want[i]);
    printf("\n");
}

static mnp_hdr_t hdr(int type, int ns, int nr, int nk, int code, int reason,
                     const uint8_t *data, int dlen)
{
    mnp_hdr_t h;
    h.type = type; h.layout = 0; h.ns = ns; h.nr = nr; h.nk = nk;
    h.code = code; h.reason = reason; h.data = data; h.dlen = dlen;
    return h;
}

static void layouts(void)
{
    static const uint8_t data[] = { 0x41, 0x42, 0x43 };
    static const uint8_t lt[] = { 0x04, 0x04, 0x01, 0x01, 0x07, 0x41, 0x42, 0x43 };
    static const uint8_t la[] = { 0x07, 0x05, 0x01, 0x01, 0x07, 0x02, 0x01, 0x08 };
    static const uint8_t ln[] = { 0x07, 0x06, 0x01, 0x01, 0x03, 0x02, 0x01, 0x02 };
    static const uint8_t lna[] = { 0x04, 0x07, 0x01, 0x01, 0x03 };
    static const uint8_t ld[] = { 0x04, 0x02, 0x01, 0x01, 0xFF };
    static const uint8_t ldu[] = { 0x07, 0x02, 0x01, 0x01, 0xFF, 0x02, 0x01, 0x09 };
    static const uint8_t olt[] = { 0x02, 0x04, 0x07, 0x41, 0x42, 0x43 };
    static const uint8_t ola[] = { 0x03, 0x05, 0x07, 0x08 };
    mnp_hdr_t h, p;
    uint8_t out[16];

    h = hdr(MNP_LT, 7, 0, 0, 0, 0, data, 3);   one_layout("LT", MNP_LAYOUT_STD, &h, lt, (int)sizeof lt);
    h = hdr(MNP_LA, 0, 7, 8, 0, 0, NULL, 0);   one_layout("LA", MNP_LAYOUT_STD, &h, la, (int)sizeof la);
    h = hdr(MNP_LN, 3, 0, 0, 2, 0, NULL, 0);   one_layout("LN", MNP_LAYOUT_STD, &h, ln, (int)sizeof ln);
    h = hdr(MNP_LNA, 0, 3, 0, 0, 0, NULL, 0);  one_layout("LNA", MNP_LAYOUT_STD, &h, lna, (int)sizeof lna);
    h = hdr(MNP_LD, 0, 0, 0, 0, 255, NULL, 0); one_layout("LD", MNP_LAYOUT_STD, &h, ld, (int)sizeof ld);
    h = hdr(MNP_LD, 0, 0, 0, 9, 255, NULL, 0); one_layout("LD user", MNP_LAYOUT_STD, &h, ldu, (int)sizeof ldu);
    h = hdr(MNP_LT, 7, 0, 0, 0, 0, data, 3);   one_layout("opt LT", MNP_LAYOUT_OPT, &h, olt, (int)sizeof olt);
    h = hdr(MNP_LA, 0, 7, 8, 0, 0, NULL, 0);   one_layout("opt LA", MNP_LAYOUT_OPT, &h, ola, (int)sizeof ola);
    /* optimised: LN, LNA and LD unchanged */
    h = hdr(MNP_LNA, 0, 3, 0, 0, 0, NULL, 0);
    CHECK_EQ_I(mnp_hdr_build(out, (int)sizeof out, MNP_LAYOUT_OPT, &h), (int)sizeof lna);
    CHECK(same(out, lna, (int)sizeof lna));

    /* an LT is never empty; an LR is not a header this builds */
    h = hdr(MNP_LT, 7, 0, 0, 0, 0, data, 0);
    CHECK_EQ_I(mnp_hdr_build(out, (int)sizeof out, MNP_LAYOUT_STD, &h), -1);
    h = hdr(MNP_LR, 0, 0, 0, 0, 0, NULL, 0);
    CHECK_EQ_I(mnp_hdr_build(out, (int)sizeof out, MNP_LAYOUT_STD, &h), -1);
    h = hdr(MNP_LA, 0, 256, 8, 0, 0, NULL, 0);
    CHECK_EQ_I(mnp_hdr_build(out, (int)sizeof out, MNP_LAYOUT_STD, &h), -1);

    /* parse: an empty LT, an LI past the end, a missing parameter: malformed */
    CHECK_EQ_I(mnp_hdr_parse(lt, 5, MNP_LAYOUT_STD, -1, &p), -1);
    CHECK_EQ_I(mnp_hdr_parse(la, 7, MNP_LAYOUT_STD, -1, &p), -1);
    {
        static const uint8_t la_nok[] = { 0x04, 0x05, 0x01, 0x01, 0x07 };
        static const uint8_t ld_none[] = { 0x01, 0x02 };
        static const uint8_t la_extra[] = { 0x0A, 0x05, 0x01, 0x01, 0x07, 0x09, 0x01, 0x55, 0x02, 0x01, 0x08 };
        CHECK_EQ_I(mnp_hdr_parse(la_nok, 5, MNP_LAYOUT_STD, -1, &p), -1);
        CHECK_EQ_I(mnp_hdr_parse(ld_none, 2, MNP_LAYOUT_STD, -1, &p), -1);
        /* an unknown parameter is skipped */
        CHECK_EQ_I(mnp_hdr_parse(la_extra, (int)sizeof la_extra, MNP_LAYOUT_STD, -1, &p), 0);
        CHECK(p.type == MNP_LA && p.nr == 7 && p.nk == 8);
    }
    /* CRC-valid, unknown type */
    {
        static const uint8_t t3[] = { 0x04, 0x03, 0x01, 0x01, 0x00 };
        static const uint8_t t9[] = { 0x02, 0x09, 0x00 };
        CHECK_EQ_I(mnp_hdr_parse(t3, (int)sizeof t3, MNP_LAYOUT_STD, -1, &p), MNP_PARSE_UNKNOWN);
        CHECK_EQ_I(mnp_hdr_parse(t9, (int)sizeof t9, MNP_LAYOUT_STD, -1, &p), MNP_PARSE_UNKNOWN);
        CHECK_EQ_I(mnp_hdr_parse(t9, 1, MNP_LAYOUT_STD, -1, &p), -1);
        CHECK_EQ_I(mnp_hdr_parse(t9, 0, MNP_LAYOUT_STD, -1, &p), -1);
    }
    /* LI 255 (an extended length): malformed */
    {
        uint8_t big[260];
        memset(big, 0, sizeof big);
        big[0] = 0xFF; big[1] = MNP_LA;
        CHECK_EQ_I(mnp_hdr_parse(big, (int)sizeof big, MNP_LAYOUT_STD, -1, &p), -1);
        big[1] = MNP_LR;
        CHECK_EQ_I(mnp_hdr_parse(big, (int)sizeof big, MNP_LAYOUT_STD, -1, &p), -1);
    }
}

/* the Courier's LR template (`LR and 0xC0`) */
static const uint8_t courier[] = {
    0x14, 0x01, 0x02, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x02, 0x01, 0x03, 0x03, 0x01, 0x08, 0x04, 0x02, 0x40, 0x00
};

static void courier_lr(void)
{
    mnp_lr_t lr;
    mnp_hdr_t h;
    uint8_t f[40], out[64];
    int n;

    CHECK_EQ_I((int)sizeof courier, 21);
    CHECK_EQ_I(mnp_lr_parse(courier, (int)sizeof courier, &lr), 0);
    CHECK_EQ_I(lr.const1, 2);
    CHECK_EQ_I(lr.framing, 3);
    CHECK_EQ_I(lr.k, 8);
    CHECK_EQ_I(lr.n401, 64);
    CHECK_EQ_I(lr.have_opt, 0);
    CHECK_EQ_I(lr.have_c0, 0);
    printf("courier_lr: const1 %d framing %d k %d n401 %d opt %d c0 %d\n",
           lr.const1, lr.framing, lr.k, lr.n401, lr.have_opt, lr.have_c0);

    /* building those fields gives the template back */
    n = mnp_lr_build(out, (int)sizeof out, &lr);
    CHECK_EQ_I(n, (int)sizeof courier);
    CHECK(n == (int)sizeof courier && same(out, courier, n));
    CHECK_EQ_I(mnp_lr_build(out, n - 1, &lr), -1);
    /* an LR is type LR to the header parser */
    CHECK_EQ_I(mnp_hdr_parse(courier, (int)sizeof courier, MNP_LAYOUT_STD, -1, &h), 0);
    CHECK_EQ_I(h.type, MNP_LR);
    CHECK_EQ_I(mnp_hdr_parse(courier, (int)sizeof courier, MNP_LAYOUT_OPT, -1, &h), 0);
    CHECK_EQ_I(h.type, MNP_LR);

    /* N401 04 02 00 01: little-endian 256 */
    memcpy(f, courier, sizeof courier);
    f[19] = 0x00; f[20] = 0x01;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), 0);
    CHECK_EQ_I(lr.n401, 256);

    /* constant parameter 1 not 2 */
    memcpy(f, courier, sizeof courier);
    f[2] = 3;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), MNP_LR_BAD_CONST1);

    /* an unknown parameter 09 01 55 is skipped; parameter 8 and 0xC0 read */
    memcpy(f, courier, sizeof courier);
    n = (int)sizeof courier;
    f[n++] = 0x09; f[n++] = 0x01; f[n++] = 0x55;
    f[n++] = 0x08; f[n++] = 0x01; f[n++] = 0x03;
    f[n++] = 0xC0; f[n++] = 0x02; f[n++] = 0x09; f[n++] = 0xFC;
    f[0] = (uint8_t)(n - 1);
    CHECK_EQ_I(mnp_lr_parse(f, n, &lr), 0);
    CHECK(lr.framing == 3 && lr.k == 8 && lr.n401 == 64);
    CHECK(lr.have_opt == 1 && lr.opt == 3);
    CHECK(lr.have_c0 == 1 && lr.c0_flags == 0x09 && lr.c0_rates == 0xFC);
    /* with the unknown one left out, building gives the same octets */
    {
        uint8_t g[40];
        int m = mnp_lr_build(g, (int)sizeof g, &lr);
        CHECK_EQ_I(m, n - 3);
        CHECK(m == n - 3 && g[0] == m - 1 && same(g + 1, f + 1, 20) && same(g + 21, f + 24, 7));
    }

    /* malformed: not an LR, an LI past the end, a parameter past the LI,
     * no framing mode, no k */
    CHECK_EQ_I(mnp_lr_parse(courier, 20, &lr), -1);
    memcpy(f, courier, sizeof courier);
    f[1] = MNP_LT;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), -1);
    memcpy(f, courier, sizeof courier);
    f[18] = 0x03;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), -1);
    memcpy(f, courier, sizeof courier);
    f[11] = 0x0A;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), -1);
    memcpy(f, courier, sizeof courier);
    f[14] = 0x0A;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), -1);
    /* no constant parameter 2 (A.7.1.5.2: always present) */
    memcpy(f, courier, sizeof courier);
    f[3] = 0x0A;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), -1);
    /* LI 255 and a zero-length frame */
    memcpy(f, courier, sizeof courier);
    f[0] = 0xFF;
    CHECK_EQ_I(mnp_lr_parse(f, (int)sizeof courier, &lr), -1);
    CHECK_EQ_I(mnp_lr_parse(courier, 0, &lr), -1);
    /* N401 in one octet is taken; in three, or none, is malformed */
    memcpy(f, courier, sizeof courier);
    f[18] = 0x01; f[19] = 0x20;
    f[0] = 19;
    CHECK_EQ_I(mnp_lr_parse(f, 20, &lr), 0);
    CHECK_EQ_I(lr.n401, 32);
    memcpy(f, courier, sizeof courier);
    f[18] = 0x03; f[21] = 0x00;
    f[0] = 21;
    CHECK_EQ_I(mnp_lr_parse(f, 22, &lr), -1);
    memcpy(f, courier, sizeof courier);
    f[18] = 0x00;
    f[0] = 18;
    CHECK_EQ_I(mnp_lr_parse(f, 19, &lr), -1);
}

static mnp_lr_t lrv(int framing, int k, int n401, int have_opt, int opt)
{
    mnp_lr_t l;
    memset(&l, 0, sizeof l);
    l.const1 = 2; l.framing = framing; l.k = k; l.n401 = n401;
    l.have_opt = have_opt; l.opt = opt;
    return l;
}

static void merge(void)
{
    mnp_lr_t ours = lrv(3, 8, 64, 1, 2), theirs = lrv(2, 4, 256, 1, 3), reply;

    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), 0);
    CHECK_EQ_I(reply.const1, 2);
    CHECK_EQ_I(reply.framing, 2);
    CHECK_EQ_I(reply.k, 4);
    CHECK_EQ_I(reply.n401, 64);
    CHECK_EQ_I(reply.have_opt, 1);
    CHECK_EQ_I(reply.opt, 2);
    CHECK_EQ_I(reply.have_c0, 0);
    printf("merge: framing %d k %d n401 %d opt %d/%d\n",
           reply.framing, reply.k, reply.n401, reply.have_opt, reply.opt);

    theirs.have_opt = 0;
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), 0);
    CHECK_EQ_I(reply.have_opt, 0);

    /* reserved bits of parameter 8 are ignored */
    theirs = lrv(3, 8, 64, 1, 0xFE);
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), 0);
    CHECK(reply.have_opt == 1 && reply.opt == 2 && reply.framing == 3);

    /* the reply's N401 never above MNP_MAX_N401 */
    ours = lrv(3, 8, 1000, 1, 3);
    theirs = lrv(3, 8, 1000, 1, 3);
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), 0);
    CHECK_EQ_I(reply.n401, MNP_MAX_N401);
    ours = lrv(3, 8, 64, 1, 2);

    /* no common framing mode, k 0, N401 0: incompatible */
    theirs = lrv(1, 8, 64, 0, 0);
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), -1);
    theirs = lrv(3, 0, 64, 0, 0);
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), -1);
    theirs = lrv(3, 8, 0, 0, 0);
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), -1);
}

/* USR's 0xC0 (`LR and 0xC0`): HST needs bit 0 from both ends, the back
 * channel is 300 if either asks, rates = ours AND theirs AND 0x7C, the
 * best is the highest bit; a C0 of another length is ignored */
static void c0(void)
{
    mnp_lr_t ours = lrv(3, 8, 64, 1, 2), theirs = lrv(3, 8, 64, 1, 3), reply;
    uint8_t f[48];
    int n, best, c0_none, r80;

    ours.have_c0 = 1; ours.c0_flags = 0x01; ours.c0_rates = 0x7C;
    theirs.have_c0 = 1; theirs.c0_flags = 0x09; theirs.c0_rates = 0xFC;
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 1, &reply), 0);
    CHECK_EQ_I(reply.have_c0, 1);
    CHECK_EQ_I(reply.c0_flags & MNP_C0_HST, MNP_C0_HST);
    CHECK_EQ_I(reply.c0_flags & MNP_C0_BACK300, MNP_C0_BACK300);
    CHECK_EQ_I(reply.c0_flags, 0x09);
    CHECK_EQ_I(reply.c0_rates, 0x7C);
    best = mnp_c0_best(reply.c0_rates);
    CHECK_EQ_I(best, 6);
    /* the reply LR ends with C0 02 09 7C */
    n = mnp_lr_build(f, (int)sizeof f, &reply);
    CHECK(n > 4 && f[n - 4] == 0xC0 && f[n - 3] == 0x02 && f[n - 2] == 0x09 && f[n - 1] == 0x7C);
    printf("c0: ours 01/7C, theirs 09/FC: HST %d, back300 %d, rates %02X, best code %d\n",
           reply.c0_flags & MNP_C0_HST, (reply.c0_flags & MNP_C0_BACK300) != 0, reply.c0_rates, best);
    /* the profile off: no 0xC0 in the reply */
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 0, &reply), 0);
    CHECK_EQ_I(reply.have_c0, 0);
    /* theirs without bit 0: no HST, no 0xC0 */
    theirs.c0_flags = 0x00; theirs.c0_rates = 0xFC;
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 1, &reply), 0);
    CHECK_EQ_I(reply.have_c0, 0);
    c0_none = reply.have_c0;
    /* theirs 01/80 (16800 only): HST but no common rate, plain MNP */
    theirs.c0_flags = 0x01; theirs.c0_rates = 0x80;
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 1, &reply), 0);
    CHECK_EQ_I(reply.c0_rates, 0);
    CHECK_EQ_I(mnp_c0_best(reply.c0_rates), 0);
    r80 = reply.c0_rates;
    /* we leave 300 out, they ask it: 300 */
    theirs.c0_flags = 0x01; theirs.c0_rates = 0x10;
    ours.c0_flags = 0x09;
    CHECK_EQ_I(mnp_lr_merge(&ours, &theirs, 1, &reply), 0);
    CHECK(reply.c0_flags == 0x09 && reply.c0_rates == 0x10);
    CHECK_EQ_I(mnp_c0_best(0x10), 4);
    CHECK_EQ_I(mnp_c0_best(0x0C), 3);
    printf("c0: theirs 00/FC: 0xC0 in reply %d; theirs 01/80: rates %02X (plain MNP)\n", c0_none, r80);
    /* a C0 of length 3 or 1 is ignored */
    memcpy(f, courier, sizeof courier);
    n = (int)sizeof courier;
    f[n++] = 0xC0; f[n++] = 0x03; f[n++] = 0x01; f[n++] = 0xFC; f[n++] = 0x00;
    f[0] = (uint8_t)(n - 1);
    CHECK_EQ_I(mnp_lr_parse(f, n, &theirs), 0);
    CHECK_EQ_I(theirs.have_c0, 0);
    n = (int)sizeof courier;
    f[n++] = 0xC0; f[n++] = 0x01; f[n++] = 0x01;
    f[0] = (uint8_t)(n - 1);
    CHECK_EQ_I(mnp_lr_parse(f, n, &theirs), 0);
    CHECK_EQ_I(theirs.have_c0, 0);
}

/* HST's streamlined layouts (`MNP frames`): no LI; the tolerant parser */
static void stream(void)
{
    static const uint8_t d41[] = { 0x41 };
    static const uint8_t slt[] = { 0x04, 0x07, 0x41 };
    static const uint8_t sla[] = { 0x05, 0x07, 0x08 };
    static const uint8_t sln[] = { 0x06, 0x03, 0x14 };
    static const uint8_t slna[] = { 0x07, 0x03 };
    static const uint8_t sld[] = { 0x02, 0xFF };
    /* standard LT N(S) 7 "41", or streamlined LT N(S) 4 "01 01 07 41" */
    static const uint8_t both[] = { 0x04, 0x04, 0x01, 0x01, 0x07, 0x41 };
    static const uint8_t unk[] = { 0x3F, 0x05, 0x41, 0x42 };
    static const uint8_t lt1[] = { 0x04, 0x01, 0x41 };
    static const uint8_t la_short[] = { 0x05, 0x07 };
    static const uint8_t lt_empty[] = { 0x04, 0x07 };
    static const int modes[] = { MNP_LAYOUT_STD, MNP_LAYOUT_OPT, MNP_LAYOUT_STREAM, MNP_LAYOUT_ANY,
                                 MNP_LAYOUT_STD | MNP_RX_ALL };
    mnp_hdr_t h, p;
    uint8_t out[16];
    int i, lr_all = 1;

    h = hdr(MNP_LT, 7, 0, 0, 0, 0, d41, 1);     one_layout("str LT", MNP_LAYOUT_STREAM, &h, slt, 3);
    h = hdr(MNP_LA, 0, 7, 8, 0, 0, NULL, 0);    one_layout("str LA", MNP_LAYOUT_STREAM, &h, sla, 3);
    h = hdr(MNP_LN, 3, 0, 0, 0x14, 0, NULL, 0); one_layout("str LN", MNP_LAYOUT_STREAM, &h, sln, 3);
    h = hdr(MNP_LNA, 0, 3, 0, 0, 0, NULL, 0);   one_layout("str LNA", MNP_LAYOUT_STREAM, &h, slna, 2);
    h = hdr(MNP_LD, 0, 0, 0, 0, 255, NULL, 0);  one_layout("str LD", MNP_LAYOUT_STREAM, &h, sld, 2);
    /* a streamlined LD has no room for a user code */
    h = hdr(MNP_LD, 0, 0, 0, 9, 255, NULL, 0);
    CHECK_EQ_I(mnp_hdr_build(out, (int)sizeof out, MNP_LAYOUT_STREAM, &h), -1);

    /* both readings fit: the agreed layout wins; none agreed, the expected N(S) */
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_STD, -1, &p), 0);
    CHECK(p.type == MNP_LT && p.layout == MNP_LAYOUT_STD && p.ns == 7 && p.dlen == 1 && p.data[0] == 0x41);
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_STD | MNP_RX_ALL, 4, &p), 0);
    CHECK(p.layout == MNP_LAYOUT_STD && p.ns == 7);
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_STREAM, -1, &p), 0);
    CHECK(p.type == MNP_LT && p.layout == MNP_LAYOUT_STREAM && p.ns == 4 && p.dlen == 4
          && same(p.data, both + 2, 4));
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_ANY, 4, &p), 0);
    CHECK(p.layout == MNP_LAYOUT_STREAM && p.ns == 4);
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_ANY, 7, &p), 0);
    CHECK(p.layout == MNP_LAYOUT_STD && p.ns == 7);
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_ANY, -1, &p), 0);
    CHECK(p.layout == MNP_LAYOUT_STD && p.ns == 7);
    CHECK_EQ_I(mnp_hdr_parse(both, 6, MNP_LAYOUT_ANY, 4, &p), 0);
    printf("stream: 04 04 01 01 07 41: std agreed N(S) 7; streamlined agreed N(S) 4; none agreed, expect 4: N(S) %d\n",
           p.ns);
    /* an unknown first octet, CRC-valid: unknown in every layout read */
    CHECK_EQ_I(mnp_hdr_parse(unk, 4, MNP_LAYOUT_STREAM, -1, &p), MNP_PARSE_UNKNOWN);
    CHECK_EQ_I(mnp_hdr_parse(unk, 4, MNP_LAYOUT_ANY, 1, &p), MNP_PARSE_UNKNOWN);
    CHECK_EQ_I(mnp_hdr_parse(unk, 4, MNP_LAYOUT_STD | MNP_RX_ALL, 1, &p), MNP_PARSE_UNKNOWN);
    /* an LR is an LR in every layout */
    for (i = 0; i < (int)(sizeof modes / sizeof modes[0]); i++) {
        int r = mnp_hdr_parse(courier, (int)sizeof courier, modes[i], 1, &p);
        CHECK_EQ_I(r, 0);
        CHECK_EQ_I(p.type, MNP_LR);
        lr_all = lr_all && r == 0 && p.type == MNP_LR;
    }
    /* a streamlined LT with N(S) 1 is not an LR */
    CHECK_EQ_I(mnp_hdr_parse(lt1, 3, MNP_LAYOUT_STREAM, -1, &p), 0);
    CHECK(p.type == MNP_LT && p.ns == 1 && p.dlen == 1);
    CHECK_EQ_I(mnp_hdr_parse(lt1, 3, MNP_LAYOUT_ANY, -1, &p), 0);
    CHECK(p.type == MNP_LT && p.ns == 1);
    /* Annex A's layouts alone (plain MNP) take no streamlined frame */
    CHECK_EQ_I(mnp_hdr_parse(sla, 3, MNP_LAYOUT_STD, -1, &p), -1);
    CHECK_EQ_I(mnp_hdr_parse(sla, 3, MNP_LAYOUT_OPT, -1, &p), -1);
    /* read tolerantly, a standard layout agreed: a streamlined LA */
    CHECK_EQ_I(mnp_hdr_parse(sla, 3, MNP_LAYOUT_STD | MNP_RX_ALL, -1, &p), 0);
    CHECK(p.type == MNP_LA && p.layout == MNP_LAYOUT_STREAM && p.nr == 7 && p.nk == 8);
    /* malformed streamlined frames */
    CHECK_EQ_I(mnp_hdr_parse(la_short, 2, MNP_LAYOUT_STREAM, -1, &p), -1);
    CHECK_EQ_I(mnp_hdr_parse(lt_empty, 2, MNP_LAYOUT_STREAM, -1, &p), -1);
    printf("stream: first octet 3F: unknown; the Courier's LR read as an LR in all %d modes: %s\n",
           (int)(sizeof modes / sizeof modes[0]), lr_all ? "yes" : "NO");
}

int main(void)
{
    crc_vectors();
    mode2_frame();
    layouts();
    courier_lr();
    merge();
    c0();
    stream();
    T_REPORT("test_mnp_frame");
}
