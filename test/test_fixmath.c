/* modem/fixmath.h against the integer square roots it replaced (audit Task
 * 23b).  Each old_* function is the code as it was in its file, kept here
 * verbatim; the shared one must agree with it on every input the old one
 * accepted (below 2^20, 10^6 random 64-bit values, and the edges). */
#include <stdio.h>
#include <stdint.h>
#include "test.h"
#include "../modem/fixmath.h"

/* anstone.c, v22rx.c, v32_code.c (v32_isqrt64), v92_design.c, v92_p34d.c */
static uint32_t old_u64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

/* probe.c (returned int64_t) */
static int64_t old_probe(int64_t v)
{
    int64_t r = 0, b = (int64_t)1 << 62;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* v90arx.c, qamrx.c (returned int32_t) */
static int32_t old_s32(int64_t v)
{
    int64_t r = 0, b = (int64_t)1 << 62;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int32_t)r;
}

/* v90dil.c */
static int32_t old_u32(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int32_t)r;
}

/* dpsk.c keeps its own (a start at 2^40): it differs from the root at 2^42 */
static int old_dpsk(int64_t v)
{
    int64_t r = 0, b = (int64_t)1 << 40;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (int)r;
}

static uint64_t s = 0x9E3779B97F4A7C15u;
static uint64_t rnd64(void)
{
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;      /* xorshift64 */
    return s;
}

static long compare(uint64_t v)
{
    long bad = 0;
    uint32_t r = isqrt_u64(v);
    bad += r != old_u64(v);
    bad += !((uint64_t)r * r <= v && (r == 0xffffffffu || (uint64_t)(r + 1) * (r + 1) > v));
    if (v <= (uint64_t)INT64_MAX) {
        int64_t sv = (int64_t)v;
        int32_t rs = isqrt_s64(sv);
        bad += rs != (int32_t)(old_probe(sv) > INT32_MAX ? INT32_MAX : old_probe(sv));
        if (old_probe(sv) <= INT32_MAX) bad += rs != old_s32(sv) || rs != (int32_t)old_probe(sv);
    }
    if (v <= 0xffffffffu) bad += (int32_t)isqrt_u64(v) != old_u32((uint32_t)v);
    return bad;
}

int main(void)
{
    long bad = 0;
    uint64_t v;
    int i;
    printf("test_fixmath: one integer square root\n");
    for (v = 0; v < ((uint64_t)1 << 20); v++) bad += compare(v);
    printf("  every input below 2^20: %ld differ\n", bad);
    CHECK_EQ_I(bad, 0);
    bad = 0;
    for (i = 0; i < 1000000; i++) {
        v = rnd64();
        bad += compare(v);
        bad += compare(v >> (rnd64() & 63));          /* every magnitude */
    }
    printf("  10^6 random 64-bit inputs (and shifted ones): %ld differ\n", bad);
    CHECK_EQ_I(bad, 0);
    /* the edges, and perfect squares with their neighbours */
    bad = 0;
    bad += compare(UINT64_MAX) + compare((uint64_t)INT64_MAX) + compare(0) + compare(1);
    for (i = 0; i < 64; i++) bad += compare((uint64_t)1 << i) + compare(((uint64_t)1 << i) - 1);
    for (v = 1; v < 0xffffffffu; v = v * 3 + 1) {
        bad += compare(v * v) + compare(v * v - 1) + compare(v * v + 1);
    }
    printf("  edges, 2^k, squares and their neighbours: %ld differ\n", bad);
    CHECK_EQ_I(bad, 0);
    /* negative: the signed forms give 0, as the old signed copies did */
    CHECK_EQ_I(isqrt_s64(-1), 0);
    CHECK_EQ_I(isqrt_s64(INT64_MIN), 0);
    CHECK_EQ_I(old_probe(-1), 0);
    CHECK_EQ_I(old_s32(INT64_MIN), 0);
    CHECK_EQ_I(isqrt_s64(INT64_MAX), INT32_MAX);        /* clamped, the root is 3037000499 */
    CHECK_EQ_I(isqrt_u64(UINT64_MAX), 0xffffffffu);
    /* dpsk.c's own copy agrees below 2^42, which its callers' pw / 2 stays under in use */
    bad = 0;
    for (i = 0; i < 1000000; i++) {
        v = rnd64() >> (22 + (rnd64() & 31));
        if (v >= ((uint64_t)1 << 42)) v >>= 22;
        bad += old_dpsk((int64_t)v) != (int)isqrt_u64(v);
    }
    printf("  dpsk.c's copy below 2^42: %ld differ\n", bad);
    CHECK_EQ_I(bad, 0);
    CHECK(old_dpsk((int64_t)1 << 62) != (int)isqrt_u64((uint64_t)1 << 62));
    T_REPORT("test_fixmath");
}
