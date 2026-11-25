/* MNP error control (V.42 Annex A, the alternative procedure): framing
 * mode 2 and the frame layouts.
 *
 * Framing mode 2 is start-stop and octet-oriented: SYN DLE STX, the body
 * with every DLE doubled, DLE ETX, then a 16-bit FCS over the body and the
 * ETX (A.3). Its octets go to and come from v42async_tx_char and
 * v42async_rx (v42_detect.h). Framing mode 3 is the HDLC of v42_hdlc.h
 * with FCS-16, unchanged (A.4).
 *
 * The body of a frame is a header (a length indication LI, the type, then
 * parameters) and, in an LT, the information field (A.6). The facts, each
 * with its source, are in docs/history/hst-facts.md, sections `MNP
 * framing`, `MNP frames` and `LR and 0xC0`.
 *
 * No allocation, no callbacks, no global state. */
#ifndef V90D_MNP_FRAME_H
#define V90D_MNP_FRAME_H
#include <stddef.h>
#include <stdint.h>

/* frame types (Table A.1); 3 is unused */
enum { MNP_LR = 1, MNP_LD = 2, MNP_LT = 4, MNP_LA = 5, MNP_LN = 6, MNP_LNA = 7 };

/* header layouts: Annex A's standard one, its optimised LT and LA
 * (LR parameter 8 bit 2), HST's streamlined one (no LI: the type first) */
enum { MNP_LAYOUT_STD = 0, MNP_LAYOUT_OPT, MNP_LAYOUT_STREAM };
/* mnp_hdr_parse's agreed_layout, beside the three: no layout known (HST,
 * the caller's layout not settled by the LRs nor yet heard); and a flag
 * or'ed into MNP_LAYOUT_STD or _OPT: read the streamlined layouts too */
#define MNP_LAYOUT_ANY (-1)
#define MNP_RX_ALL     0x10

/* The mode 2 FCS's initial value and octet order are in no public text.
 * The four candidates: initial 0 or 0xFFFF, low or high octet first; no
 * final complement in any. We take CRC-16/ARC, low octet first [OURS]; a
 * recorded LR settles it (open question 8). */
enum { MNP_FCS2_ARC_LO = 0, MNP_FCS2_ARC_HI, MNP_FCS2_FFFF_LO, MNP_FCS2_FFFF_HI };
#ifndef MNP_FCS2
#define MNP_FCS2 MNP_FCS2_ARC_LO
#endif

/* the largest information field (N401) and the largest body: a standard
 * LT header (5 octets) and that field; any LR (LI at most 254) fits too */
#define MNP_MAX_N401  256
#define MNP_LT_STD_HDR 5
#define MNP_MAX_BODY  (MNP_LT_STD_HDR + MNP_MAX_N401)

/* x^16 + x^15 + x^2 + 1, reflected (least significant bit first), no
 * final complement: from 0, "123456789" gives 0xBB3D (CRC-16/ARC). */
uint16_t mnp_crc16(uint16_t crc, const uint8_t *p, size_t n);

/* -------------------------------------------------- framing mode 2, send */
typedef struct {
    uint8_t buf[MNP_MAX_BODY];
    int     len;         /* body octets; 0 = idle */
    int     pos;         /* next body octet */
    int     step;        /* where in the frame: start flag, body, end, FCS */
    int     dle_owed;    /* the body octet just sent was a DLE: send it again */
    uint8_t fcs[2];      /* in line order */
} mnp2_tx_t;

void mnp2_tx_init(mnp2_tx_t *t);
/* Load one frame body of 1..MNP_MAX_BODY octets. Returns 0, or -1 if a
 * frame is still being sent or len is out of range. */
int  mnp2_tx_frame(mnp2_tx_t *t, const uint8_t *body, int len);
/* true while a loaded frame has octets left to send */
int  mnp2_tx_busy(const mnp2_tx_t *t);
/* the next line octet (0..255), or -1 when idle (the line is at mark) */
int  mnp2_tx_octet(mnp2_tx_t *t);

/* ----------------------------------------------- framing mode 2, receive */
enum { MNP2_RX_NONE = 0, MNP2_RX_FRAME = 1, MNP2_RX_BAD = -1 };

typedef struct {
    uint8_t buf[MNP_MAX_BODY];
    int     len;         /* body octets collected; after MNP2_RX_FRAME, the body's */
    int     max_len;     /* longest body taken, 1..MNP_MAX_BODY */
    int     state;
    uint8_t fcs0;        /* the first FCS octet received */
    unsigned long frames, bad;
} mnp2_rx_t;

/* max_len: the longest body accepted (clamped to 1..MNP_MAX_BODY) */
void mnp2_rx_init(mnp2_rx_t *r, int max_len);
/* Feed one received octet. Octets outside a frame are skipped until SYN
 * DLE STX. Returns MNP2_RX_FRAME when a frame with a good FCS has ended
 * (its body in r->buf, r->len, valid until the next call), MNP2_RX_BAD
 * when one was dropped (bad FCS, too long, empty, a DLE followed by
 * anything but DLE or ETX), else MNP2_RX_NONE. A DLE STX inside a body
 * is the next frame's start flag (the frame under way lost its end): that
 * one is dropped and the new one collected; a body DLE followed by SYN
 * drops the frame and looks for DLE STX next. */
int  mnp2_rx_octet(mnp2_rx_t *r, uint8_t c);
/* true from a start flag's STX until the frame ends (good, bad or too long);
 * the body so far is in r->buf, r->len */
int  mnp2_rx_in_frame(const mnp2_rx_t *r);

/* ------------------------------------------------------- frame headers */
/* The fields of one header. LT: ns (N(S)), data, dlen (the information
 * field, 1..N401). LA: nr (N(R)), nk (N(k), the credit). LN: ns (N(SA)),
 * code (the attention type). LNA: nr (N(RA)). LD: reason, code (the user
 * code, sent with LI 7 when non-zero; 0 = none). Fields a type does not
 * use are 0 after a parse and ignored by a build. layout is set by a
 * parse to the layout the frame came in. An LR parses as type MNP_LR with
 * data, dlen the whole frame: mnp_lr_parse reads it. */
typedef struct {
    int type, layout, ns, nr, nk, code, reason;
    const uint8_t *data;
    int dlen;
} mnp_hdr_t;

/* mnp_hdr_parse: a CRC-valid frame of a type this does not know */
#define MNP_PARSE_UNKNOWN (-2)

/* Build the body of an LT, LA, LN, LNA or LD in layout into out:
 * MNP_LAYOUT_STD; MNP_LAYOUT_OPT, which changes LT and LA only; or
 * MNP_LAYOUT_STREAM, HST's (LT `04 N(S) data`, LA `05 N(R) N(k)`, LN `06
 * N(SA) code`, LNA `07 N(RA)`, LD `02 reason`). Returns its length, or -1
 * (max too small, another type or layout, a value outside 0..255, an LT
 * with no information or more than MNP_MAX_N401, a streamlined LD with a
 * user code). */
int mnp_hdr_build(uint8_t *out, int max, int layout, const mnp_hdr_t *h);
/* Read the body f of len octets (FCS removed). Returns 0, MNP_PARSE_UNKNOWN
 * or -1 (malformed: too short, an LI past the end, a parameter past the
 * header, a mandatory parameter missing, an empty LT, a streamlined frame
 * of the wrong length). Unknown parameters are skipped.
 *
 * agreed_layout MNP_LAYOUT_STD or _OPT alone: Annex A's layouts only
 * (plain MNP); standard and optimised LT and LA are both taken, told apart
 * by their LI; a frame whose second octet is no type is MNP_PARSE_UNKNOWN.
 *
 * The others read every layout (spec "HST profile"): the standard reading
 * fits when octet 0 is a consistent LI, octet 1 a known type and the
 * header reads (an LR also as an LR); the streamlined one when octet 0 is
 * a type of LT, LA, LN, LNA or LD and the length is its layout's. When
 * both fit, MNP_LAYOUT_STREAM takes the streamlined one, MNP_LAYOUT_STD or
 * _OPT with MNP_RX_ALL the standard one, and MNP_LAYOUT_ANY the one that
 * is an LT with N(S) expect_ns (-1 none), else the standard one. When
 * neither fits: -1 if octet 0 is a type (a streamlined frame of the wrong
 * length) or the standard header is malformed, else MNP_PARSE_UNKNOWN (a
 * USR frame we do not implement, `MNP frames`). An LR is always standard:
 * a frame whose second octet is 1 and whose first is no streamlined type. */
int mnp_hdr_parse(const uint8_t *f, int len, int agreed_layout, int expect_ns, mnp_hdr_t *h);

/* ------------------------------------------------------------ link request */
/* const1: constant parameter 1 (must be 2); framing: parameter 2, the
 * framing mode (2 or 3); k: parameter 3; n401: parameter 4 (two octets,
 * little-endian, as in the Courier's template); opt: parameter 8, the
 * optimisation bit map, when have_opt; c0_flags, c0_rates: USR's
 * parameter 0xC0, when have_c0. Constant parameter 2 is always built as
 * `01 06 01 00 00 00 00 FF` and taken as any value. */
typedef struct {
    int const1, framing, k, n401, have_opt, opt, have_c0;
    uint8_t c0_flags, c0_rates;
} mnp_lr_t;

/* mnp_lr_parse: constant parameter 1 is not 2 (LD reason 2); the other
 * fields are read all the same */
#define MNP_LR_BAD_CONST1 (-2)

/* Build an LR: parameters 1, 2, 3, 4, then 8 and 0xC0 if present. Returns
 * its length, or -1 (max too small, a value out of range). */
int mnp_lr_build(uint8_t *out, int max, const mnp_lr_t *lr);
/* Read an LR body. Returns 0, MNP_LR_BAD_CONST1, or -1 (not an LR, an LI
 * past the end or extended, a parameter past the header, no constant
 * parameter 2 (A.7.1.5.2: always present), no framing mode, no k, a
 * parameter 4 of length 0 or 3 and more). Parameter 4 is two octets,
 * little-endian; one octet is taken as N401 too [OURS]. Unknown
 * parameters are skipped, so are a parameter 8 or 0xC0 of the wrong
 * length; with no parameter 4, n401 is 64 [OURS]; the reserved bits of
 * parameter 8 (3 to 8) are dropped. */
int mnp_lr_parse(const uint8_t *f, int len, mnp_lr_t *lr);
/* USR's 0xC0 (`LR and 0xC0`): flags bit 0 HST, bit 3 the 300 bit/s back
 * channel; rates bit n for 2400n bit/s, n = 2..7. We never agree 16800
 * (code 7): MNP_C0_RATES masks it out. */
#define MNP_C0_HST     0x01
#define MNP_C0_BACK300 0x08
#define MNP_C0_RATES   0x7C
/* the best rate code of a mask: its highest bit, 7 down to 2; 0 none */
int mnp_c0_best(unsigned rates);

/* Our reply to their LR (A.7.1.5): the lower framing mode, the lower k,
 * the smaller N401; parameter 8 only if they sent it, as the AND of both
 * maps. 0xC0 only with hst_profile, when both LRs carry it and theirs has
 * bit 0 (spec "HST profile"): flags = the AND of bit 0 and the OR of bit
 * 3 (300 if either asks), rates = ours AND theirs AND MNP_C0_RATES (0:
 * nothing in common, plain MNP). N401 is never above MNP_MAX_N401.
 * Returns 0, or -1 when the parameters cannot be agreed (framing mode
 * below 2, k or N401 of 0). */
int mnp_lr_merge(const mnp_lr_t *ours, const mnp_lr_t *theirs, int hst_profile, mnp_lr_t *reply);

#endif
