/* V.92 Phase 2, analogue modem (modem/p2call's V.90 mode with V.92 on, 9.3
 * of V.92) against the library's digital modem (modem/p2d with and without
 * p2d_offer_v92): INFO0a bit 26 as p2d reads it; INFO1a of Table 18 when
 * both sides indicated V.92 and INFO1d's bit 70 allows PCM upstream, with
 * UINFO, MD and what our transmitter's precoder and prefilter can take; the
 * V.90 INFO1a otherwise (the digital modem without V.92, or not allowing
 * PCM upstream, or our own V.92 off), the outcome as v90a reads it. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "../modem/p2call.h"
#include "../modem/p2d.h"

#define MAXD 4096

typedef struct { p2d_t d; p2call_t a; int a_done, d_done; } run_t;

static uint32_t nz;
static double noise1(void)
{
    int32_t a = 0;
    int i;
    for (i = 0; i < 4; i++) { nz = nz * 1664525u + 1013904223u; a += (int32_t)(nz >> 16) - 32768; }
    return a / 37837.2;
}

static int16_t clip16(double v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : floor(v + 0.5)); }

/* digital: 0 V.90 only, 1 V.92 with PCM upstream, 2 V.92 without (INFO1d bit 70 = 0);
 * ours: V.92 on or off */
static void run(int digital, int ours, run_t *r)
{
    static double ba[MAXD], bd[MAXD];
    p2d_cfg dc;
    p2call_cfg ac;
    int n, d = 80;
    memset(ba, 0, sizeof ba); memset(bd, 0, sizeof bd);
    memset(r, 0, sizeof *r);
    nz = 3;
    memset(&dc, 0, sizeof dc);
    dc.tx_dbm0 = -12; dc.max_tx_code = 23; dc.up_sr_mask = (1 << 3) | (1 << 4);
    p2d_init(&r->d, &dc);
    if (digital) p2d_offer_v92(&r->d, digital == 1);
    memset(&ac, 0, sizeof ac);
    ac.level = 4000; ac.uinfo = 90; ac.md_len = 0;
    ac.v92 = ours;
    p2call_init_v90(&r->a, &ac);
    r->a_done = r->d_done = -1;
    for (n = 0; n < 12 * 8000; n++) {
        double xa, xd;
        int16_t ta, td;
        xd = 0.5 * ba[(n - d + MAXD) % MAXD] + 0.1 * bd[(n - 1 + MAXD) % MAXD] + 2 * noise1();
        xa = 0.5 * bd[(n - d + MAXD) % MAXD] + 0.1 * ba[(n - 1 + MAXD) % MAXD] + 2 * noise1();
        td = p2d_tick(&r->d, clip16(xd));
        ta = p2call_tick(&r->a, clip16(xa));
        bd[n % MAXD] = td;
        ba[n % MAXD] = ta;
        if (r->a_done < 0 && (r->a.state == P2L_DONE || r->a.state == P2L_FAILED)) r->a_done = n;
        if (r->d_done < 0 && (r->d.state == P2D_DONE || r->d.state == P2D_FAILED)) r->d_done = n;
        if (r->a_done >= 0 && r->d_done >= 0) break;
    }
}

int main(void)
{
    static const struct { const char *name; int digital, ours, pcm, mode; } S[] = {
        { "both V.92, PCM upstream allowed", 1, 1, 1, P2D_MODE_V92 },
        { "both V.92, INFO1d bit 70 = 0", 2, 1, 0, P2D_MODE_V90 },
        { "the digital modem V.90 only", 0, 1, 0, P2D_MODE_V90 },
        { "ours with V.92 off", 1, 0, 0, P2D_MODE_V90 },
    };
    int i;
    run_t r;

    printf("V.92 Phase 2: the analogue modem (p2call) against the digital modem (p2d)\n");
    for (i = 0; i < (int)(sizeof S / sizeof S[0]); i++) {
        run(S[i].digital, S[i].ours, &r);
        printf("  %-34s analogue %d at %d ms, digital %d at %d ms: INFO0a v92 %d, INFO0d v92 %d, mode %d, pcm %d;"
               " INFO1a pcm %d, UINFO %d, MD %d, sections %d, Ltot %d, Lmax %d\n",
               S[i].name, r.a.state, r.a_done / 8, r.d.state, r.d_done / 8, r.d.info0a.v92, r.a.info0d.v92, r.d.mode,
               r.a.pcm_up, r.d.info1a.pcm, r.d.info1a.uinfo, r.d.info1a.md_len, r.d.info1a.sections,
               192 + 64 * r.d.info1a.ltot_code, 128 + 64 * r.d.info1a.lmax_code);
        CHECK_EQ_I(r.a.state, P2L_DONE);
        CHECK_EQ_I(r.d.state, P2D_DONE);
        CHECK_EQ_I(r.d.info0a.v92, S[i].ours);
        CHECK_EQ_I(r.a.info0d.v92, S[i].digital != 0);
        CHECK_EQ_I(r.d.mode, S[i].mode);
        CHECK_EQ_I(r.a.pcm_up, S[i].pcm);
        CHECK_EQ_I(r.d.info1a.pcm, S[i].pcm);
        CHECK_EQ_I(r.d.info1a.uinfo, 90);
        CHECK_NEAR(r.a.rtd, 160, 3);
        if (S[i].pcm) {
            /* Table 18: our precoder and prefilter have all four sections, the
             * longest filters INFO1a can announce; no MD */
            CHECK_EQ_I(r.d.info1a.sections, 3);
            CHECK_EQ_I(r.d.info1a.ltot_code, 3);
            CHECK_EQ_I(r.d.info1a.lmax_code, 3);
            CHECK_EQ_I(r.d.info1a.md_len, 0);
        } else {
            /* V.90's INFO1a: an upstream symbol rate */
            CHECK(r.a.sym_rate == 3000 || r.a.sym_rate == 3200);
            CHECK_EQ_I(r.d.info1a.up_sr, r.a.sym_idx);
        }
    }
    T_REPORT("p2v92a");
}
