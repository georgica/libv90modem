/* The V.90 caller with the end of V.8 moved: every call clean.
 *
 * The same call (test_orig_v90's line and seed) with our Te (V.8 8.1.1:
 * ANSam to the first CM) moved in steps of 6 ms from 1000 to 1126 ms. That
 * moves everything after V.8 by a few milliseconds: other noise, another
 * phase between our A/D clock and the network's when each sequence arrives.
 * A receiver that works only at some of these moments is not robust (before
 * the "robust" branch: 15 of 22 calls bad on 0 km +100 ppm, see PROGRESS.md).
 * Per call: data mode reached, at least the line's floor downstream, and
 * 10 s of data both ways without one failure of the self-synchronising check.
 * Host: 3 lines x 22 calls; TEST_SHORT (32-bit ARM under qemu): 3 x 4. */
#include <stdio.h>
#include <inttypes.h>
#include "test.h"
#include "v90link.h"

#define MS(x) ((int64_t)(x) * 8)

static vl_t L;

typedef struct { const char *name; double km, ppm; int alaw; uint32_t seed; int min_down; } line_t;

int main(void)
{
    static const line_t LN[] = {
        { "0 km, +100 ppm",        0,  100, 0, 21, 38666 },   /* test_orig_v90's row 2 */
        { "2 km,  +30 ppm",        2,   30, 0, 24, 44000 },
        { "2 km,  -50 ppm, A-law", 2,  -50, 1, 31, 41333 },   /* row 12 */
    };
#ifdef TEST_SHORT
    enum { NTE = 4, TSTEP = 36 };
#else
    enum { NTE = 22, TSTEP = 6 };
#endif
    int i, t;
    printf("the V.90 caller against the answerer, Te moved: %d calls per line, 10 s of data each\n", NTE);
    for (i = 0; i < (int)(sizeof LN / sizeof LN[0]); i++) {
        int bad = 0, lo = 99999, hi = 0;
        for (t = 0; t < NTE; t++) {
            vl_cfg c;
            vl_t *l = &L;
            int te = 1000 + TSTEP * t, ok, down;
            int64_t f0d, f0u, d0, u0;
            memset(&c, 0, sizeof c);
            c.loop_km = LN[i].km; c.noise_dbm0 = -75; c.ppm = LN[i].ppm; c.phase = 0.17 * (LN[i].seed % 5);
            c.alaw = LN[i].alaw; c.rbs_phase = (int)(LN[i].seed % 6); c.echo_db = -16;
            c.up_loss_db = 6; c.up_noise_dbm0 = -70; c.full = 1; c.caller_orig = 1; c.seed = LN[i].seed;
            vl_init(l, &c);
            l->org.v8.cfg.te_ms = te;
            while (l->k < MS(40000) && !(orig_moving(&l->org) && answer_moving(&l->ans)) && l->org.phase != ORIG_FAILED) vl_tick(l);
            ok = orig_moving(&l->org) && answer_moving(&l->ans);
            down = l->org.rx_bitrate;
            { int64_t e = l->k + MS(300); while (l->k < e) vl_tick(l); }
            f0d = l->down.fails; f0u = l->up.fails; d0 = l->down.got; u0 = l->up.got;
            { int64_t e = l->k + MS(10000); while (l->k < e && l->org.phase != ORIG_FAILED) vl_tick(l); }
            ok = ok && orig_moving(&l->org) && l->down.fails == f0d && l->up.fails == f0u && down >= LN[i].min_down
                 && l->down.got - d0 > (int64_t)down * 9 && l->up.got - u0 > (int64_t)l->org.bitrate * 9;
            if (!ok) {
                bad++;
                printf("    Te %4d ms: down %5d, %" PRId64 " / %" PRId64 " check failures down / up\n", te, down,
                       l->down.fails - f0d, l->up.fails - f0u);
            }
            if (down < lo) lo = down;
            if (down > hi) hi = down;
        }
        printf("  %-24s %2d of %d calls bad, downstream %d .. %d bit/s\n", LN[i].name, bad, NTE, lo, hi);
        CHECK_EQ_I(bad, 0);
    }
    T_REPORT("orig_v90_te");
}
