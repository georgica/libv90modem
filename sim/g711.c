#include "g711.h"

/* V.90 Table 1 grid: magnitude u = (seg<<4)|step, seg 0..7, step 0..15.
 * linear = ((((2*step + 33) << seg) - 33) << 2). */
int32_t ucode_linear(unsigned u)
{
    unsigned seg = (u >> 4) & 7u, step = u & 15u;
    return (int32_t)((((2u * step + 33u) << seg) - 33u) << 2);
}

uint8_t ucode_to_ulaw(unsigned u, int positive)
{
    return (uint8_t)((positive ? 0xFFu : 0x7Fu) - (u & 0x7Fu));
}

int32_t ulaw_to_linear(uint8_t b)
{
    int32_t v = ucode_linear((unsigned)(~b) & 0x7Fu);
    return (b & 0x80u) ? v : -v;
}

uint8_t linear_to_ulaw(int32_t v)
{
    int pos = v >= 0;
    uint32_t m = (uint32_t)(pos ? v : -v);
    unsigned seg = 0, u;
    m >>= 2;                 /* to the Table 1 pre-scale */
    if (m > 8158u) m = 8158u;
    m += 33u;                /* add bias */
    while ((m >> (seg + 6)) != 0 && seg < 7) seg++;
    u = (seg << 4) | ((m >> (seg + 1)) & 15u);
    return ucode_to_ulaw(u, pos);
}

int32_t alaw_to_linear(uint8_t a)
{
    unsigned c = a ^ 0x55u, seg = (c >> 4) & 7u, mant = c & 15u;
    int32_t m = seg == 0 ? (int32_t)(2 * mant + 1) : (int32_t)((2 * mant + 33) << (seg - 1));
    return (c & 0x80u) ? m * 8 : -m * 8;
}

uint8_t linear_to_alaw(int32_t v)
{
    int pos = v >= 0;
    uint32_t m = (uint32_t)(pos ? v : -(int64_t)v) >> 3;    /* 13-bit magnitude */
    unsigned seg = 0, c;
    if (m > 4095u) m = 4095u;
    if (m < 32u) c = m >> 1;
    else {
        seg = 1;
        while (seg < 7 && m >= (32u << seg)) seg++;
        c = (seg << 4) | ((m >> seg) & 15u);
    }
    if (pos) c |= 0x80u;
    return (uint8_t)(c ^ 0x55u);
}
