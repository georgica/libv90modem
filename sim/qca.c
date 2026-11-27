/* Test peer: the analogue modem's half of V.92's short Phase 1. See qca.h. */
#include <math.h>
#include "qca.h"
#include "g711.h"

#define FS 8000
#define BAUD 300

/* Table 2: WXYZ 0000..1110 */
static const uint8_t uqts_tab[15] = { 61, 62, 63, 66, 67, 70, 71, 74, 75, 78, 79, 82, 83, 86, 87 };

int qca_uqts_ucode(int wxyz)
{
    return (wxyz >= 0 && wxyz < 15) ? uqts_tab[wxyz] : -1;
}

/* ten bits of a frame, left-most first in time, from a string of 0 and 1 */
static int put(uint8_t *bits, int n, const char *s)
{
    while (*s) bits[n++] = (uint8_t)(*s++ == '1');
    return n;
}

int qca_qc1a_bits(int wxyz, int lapm, uint8_t *bits)
{
    int n = 0, copy;
    for (copy = 0; copy < 2; copy++) {
        n = put(bits, n, "1111111111");         /* bits 0:9, 30:39 */
        n = put(bits, n, "0101010101");         /* 10:19, 40:49 */
        /* 20:29, 50:59: start 0, analogue 0, QC 0, P, then W0XYZ1 */
        bits[n++] = 0; bits[n++] = 0; bits[n++] = 0;
        bits[n++] = (uint8_t)(lapm != 0);
        bits[n++] = (uint8_t)((wxyz >> 3) & 1);         /* W, bit 24 */
        bits[n++] = 0;
        bits[n++] = (uint8_t)((wxyz >> 2) & 1);         /* X, bit 26 */
        bits[n++] = (uint8_t)((wxyz >> 1) & 1);         /* Y, bit 27 */
        bits[n++] = (uint8_t)(wxyz & 1);                /* Z, bit 28 */
        bits[n++] = 1;
    }
    return n;
}

void qca_rx_init(qca_rx_t *r)
{
    v21demod_init(&r->dem, V21H_MARK, V21H_SPACE, FS, BAUD);
    /* a tone of 150 peak, about -43 dBm0 */
    r->squelch = v21demod_energy_of(&r->dem, 150);
    r->level = 1; r->clk = 0; r->hist = 0; r->n = 0; r->got = 0; r->oct = 0;
}

/* the last 30 bits read are one copy of Table 13: ten ONEs, the
 * synchronisation sequence and a character 0 1 1 P 0 0 0 L M 1. Returns its
 * information octet, or -1. hist has the latest bit in bit 0, so bit 29 of
 * hist is the first of the 30 in time. */
static int copy13(uint32_t h)
{
    int i, oct = 0;
    for (i = 0; i < 10; i++) if (((h >> (29 - i)) & 1u) != 1u) return -1;
    for (i = 0; i < 10; i++) if (((h >> (19 - i)) & 1u) != (uint32_t)(i & 1)) return -1;
    if (((h >> 9) & 1u) != 0u || (h & 1u) != 1u) return -1;      /* start, stop */
    for (i = 0; i < 8; i++) oct |= (int)((h >> (8 - i)) & 1u) << i;
    /* bits 21, 22: digital modem, QCA; 24:26 zero */
    if ((oct & 0x03) != 0x03 || (oct & 0x38) != 0) return -1;
    return oct;
}

int qca_rx_tick(qca_rx_t *r, int16_t x)
{
    int lvl = v21demod_tick(&r->dem, x), o;
    if (r->dem.energy < r->squelch) {
        r->hist = 0; r->got = r->got == 2 ? 2 : 0; r->clk = 0; r->level = 1;
        return 0;
    }
    if (lvl != r->level) { r->level = lvl; r->clk = FS / 2; return 0; }
    r->clk += BAUD;
    if (r->clk < FS) return 0;
    r->clk -= FS;
    r->hist = (r->hist << 1) | (uint32_t)lvl;
    if (r->got == 2) return 0;
    if (r->got == 1 && ++r->n == 30) {
        if (copy13(r->hist) == (int)r->oct) { r->got = 2; return 1; }
        r->got = 0;
    }
    if (r->got == 0 && (o = copy13(r->hist)) >= 0) { r->got = 1; r->n = 0; r->oct = (uint8_t)o; }
    return 0;
}

int qca_lm(uint8_t oct)
{
    return ((oct >> 6) & 1) * 2 + ((oct >> 7) & 1);
}

/* the octet of Ucode u with a sign, by the law: mu-law 0xFF - u (+) or
 * 0x7F - u (-); A-law the code u with the sign in bit 7, even bits inverted */
static uint8_t octet(int alaw, int u, int positive)
{
    if (alaw) return (uint8_t)((((positive ? 0x80 : 0) | (u & 0x7F))) ^ 0x55);
    return ucode_to_ulaw((unsigned)u, positive);
}

int qca_octet_ucode(int alaw, uint8_t o)
{
    if (alaw) return (o ^ 0x55) & 0x7F;
    return (~o) & 0x7F;
}

uint8_t qca_qts_octet(int alaw, int ucode, int i)
{
    /* QTS {+V,+0,+V,-V,-0,-V}; QTS\ {-V,-0,-V,+V,+0,+V} */
    int k = i % 6, pos = k < 3;
    if (i >= QCA_QTS_LEN) pos = !pos;
    return octet(alaw, (k % 3 == 1) ? 0 : ucode, pos);
}

uint8_t qca_anspcm_octet(int alaw, int lm, long n)
{
    static const int scl[2][4] = { { 1334, 1000, 708, 500 }, { 667, 500, 354, 250 } };
    const double pi = 3.14159265358979323846;
    long k = n % QCA_ANSPCM_PERIOD;
    double x = scl[alaw != 0][lm & 3] * sqrt(2.0) * cos(2.0 * pi * (double)k * 79.0 / 301.0 + 0.25 * pi / 301.0) + 0.5;
    /* x floored, then G.711 (the 14-bit mu-law and 13-bit A-law scales, which
     * sim/g711 takes times 4 and times 8): all 2408 entries of Tables 7 to 10
     * come out so (docs/history/qc-facts.md, ANSpcm) */
    int32_t v = (int32_t)floor(x);
    uint8_t o = alaw ? linear_to_alaw(v * 8) : linear_to_ulaw(v * 4);
    if ((n / QCA_ANSPCM_REV) & 1) o ^= 0x80;
    return o;
}
