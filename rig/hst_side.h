/* The answering side of a simulated HST call through the public API, for
 * tools/rec_hst (which records the call) and rig/bench_hst -c (which
 * replays it, on the host and on the target), run as a host runs it: 10 ms
 * blocks (HS_BLOCK samples) through v90m_process_g711, the state read once
 * a block. The modem is set up as v90m-replay -H sets it up
 * (tools/v90m_replay.c, configure, with HST offered), its V.42 endpoint (MNP
 * on) linked before the first sample, and at each block's end: reset at
 * CONNECTED, the rates after that and at each change, DATA_STOPPED /
 * DATA_MOVING passed on, a tick of 10 ms from CONNECTED on; once MNP is up
 * it is kept full of an English sentence, and what it reads is counted and
 * hashed. The recorder and the bench call the same code on the same
 * blocks, so the bench's transmit is the recorder's, codeword for codeword
 * (v90m-replay, a sample at a time and writing nothing, follows such a
 * recording until the endpoint starts, a block's end after CONNECTED).
 * Integer only. */
#ifndef V90D_RIG_HST_SIDE_H
#define V90D_RIG_HST_SIDE_H
#include <string.h>
#include <stdint.h>
#include "v90modem.h"

#define HS_BLOCK 80

typedef struct {
    v90m_t       *m;
    v90m_ec_t    *ec;
    v90m_ec_cfg   ecc;
    int           on;                 /* the endpoint started (the modem connected) */
    int           ltx, lrx, lmv, stopped;
    long          n;                  /* samples processed (whole blocks) */
    size_t        wpos;               /* octets of the endless sentence written */
    unsigned long got;                /* octets read */
    uint32_t      h;                  /* FNV-1a of them */
    long          data14400;          /* the first sample moving at 14400 after a retrain, -1 none */
} hst_side;

static const char hs_sentence[] = "the quick brown fox jumps over the lazy dog. ";

static int hs_get(void *u) { hst_side *s = (hst_side *)u; return s->on ? v90m_ec_tx_bit(s->ec) : 1; }
static void hs_put(void *u, int b) { hst_side *s = (hst_side *)u; if (s->on) v90m_ec_rx_bit(s->ec, b); }

/* 0, or -1 when the modem or the endpoint cannot be made or linked */
static int hst_side_init(hst_side *s)
{
    v90m_cfg c;
    int err;
    memset(s, 0, sizeof *s);
    s->h = 2166136261u;
    s->data14400 = -1;
    v90m_cfg_default(&c);
    c.modulations = V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34 | V90M_MOD_V90 | V90M_MOD_HST;
    c.tx_level = 3963;                 /* -12 dBm0: v90m-replay's default */
    c.max_down_bps = 56000;
    c.max_up_bps = 33600;
    c.alaw = 0;
    c.codec_linear_rx = 0;
    c.echo_delay = -1;
    c.max_tx_code = 0;
    c.getbit = hs_get; c.putbit = hs_put; c.user = s;
    if (!(s->m = v90m_create(&c, &err))) return -1;
    v90m_ec_cfg_default(&s->ecc, 0);
    s->ecc.mnp.enable = 1;
    if (!(s->ec = v90m_ec_create(&s->ecc, &err))) return -1;
    return v90m_ec_link_hst(s->ec, s->m) == V90M_OK ? 0 : -1;
}

static void hst_side_free(hst_side *s)
{
    v90m_ec_destroy(s->ec);
    v90m_destroy(s->m);
}

/* a block: HS_BLOCK codewords heard (mu-law), as many sent; the state at
 * its end in *st */
static void hst_side_block(hst_side *s, const uint8_t *heard, uint8_t *sent, v90m_state *st_out)
{
    v90m_state st;
    v90m_process_g711(s->m, heard, sent, HS_BLOCK);
    s->n += HS_BLOCK;
    v90m_get_state(s->m, &st);
    *st_out = st;
    if (st.connected && !s->on) { v90m_ec_reset(s->ec, &s->ecc); s->on = 1; }
    if (s->on && (st.tx_bps != s->ltx || st.rx_bps != s->lrx) && st.tx_bps > 0 && st.rx_bps > 0) {
        s->ltx = st.tx_bps; s->lrx = st.rx_bps;
        v90m_ec_set_rates(s->ec, s->ltx, s->lrx);
    }
    if (s->on && st.moving != s->lmv) {
        if (s->lmv) { v90m_ec_line_stopped(s->ec); s->stopped = 1; }
        else if (s->stopped) { v90m_ec_line_restored(s->ec); s->stopped = 0; }
        s->lmv = st.moving;
    }
    if (s->data14400 < 0 && st.moving && st.tx_bps == 14400 && st.modulation == V90M_CONN_HST) s->data14400 = s->n;
    if (s->on) {
        size_t k = sizeof hs_sentence - 1, w;
        uint8_t buf[256];
        v90m_ec_tick(s->ec, HS_BLOCK / 8);
        if (v90m_ec_state(s->ec) == V90M_EC_CONNECTED)
            for (;;) {
                size_t off = s->wpos % k;
                w = v90m_ec_write(s->ec, (const uint8_t *)hs_sentence + off, k - off);
                s->wpos += w;
                if (w < k - off) break;
            }
        while ((w = v90m_ec_read(s->ec, buf, sizeof buf)) > 0) {
            size_t i;
            for (i = 0; i < w; i++) s->h = (s->h ^ buf[i]) * 16777619u;
            s->got += (unsigned long)w;
        }
    }
}
#endif
