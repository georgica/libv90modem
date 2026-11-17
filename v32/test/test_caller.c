/* The calling modem (v32_init_call, 6.1, 7.1) against the answering modem of
 * this directory (v32_init) over the simulated 2-wire line, the caller at
 * the network side. There is no independent V.32bis answerer to test
 * against (sim/v32call is a caller; spandsp has no V.32bis): what the
 * answerer has been checked against is sim/v32call (test_startup,
 * test_call), and here the caller is checked against the Recommendation's
 * timing (the 64 T turn-round at its line terminals, NT, the length of every
 * segment it sends) as well as by reaching data with the answerer. */
#include <string.h>
#include "test.h"
#include "v32_priv.h"
#include "v32line.h"

#define T (10.0 / 3)

#ifdef __arm__
#define SECS 1
#else
#define SECS 3
#endif

/* 64 zeros, a one, then a pseudo-random stream; the checker finds the one */
typedef struct { uint32_t l; int n; } src_t;
typedef struct { uint32_t l; int zeros, synced; long bits, errs; } chk_t;
static int src_bit(void *u)
{
    src_t *s = u;
    if (s->n < 65) return s->n++ == 64;
    s->l = s->l * 1103515245u + 12345u;
    return (s->l >> 16) & 1;
}
static void chk_bit(void *u, int b)
{
    chk_t *c = u;
    if (!c->synced) {
        if (!b) c->zeros++;
        else { if (c->zeros >= 48) c->synced = 1; c->zeros = 0; }
        return;
    }
    c->l = c->l * 1103515245u + 12345u;
    c->bits++;
    c->errs += b != (int)((c->l >> 16) & 1);
}

static v32_t C, A;                 /* caller (network side), answerer (far port) */
static v32line_t L;
static src_t c_src, a_src; static chk_t c_chk, a_chk;
static int16_t c_rx; static long now;
static int a_mute;

static int  c_get(void *u) { (void)u; return src_bit(&c_src); }
static void c_put(void *u, int b) { (void)u; chk_bit(&c_chk, b); }
static int  a_get(void *u) { (void)u; return src_bit(&a_src); }
static void a_put(void *u, int b) { (void)u; chk_bit(&a_chk, b); }

static void data_reset(void)
{
    memset(&c_src, 0, sizeof c_src); memset(&a_src, 0, sizeof a_src);
    memset(&c_chk, 0, sizeof c_chk); memset(&a_chk, 0, sizeof a_chk);
    c_src.l = a_chk.l = 1; a_src.l = c_chk.l = 7;
}

/* A test-only initiator of a rate renegotiation (8.1) at the answering end:
 * the library's answerer has none, so the test drives its transmitter and
 * receiver by hand. AC 56 T, CA 8 T, R4 (scrambler from zero) until R5 has
 * been read and R4 has gone on for 64 T, E naming the highest rate common
 * to R4 and R5, scrambled ones for 24 T, data; the receiver reads R5 once
 * the caller's preamble is on its way and unclamps 24 T after its E. */
enum { INI_OFF, INI_AC, INI_CA, INI_R4, INI_E, INI_B1, INI_DONE };
static int ini, ini_rate, ini_listen, ini_clamped;
static unsigned ini_r4;
static uint16_t ini_r5;

static void rate_search(v32rx_t *r)
{
    v32rx_set_mode(r, V32_RX_DD4);
    r->frame_bit = -1; r->nbits = 0; r->bits = 0; r->rate_word = 0; r->rate_seen = 0; r->e_word = 0;
}

static void initiator(void)
{
    v32tx_t *t = &A.tx;
    v32rx_t *r = &A.rx;
    if (ini == INI_OFF || ini == INI_DONE) return;
    if (!ini_listen && C.state == V32S_C_RN_AA) { rate_search(r); ini_listen = 1; }
    if (ini_listen && !ini_r5 && r->mode == V32_RX_DD4 && r->rate_seen >= 2) ini_r5 = r->rate_word;
    if (ini_listen && r->e_word && r->mode == V32_RX_DATA && !ini_clamped) { r->clamp = 24; ini_clamped = 1; }
    if (!v32tx_symbol_due(t)) return;
    switch (ini) {
    case INI_AC:
        if (t->mode != V32_TX_AC) v32tx_set_mode(t, V32_TX_AC);
        else if (t->nsym >= 56) { v32tx_set_mode(t, V32_TX_CA); ini = INI_CA; }
        break;
    case INI_CA:
        if (t->nsym >= 8) {
            t->scr.reg = 0; t->rword = v32_rate_word(ini_r4);
            v32tx_set_mode(t, V32_TX_RATE); ini = INI_R4;
        }
        break;
    case INI_R4:
        if (ini_r5 && t->nsym >= 64 && t->rbit == 0) {
            unsigned common = ini_r4 & v32_word_rates(ini_r5);
            int k;
            for (ini_rate = -1, k = 0; k < V32_NRATES; k++) if (common & (1u << k)) ini_rate = k;
            t->rword = (uint16_t)(v32_rate_word(ini_rate >= 0 ? 1u << ini_rate : 0) | V32_RW_E);
            t->nsym = 1;                   /* (counts E from here: RATE goes on) */
            ini = INI_E;
        }
        break;
    case INI_E:
        if (t->nsym >= 9 && ini_rate >= 0) { t->rate = ini_rate; v32tx_set_mode(t, V32_TX_ONES); ini = INI_B1; }
        break;
    case INI_B1:
        if (t->nsym >= 24) {
            v32tx_set_mode(t, V32_TX_DATA);
            A.rate = ini_rate; A.bitrate = V32_BITRATE(ini_rate);
            ini = INI_DONE;
        }
        break;
    default:
        break;
    }
}

static double peer(void *u, double rx)
{
    long q = lrint(rx);
    int16_t tx;
    (void)u;
    initiator();
    tx = v32_tick(&A, (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q));
    return a_mute ? 0 : tx;
}

static void line_cfg(v32line_cfg *lc, int delay, int impaired)
{
    memset(lc, 0, sizeof *lc);
    lc->mulaw = 1; lc->loss_db = 6; lc->delay = delay; lc->seed = 1;
    lc->near_db = 15; lc->far_db = 20; lc->c_near_db = 15; lc->c_far_db = 20;
    lc->noise_a_dbm0 = lc->noise_c_dbm0 = -300;
    if (impaired) {
        lc->loss_db = 10;
        lc->near_db = 12; lc->far_db = 15; lc->c_near_db = 12; lc->c_far_db = 18;
        lc->noise_a_dbm0 = lc->noise_c_dbm0 = -45;
        lc->freq_offset = 1; lc->ppm = 100;
        /* 3000 RMS less 10 dB against -45 dBm0 is an SNR of about 20 dB:
         * 9600 (14400 needs about 25, test_call); impaired == 2: quieter */
        if (impaired == 2) lc->noise_a_dbm0 = lc->noise_c_dbm0 = -60;
    }
}

static void start(const v32line_cfg *lc, unsigned c_rates, unsigned a_rates)
{
    v32_cfg cc, ac;
    data_reset();
    memset(&cc, 0, sizeof cc);
    cc.level = 3000; cc.rates = c_rates; cc.getbit = c_get; cc.putbit = c_put;
    v32_init_call(&C, &cc);
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.rates = a_rates; ac.getbit = a_get; ac.putbit = a_put;
    v32_init(&A, &ac);
    v32line_init(&L, lc);
    c_rx = 0; now = 0; a_mute = 0;
}

static void run(long n) { while (n-- > 0) { c_rx = v32line_tick(&L, v32_tick(&C, c_rx), peer, NULL); now++; } }

/* both ends in data mode (1), or one gave up / `ms` went by (0) */
static int to_data(long ms)
{
    long end = now + 8 * ms;
    while (now < end) {
        run(8);
        if (C.connected && !C.retraining && C.state == V32S_DATA && A.connected && !A.retraining && A.state == V32S_DATA) return 1;
        if (C.state == V32S_FAILED || A.state == V32S_FAILED) return 0;
    }
    return 0;
}

static void data(const char *what, int rate_bps)
{
    /* (not before the sync pattern that went first is past the checker) */
    run(4000);
    data_reset();
    run(8000L * SECS);
    printf("    %s: caller -> answerer %ld bits %ld errors, answerer -> caller %ld bits %ld errors\n",
           what, a_chk.bits, a_chk.errs, c_chk.bits, c_chk.errs);
    CHECK(a_chk.bits > (long)rate_bps * SECS * 3 / 4); CHECK(c_chk.bits > (long)rate_bps * SECS * 3 / 4);
    CHECK_EQ_I(a_chk.errs, 0); CHECK_EQ_I(c_chk.errs, 0);
}

/* 6.1: one start-up, every segment the caller sends, then data */
static void startup(const char *name, int delay, int impaired, unsigned c_rates, unsigned a_rates, int want)
{
    v32line_cfg lc;
    int ok, rt;
    double c_turn, a_turn;
    line_cfg(&lc, delay, impaired);
    start(&lc, c_rates, a_rates);
    rt = L.oneway + L.oneway_back;
    ok = to_data(30000);
    printf("  %s: one-way %d samples; ", name, L.oneway);
    CHECK(ok);
    if (!ok) { printf("NO CONNECTION: caller state %d error %d, answerer state %d error %d\n", C.state, C.error, A.state, A.error); return; }
    printf("data at %.2f s, %d bit/s\n", now / 8000.0, C.bitrate);
    CHECK_EQ_I(C.bitrate, V32_BITRATE(want)); CHECK_EQ_I(A.bitrate, V32_BITRATE(want));
    /* 64 +- 2 T at each end's line terminals (pulse centres); the answerer is
     * the peer: its times + peer_lag are network time */
    c_turn = ((double)C.t_cc - ((double)A.t_ca + L.peer_lag + L.oneway_back)) / T;
    a_turn = ((double)A.t_ac2 + L.peer_lag - ((double)C.t_cc + L.oneway)) / T;
    if (!impaired) {
        printf("    turn-round: caller %.2f T, answerer %.2f T (64 +- 2); round trip %.1f T: NT %.1f T, MT %.1f T (less the other's 64 T: %.1f, %.1f)\n",
               c_turn, a_turn, rt / T, C.nt / T, A.mt / T, C.nt / T - a_turn, A.mt / T - c_turn);
        CHECK_NEAR(c_turn, 64.0, 2.0);
        CHECK_NEAR(a_turn, 64.0, 2.0);
        CHECK_NEAR(C.nt / T - a_turn, rt / T, 1.5);
        CHECK_NEAR((double)C.ec.bulk, rt > 96 + 28 ? rt - 28.0 : 96.0, 10.0);
    }
    /* S for NT (an even number of symbols), then S 256 T, S-bar 16 T, TRN 1280 to 8192 T */
    CHECK_EQ_I(C.seg_len[V32S_C_SNT] & 1, 0);
    CHECK_NEAR(C.seg_len[V32S_C_SNT], C.nt / T, 2.0);
    CHECK_EQ_I(C.seg_len[V32S_C_S], 256);
    CHECK_EQ_I(C.seg_len[V32S_C_SBAR], 16);
    CHECK(C.seg_len[V32S_C_TRN] >= 1280 && C.seg_len[V32S_C_TRN] <= 8192);
    /* R2 within R1 and our rates; whole sequences of R2 before E; E is 8 T */
    CHECK_EQ_I(C.r1_word, v32_rate_word(a_rates));
    CHECK_EQ_I(C.r2_word & ~v32_rate_word(a_rates & c_rates), 0);
    CHECK_EQ_I(A.r2_rates, v32_word_rates(C.r2_word));
    CHECK_EQ_I(C.r3_word, v32_rate_word(1u << want));
    CHECK_EQ_I(C.seg_len[V32S_C_R2] % 8, 0);
    CHECK_EQ_I(C.seg_len[V32S_C_E], 8);
    CHECK_EQ_I(A.rx.e_word, v32_rate_word(1u << want) | V32_RW_E);
    CHECK_EQ_I(C.rx.e_word, v32_rate_word(1u << want) | V32_RW_E);
    printf("    segments: S(NT) %u, S %u, S-bar %u, TRN %u, R2 %u, E %u T; echo attenuation %.1f dB; R2 %04x\n",
           C.seg_len[V32S_C_SNT], C.seg_len[V32S_C_S], C.seg_len[V32S_C_SBAR], C.seg_len[V32S_C_TRN],
           C.seg_len[V32S_C_R2], C.seg_len[V32S_C_E], C.erle_db10 / 10.0, C.r2_word);
    if (!impaired) CHECK(C.erle_db10 >= 300);      /* (a far echo with a carrier shift is not followed) */
    data("data", V32_BITRATE(want));
}

/* 7.1 / 7.2: a retrain from either end, data again afterwards */
static void retrain(int by_caller)
{
    v32line_cfg lc;
    long t0;
    int seen = 0;
    line_cfg(&lc, 160, 1);
    start(&lc, V32_RALL, V32_RALL);
    CHECK(to_data(30000));
    run(8000);
    CHECK_EQ_I(v32_retrain(by_caller ? &C : &A), 0);
    t0 = now;
    while (now - t0 < 8000L * 4 && !seen) { run(1); seen = C.retraining && A.retraining; }
    CHECK(seen);
    printf("  retrain asked for by the %s: both ends retraining %.0f ms later;", by_caller ? "caller" : "answerer", (now - t0) / 8.0);
    CHECK_EQ_I(C.connected, 1);
    CHECK(to_data(30000));
    printf(" data again after %.2f s at %d bit/s\n", (now - t0) / 8000.0, C.bitrate);
    CHECK_EQ_I(C.retrains, 1); CHECK_EQ_I(A.retrains, 1);
    data("after the retrain", C.bitrate);
}

/* 8.2: the answering end asks for a rate renegotiation (by hand, see
 * initiator()); the caller responds */
static void renegotiate(unsigned c_rates, unsigned r4, int want)
{
    v32line_cfg lc;
    long t0;
    int ok;
    line_cfg(&lc, 160, 0);
    start(&lc, c_rates, V32_RALL);
    ok = to_data(30000);
    CHECK(ok);
    if (!ok) return;
    run(4000);
    ini = INI_AC; ini_r4 = r4; ini_r5 = 0; ini_listen = 0; ini_clamped = 0; ini_rate = -1;
    t0 = now;
    while (now - t0 < 8000L * 3 && C.state != V32S_FAILED && !(ini == INI_DONE && C.state == V32S_DATA && !C.rn)) run(1);
    printf("  rate renegotiation asked for by the answerer (R4 %04x): caller R5 %04x, E for %d bit/s; after %.0f ms caller state %d at %d bit/s, error %d\n",
           v32_rate_word(r4), C.r5_word, ini_rate >= 0 ? V32_BITRATE(ini_rate) : 0, (now - t0) / 8.0, C.state, C.bitrate, C.error);
    CHECK_EQ_I(C.renegotiations, 1);
    CHECK_EQ_I(C.r4_word, v32_rate_word(r4));
    CHECK_EQ_I(C.r5_word, ini_r5);
    if (want < 0) {
        /* Note 2: no rate in common: E for 64 T, then a cleardown */
        while (now - t0 < 8000L * 4 && C.state != V32S_FAILED) run(8);
        CHECK_EQ_I(C.state, V32S_FAILED); CHECK_EQ_I(C.error, V32_ERR_NO_RATE);
        CHECK(C.seg_len[V32S_C_RN_E] >= 64);
        return;
    }
    CHECK_EQ_I(C.state, V32S_DATA); CHECK_EQ_I(C.rn, 0);
    CHECK_EQ_I(C.bitrate, V32_BITRATE(want)); CHECK_EQ_I(ini_rate, want);
    CHECK_EQ_I(C.retrains, 0); CHECK_EQ_I(A.retrains, 0);
    CHECK_EQ_I(C.connected, 1); CHECK_EQ_I(C.tx_ready, 1); CHECK_EQ_I(C.rx_ready, 1);
    /* AA 56 T, CC 8 T, R5 64 T or more in whole sequences, E 8 T, ones 24 T */
    CHECK_EQ_I(C.seg_len[V32S_C_RN_AA], 56); CHECK_EQ_I(C.seg_len[V32S_C_RN_CC], 8);
    CHECK(C.seg_len[V32S_C_RN_R5] >= 64); CHECK_EQ_I(C.seg_len[V32S_C_RN_R5] % 8, 0);
    CHECK_EQ_I(C.seg_len[V32S_C_RN_E], 8); CHECK_EQ_I(C.seg_len[V32S_C_RN_B1], 24);
    data("then", V32_BITRATE(want));
}

int main(void)
{
    v32line_cfg lc;
    printf("V.32bis calling modem against the answering modem\n");
    startup("clean, mu-law, echoes 15 / 20 dB", 40, 0, V32_RALL, V32_RALL, 4);
    startup("10 dB loss, echoes 12-18 dB, -45 dBm0 noise, 1 Hz, 100 ppm", 160, 1, V32_RALL, V32_RALL, 2);
    startup("the same, -60 dBm0 noise", 160, 2, V32_RALL, V32_RALL, 3);
#ifndef __arm__
    startup("100 ms round trip", 400, 0, V32_RALL, V32_RALL, 4);
    startup("a satellite hop, the answerer up to 9600", 2400, 0, V32_RALL, V32_R4800 | V32_R7200 | V32_R9600, 2);
    startup("caller 9600 only", 160, 1, V32_R9600, V32_RALL, 2);
    startup("caller 4800 only", 160, 1, V32_R4800, V32_RALL, 0);
#endif
    retrain(0);
    retrain(1);
    renegotiate(V32_RALL, V32_R4800 | V32_R7200 | V32_R9600, 2);
    renegotiate(V32_R4800 | V32_R7200 | V32_R9600, V32_RALL, 2);     /* R5 holds the caller to what it has */
    renegotiate(V32_R14400, V32_R4800 | V32_R9600, -1);

    printf("  v32_retrain outside data mode: ");
    line_cfg(&lc, 40, 0);
    start(&lc, V32_RALL, V32_RALL);
    run(8000);
    CHECK_EQ_I(v32_retrain(&C), -1);
    printf("%d\n", v32_retrain(&C));

    /* the answerer goes away in data mode */
    line_cfg(&lc, 160, 1);
    start(&lc, V32_RALL, V32_RALL);
    CHECK(to_data(30000));
    run(8000);
    a_mute = 1;
    {
        long t0 = now;
        while (now - t0 < 8000L * 3 && C.state != V32S_FAILED) run(8);
        printf("  the answerer silent: caller state %d error %d after %.0f ms\n", C.state, C.error, (now - t0) / 8.0);
        CHECK_EQ_I(C.state, V32S_FAILED); CHECK_EQ_I(C.error, V32_ERR_CARRIER); CHECK_EQ_I(C.connected, 0);
        CHECK(now - t0 > 8 * 400 && now - t0 < 8 * 800);
    }

    /* no rate in common: R2 calls for a cleardown, so does R3 */
    line_cfg(&lc, 160, 0);
    start(&lc, V32_R14400, V32_R4800 | V32_R9600);
    CHECK(!to_data(30000));
    while (now < 8000L * 30 && C.state != V32S_FAILED) run(8);
    printf("  nothing in common: R2 %04x; caller state %d error %d, answerer error %d\n", C.r2_word, C.state, C.error, A.error);
    CHECK_EQ_I(C.r2_word, v32_rate_word(0));
    CHECK_EQ_I(C.state, V32S_FAILED); CHECK_EQ_I(C.error, V32_ERR_NO_RATE); CHECK_EQ_I(C.connected, 0);

    /* rates in common, but the answerer's R3 calls for a cleardown (it found
     * the line too poor for any of them: here it is made to) */
    line_cfg(&lc, 160, 0);
    start(&lc, V32_RALL, V32_RALL);
    while (now < 8000L * 30 && A.state != V32S_S2 && A.state != V32S_FAILED) run(1);
    A.r2_rates = 0;
    CHECK(!to_data(30000));
    while (now < 8000L * 30 && C.state != V32S_FAILED) run(8);
    printf("  R3 calling for a cleardown: caller state %d error %d\n", C.state, C.error);
    CHECK_EQ_I(C.state, V32S_FAILED); CHECK_EQ_I(C.error, V32_ERR_NO_RATE);
    {
        long i; double pw = 0;
        for (i = 0; i < 4000; i++) { int16_t tx = v32_tick(&C, 0); pw += (double)tx * tx; }
        CHECK(pw == 0);                          /* and silent */
    }

    /* no AC at all */
    line_cfg(&lc, 40, 0);
    start(&lc, V32_RALL, V32_RALL);
    a_mute = 1;
    run(8000L * 21);
    printf("  no answerer: caller state %d error %d\n", C.state, C.error);
    CHECK_EQ_I(C.state, V32S_FAILED); CHECK_EQ_I(C.error, V32_ERR_NO_TONE);
    T_REPORT("test_caller");
}
