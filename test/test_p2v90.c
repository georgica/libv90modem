/* V.90 Phase 2, analogue modem (modem/p2call in its V.90 mode, 9.2.2)
 * against the library's digital modem (modem/p2d, 9.2.1) over a delayed,
 * attenuated, noisy, echoing path: the INFO exchange, INFO1a of Table 10 as
 * the digital modem reads it, the round trip as both ends measure it (each
 * end's figure depends on the other's 40 ms turn-round), the upstream
 * symbol rate from INFO1d, the recovery procedures, and the time-outs. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "../modem/p2call.h"
#include "../modem/p2d.h"

#define MAXD 4096

typedef struct {
    int d_ad, d_da;             /* one-way delays: analogue -> digital, digital -> analogue */
    double g_ad, g_da;          /* gains */
    double echo;                /* each end's own signal into its receiver */
    double noise;               /* RMS at each receiver */
    int io;                     /* the analogue modem's own sample path, out and back (samples) */
    int mute_ad_to;             /* analogue -> digital dead until this sample */
    int digital_dead_from;      /* the digital modem stops (silent) from this sample, 0 = never */
    int up_sr_mask;             /* the digital modem's INFO1d offer */
    int seconds;
} scen_t;

typedef struct { p2d_t d; p2call_t a; int a_done, d_done; int l12_samples; } run_t;

/* noise without the C library's maths (its log and sqrt differ in the last
 * bit between libraries, and so would this test's output): the sum of four
 * uniform integers, about Gaussian, unit RMS */
static uint32_t nz;
static double noise1(void)
{
    int32_t a = 0;
    int i;
    for (i = 0; i < 4; i++) { nz = nz * 1664525u + 1013904223u; a += (int32_t)(nz >> 16) - 32768; }
    return a / 37837.2;         /* 32768 * sqrt(4 / 3) */
}

static int16_t clip16(double v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : floor(v + 0.5)); }

static void run(const scen_t *s, run_t *r)
{
    static double ba[MAXD], bd[MAXD];
    static int16_t aio[MAXD];
    p2d_cfg dc;
    p2call_cfg ac;
    int n;
    memset(ba, 0, sizeof ba); memset(bd, 0, sizeof bd); memset(aio, 0, sizeof aio);
    memset(r, 0, sizeof *r);
    nz = 3;
    memset(&dc, 0, sizeof dc);
    dc.tx_dbm0 = -12; dc.max_tx_code = 23; dc.up_sr_mask = s->up_sr_mask ? s->up_sr_mask : (1 << 3) | (1 << 4);
    p2d_init(&r->d, &dc);
    memset(&ac, 0, sizeof ac);
    ac.level = 4000; ac.io_delay = s->io; ac.uinfo = 90; ac.md_len = 15;
    p2call_init_v90(&r->a, &ac);
    r->a_done = r->d_done = -1;
    for (n = 0; n < s->seconds * 8000; n++) {
        double xa, xd;
        int16_t ta, td, rxa;
        /* what reaches each receiver now */
        xd = s->g_ad * ba[(n - s->d_ad + MAXD) % MAXD] + s->echo * bd[(n - 1 + MAXD) % MAXD] + s->noise * noise1();
        xa = s->g_da * bd[(n - s->d_da + MAXD) % MAXD] + s->echo * ba[(n - 1 + MAXD) % MAXD] + s->noise * noise1();
        if (n < s->mute_ad_to) xd = s->echo * bd[(n - 1 + MAXD) % MAXD];
        /* the analogue modem's own path: io / 2 out, io / 2 back */
        aio[n % MAXD] = clip16(xa);
        rxa = aio[(n - s->io / 2 + MAXD) % MAXD];
        td = (s->digital_dead_from && n >= s->digital_dead_from) ? 0 : p2d_tick(&r->d, clip16(xd));
        ta = p2call_tick(&r->a, rxa);
        bd[n % MAXD] = td;
        ba[(n + s->io - s->io / 2) % MAXD] = ta;         /* on the line io - io/2 samples later */
        ba[(n + 1 + s->io - s->io / 2) % MAXD] = 0;
        if (r->a.probing) r->l12_samples++;
        if (r->a_done < 0 && (r->a.state == P2L_DONE || r->a.state == P2L_FAILED)) r->a_done = n;
        if (r->d_done < 0 && (r->d.state == P2D_DONE || r->d.state == P2D_FAILED)) r->d_done = n;
        if (r->a_done >= 0 && r->d_done >= 0) break;
    }
}

static scen_t base(void)
{
    scen_t s;
    memset(&s, 0, sizeof s);
    s.d_ad = 80; s.d_da = 80; s.g_ad = 0.5; s.g_da = 0.5; s.echo = 0.1; s.noise = 2; s.seconds = 12;
    return s;
}

static void report(const char *name, const scen_t *s, const run_t *r)
{
    printf("  %-44s analogue %d (err %d) at %d ms, digital %d at %d ms; rtd %d / %d (line %d); up %d, INFO1a: v90 %d, UINFO %d, MD %d, up_sr %d\n",
           name, r->a.state, r->a.error, r->a_done / 8, r->d.state, r->d_done / 8, r->a.rtd, r->d.rtd,
           s->d_ad + s->d_da, r->a.sym_rate, r->d.info1a.v90, r->d.info1a.uinfo, r->d.info1a.md_len, r->d.info1a.up_sr);
}

static void check_ok(const scen_t *s, const run_t *r)
{
    CHECK_EQ_I(r->a.state, P2L_DONE);
    CHECK_EQ_I(r->d.state, P2D_DONE);
    CHECK_EQ_I(r->d.info1a.v90, 1);
    CHECK_EQ_I(r->d.info1a.uinfo, 90);
    CHECK_EQ_I(r->d.info1a.md_len, 15);
    CHECK_EQ_I(r->d.info1a.up_sr, r->a.sym_idx);
    CHECK_NEAR(r->a.rtd, s->d_ad + s->d_da, 3);
    CHECK_NEAR(r->d.rtd, s->d_ad + s->d_da, 3);
    /* what INFO1d asked of our transmitter at that rate */
    CHECK_EQ_I(r->a.tx_hi_carrier, r->d.info1d.sr[r->a.sym_idx].hi_carrier);
    CHECK_EQ_I(r->a.tx_preemph, r->d.info1d.sr[r->a.sym_idx].preemph);
    CHECK_EQ_I(r->a.tx_pwr_red, r->d.info1d.min_pwr_red);
    CHECK(r->d.info1d.sr[r->a.sym_idx].max_rate > 0);
}

int main(void)
{
    scen_t s;
    run_t r;

    printf("V.90 Phase 2: the analogue modem (p2call) against the digital modem (p2d)\n");
    s = base();
    run(&s, &r);
    report("10 ms each way, echo, noise", &s, &r);
    check_ok(&s, &r);
    CHECK_EQ_I(r.a.sym_rate, 3200);
    /* L1 160 ms and L2 until Tone B: the digital modem listens to 460 ms of L2 */
    printf("    our probing lasted %d ms\n", r.l12_samples / 8);
    CHECK(r.l12_samples > 8 * 500 && r.l12_samples < 8 * 800);

    s = base(); s.d_ad = 300; s.d_da = 250; s.g_ad = 0.2; s.g_da = 0.15; s.echo = 0.3; s.noise = 10;
    run(&s, &r);
    report("69 ms round trip, 14 / 16 dB loss, strong echo", &s, &r);
    check_ok(&s, &r);

    s = base(); s.io = 96;
    run(&s, &r);
    report("our own sample path 12 ms (io_delay)", &s, &r);
    check_ok(&s, &r);

    s = base(); s.up_sr_mask = 1 << 3;
    run(&s, &r);
    report("the digital modem offers 3000 only", &s, &r);
    check_ok(&s, &r);
    CHECK_EQ_I(r.a.sym_rate, 3000);

    printf("recovery\n");
    s = base(); s.mute_ad_to = 8 * 400;
    run(&s, &r);
    report("our first INFO0a lost (9.2.2.2.1)", &s, &r);
    check_ok(&s, &r);
    CHECK(r.a.n_info0c >= 2);

    printf("time-outs\n");
    s = base(); s.digital_dead_from = 1;
    run(&s, &r);
    printf("  no digital modem: analogue %d, error %d at %d ms\n", r.a.state, r.a.error, r.a_done / 8);
    CHECK_EQ_I(r.a.state, P2L_FAILED);
    CHECK_EQ_I(r.a.error, P2L_ERR_INFO0);
    CHECK(r.a_done > 0 && r.a_done <= 8 * 8100);
    s = base(); s.digital_dead_from = 8 * 900;          /* gone after the INFO0 exchange */
    run(&s, &r);
    printf("  digital modem gone after INFO0: analogue %d, error %d at %d ms\n", r.a.state, r.a.error, r.a_done / 8);
    CHECK_EQ_I(r.a.state, P2L_FAILED);
    CHECK(r.a_done > 0 && r.a_done < 8 * 11000);
    T_REPORT("p2v90");
}
