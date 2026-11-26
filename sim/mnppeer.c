/* Test peer: an MNP error-correcting entity (V.42 Annex A). See mnppeer.h.
 * Clause numbers are V.42 (03/93) Annex A's; the values not in Annex A are
 * from docs/history/hst-facts.md, by section. */
#include <string.h>
#include "mnppeer.h"

/* The mode 2 FCS (`MNP framing`): x^16 + x^15 + x^2 + 1, reflected, over
 * the body and the ETX. Its initial value and octet order are in no public
 * text; the facts take CRC-16/ARC (initial 0, low octet first, "123456789"
 * gives 0xBB3D) [OURS]. The four candidates, numbered as the plan's Task 2
 * interface numbers MNP_FCS2: 0 initial 0 low first, 1 initial 0 high
 * first, 2 initial 0xFFFF low first, 3 initial 0xFFFF high first.
 * This peer follows a -DMNP_FCS2=n given to the compiler, but NOT a change
 * of the default in v42/mnp_frame.h, which it does not include: whoever
 * changes that default changes MNPP_FCS2's default below (and T_FCS2 in
 * v42/test/test_mnppeer.c) with it. */
#ifndef MNPP_FCS2
#ifdef MNP_FCS2
#define MNPP_FCS2 MNP_FCS2
#else
#define MNPP_FCS2 0
#endif
#endif

/* frame types (Table A.1) */
enum { T_LR = 1, T_LD = 2, T_LT = 4, T_LA = 5, T_LN = 6, T_LNA = 7 };
/* establishment steps */
enum { PH_O_WAIT, PH_O_LR, PH_R_WAIT, PH_R_REPLY, PH_R_LR, PH_DATA };

#define T400_MS      750     /* V.42 detection phase (`MNP framing`) */
#define T401_EST_MS  2000    /* `Establishment`: 2 s (0.5 to 9 s, A.7.5.1) */
#define T401_DATA_MS 4000    /* Table A.9, N401 = 64 at 2400 bit/s */
#define TRT_MS       1000    /* `Timers`: Annex A's Trt [OURS] */
#define T401_FLOOR   1500    /* `Timers`: never below 1500 ms [OURS] */
#define T404_MS      3000    /* Table A.10, 2400 bit/s and faster */
#define N400_DEF     12      /* A.7.5.5 */
#define SYN 0x16
#define DLE 0x10
#define STX 0x02
#define ETX 0x03

/* ---------------- CRCs ---------------- */

/* CRC-16/X.25 register (V.42 8.1.1.6.1): reflected 0x1021 */
static uint16_t crc_x25(uint16_t c, const uint8_t *d, int n)
{
    int i, b;
    for (i = 0; i < n; i++) {
        c = (uint16_t)(c ^ d[i]);
        for (b = 0; b < 8; b++)
            c = (uint16_t)((c & 1u) ? (c >> 1) ^ 0x8408u : c >> 1);
    }
    return c;
}

/* x^16 + x^15 + x^2 + 1, reflected (0xA001) */
static uint16_t crc_mode2(uint16_t c, const uint8_t *d, int n)
{
    int i, b;
    for (i = 0; i < n; i++) {
        c = (uint16_t)(c ^ d[i]);
        for (b = 0; b < 8; b++)
            c = (uint16_t)((c & 1u) ? (c >> 1) ^ 0xA001u : c >> 1);
    }
    return c;
}

/* the two FCS octets of a mode 2 body, in line order */
static void fcs2(const uint8_t *body, int n, uint8_t out[2])
{
    static const uint8_t etx = ETX;
    uint16_t c = (MNPP_FCS2 == 2 || MNPP_FCS2 == 3) ? 0xFFFFu : 0;
    int hi_first = MNPP_FCS2 == 1 || MNPP_FCS2 == 3;
    c = crc_mode2(c, body, n);
    c = crc_mode2(c, &etx, 1);
    out[hi_first ? 1 : 0] = (uint8_t)(c & 0xFFu);
    out[hi_first ? 0 : 1] = (uint8_t)(c >> 8);
}

static uint32_t rnd(mnppeer_t *p)
{
    uint32_t x = p->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    p->rng = x;
    return x;
}

/* ---------------- small helpers ---------------- */

static int min_i(int a, int b) { return a < b ? a : b; }

/* a timer in ms: 1 when it runs out on this tick */
static int timer_run(int *t, int ms)
{
    if (*t <= 0) return 0;
    *t -= ms;
    if (*t > 0) return 0;
    *t = 0;
    return 1;
}

static int t401_data(const mnppeer_t *p)
{
    if (p->cfg.t401_ms > 0) return p->cfg.t401_ms;
    return p->t401_f > 0 ? p->t401_f : T401_DATA_MS;
}

/* Annex A's T401 (A.7.5.1) in ms: 2 ((k/2) Lb (Lf + Llt + N401) / tx +
 * Lb (Lf + Lla) / rx) + Trt, rounded up, at least T401_FLOOR. Lb 10 and Lf
 * 7 in framing mode 2, 8 and 4 in mode 3; Llt and Lla 5 and 8 standard, 3
 * and 4 optimised, 2 and 3 streamlined (`MNP frames`, `Timers`) */
static int t401_formula(int framing, int layout, int k, int n401, int tx, int rx)
{
    uint64_t lb = framing == 2 ? 10u : 8u, lf = framing == 2 ? 7u : 4u, llt, lla, num, den, ms;
    if (layout == MNPP_LAYOUT_STREAM) { llt = 2; lla = 3; }
    else if (layout == MNPP_LAYOUT_OPT) { llt = 3; lla = 4; }
    else { llt = 5; lla = 8; }
    num = 1000u * ((uint64_t)k * lb * (lf + llt + (uint64_t)n401) * (uint64_t)rx
                   + 2u * lb * (lf + lla) * (uint64_t)tx);
    den = (uint64_t)tx * (uint64_t)rx;
    ms = (num + den - 1u) / den + TRT_MS;
    return ms < T401_FLOOR ? T401_FLOOR : (int)ms;
}
static int t401_est(const mnppeer_t *p)  { return p->cfg.t401_ms > 0 ? p->cfg.t401_ms : T401_EST_MS; }

/* the framing frames go in now: the agreed one in the data phase, else the
 * establishment one (0: none yet) */
static int cur_framing(const mnppeer_t *p)
{
    return p->phase == PH_DATA ? p->framing : p->est_framing;
}

/* the credit we can give: whole maximum-length frames of room, at most k */
static int credit_now(const mnppeer_t *p)
{
    size_t room = MNPP_RING - p->rxq_n;
    int c;
    if (p->zc_left > 0) return 0;
    c = (int)(room / (size_t)p->n401);
    return min_i(c, p->k);
}

/* stop: with an LD of this reason (0: none) */
static void finish(mnppeer_t *p, int reason)
{
    if (p->state == MNPP_DISCONNECTED) return;
    if (reason > 0 && cur_framing(p) != 0) { p->ld_due = 1; p->ld_reason = reason; }
    p->state = MNPP_DISCONNECTED;
}

/* ---------------- transmit: the frame onto the line ---------------- */

/* body (header and information) of n octets, in framing 2 or 3, through
 * the impairments */
static void put_frame(mnppeer_t *p, const uint8_t *body, int n, int framing)
{
    int i, m = 0;
    uint32_t lose = rnd(p) % 1000000u, bad = rnd(p) % 1000000u, where = rnd(p);
    p->tx_frames++;
    if (lose < (uint32_t)p->cfg.lose_ppm) return;
    if (framing == 3) {
        uint16_t c = (uint16_t)(crc_x25(0xFFFFu, body, n) ^ 0xFFFFu);
        memcpy(p->line, body, (size_t)n);
        m = n;
        p->line[m++] = (uint8_t)(c & 0xFFu);
        p->line[m++] = (uint8_t)(c >> 8);
        p->tx_ph = 1;
        p->flag_i = 0;
    } else {
        uint8_t f[2];
        p->line[m++] = SYN; p->line[m++] = DLE; p->line[m++] = STX;
        for (i = 0; i < n; i++) {
            p->line[m++] = body[i];
            if (body[i] == DLE) p->line[m++] = DLE;
        }
        p->line[m++] = DLE; p->line[m++] = ETX;
        fcs2(body, n, f);
        p->line[m++] = f[0]; p->line[m++] = f[1];
        p->line_bit = -1;
        p->gap = 0;
        p->gap_left = 0;
    }
    if (bad < (uint32_t)p->cfg.corrupt_ppm) {
        uint32_t bit = where % (uint32_t)(m * 8);
        p->line[bit / 8] = (uint8_t)(p->line[bit / 8] ^ (1u << (bit % 8)));
    }
    p->line_n = m;
    p->line_i = 0;
    p->line_bit = framing == 3 ? 0 : -1;
    p->tx_ones = 0;
    p->line_framing = framing;
}

/* V.42 7.2.1.2's ODP: DC1 even parity, ones, DC1 odd parity, ones */
static void put_odp(mnppeer_t *p)
{
    p->line[0] = 0x11;
    p->line[1] = 0x91;
    p->line_n = 2;
    p->line_i = 0;
    p->line_bit = -1;
    p->gap = 10;
    p->gap_left = 0;
    p->line_framing = 2;
}

static void send_lr(mnppeer_t *p)
{
    uint8_t b[32];
    int n = 0, fr, k, n401, have_opt, opt;
    if (p->cfg.originator) {
        fr = p->cfg.framing; k = p->cfg.k; n401 = p->cfg.n401;
        have_opt = p->cfg.send_opt; opt = p->cfg.opt & 0xFF;
    } else {
        fr = p->framing; k = p->k; n401 = p->n401;
        have_opt = p->lr_have_opt; opt = p->lr_opt;
    }
    b[n++] = 0;                     /* LI, below */
    b[n++] = T_LR;
    b[n++] = 2;                     /* constant parameter 1 (A.6.4.1.3) */
    b[n++] = 1; b[n++] = 6;         /* constant parameter 2 (A.6.4.1.4) */
    b[n++] = 1; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0xFF;
    b[n++] = 2; b[n++] = 1; b[n++] = (uint8_t)fr;
    b[n++] = 3; b[n++] = 1; b[n++] = (uint8_t)k;
    /* N401 low octet first, as the Courier's template has it (`LR and 0xC0`) */
    b[n++] = 4; b[n++] = 2; b[n++] = (uint8_t)(n401 & 0xFF); b[n++] = (uint8_t)(n401 >> 8);
    if (have_opt) { b[n++] = 8; b[n++] = 1; b[n++] = (uint8_t)opt; }
    /* USR's parameter 0xC0, `C0 02 flags rates` (`LR and 0xC0`) */
    if (p->cfg.originator ? p->cfg.c0 : p->c0_reply) {
        b[n++] = 0xC0; b[n++] = 2;
        b[n++] = p->cfg.originator ? (uint8_t)p->cfg.c0_flags : p->c0_reply_flags;
        b[n++] = p->cfg.originator ? (uint8_t)p->cfg.c0_rates : p->c0_reply_rates;
    }
    b[0] = (uint8_t)(n - 1);
    put_frame(p, b, n, p->est_framing);
    p->est_tries++;
    p->est_t = t401_est(p);
}

static void send_la(mnppeer_t *p, int framing)
{
    uint8_t b[8];
    int nk = credit_now(p), n;
    uint8_t nr = (uint8_t)(p->vr - 1);
    if (p->layout == MNPP_LAYOUT_STD) {
        b[0] = 7; b[1] = T_LA; b[2] = 1; b[3] = 1; b[4] = nr; b[5] = 2; b[6] = 1; b[7] = (uint8_t)nk;
        n = 8;
    } else if (p->layout == MNPP_LAYOUT_STREAM) {
        b[0] = T_LA; b[1] = nr; b[2] = (uint8_t)nk;          /* `05 N(R) N(k)` */
        n = 3;
    } else {
        b[0] = 3; b[1] = T_LA; b[2] = nr; b[3] = (uint8_t)nk;
        n = 4;
    }
    put_frame(p, b, n, framing);
    p->adv_credit = nk;
    /* the furthest N(S) any LA of ours has allowed: N(R) + N(k) */
    if ((uint8_t)(nr + nk - p->adv_edge) < 128) p->adv_edge = (uint8_t)(nr + nk);
    p->unacked = 0;
    p->la_now = 0;
    p->t402 = 0;
    p->t404 = T404_MS;
}

/* octets of the write queue a new LT may take now. A non-expedited LN
 * goes after the LTs pending at its request are acknowledged, "but before
 * any subsequent user data" (A.7.4.1), and the data after it is held until
 * its acknowledgement (Table 4): so new LTs stop at the data written before
 * the first LN 3 queued, and none goes while an LN 3 is outstanding */
static size_t new_data(const mnppeer_t *p)
{
    int i;
    if (p->hst) return p->txq_n;            /* HST: attention codes, no breaks */
    if (p->ln_code == 3) return 0;
    for (i = 0; i < p->lnq_n; i++)
        if (p->lnq[i] == 3) return p->lnq_ahead[i] < p->txq_n ? p->lnq_ahead[i] : p->txq_n;
    return p->txq_n;
}

/* the write queue was emptied (a destructive break): nothing is ahead of
 * a queued LN any more */
static void lnq_flushed(mnppeer_t *p)
{
    int i;
    for (i = 0; i < p->lnq_n; i++) p->lnq_ahead[i] = 0;
}

/* can an LT go now (a new one or one again)? */
static int lt_ready(const mnppeer_t *p)
{
    int pos = (uint8_t)(p->nxt - p->ack);   /* 1: the first after N(R) */
    if (p->state != MNPP_CONNECTED || p->brk_wait) return 0;
    if (pos > p->credit) return 0;          /* S(k) (A.7.3.1, A.7.3.4) */
    if (p->nxt != p->vs) return 1;          /* again (A.7.3.5) */
    return new_data(p) > 0 && pos <= p->k;
}

static void send_lt(mnppeer_t *p)
{
    uint8_t b[MNPP_BODY];
    uint8_t ns = p->nxt;
    int slot = ns % MNPP_MAX_K, h, len, i, first = ns == p->vs;
    if (first) {
        size_t avail = new_data(p);
        len = (int)(avail < (size_t)p->n401 ? avail : (size_t)p->n401);
        for (i = 0; i < p->lnq_n; i++)
            p->lnq_ahead[i] -= p->lnq_ahead[i] < (size_t)len ? p->lnq_ahead[i] : (size_t)len;
        for (i = 0; i < len; i++) {
            p->sto[slot][i] = p->txq[p->txq_h];
            p->txq_h = (p->txq_h + 1) % MNPP_RING;
        }
        p->txq_n -= (size_t)len;
        p->sto_n[slot] = len;
        p->vs++;
        p->tx_lt++;
        /* HST: every Nth new LT goes once as a frame of an unknown id */
        p->unk[slot] = 0;
        if (p->cfg.unknown_ids > 0 && ++p->unk_n % p->cfg.unknown_ids == 0) p->unk[slot] = 1;
    } else {
        p->tx_retx++;
        if (ns == (uint8_t)(p->ack + 1)) p->t401 = 0;   /* restarted below */
    }
    if (first && p->unk[slot]) {
        /* its first sending: `3F N(S) data`; again, an ordinary LT */
        b[0] = 0x3F; b[1] = ns; h = 2;
        p->unk[slot] = 0;
        p->tx_unknown++;
    } else if (p->layout == MNPP_LAYOUT_STD) {
        b[0] = 4; b[1] = T_LT; b[2] = 1; b[3] = 1; b[4] = ns; h = 5;
    } else if (p->layout == MNPP_LAYOUT_STREAM) {
        b[0] = T_LT; b[1] = ns; h = 2;                       /* `04 N(S) data` */
    } else {
        b[0] = 2; b[1] = T_LT; b[2] = ns; h = 3;
    }
    memcpy(b + h, p->sto[slot], (size_t)p->sto_n[slot]);
    put_frame(p, b, h + p->sto_n[slot], p->framing);
    p->nxt++;
    if (p->t401 == 0) p->t401 = t401_data(p);
}

/* the next frame, when the line is free */
static void next_frame(mnppeer_t *p)
{
    uint8_t b[8];
    if (p->ld_due) {
        p->ld_due = 0;
        if (p->phase == PH_DATA && p->layout == MNPP_LAYOUT_STREAM) {
            b[0] = T_LD; b[1] = (uint8_t)p->ld_reason;          /* `02 reason` */
            put_frame(p, b, 2, cur_framing(p));
            return;
        }
        b[0] = 4; b[1] = T_LD; b[2] = 1; b[3] = 1; b[4] = (uint8_t)p->ld_reason;
        put_frame(p, b, 5, cur_framing(p));
        return;
    }
    if (p->state == MNPP_DISCONNECTED) return;
    if (p->phase == PH_O_WAIT) {
        if (p->cfg.odp_first && p->now_ms < T400_MS) put_odp(p);
        return;
    }
    if (p->phase == PH_O_LR || p->phase == PH_R_LR) {
        if (p->est_t == 0 && p->est_tries == 0) send_lr(p);
        else if (p->est_t < 0) send_lr(p);      /* asked again (tick, rx) */
        return;
    }
    if (p->phase != PH_DATA) return;
    if (p->conf_la) {
        /* the confirming LA: N(R) 0, our credit; establishment framing, in
         * the layout the responder's LR set (A.6.4.2) */
        int lay = p->layout;
        p->conf_la = 0;
        /* HST: the last standard-layout frame [BM] */
        if (p->cfg.confirm_la_standard || lay == MNPP_LAYOUT_STREAM) p->layout = MNPP_LAYOUT_STD;
        send_la(p, p->est_framing);
        p->layout = lay;
        return;
    }
    /* an LN goes "immediately following the transmission in progress"
     * (A.7.4.1, expedited), before an LNA owed */
    /* a non-expedited one (type 3) once the data written before it has
     * gone and every LT stored is acknowledged (A.7.4.1) */
    if (p->ln_code < 0 && p->lnq_n > 0
        && (p->hst || p->lnq[0] != 3 || (p->lnq_ahead[0] == 0 && p->vs == (uint8_t)(p->ack + 1)))) {
        p->ln_code = p->lnq[0];
        memmove(p->lnq, p->lnq + 1, (size_t)(p->lnq_n - 1) * sizeof p->lnq[0]);
        memmove(p->lnq_ahead, p->lnq_ahead + 1, (size_t)(p->lnq_n - 1) * sizeof p->lnq_ahead[0]);
        p->lnq_n--;
        p->ln_send = 1;
        p->ln_tries = 0;
    }
    if (p->ln_send) {
        p->ln_send = 0;
        p->ln_tries++;
        p->ln_t = t401_data(p);
        if (p->layout == MNPP_LAYOUT_STREAM) {
            b[0] = T_LN; b[1] = p->vsa; b[2] = (uint8_t)p->ln_code;   /* `06 N(SA) code` */
            put_frame(p, b, 3, p->framing);
            return;
        }
        b[0] = 7; b[1] = T_LN; b[2] = 1; b[3] = 1; b[4] = p->vsa; b[5] = 2; b[6] = 1; b[7] = (uint8_t)p->ln_code;
        put_frame(p, b, 8, p->framing);
        return;
    }
    if (p->lna_due) {
        p->lna_due = 0;
        if (p->layout == MNPP_LAYOUT_STREAM) {
            b[0] = T_LNA; b[1] = p->lna_nr;                     /* `07 N(RA)` */
            put_frame(p, b, 2, p->framing);
            return;
        }
        b[0] = 4; b[1] = T_LNA; b[2] = 1; b[3] = 1; b[4] = p->lna_nr;
        put_frame(p, b, 5, p->framing);
        return;
    }
    {
        int ready = lt_ready(p), half = p->k / 2 > 0 ? p->k / 2 : 1;
        int la = !p->brk_wait
              && (p->la_now
                  || (p->unacked > 0 && (!ready || p->unacked >= half))      /* A.7.3.3 e, f */
                  || (p->adv_credit == 0 && credit_now(p) > 0));            /* credit back */
        if (la) { send_la(p, p->framing); return; }
        if (ready) send_lt(p);
    }
}

int mnppeer_tx_bit(mnppeer_t *p)
{
    int bit;
    int flags = cur_framing(p) == 3 && p->sent_hdlc && p->state != MNPP_DISCONNECTED;
    /* a frame starts after a whole idle flag, never inside one */
    if (p->line_framing == 0 && (!flags || p->flag_i == 0)) next_frame(p);
    if (p->line_framing == 0) {
        /* idle: flags in mode 3 once a frame has gone, else mark */
        if (flags) {
            bit = (0x7E >> p->flag_i) & 1;
            p->flag_i = (p->flag_i + 1) & 7;
            return bit;
        }
        return 1;
    }
    if (p->line_framing == 3) {
        switch (p->tx_ph) {
        case 1:                                     /* opening flag */
            bit = (0x7E >> p->flag_i) & 1;
            if (++p->flag_i == 8) { p->flag_i = 0; p->tx_ph = 2; }
            return bit;
        case 2:                                     /* body and FCS */
            if (p->tx_ones == 5) { p->tx_ones = 0; return 0; }
            bit = (p->line[p->line_i] >> p->line_bit) & 1;
            p->tx_ones = bit ? p->tx_ones + 1 : 0;
            if (++p->line_bit == 8) {
                p->line_bit = 0;
                if (++p->line_i == p->line_n) p->tx_ph = 3;
            }
            return bit;
        default:                                    /* closing flag */
            if (p->tx_ones == 5) { p->tx_ones = 0; return 0; }
            bit = (0x7E >> p->flag_i) & 1;
            if (++p->flag_i == 8) { p->flag_i = 0; p->line_framing = 0; p->sent_hdlc = 1; }
            return bit;
        }
    }
    /* start-stop: start bit, 8 data bits low first, stop bit, then the gap */
    if (p->gap_left > 0) {
        if (--p->gap_left == 0 && p->line_i == p->line_n) p->line_framing = 0;
        return 1;
    }
    if (p->line_bit < 0) { p->line_bit = 0; return 0; }
    if (p->line_bit < 8) return (p->line[p->line_i] >> p->line_bit++) & 1;
    p->line_bit = -1;
    p->line_i++;
    p->gap_left = p->gap;
    if (p->line_i == p->line_n && p->gap_left == 0) p->line_framing = 0;
    return 1;
}

/* ---------------- receive: frames ---------------- */

/* a frame from this framing is ours to react to */
static int our_framing(const mnppeer_t *p, int framing)
{
    if (p->phase == PH_DATA) return framing == p->framing;
    return p->est_framing == 0 || framing == p->est_framing;
}

/* LT and LA state at its start: on link up, and after a destructive
 * break (Tables 4 and 5 note a: every state variable but the break ones
 * to its initial value) */
static void seq_start(mnppeer_t *p, int credit)
{
    p->vs = 1; p->ack = 0; p->nxt = 1;
    p->credit = min_i(credit, p->k);
    p->last_nr = -1;
    p->t401 = 0; p->retries = 0;
    p->vr = 1; p->dup_seen = 0; p->unacked = 0;
    p->la_now = 0;
    p->adv_credit = p->k;
    /* R(k) at the start: the k of the LRs (A.6.3.10) */
    p->adv_edge = (uint8_t)p->k;
    p->t402 = 0; p->t404 = T404_MS;
}

/* a destructive break: data not yet sent and data not yet delivered are
 * discarded (Tables 4 and 5, A.6.7.1.4), the sequences start again */
static void brk_reset(mnppeer_t *p)
{
    p->txq_n = 0;
    lnq_flushed(p);
    p->rxq_n = 0;
    seq_start(p, p->k);
    p->resets++;
}

/* Table 4, destructive, on the break's arrival: data not yet sent and
 * data not yet delivered are discarded; nothing more goes and what comes
 * is discarded until the acknowledgement */
static void brk_begin(mnppeer_t *p)
{
    p->brk_wait = 1;
    p->txq_n = 0;
    lnq_flushed(p);
    p->rxq_n = 0;
    p->t401 = 0;
    p->unacked = 0;
    p->la_now = 0;
}

static void link_up(mnppeer_t *p, int credit)
{
    int i;
    p->state = MNPP_CONNECTED;
    p->phase = PH_DATA;
    /* our reply LR carried parameter 4's lower value; bit 1 of parameter 8
     * agreed makes it 256 from here (A.6.4.1.8) */
    if (!p->cfg.originator && (p->lr_opt & 1)) p->n401 = MNPP_MAX_N401;
    p->connect_ms = p->now_ms;
    if (p->cfg.tx_bps > 0 && p->cfg.rx_bps > 0)
        p->t401_f = t401_formula(p->framing, p->layout, p->k, p->n401, p->cfg.tx_bps, p->cfg.rx_bps);
    seq_start(p, credit);
    /* the originator hears the establishment framing too until the
     * responder is heard in the agreed one (a repeated LR, A.7.1.3) */
    p->est_listen = p->cfg.originator && p->est_framing != p->framing;
    p->zc_left = p->cfg.zero_credit_ms;
    if (p->zc_left > 0 && !p->cfg.originator) p->la_now = 1;
    p->est_t = 0;
    /* a destructive break asked for before the link was up begins now */
    for (i = 0; i < p->lnq_n && !p->hst; i++) if (p->lnq[i] == 1) brk_begin(p);
}

static void bad_frame(mnppeer_t *p, int framing)
{
    if (!our_framing(p, framing)) return;
    p->rx_bad++;
    if (p->state != MNPP_CONNECTED && p->state != MNPP_IDLE) return;
    if (p->phase == PH_DATA) p->la_now = 1;                 /* A.7.3.3 a */
    else if (p->phase == PH_O_LR) {                          /* A.7.1.2 b */
        if (p->est_tries < p->n400) p->est_t = -1;
    } else if (p->phase == PH_R_LR) {                        /* A.7.1.3 */
        if (p->est_tries >= 2) finish(p, 1);
        else p->est_t = -1;
    }
}

typedef struct { int const1, have_c2, fr, k, n401, have_opt, opt, have_c0, c0_flags, c0_rates; } lr_t;

/* -1 malformed */
static int parse_lr(const uint8_t *b, int n, lr_t *r)
{
    int i;
    memset(r, 0, sizeof *r);
    r->fr = r->k = r->n401 = -1;
    if (n < 3 || b[0] == 255 || b[0] + 1 != n) return -1;
    r->const1 = b[2];
    for (i = 3; i < n; ) {
        int t, l;
        if (i + 2 > n) return -1;
        t = b[i]; l = b[i + 1];
        if (l < 1 || i + 2 + l > n) return -1;
        switch (t) {
        case 1: r->have_c2 = 1; break;
        case 2: r->fr = b[i + 2]; break;
        case 3: r->k = b[i + 2]; break;
        case 4:
            if (l > 2) return -1;
            r->n401 = l == 1 ? b[i + 2] : b[i + 2] | (b[i + 3] << 8);
            break;
        case 8: r->have_opt = 1; r->opt = b[i + 2]; break;
        case 0xC0:                /* `C0 02 flags rates`; another length ignored */
            if (l == 2) { r->have_c0 = 1; r->c0_flags = b[i + 2]; r->c0_rates = b[i + 3]; }
            break;
        default: break;          /* unknown: ignored (A.7.1.5) */
        }
        i += 2 + l;
    }
    return 0;
}

static void on_lr(mnppeer_t *p, const uint8_t *b, int n, int framing)
{
    lr_t r;
    int fr, k, n401, opt;
    if (parse_lr(b, n, &r) < 0) { bad_frame(p, framing); return; }
    if (p->cfg.originator) {
        if (p->phase == PH_DATA) { p->conf_la = 1; return; }   /* confirm again */
        if (p->phase != PH_O_LR) return;
        if (r.const1 != 2) { finish(p, 2); return; }
        /* the reply may only lower what we offered */
        if (!r.have_c2 || r.fr < 2 || r.fr > p->cfg.framing || r.k < 1 || r.k > p->cfg.k
            || r.n401 < 1 || r.n401 > p->cfg.n401) { finish(p, 3); return; }
        opt = (p->cfg.send_opt && r.have_opt) ? (r.opt & p->cfg.opt) : 0;
        p->framing = r.fr; p->k = r.k; p->n401 = r.n401;
        p->layout = (opt & 2) ? MNPP_LAYOUT_OPT : MNPP_LAYOUT_STD;
        if (opt & 1) p->n401 = MNPP_MAX_N401;
        /* HST: bit 0 from both ends and a rate in common (`LR and 0xC0`) */
        if (p->cfg.c0 && r.have_c0 && (r.c0_flags & p->cfg.c0_flags & 1) && (r.c0_rates & 0xFC)) {
            p->hst = 1;
            p->hst_rates = r.c0_rates & 0xFC;
            p->back300 = (r.c0_flags & 8) != 0;
            if (p->cfg.stream_always
                || (p->cfg.send_opt && (p->cfg.opt & 0xFF) != 0 && r.have_opt && r.opt != 0))
                p->layout = MNPP_LAYOUT_STREAM;
        }
        link_up(p, r.k);
        p->conf_la = 1;
        return;
    }
    /* responder */
    if (p->phase == PH_R_LR) {                       /* asked again (A.7.1.3) */
        if (p->est_tries >= 2) finish(p, 1);
        else p->est_t = -1;
        return;
    }
    if (p->phase != PH_R_WAIT) return;
    if (r.const1 != 2) { p->est_framing = framing; finish(p, 2); return; }
    fr = min_i(r.fr, p->cfg.framing);
    k = min_i(r.k, p->cfg.k);
    n401 = min_i(r.n401, p->cfg.n401);
    /* code 3 is the initiator's only (Table A.5 Note); the responder that
     * cannot take an LR says it got none it could use: code 1 [OURS] */
    if (!r.have_c2 || fr < 2 || k < 1 || n401 < 1) { p->est_framing = framing; finish(p, 1); return; }
    p->framing = fr; p->k = k; p->n401 = n401;
    opt = (r.have_opt && p->cfg.send_opt) ? (r.opt & p->cfg.opt & 3) : 0;
    p->lr_have_opt = opt != 0;                       /* A.6.4.1.8: only if agreeing */
    p->lr_opt = (uint8_t)opt;
    p->layout = (opt & 2) ? MNPP_LAYOUT_OPT : MNPP_LAYOUT_STD;
    /* HST: a reply 0xC0 to an LR whose 0xC0 has bit 0; the merge [BM] */
    if (p->cfg.c0 && r.have_c0 && (r.c0_flags & 1)) {
        p->c0_reply = 1;
        p->c0_reply_flags = (uint8_t)((p->cfg.c0_flags & r.c0_flags & 1) | ((p->cfg.c0_flags | r.c0_flags) & 8));
        p->c0_reply_rates = (uint8_t)(p->cfg.c0_rates & r.c0_rates & 0xFC);
        if ((p->c0_reply_flags & 1) && p->c0_reply_rates) {
            p->hst = 1;
            p->hst_rates = p->c0_reply_rates;
            p->back300 = (p->c0_reply_flags & 8) != 0;
            if (p->cfg.stream_always || (opt != 0 && r.opt != 0)) p->layout = MNPP_LAYOUT_STREAM;
        }
    }
    p->est_framing = framing;
    p->phase = PH_R_REPLY;
    p->lr_due = p->cfg.lr_delay_ms;
    if (p->lr_due <= 0 && !p->cfg.never_lr) p->phase = PH_R_LR;
}

static void on_la(mnppeer_t *p, uint8_t nr, int nk)
{
    int outstanding, advanced, dup;
    if ((uint8_t)(nr - p->ack) > (uint8_t)(p->vs - 1 - p->ack)) return;   /* N(R) not sent */
    advanced = nr != p->ack;
    dup = !advanced && p->last_nr == nr;
    p->last_nr = nr;
    if (advanced) {
        int pos;
        p->ack = nr;
        p->retries = 0;
        pos = (uint8_t)(p->nxt - p->ack);
        if (pos == 0 || pos > (uint8_t)(p->vs - p->ack)) p->nxt = (uint8_t)(p->ack + 1);
    }
    outstanding = (uint8_t)(p->vs - 1 - p->ack);
    p->credit = min_i(nk, p->k);
    if (!outstanding) p->t401 = 0;
    else if (advanced) p->t401 = t401_data(p);
    if (dup && outstanding && p->credit > 0) {       /* A.7.3.5 a */
        p->nxt = (uint8_t)(p->ack + 1);
        if (++p->retries >= p->n400) finish(p, 4);
    }
}

static void on_lt(mnppeer_t *p, uint8_t ns, const uint8_t *info, int len)
{
    p->rx_lt++;
    if (len < 1 || len > p->n401) { p->rx_bad++; p->la_now = 1; return; }
    /* beyond every credit we gave (A.6.3.10, A.6.3.11), whatever V(R) is:
     * the sender broke the window; handled as an LT without receive credit
     * (A.7.3.2.3). An honest sender never gets there: its window is an
     * N(R) + N(k) of one of our LAs, and the edge is the furthest of them. */
    if ((uint8_t)(ns - p->adv_edge) != 0 && (uint8_t)(ns - p->adv_edge) < 128) {
        p->rx_over_credit++;
        p->la_now = 1;
        return;
    }
    if (ns == p->vr) {
        if (credit_now(p) == 0 || MNPP_RING - p->rxq_n < (size_t)len) { p->la_now = 1; return; }  /* A.7.3.2.3 */
        {
            int i;
            for (i = 0; i < len; i++)
                p->rxq[(p->rxq_h + p->rxq_n + (size_t)i) % MNPP_RING] = info[i];
        }
        p->rxq_n += (size_t)len;
        p->vr++;
        p->unacked++;
        p->dup_seen = 0;
        if (p->t402 == 0) p->t402 = t401_data(p) / 2;           /* T402 = T401 / 2 (A.7.5.2) */
        if (p->k == 1) p->la_now = 1;
    } else if (ns == (uint8_t)(p->vr - 1) && !p->dup_seen) {
        p->dup_seen = 1;                                         /* A.7.3.2.2 */
    } else {
        p->la_now = 1;
    }
}

/* a streamlined type octet: LD, LT, LA, LN, LNA */
static int stype(int c)
{
    return c == T_LD || c == T_LT || c == T_LA || c == T_LN || c == T_LNA;
}

/* the length of a streamlined frame of that type fits n octets */
static int sfits(int type, int n)
{
    switch (type) {
    case T_LT: return n >= 3;
    case T_LA: case T_LN: return n == 3;
    default: return n == 2;                 /* LD, LNA */
    }
}

/* an Annex A frame exactly as this peer builds it (standard or optimised) */
static int annex_form(const uint8_t *b, int n)
{
    int li = b[0];
    if (n < 2 || li == 255 || li + 1 > n) return 0;
    switch (b[1]) {
    case T_LR: return 1;
    case T_LT: return (li == 4 && n >= 6 && b[2] == 1 && b[3] == 1) || (li == 2 && n >= 4);
    case T_LA: return (li == 7 && n == 8 && b[2] == 1 && b[3] == 1) || (li == 3 && n == 4);
    case T_LN: return li == 7 && n == 8 && b[2] == 1 && b[3] == 1;
    case T_LNA: case T_LD: return li == 4 && n == 5 && b[2] == 1 && b[3] == 1;
    default: return 0;
    }
}

static void on_frame_annex(mnppeer_t *p, const uint8_t *b, int n, int framing);

/* HST: a streamlined frame is taken as its standard form (the LI and
 * parameter octets put back); an unknown first octet is counted. In the
 * streamlined layout agreed, every frame that reads as one; with
 * layout_rx_any, only those that are no Annex A frame as this peer
 * builds them. 1 when handled here. */
static int on_frame_stream(mnppeer_t *p, const uint8_t *b, int n, int framing)
{
    uint8_t s[MNPP_BODY + 8];
    int m = 0;
    int fits;
    if (n < 2 || (b[1] == T_LR && !stype(b[0]))) return 0;           /* an LR: standard */
    fits = stype(b[0]) && sfits(b[0], n);
    if (p->layout != MNPP_LAYOUT_STREAM || p->cfg.layout_rx_any) {
        if (!fits || annex_form(b, n)) return 0;      /* an Annex A frame first */
    } else if (!fits) {
        if (annex_form(b, n)) return 0;           /* the confirming LA, standard [BM] */
        if (!stype(b[0])) { p->rx_frames++; p->rx_unknown++; }      /* LT1-3, LTR, ... */
        else bad_frame(p, framing);
        return 1;
    }
    switch (b[0]) {
    case T_LT:
        s[m++] = 4; s[m++] = T_LT; s[m++] = 1; s[m++] = 1; s[m++] = b[1];
        memcpy(s + m, b + 2, (size_t)(n - 2));
        m += n - 2;
        break;
    case T_LA:  s[m++] = 7; s[m++] = T_LA; s[m++] = 1; s[m++] = 1; s[m++] = b[1];
                s[m++] = 2; s[m++] = 1; s[m++] = b[2]; break;
    case T_LN:  s[m++] = 7; s[m++] = T_LN; s[m++] = 1; s[m++] = 1; s[m++] = b[1];
                s[m++] = 2; s[m++] = 1; s[m++] = b[2]; break;
    default:    s[m++] = 4; s[m++] = b[0]; s[m++] = 1; s[m++] = 1; s[m++] = b[1]; break;
    }
    p->syn = 1;
    on_frame_annex(p, s, m, framing);
    p->syn = 0;
    return 1;
}

static void on_frame(mnppeer_t *p, const uint8_t *b, int n, int framing)
{
    if (p->state == MNPP_DISCONNECTED) return;
    if ((p->hst && (p->layout == MNPP_LAYOUT_STREAM || p->cfg.layout_rx_any)
         && (p->phase == PH_DATA || p->phase == PH_R_LR))
        && on_frame_stream(p, b, n, framing))
        return;
    on_frame_annex(p, b, n, framing);
}

static void on_frame_annex(mnppeer_t *p, const uint8_t *b, int n, int framing)
{
    int li, type, lay;
    if (n < 2) { bad_frame(p, framing); return; }
    li = b[0]; type = b[1];
    if (li == 255 || li + 1 > n) { bad_frame(p, framing); return; }
    p->rx_frames++;
    if (p->phase == PH_DATA && framing == p->framing) p->est_listen = 0;
    switch (type) {
    case T_LR:
        on_lr(p, b, n, framing);
        return;
    case T_LD:
        if (li < 4 || b[2] != 1 || b[3] != 1) { bad_frame(p, framing); return; }
        p->rx_ld_reason = b[4];
        p->state = MNPP_DISCONNECTED;
        return;
    case T_LT: case T_LA: {
        uint8_t nr; int nk, form;
        if (type == T_LT && li == 4 && b[2] == 1 && b[3] == 1) lay = MNPP_LAYOUT_STD;
        else if (type == T_LT && li == 2) lay = MNPP_LAYOUT_OPT;
        else if (type == T_LA && li == 7 && n == 8 && b[2] == 1 && b[3] == 1 && b[5] == 2 && b[6] == 1) lay = MNPP_LAYOUT_STD;
        else if (type == T_LA && li == 3 && n == 4) lay = MNPP_LAYOUT_OPT;
        else { bad_frame(p, framing); return; }
        form = lay;                               /* where the fields are */
        if (p->syn) lay = MNPP_LAYOUT_STREAM;     /* how it came */
        if (p->phase != PH_DATA && p->phase != PH_R_LR) return;
        /* the confirming LA is taken in either layout: A.6.4.2 wants A.3b
         * when bit 2 is agreed, BinModem sends A.3a [BM] */
        if (lay != p->layout && !p->cfg.layout_rx_any && !(p->phase == PH_R_LR && type == T_LA)) {
            bad_frame(p, framing);
            return;
        }
        p->last_layout_rx = lay;
        /* our destructive LN is out: what comes is discarded (Table 4) */
        if (p->brk_wait && p->phase == PH_DATA) { if (type == T_LT) p->rx_lt++; return; }
        if (type == T_LA) {
            nr = form == MNPP_LAYOUT_STD ? b[4] : b[2];
            nk = form == MNPP_LAYOUT_STD ? b[7] : b[3];
            if (p->phase == PH_R_LR) { link_up(p, nk); p->last_nr = nr; return; }
            on_la(p, nr, nk);
        } else {
            if (p->phase == PH_R_LR) link_up(p, p->k);   /* the LA was lost */
            if (form == MNPP_LAYOUT_STD) on_lt(p, b[4], b + 5, n - 5);
            else on_lt(p, b[2], b + 3, n - 3);
        }
        return;
    }
    case T_LN:
        if (li != 7 || n != 8 || b[2] != 1 || b[3] != 1 || b[5] != 2 || b[6] != 1) { bad_frame(p, framing); return; }
        if (p->phase != PH_DATA) return;
        if (p->ln_dropped < p->cfg.drop_ln) { p->ln_dropped++; return; }   /* lost (drop_ln) */
        if (b[4] == p->vra) {                            /* A.7.4.3 */
            if (p->lnr_n == MNPP_LNQ) {
                /* no room: neither taken nor acknowledged; the sender's
                 * T401 brings it again (A.7.4.7) */
                p->ln_rx_full++;
                return;
            }
            p->lnr[p->lnr_n++] = b[7];
            p->vra++;
            /* Table 5, destructive; but during our own destructive break
             * (Table 4: data held and discarded "until receive
             * acknowledgement") the reset waits for that acknowledgement,
             * so a break from both ends resets once */
            if (b[7] == 1 && !p->brk_wait && !p->hst) brk_reset(p);
        }
        p->lna_due = 1;
        p->lna_nr = b[4];
        return;
    case T_LNA:
        if (li != 4 || n != 5 || b[2] != 1 || b[3] != 1) { bad_frame(p, framing); return; }
        if (p->phase != PH_DATA || p->ln_code < 0) return;
        if (b[4] == p->vsa) {                            /* A.7.4.6 */
            int was = p->ln_code, i;
            p->vsa++;
            p->ln_code = -1;
            p->ln_send = 0;
            p->ln_t = 0;
            if (was == 1 && p->brk_wait) {               /* Table 4 note a */
                p->brk_wait = 0;
                brk_reset(p);
                for (i = 0; i < p->lnq_n; i++) if (p->lnq[i] == 1) p->brk_wait = 1;   /* the next break */
            }
        } else {
            p->ln_send = 1;                              /* A.7.4.7 */
        }
        return;
    default:
        p->rx_unknown++;
        return;
    }
}

/* ---------------- receive: bits ---------------- */

static void rx_hdlc(mnppeer_t *p, int bit)
{
    if (bit) {
        if (p->h_ones < 8) p->h_ones++;
        if (p->h_ones >= 7) { p->h_in = 0; return; }        /* abort, or mark */
    } else {
        int ones = p->h_ones;
        p->h_ones = 0;
        if (ones == 6) {                                     /* flag */
            /* a frame ends on an octet boundary: the flag's 0 and six 1s
             * make 7 bits past it */
            if (p->h_in && p->h_n > 0) {
                if (p->h_bits == 7 && p->h_n >= 4 && crc_x25(0xFFFFu, p->h_buf, p->h_n) == 0xF0B8u)
                    on_frame(p, p->h_buf, p->h_n - 2, 3);
                else
                    bad_frame(p, 3);
            }
            p->h_in = 1; p->h_n = 0; p->h_bits = 0; p->h_cur = 0;
            return;
        }
        if (ones == 5) return;                               /* inserted zero */
    }
    if (!p->h_in) return;
    p->h_cur = (uint8_t)(p->h_cur | (bit << p->h_bits));
    if (++p->h_bits == 8) {
        if (p->h_n == (int)sizeof p->h_buf) { p->h_in = 0; bad_frame(p, 3); return; }
        p->h_buf[p->h_n++] = p->h_cur;
        p->h_cur = 0;
        p->h_bits = 0;
    }
}

static void m_add(mnppeer_t *p, uint8_t c)
{
    if (p->m_n == (int)sizeof p->m_buf) { p->m_state = 0; p->m_prev = 0; bad_frame(p, 2); return; }
    p->m_buf[p->m_n++] = c;
}

/* mode 2 octets: SYN DLE STX, body with DLE doubled, DLE ETX, FCS (A.3) */
static void rx_mode2(mnppeer_t *p, uint8_t c)
{
    switch (p->m_state) {
    case 0:                                  /* hunting DLE STX */
        if (p->m_prev == DLE && c == STX) { p->m_state = 1; p->m_n = 0; }
        p->m_prev = c;
        return;
    case 1:
        if (c == DLE) p->m_state = 2; else m_add(p, c);
        return;
    case 2:                                  /* after a DLE */
        if (c == DLE) { p->m_state = 1; m_add(p, DLE); }
        else if (c == ETX) p->m_state = 3;
        else if (c == STX) { bad_frame(p, 2); p->m_state = 1; p->m_n = 0; }   /* the next frame's start */
        else { bad_frame(p, 2); p->m_state = 0; p->m_prev = c; }
        return;
    case 3:
        p->m_f0 = c;
        p->m_state = 4;
        return;
    default: {
        uint8_t f[2];
        p->m_state = 0;
        p->m_prev = 0;
        if (p->m_n == 0) { bad_frame(p, 2); return; }
        fcs2(p->m_buf, p->m_n, f);
        if (f[0] == p->m_f0 && f[1] == c) on_frame(p, p->m_buf, p->m_n, 2);
        else bad_frame(p, 2);
        return;
    }
    }
}

/* start-stop characters: 0, 8 data bits low first, 1 */
static void rx_async(mnppeer_t *p, int bit)
{
    switch (p->s_state) {
    case 0:
        if (!bit) { p->s_state = 1; p->s_bits = 0; p->s_c = 0; }
        return;
    case 1:
        p->s_c |= (unsigned)bit << p->s_bits;
        if (++p->s_bits == 8) p->s_state = 2;
        return;
    case 2:
        if (bit) { p->s_state = 0; rx_mode2(p, (uint8_t)p->s_c); }
        else p->s_state = 3;                 /* framing error: wait for mark */
        return;
    default:
        if (bit) p->s_state = 0;
        return;
    }
}

void mnppeer_rx_bit(mnppeer_t *p, int bit)
{
    int both = p->phase != PH_DATA;
    bit = bit ? 1 : 0;
    if (p->state == MNPP_DISCONNECTED) return;
    if (both || p->framing == 3 || (p->est_listen && p->est_framing == 3)) rx_hdlc(p, bit);
    if (both || p->framing == 2 || (p->est_listen && p->est_framing == 2)) rx_async(p, bit);
}

/* ---------------- time ---------------- */

void mnppeer_tick(mnppeer_t *p, unsigned ms)
{
    int m = ms > 60000u ? 60000 : (int)ms;
    p->now_ms += (uint32_t)m;
    if (p->state == MNPP_DISCONNECTED) return;
    switch (p->phase) {
    case PH_O_WAIT: {
        uint32_t start = (uint32_t)(p->cfg.lr_delay_ms > 0 ? p->cfg.lr_delay_ms : 0);
        if (p->cfg.odp_first && start < T400_MS) start = T400_MS;
        if (p->now_ms >= start && !p->cfg.never_lr) p->phase = PH_O_LR;
        return;
    }
    case PH_O_LR:
        if (timer_run(&p->est_t, m)) {
            if (p->est_tries >= p->n400) finish(p, 0);   /* nothing heard: no LD (A.7.2.2) */
            else p->est_t = -1;
        }
        return;
    case PH_R_WAIT:
        if (timer_run(&p->wait_t, m)) finish(p, 0);
        return;
    case PH_R_REPLY:
        p->lr_due -= m;
        if (p->lr_due <= 0 && !p->cfg.never_lr) p->phase = PH_R_LR;
        return;
    case PH_R_LR:
        if (timer_run(&p->est_t, m)) {
            if (p->est_tries >= 2) finish(p, 1);          /* A.7.1.4; codes 1-3 only (A.6.5.1.3) */
            else p->est_t = -1;
        }
        return;
    default:
        break;
    }
    /* data phase */
    if (p->zc_left > 0) { p->zc_left -= m; if (p->zc_left < 0) p->zc_left = 0; }
    if (timer_run(&p->t401, m)) {                        /* A.7.3.5 b */
        if ((uint8_t)(p->vs - 1 - p->ack) != 0) {
            if (p->credit > 0) {
                p->nxt = (uint8_t)(p->ack + 1);
                if (++p->retries >= p->n400) { finish(p, 4); return; }
            }
            p->t401 = t401_data(p);
        }
    }
    if (timer_run(&p->t402, m) && p->unacked > 0) p->la_now = 1;   /* A.7.3.3 g */
    if (timer_run(&p->t404, m)) p->la_now = 1;                     /* A.7.3.3 d */
    if (timer_run(&p->ln_t, m) && p->ln_code >= 0) {               /* A.7.4.7, A.7.4.8 */
        if (p->ln_tries >= p->n400) finish(p, 4);
        else p->ln_send = 1;
    }
}

/* ---------------- the user's side ---------------- */

void mnppeer_init(mnppeer_t *p, const mnp_peer_cfg *c)
{
    memset(p, 0, sizeof *p);
    p->cfg = *c;
    if (p->cfg.framing != 2) p->cfg.framing = 3;
    if (p->cfg.k <= 0) p->cfg.k = 8;
    if (p->cfg.k > MNPP_MAX_K) p->cfg.k = MNPP_MAX_K;
    if (p->cfg.n401 <= 0) p->cfg.n401 = 64;
    if (p->cfg.n401 > MNPP_MAX_N401) p->cfg.n401 = MNPP_MAX_N401;
    p->n400 = p->cfg.n400 > 0 ? p->cfg.n400 : N400_DEF;
    p->state = MNPP_IDLE;
    p->framing = p->cfg.framing; p->k = p->cfg.k; p->n401 = p->cfg.n401;
    p->layout = MNPP_LAYOUT_STD;
    if (p->cfg.originator) {
        p->phase = PH_O_WAIT;
        p->est_framing = p->cfg.framing;
    } else {
        p->phase = PH_R_WAIT;
        p->est_framing = 0;
        p->wait_t = p->n400 * t401_est(p);
    }
    p->ln_code = -1;
    p->vsa = p->vra = 1;
    p->last_layout_rx = -1;
    p->last_nr = -1;
    p->rng = p->cfg.seed ? p->cfg.seed : 0x2545F491u;
}

size_t mnppeer_write(mnppeer_t *p, const uint8_t *d, size_t n)
{
    size_t i, room = MNPP_RING - p->txq_n;
    if (n > room) n = room;
    for (i = 0; i < n; i++) p->txq[(p->txq_h + p->txq_n + i) % MNPP_RING] = d[i];
    p->txq_n += n;
    return n;
}

size_t mnppeer_read(mnppeer_t *p, uint8_t *d, size_t max)
{
    size_t i, n = p->rxq_n < max ? p->rxq_n : max;
    for (i = 0; i < n; i++) {
        d[i] = p->rxq[p->rxq_h];
        p->rxq_h = (p->rxq_h + 1) % MNPP_RING;
    }
    p->rxq_n -= n;
    return n;
}

int mnppeer_send_ln(mnppeer_t *p, int code)
{
    if (p->lnq_n == MNPP_LNQ) return 0;
    /* type 3 goes after the data written before it (A.7.4.1) */
    p->lnq_ahead[p->lnq_n] = (code & 0xFF) == 3 ? p->txq_n : 0;
    p->lnq[p->lnq_n++] = code & 0xFF;
    /* the destructive break begins on its arrival; before the link is up,
     * when the link comes up */
    if ((code & 0xFF) == 1 && p->state == MNPP_CONNECTED && !p->hst) brk_begin(p);
    return 1;
}

int mnppeer_get_ln(mnppeer_t *p)
{
    int c;
    if (p->lnr_n == 0) return -1;
    c = p->lnr[0];
    memmove(p->lnr, p->lnr + 1, (size_t)(p->lnr_n - 1) * sizeof p->lnr[0]);
    p->lnr_n--;
    return c;
}

void mnppeer_release(mnppeer_t *p)
{
    finish(p, 255);
}

int mnppeer_state(const mnppeer_t *p)
{
    return p->state;
}
