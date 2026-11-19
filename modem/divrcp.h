/* Exact 64-bit division by a divisor used many times, through its
 * reciprocal: ARMv5 has no divide instruction and divides 64-bit numbers in
 * software (some 200 to 300 instructions); this is four 32 by 32 bit
 * multiplies and a correction. Integer only, the same quotient as C's.
 *
 *   uint64_t rc = divrcp_rcp(d);      d > 0
 *   q = divrcp_sdiv(n, d, rc);        n / d, truncated as C truncates
 *
 * With rc = floor((2^64 - 1) / d), the high word of |n| rc is the quotient
 * or one less, for any |n| < 2^64; the remainder decides. */
#ifndef V90D_MODEM_DIVRCP_H
#define V90D_MODEM_DIVRCP_H
#include <stdint.h>

static inline uint64_t divrcp_mulhi(uint64_t u, uint64_t v)
{
    uint32_t u0 = (uint32_t)u, u1 = (uint32_t)(u >> 32), v0 = (uint32_t)v, v1 = (uint32_t)(v >> 32);
    uint64_t p00 = (uint64_t)u0 * v0, p01 = (uint64_t)u0 * v1, p10 = (uint64_t)u1 * v0, p11 = (uint64_t)u1 * v1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static inline uint64_t divrcp_rcp(uint64_t d) { return UINT64_MAX / d; }

static inline int64_t divrcp_sdiv(int64_t n, uint64_t d, uint64_t rc)
{
    uint64_t u = n < 0 ? (uint64_t)0 - (uint64_t)n : (uint64_t)n, q = divrcp_mulhi(u, rc);
    if (u - q * d >= d) q++;
    return n < 0 ? -(int64_t)q : (int64_t)q;
}

#endif
