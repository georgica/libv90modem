/* The V.8bis messages; see v8bismsg.h and docs/history/k56-facts.md. */
#include <string.h>
#include "v8bismsg.h"

static const uint8_t cl_ns9[16] = {
    0x12, 0xc9, 0x80, 0x80, 0x81, 0xc2,
    0x09, 0xb5, 0x02, 0x00, 0x94, 0x81, 0x83, 0x42, 0x46, 0xe0 };
static const uint8_t cl_ns7[13] = {
    0x12, 0xc9, 0x80, 0x80, 0x80,
    0x07, 0xb5, 0x02, 0x00, 0x94, 0x81, 0x83, 0x61 };

#define PREAMBLE 32
#define FLAGS_BEFORE 3

/* ISO/IEC 3309 as 7.2.7 has it: the register preset to ones, octets least
 * significant bit first (the reflected polynomial 0x8408), the ones'
 * complement as the result */
uint16_t v8bis_fcs16(const uint8_t *o, int n)
{
    uint32_t crc = 0xffff;
    int i, b;
    for (i = 0; i < n; i++) {
        crc ^= o[i];
        for (b = 0; b < 8; b++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408u : crc >> 1;
    }
    return (uint16_t)(~crc & 0xffff);
}

int v8bis_cl_octets(int ns_form, int vers, uint8_t *o)
{
    if (ns_form == V8B_NS7) {
        memcpy(o, cl_ns7, sizeof cl_ns7);
        return (int)sizeof cl_ns7;
    }
    memcpy(o, cl_ns9, sizeof cl_ns9);
    o[13] = (uint8_t)(V8B_VER_DIGITAL | (vers & V8B_VER_MASK));
    return (int)sizeof cl_ns9;
}

int v8bis_cl_versions(int ns_form, int vers)
{
    return ns_form == V8B_NS7 ? (cl_ns7[12] & V8B_VER_MASK) : (vers & V8B_VER_MASK);
}

int v8bis_ack1_octets(uint8_t *o)
{
    o[0] = 0x14;   /* revision 1, ACK(1) */
    return 1;
}

typedef struct { uint8_t *bits; int n, ones; } wr_t;

static void put_raw(wr_t *w, int bit) { w->bits[w->n++] = (uint8_t)bit; }

static void put_flag(wr_t *w)
{
    int i;
    put_raw(w, 0);
    for (i = 0; i < 6; i++) put_raw(w, 1);
    put_raw(w, 0);
}

static void put_stuffed(wr_t *w, int bit)
{
    put_raw(w, bit);
    if (bit) {
        if (++w->ones == 5) { put_raw(w, 0); w->ones = 0; }
    } else
        w->ones = 0;
}

static void put_octet(wr_t *w, int v)
{
    int b;
    for (b = 0; b < 8; b++) put_stuffed(w, (v >> b) & 1);
}

int v8bis_frame(const uint8_t *o, int n, uint8_t *bits)
{
    wr_t w;
    uint16_t f;
    int i;

    if (n < 1 || n > V8B_MAXOCT) return 0;
    w.bits = bits; w.n = 0; w.ones = 0;
    for (i = 0; i < PREAMBLE; i++) put_raw(&w, 1);
    for (i = 0; i < FLAGS_BEFORE; i++) put_flag(&w);
    for (i = 0; i < n; i++) put_octet(&w, o[i]);
    f = v8bis_fcs16(o, n);
    put_octet(&w, f & 0xff);
    put_octet(&w, f >> 8);
    put_flag(&w);
    return w.n;
}

void v8bis_rx_init(v8bis_rx_t *r)
{
    memset(r, 0, sizeof *r);
}

static void rx_restart(v8bis_rx_t *r)
{
    r->nbits = 0; r->overflow = 0;
}

/* the frame between two flags has ended with `bits` bits (the closing
 * flag's seven bits before its last zero already taken off) */
static int rx_end(v8bis_rx_t *r, int bits, uint8_t *o)
{
    int n;
    if (bits <= 0) return 0;                 /* flags in a row */
    if (r->overflow || (bits & 7) || bits < 24) return -1;
    n = bits / 8 - 2;
    if (v8bis_fcs16(r->buf, n) !=
        (uint16_t)(r->buf[n] | (r->buf[n + 1] << 8)))
        return -1;
    memcpy(o, r->buf, (size_t)n);
    return n;
}

int v8bis_rx_bit(v8bis_rx_t *r, int bit, uint8_t *o)
{
    int res = 0;

    if (bit) {
        if (r->ones < 7) r->ones++;
        if (r->ones >= 7) {                  /* an abort, or the idle mark */
            if (r->in_frame && r->nbits > 7) res = -1;
            r->in_frame = 0;
            rx_restart(r);
            return res;
        }
    } else {
        int ones = r->ones;
        r->ones = 0;
        if (ones == 6) {                     /* a flag */
            if (r->in_frame) res = rx_end(r, r->nbits - 7, o);
            r->in_frame = 1;
            rx_restart(r);
            return res;
        }
        if (ones == 5) return 0;             /* a stuffed zero */
    }
    if (r->in_frame) {
        if (r->nbits >= 8 * (int)sizeof r->buf)
            r->overflow = 1;        /* nbits stops here: no counter can wrap */
        else {
            if (bit) r->buf[r->nbits >> 3] |= (uint8_t)(1 << (r->nbits & 7));
            else r->buf[r->nbits >> 3] &= (uint8_t)~(1 << (r->nbits & 7));
            r->nbits++;
        }
    }
    return 0;
}

int v8bis_cl_is_k56(const uint8_t *b, int len)
{
    if (len < 1 || b[0] <= 6 || len < b[0] + 1) return 0;
    return b[1] == 0xb5 && b[2] == 0x02 && (b[6] & 1) &&
           (b[7] & 0x2f) >= 2 && (b[7] & 0x40);
}

/* ---- the information-field trees (8.2.3) ---- */

/* skip a level 1 block (bit 8 ends it); returns the new position or -1 */
static int skip1(const uint8_t *o, int n, int p)
{
    while (p < n) {
        if (o[p++] & 0x80) return p;
    }
    return -1;
}

/* skip one octet block of level 2 or 3 (bit 7 ends it) */
static int skip23(const uint8_t *o, int n, int p)
{
    while (p < n) {
        if (o[p++] & 0x40) return p;
    }
    return -1;
}

/* one Par(2) block: {NPar(2)}, then unless the last NPar(2) octet has bit 8
 * as well, {SPar(2)} and an {NPar(3)} block for each SPar(2) bit set */
static int skip_par2(const uint8_t *o, int n, int p)
{
    int q = p, sp, se, k, bits, last;

    while (q < n && !(o[q] & 0x40)) q++;
    if (q >= n) return -1;
    last = o[q++];
    if (last & 0x80) return q;
    sp = q;
    q = skip23(o, n, q);
    if (q < 0) return -1;
    se = q;
    for (k = sp; k < se; k++)
        for (bits = o[k] & 0x3f; bits; bits &= bits - 1) {
            q = skip23(o, n, q);
            if (q < 0) return -1;
        }
    return q;
}

/* one field's tree: {NPar(1)}, {SPar(1)}, a Par(2) block per SPar(1) bit.
 * *first gets the first NPar(1) octet. Returns the position after it, -1 */
static int skip_field(const uint8_t *o, int n, int p, int *first)
{
    int q, sp, se, k, bits;

    if (p >= n) return -1;
    *first = o[p];
    q = skip1(o, n, p);
    if (q < 0) return -1;
    sp = q;
    q = skip1(o, n, q);
    if (q < 0) return -1;
    se = q;
    for (k = sp; k < se; k++)
        for (bits = o[k] & 0x7f; bits; bits &= bits - 1) {
            q = skip_par2(o, n, q);
            if (q < 0) return -1;
        }
    return q;
}

/* where the MS's K56flex block begins (its length octet), -1 none */
static int ms_block(const uint8_t *o, int n)
{
    int p, first, sfirst;

    if (n < 1 || (o[0] & 0x0f) != V8B_MSG_MS) return -1;
    p = skip_field(o, n, 1, &first);          /* identification field */
    if (p < 0) return -1;
    p = skip_field(o, n, p, &sfirst);         /* standard information field */
    if (p < 0) return -1;
    if (!(first & 0x40)) return -1;           /* no NS field */
    while (p < n) {                           /* the NS blocks */
        int len = o[p];
        if (p + 1 + len > n) break;           /* not whole: nor those after it */
        if (len > 6 && o[p + 1] == 0xb5 && o[p + 2] == 0x02 && (o[p + 6] & 1))
            return p;
        p += 1 + len;
    }
    return -1;
}

int v8bis_ms_is_k56(const uint8_t *o, int n) { return ms_block(o, n) >= 0; }

/* len > 6: blk[7] is in the block */
int v8bis_ms_version(const uint8_t *o, int n)
{
    int p = ms_block(o, n);
    return p < 0 ? -1 : o[p + 7];
}

int v8bis_version_of(int octet)
{
    int v = 0, m = octet & V8B_VER_MASK;
    while (m) { v++; m >>= 1; }
    return v;
}

/* the octets from a to b (a block) carry no parameter bits but those in
 * keep, in the first octet; mask: the level's parameter bits */
static int only(const uint8_t *o, int a, int b, int mask, int keep)
{
    int k;
    for (k = a; k < b; k++)
        if (o[k] & mask & ~(k == a ? keep : 0)) return 0;
    return 1;
}

int v8bis_ms_v8(const uint8_t *o, int n)
{
    int p, q, sp, se, end, first, sfirst;

    if (n < 1 || (o[0] & 0x0f) != V8B_MSG_MS) return 0;
    p = skip_field(o, n, 1, &first);              /* identification field */
    if (p < 0 || !(first & 0x01)) return 0;       /* V.8 requested */
    end = skip_field(o, n, p, &sfirst);           /* the standard information field parses */
    if (end < 0) return 0;
    sp = skip1(o, n, p);                          /* past its {NPar(1)}: {SPar(1)} */
    se = skip1(o, n, sp);
    if (!(o[sp] & 0x01) || !only(o, sp, se, 0x7f, 0x01)) return 0;    /* data, and nothing else */
    /* the data Par(2) block, the only one: {NPar(2)} of bits 1 to 4 ... */
    q = se;
    while (!(o[q] & 0x40)) q++;
    if (!only(o, se, q + 1, 0x3f, 0x0f)) return 0;
    /* ... and an {SPar(2)}, if there is one, with none set (so no NPar(3)) */
    if (!(o[q] & 0x80) && !only(o, q + 1, end, 0x3f, 0)) return 0;
    if (v8bis_ms_is_k56(o, n)) return 0;
    return (first & 0x08) ? 2 : 1;
}
