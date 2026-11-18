/* Minimal C test harness: no external deps. Count checks, report, exit nonzero
 * on any failure. Portable C99; builds on x86 and the ARM target alike. */
#ifndef V90D_TEST_H
#define V90D_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* TEST_SHORT: some long tests run a shorter version on 32-bit ARM, the box's
 * architecture, where `make check-arm` runs them under qemu. The port matrix
 * (tools/check-ports.sh) builds every target with -DTEST_FULL, so that all
 * of them run the same, whole tests and must print the same. */
#if defined(__arm__) && !defined(TEST_FULL)
#define TEST_SHORT 1
#endif
/* TEST_EMULATED=1: the program runs under an emulator (tools/check-ports.sh
 * says so for its qemu targets), where wall-clock time measures the
 * emulator; a check on it still counts, but cannot fail */
#ifndef TEST_EMULATED
#define TEST_EMULATED 0
#endif

static int t_checks = 0, t_fails = 0;

#define CHECK(cond) do { \
    t_checks++; \
    if (!(cond)) { t_fails++; \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ_I(got, want) do { \
    long long _g = (long long)(got), _w = (long long)(want); \
    t_checks++; \
    if (_g != _w) { t_fails++; \
        printf("  FAIL %s:%d: %s == %lld, want %lld\n", __FILE__, __LINE__, #got, _g, _w); } \
} while (0)

/* near: |got-want| <= tol */
#define CHECK_NEAR(got, want, tol) do { \
    double _g = (double)(got), _w = (double)(want), _t = (double)(tol); \
    t_checks++; \
    if (fabs(_g - _w) > _t) { t_fails++; \
        printf("  FAIL %s:%d: %s = %g, want %g +/- %g\n", __FILE__, __LINE__, #got, _g, _w, _t); } \
} while (0)

#define T_REPORT(name) do { \
    printf("%s: %d checks, %d failures\n", (name), t_checks, t_fails); \
    return t_fails ? 1 : 0; \
} while (0)

#endif
