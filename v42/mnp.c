/* MNP error control, the link (V.42 Annex A). See mnp.h. Clause numbers
 * are Annex A's; the values Annex A leaves open are from
 * docs/history/hst-facts.md, by section, tagged there. */
#include <string.h>
#include "mnp.h"

/* establishment LD reasons (Table A.5); 4, retries out, and 255, the
 * user's, in the data phase (`MNP frames`) */
#define LD_NO_LR    1
#define LD_CONST1   2
#define LD_RETRY    4
#define LD_USER   255

static int imin(int a, int b) { return a < b ? a : b; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a timer in ms: 1 when it runs out on this tick */
static int timer_run(int *t, int ms)
{
    if (*t <= 0) return 0;
    *t -= ms;
    if (*t > 0) return 0;
    *t = 0;
    return 1;
}

void mnp_cfg_default(mnp_cfg_t *c)
{
    c->k = 8;
    c->n401 = 64;
    c->opt = 2;
    c->framing = 3;
    c->t401_min_ms = 1500;
    c->n400 = 12;
}

int mnp_ln_of_brk(int brk_opt)
{
    switch (brk_opt) {
    case V42_BRK_DESTRUCTIVE_EXPEDITED: return MNP_LN_DESTRUCTIVE;
    case V42_BRK_NONDESTRUCTIVE_EXPEDITED: return MNP_LN_EXPEDITED;
    case V42_BRK_NONDESTRUCTIVE_INSEQ: return MNP_LN_INSEQ;
    default: return -1;
    }
}

int mnp_brk_of_ln(int ln_type)
{
    switch (ln_type) {
    case MNP_LN_DESTRUCTIVE: return V42_BRK_DESTRUCTIVE_EXPEDITED;
    case MNP_LN_EXPEDITED: return V42_BRK_NONDESTRUCTIVE_EXPEDITED;
    case MNP_LN_INSEQ: return V42_BRK_NONDESTRUCTIVE_INSEQ;
    default: return -1;
    }
}

int mnp_t401_ms(int framing, int layout, int k, int n401, int tx_bps, int rx_bps, int floor_ms)
{
    /* Lb, Lf by framing; Llt, Lla by layout (A.7.5.1; `MNP frames`) */
    uint64_t lb = framing == 2 ? 10u : 8u, lf = framing == 2 ? 7u : 4u, llt, lla;
    uint64_t tx = (uint64_t)clampi(tx_bps, 75, 10000000), rx = (uint64_t)clampi(rx_bps, 75, 10000000);
    uint64_t kk = (uint64_t)clampi(k, 1, 255), n = (uint64_t)clampi(n401, 1, 65535), num, den, ms;
    switch (layout) {
    case MNP_LAYOUT_OPT: llt = 3; lla = 4; break;
    case MNP_LAYOUT_STREAM: llt = 2; lla = 3; break;
    default: llt = 5; lla = 8; break;
    }
    /* 2 x ((k/2) Lb (Lf + Llt + N401) / tx + Lb (Lf + Lla) / rx) in ms,
     * over the common denominator tx rx, rounded up */
    num = 1000u * (kk * lb * (lf + llt + n) * rx + 2u * lb * (lf + lla) * tx);
    den = tx * rx;
    ms = (num + den - 1u) / den + MNP_TRT_MS;
    if (ms > 3600000u) ms = 3600000u;
    return (int)ms < floor_ms ? floor_ms : (int)ms;
}

/* T401 and T404 for the parameters and rates in force. T404 from the rate
 * we receive LTs at: 3 s at 2400 bit/s and above, 7 s below (`Timers`) */
static void timers_again(mnp_t *m)
{
    m->t401_ms = mnp_t401_ms(m->framing, m->layout, m->k, m->n401, m->tx_bps, m->rx_bps, m->cfg.t401_min_ms);
    m->t404_ms = m->rx_bps >= 2400 ? 3000 : 7000;
}

void mnp_init(mnp_t *m, const mnp_cfg_t *c)
{
    memset(m, 0, sizeof *m);
    m->cfg = *c;
    m->cfg.k = clampi(c->k, 1, MNP_MAX_K);
    m->cfg.n401 = clampi(c->n401, 1, MNP_MAX_N401);
    m->cfg.opt = c->opt & 3;
    m->cfg.framing = clampi(c->framing, 2, 3);
    m->cfg.t401_min_ms = clampi(c->t401_min_ms, 1, 60000);
    m->cfg.n400 = clampi(c->n400, 1, 255);
    m->state = MNP_S_IDLE;
    m->framing = m->cfg.framing;
    m->k = m->cfg.k;
    m->n401 = m->cfg.n401;
    m->layout = MNP_LAYOUT_STD;
    m->tx_bps = m->rx_bps = 2400;
    m->last_nr = -1;
    m->ln_code = -1;
    m->vsa = m->vra = 1;
    mnp2_tx_init(&m->tx2);
    mnp2_rx_init(&m->rx2, MNP_MAX_BODY);
    timers_again(m);
}

/* the framing frames go in now */
static int cur_framing(const mnp_t *m)
{
    return m->state == MNP_S_CONNECTED ? m->framing : m->est_framing;
}

/* the end: with an LD of this reason (0 none) when a framing is known */
static void finish(mnp_t *m, int ld_reason, int reason)
{
    int fr = cur_framing(m);
    if (m->state == MNP_S_DISCONNECTED) return;
    if (ld_reason > 0 && fr != 0) {
        m->ld_due = 1;
        m->ld_reason = ld_reason;
        m->tx_framing = fr;
    }
    m->state = MNP_S_DISCONNECTED;
    m->reason = reason;
    m->t401 = m->t402 = m->t404 = m->ln_t = m->est_t = 0;
}

/* credit we give: none while the user is busy, else the window */
static int credit_now(const mnp_t *m)
{
    return m->busy ? 0 : m->k;
}

int mnp_accept_lr(mnp_t *m, const uint8_t *f, int len, int framing_in)
{
    mnp_lr_t theirs, ours;
    int r;
    if (m->state != MNP_S_IDLE) return MNP_R_PROTOCOL;
    m->est_framing = framing_in == 2 ? 2 : 3;
    r = mnp_lr_parse(f, len, &theirs);
    if (r == MNP_LR_BAD_CONST1) {                         /* A.7.1.5.1 */
        finish(m, LD_CONST1, MNP_R_NEGOTIATION);
        return MNP_R_NEGOTIATION;
    }
    memset(&ours, 0, sizeof ours);
    ours.const1 = 2;
    ours.framing = m->cfg.framing;
    ours.k = m->cfg.k;
    ours.n401 = m->cfg.n401;
    ours.have_opt = 1;
    ours.opt = m->cfg.opt;
    /* an LR we cannot take: code 3 is the initiator's only (Table A.5
     * Note), the responder says it had no LR it could use */
    if (r < 0 || mnp_lr_merge(&ours, &theirs, 0, &m->lr) < 0) {
        finish(m, LD_NO_LR, MNP_R_NEGOTIATION);
        return MNP_R_NEGOTIATION;
    }
    m->framing = m->lr.framing;
    m->k = imin(m->lr.k, MNP_MAX_K);
    m->n401 = m->lr.n401;
    m->opt = m->lr.have_opt ? m->lr.opt : 0;
    /* parameter 8 bit 1 agreed: N401 256 both ways (A.6.4.1.8) */
    if (m->opt & 1) m->n401 = MNP_MAX_N401;
    m->layout = (m->opt & 2) ? MNP_LAYOUT_OPT : MNP_LAYOUT_STD;
    m->state = MNP_S_CONFIRMING;
    m->lr_due = 1;
    m->lr_tries = 0;
    timers_again(m);
    return 0;
}

/* our LR again (A.7.1.3) on T401 or a bad frame; after one such resend,
 * the next T401 expiry or bad frame rejects the connection (A.7.1.4).
 * A failure while the resend it asked for has not gone yet counts for
 * nothing: rejection comes only "after resending". A resend for a
 * repeated LR does not count as that resend (the lenient reading). */
static void lr_failure(mnp_t *m)
{
    if (m->lr_due) return;
    if (m->est_fail >= 1) { finish(m, LD_NO_LR, MNP_R_N400); return; }
    m->est_fail++;
    m->lr_due = 1;
}

/* the caller's LR again (A.7.1.3 c): our LR again, never a rejection;
 * at most N400 of ours in all, so the wait is bounded */
static void lr_repeated(mnp_t *m)
{
    if (m->lr_tries >= m->cfg.n400) finish(m, LD_NO_LR, MNP_R_N400);
    else m->lr_due = 1;
}

/* the sequence state at its start: on link up, and after a destructive
 * break, when "all state variables ... except those pertaining to break
 * transfer, are reset to their initial values" (Tables 4 and 5 note a) */
static void seq_init(mnp_t *m, int credit)
{
    m->vs = m->nxt = m->vh = 1;
    m->ack = 0;
    m->credit = imin(credit, m->k);
    m->last_nr = -1;
    m->retries = 0;
    m->stall = 0;
    m->vr = 1;
    m->dup_seen = 0;
    m->rx_unacked = 0;
    m->la_due = 0;
    m->adv_credit = m->k;          /* R(k) at the start: the LRs' k (A.6.3.10) */
    m->hold_len = m->hold_pos = 0;
    m->t401 = m->t402 = 0;
    m->t404 = m->t404_ms;
}

static void link_up(mnp_t *m, int credit)
{
    m->state = MNP_S_CONNECTED;
    seq_init(m, credit);
    m->lr_due = 0;
    m->est_t = 0;
}

/* a destructive break took effect: the link starts its sequences again */
static void brk_reset(mnp_t *m)
{
    seq_init(m, m->k);
    m->resets++;
}

/* LTs stored and not acknowledged; of them, sent at least once */
static int stored(const mnp_t *m) { return (uint8_t)(m->vs - 1 - m->ack); }
static int sent(const mnp_t *m)   { return (uint8_t)(m->vh - 1 - m->ack); }

static void on_la(mnp_t *m, int nr, int nk)
{
    int advanced, dup, off = (uint8_t)(nr - m->ack);
    m->rx_la++;
    if (off > sent(m)) { m->violations++; return; }      /* N(R) never sent */
    advanced = off != 0;
    dup = !advanced && m->last_nr == nr;
    m->last_nr = nr;
    m->stall = 0;                                         /* the far end answers */
    m->credit = imin(nk, m->k);                           /* A.6.6.2.2 */
    if (advanced) {
        int pos;
        m->ack = (uint8_t)nr;
        m->retries = 0;
        pos = (uint8_t)(m->nxt - m->ack);
        if (pos == 0 || pos > (uint8_t)(m->vh - m->ack)) m->nxt = (uint8_t)(m->ack + 1);
    }
    if (stored(m) == 0) m->t401 = 0;
    else if (advanced || m->t401 == 0) m->t401 = m->t401_ms;
    if (dup && sent(m) > 0 && m->credit > 0) {           /* A.7.3.5 a: go back */
        if (m->retries >= m->cfg.n400) { finish(m, LD_RETRY, MNP_R_N400); return; }
        m->retries++;
        m->nxt = (uint8_t)(m->ack + 1);
    }
}

static void on_lt(mnp_t *m, int ns, const uint8_t *d, int n)
{
    m->rx_lt++;
    if (n < 1 || n > m->n401) { m->rx_bad++; m->la_due = 1; return; }
    if (ns == m->vr) {
        /* no room: discarded, an LA (A.7.3.2.3) */
        if (credit_now(m) == 0 || m->hold_pos < m->hold_len) { m->rx_dropped++; m->la_due = 1; return; }
        memcpy(m->hold, d, (size_t)n);
        m->hold_len = n;
        m->hold_pos = 0;
        m->vr++;
        m->rx_unacked++;
        m->dup_seen = 0;
        if (m->t402 == 0) m->t402 = m->t401_ms / 2;      /* T402 = T401 / 2 (A.7.5.2) */
        if (m->k == 1) m->la_due = 1;
    } else if (ns == (uint8_t)(m->vr - 1) && !m->dup_seen) {
        m->dup_seen = 1;                                  /* the first one: no LA */
    } else {
        m->la_due = 1;                                    /* out of sequence */
    }
}

static void on_ln(mnp_t *m, int nsa, int code)
{
    if (nsa == m->vra) {                                  /* A.7.4.3 */
        /* no room: neither taken nor acknowledged; it comes again */
        if (m->lnq_n == MNP_LNQ) return;
        m->lnq[m->lnq_n++] = code;
        m->vra++;
        m->attn_rx = code;
        /* Table 5, destructive: what has not gone and what is not
         * delivered is discarded, the sequences start again; the LNA
         * follows (A.6.7.1.4). While our own LN 1 waits for its LNA, that
         * LNA's reset is the one (Table 4): one reset per break. */
        if (code == MNP_LN_DESTRUCTIVE && !m->brk_wait) brk_reset(m);
    }
    m->lna_due = 1;
    m->lna_nr = (uint8_t)nsa;
}

static void on_lna(mnp_t *m, int nr)
{
    if (m->ln_code < 0) return;
    if (nr == m->vsa) {                                   /* A.7.4.6 */
        m->vsa++;
        m->ln_code = -1;
        m->ln_send = 0;
        m->ln_t = 0;
        if (m->brk_wait) { m->brk_wait = 0; brk_reset(m); }   /* Table 4 note a */
    } else {
        m->ln_send = 1;                                   /* A.7.4.7 */
    }
}

void mnp_rx_bad(mnp_t *m)
{
    m->rx_bad++;
    if (m->state == MNP_S_CONFIRMING) lr_failure(m);      /* A.7.1.3 b */
    else if (m->state == MNP_S_CONNECTED) m->la_due = 1;  /* A.7.3.3 a */
}

void mnp_rx_frame(mnp_t *m, const uint8_t *f, int len)
{
    mnp_hdr_t h;
    int r;
    if (m->state != MNP_S_CONFIRMING && m->state != MNP_S_CONNECTED) return;
    r = mnp_hdr_parse(f, len, m->layout, -1, &h);
    if (r == MNP_PARSE_UNKNOWN) {
        /* counted and dropped, unacknowledged (`MNP frames`) */
        m->rx_unknown++;
        m->rx_unknown_id = f[0];
        return;
    }
    if (r < 0) { mnp_rx_bad(m); return; }
    /* our destructive LN is out: data from the far end is discarded, and
     * its LAs speak of the sequences about to start again (Table 4) */
    if (m->brk_wait && (h.type == MNP_LT || h.type == MNP_LA)) {
        if (h.type == MNP_LT) { m->rx_lt++; m->rx_dropped++; }
        return;
    }
    switch (h.type) {
    case MNP_LR:
        if (m->state == MNP_S_CONFIRMING) lr_repeated(m);   /* A.7.1.3 c */
        return;
    case MNP_LD:
        m->peer_ld_reason = h.reason;
        finish(m, 0, MNP_R_PEER_DISC);
        return;
    case MNP_LA:
        if (m->state == MNP_S_CONFIRMING) {               /* A.7.1.3 */
            link_up(m, h.nk);
            m->last_nr = h.nr;
            return;
        }
        on_la(m, h.nr, h.nk);
        return;
    case MNP_LT:
        if (m->state == MNP_S_CONFIRMING) {
            /* the confirming LA was lost: up, this LT dropped, an LA */
            link_up(m, m->k);
            m->rx_lt++;
            m->rx_dropped++;
            m->la_due = 1;
            return;
        }
        on_lt(m, h.ns, h.data, h.dlen);
        return;
    case MNP_LN:
        if (m->state == MNP_S_CONNECTED) on_ln(m, h.ns, h.code);
        return;
    case MNP_LNA:
        if (m->state == MNP_S_CONNECTED) on_lna(m, h.nr);
        return;
    default:
        return;
    }
}

/* can an LT go now, new or again? Within the credit (A.7.3.1, A.7.3.4) */
static int lt_ready(const mnp_t *m)
{
    int pos = (uint8_t)(m->nxt - m->ack);
    return !m->brk_wait && m->nxt != m->vs && pos <= m->credit;
}

static int build(uint8_t *f, int layout, const mnp_hdr_t *h)
{
    return mnp_hdr_build(f, MNP_MAX_BODY, layout, h);
}

static int send_la(mnp_t *m, uint8_t *f)
{
    mnp_hdr_t h;
    memset(&h, 0, sizeof h);
    h.type = MNP_LA;
    h.nr = (uint8_t)(m->vr - 1);
    h.nk = credit_now(m);
    m->adv_credit = h.nk;
    m->rx_unacked = 0;
    m->la_due = 0;
    m->t402 = 0;
    m->t404 = m->t404_ms;
    m->tx_la++;
    return build(f, m->layout, &h);
}

static int send_lt(mnp_t *m, uint8_t *f)
{
    mnp_hdr_t h;
    uint8_t ns = m->nxt;
    int slot = ns % MNP_MAX_K;
    if (ns == m->vh) {
        m->vh++;
        m->tx_lt++;
    } else {
        m->tx_retx++;
        if (ns == (uint8_t)(m->ack + 1)) m->t401 = 0;     /* restarted below */
    }
    memset(&h, 0, sizeof h);
    h.type = MNP_LT;
    h.ns = ns;
    h.data = m->slot[slot];
    h.dlen = m->slot_len[slot];
    m->nxt++;
    if (m->t401 == 0) m->t401 = m->t401_ms;
    return build(f, m->layout, &h);
}

int mnp_tx_frame(mnp_t *m, uint8_t *f)
{
    mnp_hdr_t h;
    memset(&h, 0, sizeof h);
    if (m->ld_due) {                                      /* tx_framing set by finish */
        m->ld_due = 0;
        h.type = MNP_LD;
        h.reason = m->ld_reason;
        return build(f, MNP_LAYOUT_STD, &h);
    }
    m->tx_framing = cur_framing(m);
    if (m->state == MNP_S_CONFIRMING) {
        if (!m->lr_due) return 0;
        m->lr_due = 0;
        m->lr_tries++;
        m->est_t = MNP_T401_EST_MS;
        return mnp_lr_build(f, MNP_MAX_BODY, &m->lr);
    }
    if (m->state != MNP_S_CONNECTED) return 0;
    /* expedited LNs at once, "immediately following the transmission in
     * progress", ahead of an LNA too: when both ends break at once each
     * LN then meets the other end's break still waiting, one reset each;
     * a non-expedited one once the LTs pending at its request are
     * acknowledged (A.7.4.1) */
    if (m->ln_send && m->ln_code >= 0 && (m->ln_code != MNP_LN_INSEQ || stored(m) == 0)) {
        m->ln_send = 0;
        m->ln_tries++;
        m->ln_t = m->t401_ms;
        m->attn_tx = m->ln_code;
        h.type = MNP_LN;
        h.ns = m->vsa;
        h.code = m->ln_code;
        return build(f, MNP_LAYOUT_STD, &h);
    }
    if (m->lna_due) {
        m->lna_due = 0;
        h.type = MNP_LNA;
        h.nr = m->lna_nr;
        return build(f, MNP_LAYOUT_STD, &h);
    }
    {
        int ready = lt_ready(m), half = m->k / 2 > 0 ? m->k / 2 : 1;
        /* A.7.3.3: an LA when due, for LTs taken when we have none to send
         * or k/2 of them, and when credit comes back */
        if (!m->brk_wait && (m->la_due || (m->rx_unacked > 0 && (!ready || m->rx_unacked >= half))
                             || (m->adv_credit == 0 && credit_now(m) > 0)))
            return send_la(m, f);
        if (ready) return send_lt(m, f);
    }
    return 0;
}

/* a break LN (types 1 to 3) holds the user's data until its LNA (Table 4) */
static int ln_holds_data(const mnp_t *m)
{
    return m->ln_code >= MNP_LN_DESTRUCTIVE && m->ln_code <= MNP_LN_INSEQ;
}

int mnp_can_send(const mnp_t *m)
{
    if (m->state != MNP_S_CONNECTED || stored(m) >= m->k || ln_holds_data(m)) return 0;
    return m->n401;
}

int mnp_send(mnp_t *m, const uint8_t *d, int n)
{
    int slot;
    if (n < 1 || n > mnp_can_send(m)) return -1;
    slot = m->vs % MNP_MAX_K;
    memcpy(m->slot[slot], d, (size_t)n);
    m->slot_len[slot] = (uint16_t)n;
    m->vs++;
    /* no credit: T401 bounds the wait for it */
    if (m->credit == 0 && m->t401 == 0) m->t401 = m->t401_ms;
    return 0;
}

int mnp_unacked(const mnp_t *m)
{
    return m->state == MNP_S_CONNECTED ? stored(m) : 0;
}

int mnp_rx_avail(const mnp_t *m, const uint8_t **p)
{
    if (m->hold_pos >= m->hold_len) return 0;
    if (p) *p = m->hold + m->hold_pos;
    return m->hold_len - m->hold_pos;
}

void mnp_rx_consume(mnp_t *m, int n)
{
    if (n <= 0) return;
    m->hold_pos += imin(n, m->hold_len - m->hold_pos);
    if (m->hold_pos >= m->hold_len) m->hold_pos = m->hold_len = 0;
}

void mnp_set_busy(mnp_t *m, int busy)
{
    busy = busy != 0;
    if (busy && !m->busy) m->la_due = 1;      /* N(k) 0 at once */
    m->busy = busy;                           /* cleared: the credit rule sends it */
}

int mnp_send_ln(mnp_t *m, int code)
{
    if (m->state != MNP_S_CONNECTED || m->ln_code >= 0) return -1;
    m->ln_code = code & 0xFF;
    m->ln_send = 1;
    m->ln_tries = 0;
    if (m->ln_code == MNP_LN_DESTRUCTIVE) {
        /* Table 4, destructive: LTs not yet sent are discarded, nothing
         * more goes and what comes is discarded until the LNA, then the
         * sequences start again; what is not delivered is discarded */
        m->brk_wait = 1;
        m->vs = m->nxt = m->vh;
        m->t401 = m->t402 = 0;
        m->retries = m->stall = 0;
        m->la_due = 0;
        m->rx_unacked = 0;
        m->hold_len = m->hold_pos = 0;
    }
    return 0;
}

int mnp_get_ln(mnp_t *m, int *code)
{
    if (m->lnq_n == 0) return 0;
    *code = m->lnq[0];
    memmove(m->lnq, m->lnq + 1, (size_t)(m->lnq_n - 1) * sizeof m->lnq[0]);
    m->lnq_n--;
    return 1;
}

void mnp_tick(mnp_t *m, unsigned ms)
{
    int t = ms > 60000u ? 60000 : (int)ms;
    if (m->stopped || t == 0) return;
    if (m->state == MNP_S_CONFIRMING) {
        if (timer_run(&m->est_t, t)) lr_failure(m);      /* A.7.1.3 a */
        return;
    }
    if (m->state != MNP_S_CONNECTED) return;
    if (timer_run(&m->t401, t)) {                         /* A.7.3.5 b */
        m->t401_expiries++;
        if (stored(m) > 0) {
            if (m->credit > 0) {
                /* again from the first unacknowledged LT; N400 of them (A.7.3.6) */
                if (m->retries >= m->cfg.n400) { finish(m, LD_RETRY, MNP_R_N400); return; }
                m->retries++;
                m->nxt = (uint8_t)(m->ack + 1);
            } else {
                /* no credit: nothing to send again; N400 T401s with no LA
                 * at all is a dead far end */
                if (m->stall >= m->cfg.n400) { finish(m, LD_RETRY, MNP_R_N400); return; }
                m->stall++;
            }
            m->t401 = m->t401_ms;
        }
    }
    if (timer_run(&m->t402, t) && m->rx_unacked > 0) m->la_due = 1;   /* A.7.3.3 g */
    if (timer_run(&m->t404, t)) m->la_due = 1;                        /* A.7.3.3 d */
    if (timer_run(&m->ln_t, t) && m->ln_code >= 0) {                  /* A.7.4.7, A.7.4.8 */
        /* N400 retransmissions, as for an LT */
        if (m->ln_tries - 1 >= m->cfg.n400) finish(m, LD_RETRY, MNP_R_N400);
        else m->ln_send = 1;
    }
}

void mnp_line_stopped(mnp_t *m)
{
    m->stopped = 1;
}

void mnp_line_restored(mnp_t *m)
{
    m->stopped = 0;
}

void mnp_release(mnp_t *m)
{
    finish(m, LD_USER, MNP_R_LOCAL);
}

void mnp_set_rates(mnp_t *m, int tx_bps, int rx_bps)
{
    if (tx_bps > 0) m->tx_bps = tx_bps;
    if (rx_bps > 0) m->rx_bps = rx_bps;
    timers_again(m);
}
