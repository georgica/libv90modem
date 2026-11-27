/* V.92 short Phase 2 (9.4.1, 9.4.2, Figure 9), the digital modem: p2d
 * against the simulated caller (sim/p2a) over sim/channel, at a round-trip
 * delay of 0, 400 and 2400 samples. INFO0d with bit 26, the short exchange
 * ended by INFO1a of Table 18 or Table 19, a caller that does not ask for it,
 * and the recoveries of 9.4.1.2.2 and 9.4.1.2.3 into the full Phase 2. */
#include <string.h>
#include "test.h"
#include "../modem/p2d.h"
#include "../sim/p2a.h"
#include "../sim/channel.h"
#include "../sim/g711.h"

#define MAXD 4096
#define MS(x) ((x) * 8)
#define TAIL 80

/* INFO0d (62 bits, first in time first) as the 0.7.3 library sent it after
 * p2d_init() and p2d_offer_v92(p, 1), with the p2d_cfg of run() below:
 * captured from v0.7.3's modem/p2d.c, not from this tree */
static const char info0d_073_mu[] = "11110111001000011110010100010011011101000001100100000010011111";
static const char info0d_073_a[]  = "11110111001000011110010100010011011101010000100100100011011111";

typedef struct {
    int     shrt;       /* p2d_offer_short() */
    int     alaw;
    int     D;          /* round-trip delay of the line, samples */
    p2a_cfg ac;
} scen_t;

typedef struct {
    p2d_t   d; p2a_t a;
    int     n;
    int32_t d_done_at, d_fail_at;
    unsigned seen;      /* bit s: p2d was in state s */
    int32_t rev_at;     /* our short Phase 2 reversal at the line (b_rev_line), -1 = none */
    int32_t full_at;    /* when P2D_V92_SHORT_FULL was set, -1 = never */
    int     loud;       /* our nonzero samples from 10 ms (plus the ramp) after that reversal to the end of short Phase 2 */
    char    info0d[INFO0D_BITS + 1];
} run_t;

static void caller(p2a_cfg *ac, int pcm_up, int shrt)
{
    memset(ac, 0, sizeof *ac);
    ac->level = 4000; ac->v90 = 1; ac->up_sr = -1; ac->uinfo = 91;
    ac->info0a.lo3000 = ac->info0a.hi3000 = 1; ac->info0a.lo3200 = ac->info0a.hi3200 = 1;
    ac->info0a.pwr_reduce = 1; ac->info0a.max_sr_diff = 5; ac->info0a.c1664 = 1;
    ac->info0a.v92 = 1;
    ac->pcm_up = pcm_up; ac->sections = 2; ac->ltot_code = 1; ac->lmax_code = 3; ac->md_len = 0;
    ac->short_p2 = shrt; ac->info0a.short_p2 = (uint8_t)(shrt != 0);
}

static void run(const scen_t *s, run_t *r)
{
    static uint8_t ds[MAXD], us[MAXD];
    static const double one[1] = { 1.0 };
    channel_cfg cc; channel_t ch;
    p2d_cfg dc;
    int16_t dtx = 0, atx = 0;
    int i, k, was;

    memset(ds, 0xff, sizeof ds); memset(us, 0xff, sizeof us);      /* mu-law zero */
    memset(&cc, 0, sizeof cc);
    cc.ds_noise_dbfs = -70; cc.us_noise_dbfs = -70; cc.ds_taps = one; cc.ds_ntaps = 1; cc.us_taps = one; cc.us_ntaps = 1;
    cc.echo_gain = 0.05; cc.echo_delay = 12; cc.full_scale = 32124; cc.seed = 92;
    channel_init(&ch, &cc);
    memset(&dc, 0, sizeof dc);
    dc.tx_dbm0 = -12; dc.max_tx_code = 23; dc.up_sr_mask = (1 << 3) | (1 << 4); dc.up_hi_carrier = 1 << 4;
    dc.alaw = s->alaw;
    p2d_init(&r->d, &dc);
    p2d_offer_v92(&r->d, 1);
    if (s->shrt) p2d_offer_short(&r->d);
    for (k = 0; k < INFO0D_BITS; k++) r->info0d[k] = (char)('0' + (k < r->d.tx.nbits ? r->d.tx.bits[k] : 9));
    r->info0d[INFO0D_BITS] = 0;
    p2a_init(&r->a, &s->ac);
    r->d_done_at = r->d_fail_at = r->rev_at = r->full_at = -1;
    r->seen = 0; r->loud = 0;
    for (i = 0; i < 8000 * 16; i++) {
        double crx;
        uint8_t srx;
        int32_t xa;
        ds[i % MAXD] = linear_to_ulaw(dtx);
        channel_tick(&ch, ds[(i - s->D / 2 + MAXD) % MAXD], (double)atx, &crx, &srx);
        us[i % MAXD] = srx;
        xa = (int32_t)(crx >= 0 ? crx + 0.5 : crx - 0.5);
        if (xa > 32767) xa = 32767;
        if (xa < -32768) xa = -32768;
        was = (int)r->d.state;
        dtx = p2d_tick(&r->d, (int16_t)ulaw_to_linear(us[(i - (s->D - s->D / 2) + MAXD) % MAXD]));
        atx = p2a_tick(&r->a, (int16_t)xa);
        r->seen |= 1u << (unsigned)r->d.state;
        if (was == P2D_SHORT_REV && r->d.state != P2D_SHORT_REV && r->rev_at < 0) r->rev_at = r->d.b_rev_line;
        if ((r->d.v92 & P2D_V92_SHORT_FULL) && r->full_at < 0) r->full_at = r->d.now;
        if (r->rev_at >= 0 && r->full_at < 0 && r->d_done_at < 0 && r->d.now > r->rev_at + TAIL + 80 && dtx != 0) r->loud++;
        if (r->d.state == P2D_DONE && r->d_done_at < 0) r->d_done_at = r->d.now;
        if (r->d.state == P2D_FAILED && r->d_fail_at < 0) r->d_fail_at = r->d.now;
        if ((r->d.state == P2D_DONE || r->d.state == P2D_FAILED) &&
            (r->a.state == P2A_DONE || r->a.state == P2A_FAILED)) break;
    }
    r->n = i;
}

#define SEEN(r, s) (((r)->seen >> (unsigned)(s)) & 1u)

int main(void)
{
    static run_t r, f;
    static const int Ds[3] = { 0, 400, 2400 };
    scen_t s;
    int j;

    /* ---- info0d_bits: INFO0d, Table 15 bits 26 (short Phase 2) and 27 (V.92) ---- */
    memset(&s, 0, sizeof s);
    caller(&s.ac, 1, 0);
    run(&s, &r);
    CHECK(strcmp(r.info0d, info0d_073_mu) == 0);
    CHECK_EQ_I(r.info0d[26], '0'); CHECK_EQ_I(r.info0d[27], '1');
    s.alaw = 1;
    run(&s, &r);
    CHECK(strcmp(r.info0d, info0d_073_a) == 0);
    s.alaw = 0; s.shrt = 1;
    run(&s, &r);
    CHECK_EQ_I(r.info0d[26], '1'); CHECK_EQ_I(r.info0d[27], '1'); CHECK_EQ_I(r.info0d[28], '0');
    printf("info0d_bits: 0.7.3 %s\n             short %s\n", info0d_073_mu, r.info0d);

    for (j = 0; j < 3; j++) {
        int D = Ds[j];

        /* ---- short_t18: Table 18, PCM upstream, no probing, no INFO1d ---- */
        memset(&s, 0, sizeof s);
        s.D = D; s.shrt = 1;
        caller(&s.ac, 1, 1);
        run(&s, &r);
        CHECK_EQ_I(r.d.state, P2D_DONE); CHECK_EQ_I(r.a.state, P2A_DONE);
        CHECK_EQ_I(r.d.mode, P2D_MODE_V92);
        CHECK(r.d.v92 & P2D_V92_ON); CHECK(r.d.v92 & P2D_V92_SHORT);
        CHECK(r.d.v92 & P2D_V92_SHORT_RAN); CHECK(!(r.d.v92 & P2D_V92_SHORT_FULL));
        CHECK_EQ_I(r.a.short_run, 1);
        CHECK(!SEEN(&r, P2D_INFO1D)); CHECK(!SEEN(&r, P2D_L1)); CHECK(!SEEN(&r, P2D_L2));
        CHECK(!SEEN(&r, P2D_PROBE_RX)); CHECK(!SEEN(&r, P2D_WAIT_REV1));
        CHECK(r.rev_at > 0); CHECK_EQ_I(r.loud, 0);
        CHECK_EQ_I(r.d.n_recover, 0);
        CHECK_EQ_I(r.d.info1a.pcm, 1); CHECK_EQ_I(r.d.info1a.uinfo, 91);
        /* the full Phase 2 on the same line */
        s.shrt = 0; s.ac.short_p2 = 0; s.ac.info0a.short_p2 = 0;
        run(&s, &f);
        CHECK_EQ_I(f.d.state, P2D_DONE); CHECK_EQ_I(f.d.mode, P2D_MODE_V92);
        CHECK(SEEN(&f, P2D_INFO1D)); CHECK_EQ_I(f.d.v92, P2D_V92_ON);
        CHECK_NEAR(r.d.rtd, f.d.rtd, 16);
        CHECK(f.d_done_at - r.d_done_at >= MS(1500));
        printf("short_t18 D %4d: done %.3f s (full %.3f s), rtd %d (full %d), v92 %d\n", D,
               r.d_done_at / 8000.0, f.d_done_at / 8000.0, (int)r.d.rtd, (int)f.d.rtd, r.d.v92);

        /* ---- short_t19: Table 19, V.34 upstream at 3200 on the high carrier ---- */
        memset(&s, 0, sizeof s);
        s.D = D; s.shrt = 1;
        caller(&s.ac, 0, 1);
        s.ac.short_t19 = 1; s.ac.short_sr = 4; s.ac.short_hi = 1;
        run(&s, &r);
        CHECK_EQ_I(r.d.state, P2D_DONE); CHECK_EQ_I(r.d.mode, P2D_MODE_V90);
        CHECK(r.d.v92 & P2D_V92_SHORT_RAN); CHECK(!(r.d.v92 & P2D_V92_SHORT_FULL));
        CHECK_EQ_I(r.d.info1a.hi_carrier, 1); CHECK_EQ_I(r.d.info1a.up_sr, 4);
        CHECK_EQ_I(r.d.info1a.pcm, 0); CHECK_EQ_I(r.d.info1a.uinfo, 91);
        CHECK(!SEEN(&r, P2D_INFO1D)); CHECK_EQ_I(r.loud, 0);
        printf("short_t19 D %4d: done %.3f s, rtd %d, up_sr %d hi %d\n", D, r.d_done_at / 8000.0,
               (int)r.d.rtd, r.d.info1a.up_sr, r.d.info1a.hi_carrier);

        /* ---- peer_no_short: INFO0a bit 27 clear: full Phase 2, INFO1d of Table 17 ---- */
        memset(&s, 0, sizeof s);
        s.D = D; s.shrt = 1;
        caller(&s.ac, 1, 0);
        run(&s, &r);
        CHECK_EQ_I(r.d.state, P2D_DONE); CHECK_EQ_I(r.d.mode, P2D_MODE_V92);
        CHECK_EQ_I(r.d.v92, P2D_V92_ON | P2D_V92_SHORT);
        CHECK(SEEN(&r, P2D_INFO1D)); CHECK(SEEN(&r, P2D_L2)); CHECK(!SEEN(&r, P2D_SHORT_REV));
        CHECK_EQ_I(r.a.info0d.short_p2, 1);
        CHECK_EQ_I(r.a.info1d.sr[5].hi_carrier, 1);         /* bit 70 of Table 17 */
        CHECK_EQ_I(r.d.n_recover, 0);
        printf("peer_no_short D %4d: done %.3f s, rtd %d, v92 %d\n", D, r.d_done_at / 8000.0, (int)r.d.rtd, r.d.v92);

        /* ---- recovery (9.4.1.2.2, 9.4.1.2.3, Readings 8) ---- */
        {
            static const char *const nm[3] = { "no_rev", "no_info1a", "t19_3429" };
            int c;
            for (c = 0; c < 3; c++) {
                memset(&s, 0, sizeof s);
                s.D = D; s.shrt = 1;
                caller(&s.ac, 1, 1);
                if (c == 0) s.ac.short_no_rev = 1;
                if (c == 1) s.ac.short_no_info1a = 1;
                if (c == 2) { s.ac.pcm_up = 0; s.ac.short_t19 = 1; s.ac.short_sr = 5; }
                run(&s, &r);
                CHECK_EQ_I(r.d.state, P2D_DONE);
                CHECK(r.d.v92 & P2D_V92_SHORT_FULL); CHECK(!(r.d.v92 & P2D_V92_SHORT_RAN));
                CHECK(SEEN(&r, P2D_SHORT_REV)); CHECK(SEEN(&r, P2D_INFO1D)); CHECK(SEEN(&r, P2D_L2));
                CHECK_EQ_I(r.d.n_recover, 1);
                CHECK(r.rev_at > 0); CHECK(r.full_at > r.rev_at);
                if (c < 2) CHECK_NEAR(r.full_at - r.rev_at, MS(2500), 160);
                if (c == 0) CHECK(SEEN(&r, P2D_RECOVER)); else CHECK(!SEEN(&r, P2D_RECOVER));
                CHECK_EQ_I(r.d.mode, c == 2 ? P2D_MODE_V90 : P2D_MODE_V92);
                printf("%s D %4d: full after %.3f s, done %.3f s, rtd %d, mode %d, v92 %d\n", nm[c], D,
                       (r.full_at - r.rev_at) / 8000.0, r.d_done_at / 8000.0, (int)r.d.rtd, r.d.mode, r.d.v92);
            }
        }
    }

    /* ---- alaw_short: INFO0d bit 39 (A-law), short Phase 2 done ---- */
    memset(&s, 0, sizeof s);
    s.D = 400; s.shrt = 1; s.alaw = 1;
    caller(&s.ac, 1, 1);
    run(&s, &r);
    CHECK_EQ_I(r.info0d[39], '1'); CHECK_EQ_I(r.info0d[26], '1'); CHECK_EQ_I(r.info0d[27], '1');
    CHECK_EQ_I(r.a.info0d.alaw, 1);
    CHECK_EQ_I(r.d.state, P2D_DONE); CHECK_EQ_I(r.d.mode, P2D_MODE_V92);
    CHECK(r.d.v92 & P2D_V92_SHORT_RAN);
    printf("alaw_short: done %.3f s, rtd %d\n", r.d_done_at / 8000.0, (int)r.d.rtd);

    T_REPORT("p2_qc");
}
