/* The public API (include/v90modem.h), from the outside: the answering modem
 * comes from libv90modem.a as it is installed (only v90m_* visible); the
 * simulated calling modem and V.14 terminal come from the dev archive. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "v90modem.h"
#include "test.h"
/* the simulator's calling modem (dev archive) */
#include "callpeer.h"

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p; size_t i;
    for (i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

/* ---- configuration and memory ---- */
static void test_cfg(void)
{
    v90m_cfg c; v90m_t *m; int err = 99;
    static uint8_t buf[1 << 20] __attribute__((aligned(16)));

    CHECK(strcmp(v90m_version(), "0.1.0") == 0);
    CHECK_EQ_I(V90M_VERSION_MAJOR * 10000 + V90M_VERSION_MINOR * 100 + V90M_VERSION_PATCH, 100);

    v90m_cfg_default(&c);
    CHECK_EQ_I(c.role, V90M_ROLE_ANSWER);
    CHECK_EQ_I(c.tx_level, 3000);
    CHECK_EQ_I(c.echo_delay, -1);
    CHECK(c.modulations & V90M_MOD_V90);
    CHECK(!(c.modulations & V90M_MOD_V92));

    c.role = V90M_ROLE_ORIGINATE;               /* not yet: Phase 2 */
    m = v90m_create(&c, &err);
    CHECK(m == NULL); CHECK_EQ_I(err, V90M_ENOTSUP);
    c.role = V90M_ROLE_ANSWER; c.modulations = 0x8000;
    m = v90m_create(&c, &err);
    CHECK(m == NULL); CHECK_EQ_I(err, V90M_EINVAL);
    v90m_cfg_default(&c); c.max_tx_code = 32;
    CHECK(v90m_create(&c, &err) == NULL); CHECK_EQ_I(err, V90M_EINVAL);

    v90m_cfg_default(&c);
    m = v90m_create(&c, &err);
    CHECK(m != NULL); CHECK_EQ_I(err, V90M_OK);
    c.role = V90M_ROLE_ORIGINATE;
    CHECK_EQ_I(v90m_reset(m, &c), V90M_ENOTSUP);
    v90m_destroy(m);

    /* in memory of the caller's */
    v90m_cfg_default(&c);
    CHECK(v90m_mem_size() <= sizeof buf);
    CHECK(v90m_init(buf, v90m_mem_size() - 1, &c, &err) == NULL); CHECK_EQ_I(err, V90M_ENOMEM);
    CHECK(v90m_init(buf + 1, sizeof buf - 1, &c, &err) == NULL); CHECK_EQ_I(err, V90M_EINVAL);
    m = v90m_init(buf, sizeof buf, &c, &err);
    CHECK(m != NULL); CHECK_EQ_I(err, V90M_OK);
    v90m_destroy(m);                            /* not ours to free: does nothing */

    /* the bounds of the header, and memory full of rubbish */
    CHECK(v90m_mem_size() <= V90M_MEM_SIZE);
    CHECK(v90m_ec_mem_size() <= V90M_EC_MEM_SIZE);
    CHECK(v90m_async_mem_size() <= V90M_ASYNC_MEM_SIZE);
    {
        static union { v90m_align a; uint8_t b[V90M_MEM_SIZE]; } junk;
        v90m_state st;
        memset(junk.b, 0xA5, sizeof junk.b);
        m = v90m_init(junk.b, sizeof junk.b, &c, &err);
        CHECK(m != NULL);
        v90m_get_state(m, &st);
        CHECK_EQ_I(st.connected, 0); CHECK_EQ_I(st.modulation, V90M_CONN_NONE); CHECK_EQ_I(st.end, V90M_END_NONE);
    }
}

/* ---- G.711 at the Table 1 scale ---- */
static void test_g711(void)
{
    int law, c, bad = 0;
    for (law = 0; law < 2; law++)
        for (c = 0; c < 256; c++) {
            int16_t v = v90m_g711_decode(law, (uint8_t)c);
            uint8_t back = v90m_g711_encode(law, v);
            /* both zero codewords decode to 0 */
            if (back != c && v90m_g711_decode(law, back) != v) bad++;
        }
    CHECK_EQ_I(bad, 0);
    CHECK_EQ_I(v90m_g711_decode(0, 0xFF), 0);
    CHECK(v90m_g711_decode(0, 0x80) > 30000);
    CHECK(v90m_g711_decode(0, 0x00) < -30000);
}

/* ---- a V.22bis call: the library answers, the simulator calls ---- */
typedef struct {
    v90m_t *m; v90m_async_t *a; callpeer_t c;
    int ev[8]; int n_ev;
    uint32_t hash;
} call_t;

static void on_event(void *user, v90m_event ev)
{
    call_t *k = user;
    if (k->n_ev < 8) k->ev[k->n_ev] = (int)ev;
    k->n_ev++;
}

static int async_get(void *u) { call_t *k = u; return v90m_async_getbit(k->a); }
static void async_put(void *u, int b) { call_t *k = u; v90m_async_putbit(k->a, b); }

static void call_init(call_t *k)
{
    v90m_cfg c; int err;
    memset(k, 0, sizeof *k);
    k->a = v90m_async_create(&err);
    v90m_cfg_default(&c);
    c.modulations = V90M_MOD_V22 | V90M_MOD_V21;
    c.getbit = async_get; c.putbit = async_put; c.on_event = on_event; c.user = k;
    k->m = v90m_create(&c, &err);
    callpeer_init(&k->c, 1, V8_MOD_V22 | V8_MOD_V21, 1, 3000);
    k->hash = 2166136261u;
}

/* n samples, the answerer's block size `blk` */
static void call_run(call_t *k, int16_t *line, int n, int blk)
{
    int16_t rx[160], tx[160]; int i, done = 0;
    while (done < n) {
        int b = n - done < blk ? n - done : blk;
        for (i = 0; i < b; i++) rx[i] = callpeer_tick(&k->c, line[done + i]);
        v90m_process_linear(k->m, rx, tx, (size_t)b);
        for (i = 0; i < b; i++) line[done + i] = tx[i];
        k->hash = fnv(k->hash, tx, sizeof tx[0] * (size_t)b);
        done += b;
    }
}

static void test_call(void)
{
    static call_t k1, k2;
    int16_t l1[80], l2[80];
    v90m_state st; v90m_stats sx;
    char got[64]; size_t ng = 0;
    int f, sent = 0;

    memset(l1, 0, sizeof l1); memset(l2, 0, sizeof l2);
    /* two modems in one process, side by side; the second one is run in
     * blocks of 1 sample, the first in blocks of 80: the same call must
     * come out of both */
    call_init(&k1); call_init(&k2);
    for (f = 0; f < 100 * 12; f++) {
        call_run(&k1, l1, 80, 80);
        call_run(&k2, l2, 80, 1);
        if (k1.c.connected && !sent && f > 600) {
            v14_write(&k1.c.dte, (const uint8_t *)"hello", 5);
            sent = 1;
        }
        {
            uint8_t b[16]; size_t r = v90m_async_read(k1.a, b, sizeof b);
            if (r && ng + r < sizeof got) { memcpy(got + ng, b, r); ng += r; }
        }
    }
    got[ng] = 0;
    v90m_get_state(k1.m, &st);
    CHECK_EQ_I(st.connected, 1);
    CHECK_EQ_I(st.modulation, V90M_CONN_V22BIS);
    CHECK_EQ_I(st.tx_bps, 2400); CHECK_EQ_I(st.rx_bps, 2400);
    CHECK_EQ_I(st.used_v8, 1);
    CHECK_EQ_I(st.end, V90M_END_NONE);
    CHECK_EQ_I(st.moving, 1);
    CHECK(strcmp(got, "hello") == 0);
    CHECK(k1.n_ev >= 2);
    CHECK_EQ_I(k1.ev[0], V90M_EV_CONNECTED);
    CHECK_EQ_I(k1.ev[1], V90M_EV_DATA_MOVING);
    v90m_get_stats(k1.m, &sx);
    CHECK_EQ_I(sx.modulation, V90M_CONN_V22BIS);
    CHECK_EQ_I(sx.retrains, 0);
    CHECK(sx.snr_db10 > 200);
    /* the second call: same events, and the same samples on the line up to
     * the moment the first one was handed data */
    v90m_get_state(k2.m, &st);
    CHECK_EQ_I(st.connected, 1);
    CHECK_EQ_I(st.modulation, V90M_CONN_V22BIS);
    CHECK_EQ_I(k2.ev[0], V90M_EV_CONNECTED);
    /* data was only handed to the caller's terminal: the answerers must
     * have sent the same samples */
    CHECK_EQ_I(k2.hash, k1.hash);
    printf("  V.22bis through the API: %d events; the answerers in blocks of 80 and of 1 sent %s samples\n",
           k1.n_ev, k1.hash == k2.hash ? "the same" : "different");

    /* a new call on the same memory */
    {
        v90m_cfg c; v90m_cfg_default(&c);
        c.modulations = V90M_MOD_V22 | V90M_MOD_V21;
        CHECK_EQ_I(v90m_reset(k2.m, &c), V90M_OK);
        v90m_get_state(k2.m, &st);
        CHECK_EQ_I(st.connected, 0); CHECK_EQ_I(st.modulation, V90M_CONN_NONE);
    }
    v90m_destroy(k1.m); v90m_destroy(k2.m);
    v90m_async_destroy(k1.a); v90m_async_destroy(k2.a);
}

/* block size alone: the same call cut into blocks of 1, 7 and 160 */
static void test_blocks(void)
{
    static call_t k[3];
    static const int blk[3] = { 1, 7, 160 };
    int16_t line[3][160];
    int j, f;
    for (j = 0; j < 3; j++) { call_init(&k[j]); memset(line[j], 0, sizeof line[j]); }
    for (f = 0; f < 50 * 8; f++)
        for (j = 0; j < 3; j++) call_run(&k[j], line[j], 160, blk[j]);
    CHECK(k[0].c.connected);
    CHECK_EQ_I(k[1].hash, k[0].hash);
    CHECK_EQ_I(k[2].hash, k[0].hash);
    for (j = 0; j < 3; j++) { v90m_destroy(k[j].m); v90m_async_destroy(k[j].a); }
}

/* ---- V.42 alone: two endpoints wired bit to bit ---- */
static void test_ec(void)
{
    v90m_ec_cfg co, ca; v90m_ec_t *o, *a; v90m_ec_status st;
    static uint8_t mem[1 << 18] __attribute__((aligned(16)));
    uint8_t buf[600], got[600]; size_t ng = 0;
    int i, ms, err;

    v90m_ec_cfg_default(&co, 1);
    v90m_ec_cfg_default(&ca, 0);
    CHECK_EQ_I(co.originator, 1); CHECK_EQ_I(ca.originator, 0);
    o = v90m_ec_create(&co, &err); CHECK(o != NULL);
    CHECK(v90m_ec_mem_size() <= sizeof mem);
    a = v90m_ec_init(mem, sizeof mem, &ca, &err); CHECK(a != NULL);
    for (i = 0; i < (int)sizeof buf; i++) buf[i] = (uint8_t)(i * 7);
    for (ms = 0; ms < 6000; ms++) {
        int b;
        for (b = 0; b < 24; b++) {              /* 24000 bit/s both ways */
            v90m_ec_rx_bit(a, v90m_ec_tx_bit(o));
            v90m_ec_rx_bit(o, v90m_ec_tx_bit(a));
        }
        v90m_ec_tick(o, 1); v90m_ec_tick(a, 1);
        if (ms == 2000) CHECK_EQ_I(v90m_ec_write(o, buf, sizeof buf), sizeof buf);
        ng += v90m_ec_read(a, got + ng, sizeof got - ng);
    }
    CHECK_EQ_I(v90m_ec_state(o), V90M_EC_CONNECTED);
    CHECK_EQ_I(v90m_ec_state(a), V90M_EC_CONNECTED);
    CHECK_EQ_I(ng, sizeof buf);
    CHECK(memcmp(got, buf, sizeof buf) == 0);
    CHECK(v90m_ec_tx_idle(o));
    v90m_ec_get_status(a, &st);
    CHECK_EQ_I(st.state, V90M_EC_CONNECTED);
    CHECK(st.rx_i >= 1);
    CHECK_EQ_I(st.comp_alg, V90M_EC_COMP_V44);   /* both ends offer V.44 by default */
    v90m_ec_release(o);
    for (ms = 0; ms < 3000; ms++) {
        int b;
        for (b = 0; b < 24; b++) { v90m_ec_rx_bit(a, v90m_ec_tx_bit(o)); v90m_ec_rx_bit(o, v90m_ec_tx_bit(a)); }
        v90m_ec_tick(o, 1); v90m_ec_tick(a, 1);
    }
    v90m_ec_get_status(a, &st);
    CHECK_EQ_I(st.state, V90M_EC_DISCONNECTED);
    CHECK_EQ_I(st.reason, V90M_EC_R_PEER_DISC);
    v90m_ec_destroy(o); v90m_ec_destroy(a);
}

/* ---- the calling role: an API caller and an API answerer back to back ---- */
typedef struct { v90m_t *m; v90m_async_t *a; int ev[8]; int n_ev; } end_t;
static void end_event(void *u, v90m_event ev) { end_t *e = u; if (e->n_ev < 8) e->ev[e->n_ev] = (int)ev; e->n_ev++; }
static int end_get(void *u) { return v90m_async_getbit(((end_t *)u)->a); }
static void end_put(void *u, int b) { v90m_async_putbit(((end_t *)u)->a, b); }

static void end_init(end_t *e, v90m_role role, unsigned mods)
{
    v90m_cfg c; int err;
    memset(e, 0, sizeof *e);
    e->a = v90m_async_create(&err);
    v90m_cfg_default(&c);
    c.role = role; c.modulations = mods;
    c.getbit = end_get; c.putbit = end_put; c.on_event = end_event; c.user = e;
    e->m = v90m_create(&c, &err);
    CHECK(e->m != NULL);
}

#define V90M_MOD_ALL_DEFAULT (V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34 | V90M_MOD_V90)

static void test_originate(void)
{
    static end_t cl, an;
    v90m_cfg c; v90m_state sc, sa; int err, k;
    int16_t c_rx = 0, a_rx = 0;
    char got_c[64], got_a[64]; size_t nc = 0, na = 0;

    /* what the calling side cannot do yet */
    v90m_cfg_default(&c); c.role = V90M_ROLE_ORIGINATE;
    CHECK(v90m_create(&c, &err) == NULL); CHECK_EQ_I(err, V90M_ENOTSUP);          /* V.90 among the defaults */
    c.modulations = V90M_MOD_V34 | V90M_MOD_V22;
    CHECK(v90m_create(&c, &err) == NULL); CHECK_EQ_I(err, V90M_ENOTSUP);
    c.modulations = 0;
    CHECK(v90m_create(&c, &err) == NULL); CHECK_EQ_I(err, V90M_EINVAL);

    end_init(&cl, V90M_ROLE_ORIGINATE, V90M_MOD_V22 | V90M_MOD_V21);
    end_init(&an, V90M_ROLE_ANSWER, V90M_MOD_V22 | V90M_MOD_V21);
    for (k = 0; k < 8000 * 10; k++) {
        int16_t c_tx, a_tx;
        v90m_process_linear(cl.m, &c_rx, &c_tx, 1);
        v90m_process_linear(an.m, &a_rx, &a_tx, 1);
        a_rx = c_tx; c_rx = a_tx;                      /* a 4-wire line, one sample each way */
        if (k == 8000 * 6) {
            v90m_async_write(cl.a, (const uint8_t *)"from the caller", 15);
            v90m_async_write(an.a, (const uint8_t *)"from the answerer", 17);
        }
        nc += v90m_async_read(cl.a, (uint8_t *)got_c + nc, sizeof got_c - 1 - nc);
        na += v90m_async_read(an.a, (uint8_t *)got_a + na, sizeof got_a - 1 - na);
    }
    got_c[nc] = 0; got_a[na] = 0;
    v90m_get_state(cl.m, &sc); v90m_get_state(an.m, &sa);
    printf("  API caller to API answerer: caller %d %s %d bit/s (V.8 %d), answerer %d %s; caller got \"%s\", answerer got \"%s\"\n",
           sc.connected, sc.modulation == V90M_CONN_V22BIS ? "V.22bis" : "?", sc.tx_bps, sc.used_v8,
           sa.connected, sa.modulation == V90M_CONN_V22BIS ? "V.22bis" : "?", got_c, got_a);
    CHECK_EQ_I(sc.connected, 1); CHECK_EQ_I(sc.modulation, V90M_CONN_V22BIS); CHECK_EQ_I(sc.tx_bps, 2400);
    CHECK_EQ_I(sc.used_v8, 1); CHECK_EQ_I(sc.moving, 1);
    CHECK_EQ_I(sa.connected, 1); CHECK_EQ_I(sa.modulation, V90M_CONN_V22BIS);
    CHECK(strcmp(got_c, "from the answerer") == 0);
    CHECK(strcmp(got_a, "from the caller") == 0);
    CHECK_EQ_I(cl.ev[0], V90M_EV_CONNECTED);
    CHECK_EQ_I(v90m_retrain(cl.m), V90M_OK);           /* V.22bis at 2400: the caller may retrain */
    CHECK_EQ_I(v90m_cleardown(cl.m), V90M_ESTATE);     /* V.22bis has no cleardown */
    v90m_destroy(cl.m); v90m_destroy(an.m); v90m_async_destroy(cl.a); v90m_async_destroy(an.a);

    /* V.32bis: an API caller and an API answerer (its default modulations) back to back */
    v90m_cfg_default(&c); c.role = V90M_ROLE_ORIGINATE;
    c.modulations = V90M_MOD_V90 | V90M_MOD_V32BIS;
    CHECK(v90m_create(&c, &err) == NULL); CHECK_EQ_I(err, V90M_ENOTSUP);
    c.modulations = V90M_MOD_V34 | V90M_MOD_V32BIS;
    CHECK(v90m_create(&c, &err) == NULL); CHECK_EQ_I(err, V90M_ENOTSUP);
    end_init(&cl, V90M_ROLE_ORIGINATE, V90M_MOD_V32BIS | V90M_MOD_V22);
    end_init(&an, V90M_ROLE_ANSWER, V90M_MOD_ALL_DEFAULT);
    nc = na = 0;
    if (cl.m && an.m) {
        int stopped = 0, moving_again = 0, k_rt = -1;
        v90m_stats st;
        for (k = 0; k < 8000 * 30; k++) {
            int16_t c_tx, a_tx;
            v90m_process_linear(cl.m, &c_rx, &c_tx, 1);
            v90m_process_linear(an.m, &a_rx, &a_tx, 1);
            a_rx = c_tx; c_rx = a_tx;
            v90m_get_state(cl.m, &sc);
            if (k_rt < 0 && sc.connected && sc.moving && k % 8000 == 0) {
                v90m_async_write(cl.a, (const uint8_t *)"from the caller", 15);
                v90m_async_write(an.a, (const uint8_t *)"from the answerer", 17);
                k_rt = k + 8000 * 2;
            }
            if (k == k_rt) {
                got_c[nc] = 0; got_a[na] = 0;
                v90m_get_state(an.m, &sa);
                v90m_get_stats(cl.m, &st);
                printf("  V.32bis: caller %d %s %d/%d bit/s (V.8 %d), answerer %d %s; SNR %.1f dB, round trip %d samples; caller got \"%s\", answerer got \"%s\"\n",
                       sc.connected, sc.modulation == V90M_CONN_V32BIS ? "V.32bis" : "?", sc.tx_bps, sc.rx_bps, sc.used_v8,
                       sa.connected, sa.modulation == V90M_CONN_V32BIS ? "V.32bis" : "?", st.snr_db10 / 10.0, st.rtd, got_c, got_a);
                CHECK_EQ_I(sc.modulation, V90M_CONN_V32BIS); CHECK_EQ_I(sc.tx_bps, 14400); CHECK_EQ_I(sc.rx_bps, 14400);
                CHECK_EQ_I(sc.used_v8, 1); CHECK_EQ_I(sc.moving, 1);
                CHECK_EQ_I(sa.connected, 1); CHECK_EQ_I(sa.modulation, V90M_CONN_V32BIS);
                CHECK(strcmp(got_c, "from the answerer") == 0);
                CHECK(strcmp(got_a, "from the caller") == 0);
                CHECK(st.snr_db10 > 300); CHECK(st.rtd >= 0 && st.rtd < 40); CHECK_EQ_I(st.retrains, 0);
                CHECK_EQ_I(cl.ev[0], V90M_EV_CONNECTED);
                CHECK_EQ_I(v90m_cleardown(cl.m), V90M_ESTATE);     /* V.32bis has no cleardown in data mode */
                CHECK_EQ_I(v90m_retrain(cl.m), V90M_OK);            /* 7.1 */
                nc = na = 0;
            }
            if (k > k_rt && k_rt >= 0) {
                if (!sc.moving && sc.retraining) stopped = 1;
                if (stopped && sc.moving && !moving_again) {
                    /* (before it knew our AA for a retrain, 128 T of it,
                     * the answerer's receiver took it for data: 7.2 has
                     * nothing quicker; what it made of it is dropped) */
                    moving_again = 1; na = 0;
                    while (v90m_async_read(an.a, (uint8_t *)got_a, sizeof got_a) > 0) ;
                    v90m_async_write(cl.a, (const uint8_t *)"again", 5);
                }
            }
            nc += v90m_async_read(cl.a, (uint8_t *)got_c + nc, sizeof got_c - 1 - nc);
            na += v90m_async_read(an.a, (uint8_t *)got_a + na, sizeof got_a - 1 - na);
        }
        got_a[na] = 0;
        v90m_get_stats(cl.m, &st);
        v90m_get_state(cl.m, &sc);
        printf("  V.32bis retrain asked for by the caller: stopped %d, moving again %d, retrains %u; answerer got \"%s\"\n",
               stopped, moving_again, st.retrains, got_a);
        CHECK(stopped); CHECK(moving_again); CHECK_EQ_I(st.retrains, 1); CHECK_EQ_I(sc.connected, 1);
        CHECK(strcmp(got_a, "again") == 0);
    }
    v90m_destroy(cl.m); v90m_destroy(an.m); v90m_async_destroy(cl.a); v90m_async_destroy(an.a);

    /* nobody answers with a tone */
    end_init(&cl, V90M_ROLE_ORIGINATE, V90M_MOD_V22 | V90M_MOD_V21);
    for (k = 0, c_rx = 0; k < 8000 * 11; k++) { int16_t t; v90m_process_linear(cl.m, &c_rx, &t, 1); }
    v90m_get_state(cl.m, &sc);
    CHECK_EQ_I(sc.end, V90M_END_NO_ANSWER_TONE);
    CHECK_EQ_I(cl.n_ev, 1); CHECK_EQ_I(cl.ev[0], V90M_EV_ENDED);
    v90m_destroy(cl.m); v90m_async_destroy(cl.a);
}

int main(void)
{
    test_cfg();
    test_g711();
    test_call();
    test_blocks();
    test_ec();
    test_originate();
    T_REPORT("api");
}
