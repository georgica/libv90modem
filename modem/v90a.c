/* V.90 analogue modem. See v90a.h. */
#include <string.h>
#include "v90a.h"
#include "v34train.h"
#include "probe.h"
#include "pcmenc.h"
#include "../v90/v90_pcm.h"

#define MS(x) ((int32_t)(x) * 8)
#define YF V90ARX_YF

/* our choices where the Recommendation leaves them open */
#define TRN_SYMS     1024           /* TRN: >= 512 T (9.3.2.3) */
#define DIL_MIN      8              /* DIL symbols per interval and Ucode before we end it (4 passes) */
#define TRN2D_MEAS   4000           /* TRN2d symbols decided with CPt's points before CP is designed
                                       (the digital modem's MP comes after 7200 here) */
#define CLEAR_CPS    4              /* CP with drn = 0 sent before we go silent (9.7) */
#define LOSS_SYMS    4000           /* data mode: half a second of an input 12 dB under the start's */
/* time limits */
#define T_SD_MS      1500           /* 9.3.2.4: Sd-bar within 1500 ms of the start of Ja */
#define T_JD_MS      4500           /* 9.3.2.7: Jd within 4500 ms of the end of Ja */
#define T_JDP_MS     3000           /* Jd' after our S (Jd is 9 ms; the digital modem answers at once) */
#define T_DIL_MS     4000           /* enough DIL within this of Jd' (9.3.2.10: 5000 ms) */
#define T_RI_MS      3000           /* Ri after our S-bar */
#define T_P4_MS      15000          /* 9.4.2: B1d within 15 s (+ 5 round trips) of INFO1a */
#define T_RENEG_MS   6000           /* 9.6.2: Ed within 5000 ms (+ 2 round trips) */

enum { U_SIL, U_S1, U_SB1, U_MD, U_S2, U_SB2, U_PP, U_TRN, U_JA, U_QUIET, U_SJ, U_SBJ, U_DILQ, U_S4, U_SB4,
       U_CPT, U_SCR, U_CP, U_E, U_DATA, U_RS, U_RSB, U_OFF };
enum { D_SD, D_JD, D_DIL, D_DILEND, D_RI, D_TRAIN, D_B1D, D_DATA, D_RD };

static void end(v90a_t *a, int err)
{
    a->state = V90A_ENDED; a->error = err; a->t = 0;
    a->tx_ready = a->rx_ready = 0;
    a->us = U_OFF; a->md_on = 0;
}

static void ugo(v90a_t *a, int s) { a->us = s; a->un = 0; }

/* ---- the DIL as known symbols for the receiver ---- */

static int dil_known(void *ctx, int32_t m, int *u, int *pos, int32_t *lev)
{
    v90a_t *a = (v90a_t *)ctx;
    int32_t l;
    if (a->dil_m0 < 0 || m < a->dil_m0 || (a->dil_end >= 0 && m >= a->dil_end)) return 0;
    while (a->dil_mnext <= m) {
        int uu, pp, f = v90dil_next(&a->dil, &a->dcur, &uu, &pp);
        int i = a->dil_mnext & 63;
        a->dsu[i] = (uint8_t)uu; a->dsp[i] = (uint8_t)pp; a->dsf[i] = (uint8_t)f;
        a->dil_mnext++;
    }
    if (a->dsf[m & 63]) a->cb_seg = m;
    /* after we asked for the end of the DIL: a segment's fourth symbol is a
     * positive training symbol, Ri's is -UINFO (v90dil.h). The third one's
     * output is known by now, from this tick or an earlier one. */
    if (a->ds == D_DILEND && m == a->cb_seg + 4) {
        int32_t L = v90_ucode_linear(a->alaw, (unsigned)a->cfg.uinfo) * (1 << YF), y3;
        y3 = a->rx.nout > 0 && a->rx.out[a->rx.nout - 1].m == m - 1 ? a->rx.out[a->rx.nout - 1].y : a->ry[(m - 1) & 63];
        if (y3 < -L / 2) {
            a->ri_m0 = a->cb_seg; a->dil_end = a->cb_seg; a->t_ri = a->now;
            a->hold_until = m + V90ARX_NB;          /* until the DIL's levels have left the feedback line */
            v90arx_hold(&a->rx, 1);
            a->ri_units = 0;
            a->ds = D_RI;
            return 0;
        }
    }
    *u = a->dsu[m & 63]; *pos = a->dsp[m & 63];
    /* the level it arrives at, as far as it has been measured: what the
     * decision feedback cancels (Table 1's would leave the error of a pad
     * or a robbed bit in every later symbol) */
    l = a->acc.cnt[m % 6][*u] ? v90dil_acc_mean(&a->acc, (int)(m % 6), *u) : v90_ucode_linear(a->alaw, (unsigned)*u) * (1 << YF);
    *lev = *pos ? l : -l;
    return 1;
}

/* the receiver's constellations for a CPt / CP: its points at the levels the
 * DIL measured */
static void set_cons(v90a_t *a, const v90_cp_t *cp, int32_t m)
{
    v90arx_cons_t c;
    int s, u, n;
    memset(&c, 0, sizeof c);
    for (s = 0; s < 6; s++) {
        const uint8_t *mk = cp->mask[cp->idx[s] > 5 ? 0 : cp->idx[s]];
        for (n = 0, u = 0; u < 128; u++)
            if ((mk[u >> 3] >> (u & 7)) & 1) {
                c.u[s][n] = (uint8_t)u;
                c.lv[s][n] = a->res.lv[s][u] >= 0 ? a->res.lv[s][u] : v90_ucode_linear(a->alaw, (unsigned)u) * (1 << YF);
                n++;
            }
        c.n[s] = (uint8_t)n;
    }
    v90arx_set_cons(&a->rx, &c, m);
}

/* ---- start ---- */

void v90a_init(v90a_t *a, const v90a_cfg *cfg)
{
    p2call_cfg pc;
    memset(a, 0, sizeof *a);
    a->cfg = *cfg;
    if (a->cfg.uinfo <= 0) a->cfg.uinfo = 90;
    if (a->cfg.md_len <= 0) a->cfg.md_len = 22;
    if (a->cfg.md_len > 127) a->cfg.md_len = 127;
    if (a->cfg.z_q4 <= 0) a->cfg.z_q4 = V90A_Z_CP;
    if (a->cfg.echo_delay < 0) a->cfg.echo_delay = 0;
    if (a->cfg.echo_delay >= V90A_ECDL) a->cfg.echo_delay = V90A_ECDL - 1;
    memset(&pc, 0, sizeof pc);
    pc.level = a->cfg.level; pc.io_delay = a->cfg.echo_delay;
    pc.uinfo = a->cfg.uinfo; pc.md_len = a->cfg.md_len;
    pc.max_rate = a->cfg.max_rx_bps;
    p2call_init_v90(&a->p2, &pc);
    a->state = V90A_P2;
    a->t_info1a = a->t_ja = a->t_ja_end = a->t_sd = a->t_jd = a->t_jdp = a->t_dil_end = -1;
    a->t_ri = a->t_ribar = a->t_mp = a->t_ed = a->t_data = -1;
    a->dil_m0 = a->dil_end = a->ri_m0 = a->ribar_m0 = a->m_trn2d = a->m_b1d = -1;
}

/* Phase 2 is over: INFO1a has gone (9.3.2.1) */
static void start_p3(v90a_t *a)
{
    v90arx_cfg_t rc;
    int num, den, k, sr = a->p2.sym_rate;
    a->alaw = a->p2.info0d.alaw != 0;
    a->t_info1a = a->now;
    v34_carrier(a->p2.sym_idx, a->p2.tx_hi_carrier, &num, &den);
    if (v34mod_init(&a->mod, sr, num, den, a->p2.tx_preemph, a->cfg.level, a->p2.tx_pwr_red) != 0) { end(a, V90A_ERR_PHASE2); return; }
    memset(&rc, 0, sizeof rc);
    rc.alaw = a->alaw; rc.uinfo = a->cfg.uinfo;
    rc.known_lv = dil_known; rc.known_ctx = a;
    v90arx_init(&a->rx, &rc);
    a->rx_on = 1;
    v90dil_design(&a->dil, a->cfg.uinfo);
    for (k = 0; k < 13; k++) {          /* CP bits 36:48: bit k = 4800 + 2400 k bit/s */
        v34_map_t mp;
        int rate = 4800 + 2400 * k;
        if (a->cfg.max_tx_bps > 0 && rate > a->cfg.max_tx_bps) break;
        if (v34_map_params((unsigned)sr, (unsigned)rate, 0, &mp) == 0) a->up_rates |= (uint16_t)(1u << k);
    }
    a->n_md = a->cfg.md_len * 35 * sr / 1000;
    a->n_trn = TRN_SYMS;
    a->md_rng = 0x2545F491u;
    a->ds = D_SD;
    ugo(a, U_SIL);
    a->state = V90A_P3; a->t = 0;
}

/* ---- upstream ---- */

/* a sequence built one bit per octet, kept packed */
static void load_bits(v90a_t *a, const uint8_t *bits, int n)
{
    a->frn = n > 0 ? n : 0; a->frpos = 0;
    memset(a->fr, 0, sizeof a->fr);
    v90_pack_bits(bits, a->frn, a->fr);
}

static void load_cp(v90a_t *a, const v90_cp_t *cp, int ack)
{
    v90_cp_t c = *cp;
    uint8_t bits[V90_CP_MAX_BITS];
    c.ack = (uint8_t)(ack != 0);
    load_bits(a, bits, v90_cp_build(&c, bits));
    a->fr_ack = ack != 0;
}

static unsigned fr_bit(const v90a_t *a, int i) { return (unsigned)(a->fr[i >> 3] >> (i & 7)) & 1u; }

/* the next two bits of the sequence being sent; Ja wraps (it may stop anywhere) */
static unsigned bits2(v90a_t *a, int wrap)
{
    unsigned b0 = 0, b1 = 0;
    if (wrap && a->frpos >= a->frn) a->frpos = 0;
    if (a->frpos < a->frn) b0 = fr_bit(a, a->frpos++);
    if (wrap && a->frpos >= a->frn) a->frpos = 0;
    if (a->frpos < a->frn) b1 = fr_bit(a, a->frpos++);
    return b0 | (b1 << 1);
}

/* E is over: B1 and data at the rate MP allows (9.4.2.4) */
static void start_data(v90a_t *a)
{
    v34_cfg_t c;
    unsigned common = (unsigned)a->mp.rates & a->up_rates;
    int k;
    a->up_rate = 0;
    for (k = 12; k >= 0; k--)
        if ((common >> k) & 1 && 4800 + 2400 * k <= a->mp.max_rate * 2400) { a->up_rate = 4800 + 2400 * k; break; }
    memset(&c, 0, sizeof c);
    c.symbol_rate = (uint16_t)a->p2.sym_rate; c.data_rate = (uint16_t)a->up_rate;
    c.trellis = a->mp.trellis; c.expanded = a->mp.shaping; c.nonlinear = a->mp.nonlinear;
    c.scrambler = V34_GPA;
    if (a->mp.type) memcpy(c.h, a->mp.h, sizeof c.h);
    if (!a->up_rate || v34_params_init(&a->vpar, &c) != 0) { end(a, V90A_ERR_RATE); return; }
    v34_tx_init(&a->vtx, &a->vpar, a->cfg.getbit, a->cfg.user);
    a->tx_dg = (int32_t)v34_gain_q14(a->vpar.energy_tx_q8);
    ugo(a, U_DATA);
}

/* one upstream symbol, in the modulator's units */
static void up_symbol(v90a_t *a, int32_t *re, int32_t *im)
{
    v34_pt_t pt;
    int32_t g = 16384;
    int n = (int)a->un;
    *re = *im = 0;
    pt.re = pt.im = 0;
    switch (a->us) {
    case U_SIL:
        if (++a->un >= a->p2.sym_rate * 70 / 1000) ugo(a, U_S1);    /* 9.3.2.1: 70 ms */
        return;
    case U_S1: case U_S2: case U_S4: case U_RS:
        v34_s(n, V34_S_LEN, re, im);
        if (++a->un >= V34_S_LEN)
            ugo(a, a->us == U_S1 ? U_SB1 : a->us == U_S2 ? U_SB2 : a->us == U_S4 ? U_SB4 : U_RSB);
        return;
    case U_SB1: case U_SB2: case U_SBJ: case U_SB4: case U_RSB:
        v34_sbar(n, re, im);
        if (++a->un < V34_SBAR_LEN) return;
        if (a->us == U_SB1) {
            if (a->n_md > 0) {
                /* MD: white noise, the canceller learns (v90a.h) */
                ugo(a, U_MD);
                a->md_on = 1; a->md_samples = 0;
                a->md_total = a->n_md * 8000 / a->p2.sym_rate;
                a->erle_rx = a->erle_res = 0;
            } else ugo(a, U_PP);
        } else if (a->us == U_SB2) ugo(a, U_PP);
        else if (a->us == U_SBJ) ugo(a, U_DILQ);
        else if (a->us == U_SB4) {
            /* Phase 4: scrambler and differential encoder from zero (8.5.2) */
            v34_tmod_init(&a->tmod, V34_GPA, 4);
            load_cp(a, &a->cpt, 0);
            a->state = V90A_P4;
            ugo(a, U_CPT);
        } else {                        /* U_RSB: renegotiation or cleardown, CP follows */
            a->cp_ack_sent = 0; a->cps_sent = 0;
            if (a->clearing) { v90_cp_t c = a->cp; c.drn = 0; load_cp(a, &c, 0); }
            else load_cp(a, &a->cp, 0);
            ugo(a, U_CP);
        }
        return;
    case U_MD:
        if (++a->un >= a->n_md) {
            a->md_on = 0;
            v90arx_ec(&a->rx, -1);
            a->erle_db10 = probe_db10(a->erle_rx + 1, a->erle_res + 1);
            ugo(a, U_S2);
        }
        return;
    case U_PP:
        v34_pp(n, re, im);
        if (++a->un >= V34_PP_LEN) { v34_tmod_init(&a->tmod, V34_GPA, 4); ugo(a, U_TRN); }
        return;
    case U_TRN:
        pt = v34_tmod_trn(&a->tmod);
        if (++a->un >= a->n_trn) {
            uint8_t bits[V90_DIL_MAX_BITS];
            load_bits(a, bits, v90_dil_build(&a->dil, bits));
            a->t_ja = a->now;
            ugo(a, U_JA);
        }
        break;
    case U_JA:
        pt = v34_tmod_put(&a->tmod, bits2(a, 1));
        a->un++;
        break;
    case U_QUIET: case U_OFF:
        return;
    case U_DILQ:
        if (a->t_dil_end >= 0) { ugo(a, U_S4); up_symbol(a, re, im); }
        return;
    case U_SJ:
        /* S until Jd', ending on its rotated point (then S-bar) */
        v34_s(n & 1, 2, re, im);
        a->un++;
        if (a->t_jdp >= 0 && a->un >= 64 && (a->un & 1) == 0) ugo(a, U_SBJ);
        return;
    case U_CPT:
        if (a->frpos >= a->frn) {
            if (a->ribar_seen) { ugo(a, U_SCR); up_symbol(a, re, im); return; }   /* 9.4.2.2 */
            load_cp(a, &a->cpt, 0);
        }
        pt = v34_tmod_put(&a->tmod, bits2(a, 0));
        break;
    case U_SCR:
        if (a->cp_ready) { load_cp(a, &a->cp, a->got_mp || a->got_mpa); ugo(a, U_CP); up_symbol(a, re, im); return; }
        pt = v34_tmod_put(&a->tmod, 3);
        break;
    case U_CP:
        if (a->frpos >= a->frn) {
            if (a->clearing) {
                if (++a->cps_sent >= CLEAR_CPS) { end(a, V90A_ERR_HANGUP); return; }
                { v90_cp_t c = a->cp; c.drn = 0; load_cp(a, &c, 0); }
            } else {
                if (a->fr_ack) a->cp_ack_sent++;
                /* 9.4.2.4: a CP' sent and MP' or Ed received: E */
                if (a->cp_ack_sent >= 1 && (a->got_mpa || a->got_ed)) { ugo(a, U_E); up_symbol(a, re, im); return; }
                load_cp(a, &a->cp, a->got_mp || a->got_mpa);
            }
        }
        pt = v34_tmod_put(&a->tmod, bits2(a, 0));
        break;
    case U_E:
        pt = v34_tmod_put(&a->tmod, 3);         /* 20 ones */
        if (++a->un >= 10) start_data(a);
        break;
    case U_DATA:
        if (!v34_tx_in_b1(&a->vtx)) a->tx_ready = 1;
        v34_tx_symbol(&a->vtx, &pt, NULL);
        g = a->tx_dg;
        break;
    default:
        return;
    }
    *re = (pt.re * g) >> 9; *im = (pt.im * g) >> 9;
}

/* ---- downstream ---- */

/* Jd, Jd': the sign of UINFO, differentially, through the GPC descrambler
 * that has run since TRN1d began (8.4.2, 8.4.3) */
static void jd_bit(v90a_t *a, const v90arx_dec_t *d)
{
    int c = d->sign ^ a->jprev, bit;
    a->jprev = d->sign;
    bit = v90_descramble_gpc(&a->jscr, c);
    if (!a->jd_ok) {
        if (a->jbn > 0) {
            a->jb[a->jbn++] = (uint8_t)bit;
            if (a->jbn == V90_JD_BITS) {
                if (v90_jd_parse(a->jb, V90_JD_BITS, &a->jd) == V90_JD_BITS) {
                    a->jd_ok = 1; a->t_jd = a->now; a->jdp_zeros = 0; a->jbn = 0;
                    if (a->us == U_QUIET) ugo(a, U_SJ);         /* 9.3.2.7: S */
                } else a->jbn = 0;
                a->jones = 0;
            }
            return;
        }
        if (bit) { a->jones++; return; }
        if (a->jones >= 17) { memset(a->jb, 1, 17); a->jb[17] = 0; a->jbn = 18; }
        a->jones = 0;
        return;
    }
    /* after a good Jd: another Jd (it begins with ones), or Jd' (12 zeros) */
    if (a->jbn == 0 && !bit) {
        a->jdp_m = d->m; a->jdp_zeros = 1;
        /* the DIL begins 12 symbols on, at a data frame boundary: known from
         * here, nothing learnt while it runs */
        a->dil_m0 = d->m + 12; a->dil_mnext = a->dil_m0; a->dil_end = -1;
        v90dil_cur_init(&a->dcur);
        v90arx_hold(&a->rx, 1);
    } else if (a->jdp_zeros > 0) {
        if (bit) { a->jdp_zeros = 0; a->dil_m0 = -1; v90arx_hold(&a->rx, 0); }     /* a Jd after all */
        else if (++a->jdp_zeros == 12) {
            int s;
            a->t_jdp = a->now;
            for (s = 0; s < 6; s++) a->sig_jd[s] = (uint32_t)v90arx_mse_slot(&a->rx, s);
            v90dil_acc_init(&a->acc);
            a->acc_on = 1;
            a->ds = D_DIL;
            return;
        }
    }
    if (++a->jbn >= V90_JD_BITS) a->jbn = 0;
}

/* enough of the DIL: what it says, CPt, and S, S-bar to end it (9.3.2.10) */
static void dil_finish(v90a_t *a)
{
    v90dil_req_t q;
    int s;
    int32_t L = v90_ucode_linear(a->alaw, (unsigned)a->cfg.uinfo) * (1 << YF);
    a->acc_on = 0;
    a->dil_passes = v90dil_acc_min(&a->acc);
    v90dil_analyse(&a->acc, a->alaw, a->cfg.uinfo, &a->res);
    if (a->cfg.table1) {
        int u;
        for (s = 0; s < 6; s++) for (u = 0; u < V90DIL_NU; u++) a->res.lv[s][u] = v90_ucode_linear(a->alaw, (unsigned)u) * (1 << YF);
    }
    /* UINFO itself must have arrived where training put it */
    for (s = 0; s < 6; s++) {
        int32_t d = a->res.lv[s][a->cfg.uinfo] - L;
        if (d < 0) d = -d;
        if (d > L / 4) { end(a, V90A_ERR_DIL); return; }
    }
    memset(&q, 0, sizeof q);
    q.alaw = a->alaw; q.is_cp = 0; q.z_q4 = V90A_Z_CPT;
    q.max_k = 16;                       /* CPt carries MP: 29333 bit/s at most, wide apart */
    q.max_tx_code = a->p2.info0d.max_tx_power;
    for (s = 0; s < 6; s++) q.sigma2_q8[s] = a->sig_jd[s];
    q.up_rates = a->up_rates;
    if (v90dil_design_cp(&a->res, &q, &a->cpt, &a->cpt_ch) == 0 || v90_map_from_cp(&a->tmap, &a->cpt) != 0) {
        end(a, V90A_ERR_RATE); return;
    }
    a->t_dil_end = a->now;
    a->ds = D_DILEND;
}

static void dil_dec(v90a_t *a, const v90arx_dec_t *d)
{
    int32_t m = d->m;
    if (a->dil_m0 < 0 || m < a->dil_m0 || (a->dil_end >= 0 && m >= a->dil_end)) return;
    if (a->dsf[m & 63]) {
        int mn = a->acc_on ? v90dil_acc_min(&a->acc) : 0;
        a->seg_m = m;
        /* every level measured twice: the receiver may learn again, on
         * those levels (at 100 ppm what the band edge aliases turns by a
         * fifth of a sample in one pass) */
        if (a->acc_on && mn >= 2 && a->rx.hold) v90arx_hold(&a->rx, 0);
        if (a->acc_on && mn >= DIL_MIN) { dil_finish(a); if (a->state == V90A_ENDED) return; }
    }
    if (a->acc_on) v90dil_acc_add(&a->acc, d->slot, a->dsu[m & 63], a->dsp[m & 63], d->y);
}

static void ri_dec(v90a_t *a, const v90arx_dec_t *d)
{
    int32_t m = d->m;
    int k, r = 0;
    if (a->hold_until >= 0 && m >= a->hold_until) { v90arx_hold(&a->rx, 0); a->hold_until = -1; }
    if ((m - a->ri_m0) % 6 != 5 || m < a->ri_m0 + 11) return;   /* the first unit had a DIL symbol fed back */
    for (k = 0; k < 6; k++) r += a->rsg[(m - 5 + k) & 63] == (k < 3);
    if (r >= 5) { a->ri_units++; return; }
    if (r <= 1 && a->ri_units >= 2) {
        /* Ri-bar, 24 T; TRN2d after it with CPt's constellations (8.6.5) */
        a->ribar_m0 = m - 5; a->m_trn2d = a->ribar_m0 + 24; a->t_ribar = a->now;
        a->ribar_seen = 1;
        set_cons(a, &a->cpt, a->m_trn2d);
        v90adec_init(&a->dec, &a->tmap);
        v90_scr_init(&a->dscr);
        a->mbn = a->mones = 0; a->got_mp = a->got_mpa = a->after_mpa = a->got_ed = 0;
        a->trn2d_syms = 0;
        a->ds = D_TRAIN;
    }
}

/* CP from the noise the receiver shows on TRN2d */
static void design_cp(v90a_t *a)
{
    v90dil_req_t q;
    int s;
    memset(&q, 0, sizeof q);
    q.alaw = a->alaw; q.is_cp = 1; q.z_q4 = a->cfg.z_q4;
    q.max_tx_code = a->p2.info0d.max_tx_power;
    /* data mode shows 2 to 4 dB more error than TRN2d with CPt's sparser
     * points (measured): the noise taken as twice what TRN2d shows */
    for (s = 0; s < 6; s++) {
        uint64_t v = 2 * v90arx_mse_slot(&a->rx, s);
        q.sigma2_q8[s] = v > 0xffffffffu ? 0xffffffffu : (uint32_t)v;
    }
    q.rates = a->jd.rates; q.max_bps = a->cfg.max_rx_bps; q.up_rates = a->up_rates;
    if (v90dil_design_cp(&a->res, &q, &a->cp, &a->cp_ch) == 0 || v90_map_from_cp(&a->dmap, &a->cp) != 0) {
        /* nothing fits: CP with drn = 0 is the cleardown (8.5.2) */
        a->clearing = 1; a->error = V90A_ERR_RATE;
    }
    a->cp_ready = 1;
}

static void deliver(v90a_t *a, const uint8_t *bits, int n)
{
    int i;
    if (!a->cfg.putbit) return;
    for (i = 0; i < n; i++) a->cfg.putbit(a->cfg.user, bits[i]);
}

/* one MP bit (TRN2d, MP, MP', Ed: descrambled) */
static void mp_bit(v90a_t *a, int bit)
{
    if (a->mbn > 0) {
        a->mb[a->mbn++] = (uint8_t)bit;
        if (a->mbn == V90_MP0_BITS || a->mbn == V90_MP1_BITS) {
            v90_mp_t mp;
            int r = v90_mp_parse(a->mb, a->mbn, &mp);
            if (r > 0) {
                a->mp = mp;
                if (mp.ack) { a->got_mpa = 1; a->after_mpa = 1; } else a->got_mp = 1;
                if (a->t_mp < 0) a->t_mp = a->now;
                a->mbn = 0; a->mones = 0;
                if (mp.max_rate == 0) end(a, a->clearing ? V90A_ERR_HANGUP : V90A_ERR_CLEARDOWN);   /* 9.7 */
            } else if (r != V90_ERR_SHORT || a->mbn >= V90_MP1_BITS) {
                a->mbn = 0; a->mones = 0;
            }
        }
        return;
    }
    if (bit) { a->mones++; return; }
    if (a->mones >= 17) { memset(a->mb, 1, 17); a->mb[17] = 0; a->mbn = 18; }
    a->mones = 0;
}

/* a whole data frame of the downstream */
static void frame(v90a_t *a, int32_t m0)
{
    uint8_t bits[V90_MAX_D];
    int D = a->dec.D, i, ones = 0;
    (void)v90adec_frame(&a->dec, a->fu, a->fp, bits);
    for (i = 0; i < D; i++) { bits[i] = (uint8_t)v90_descramble_gpc(&a->dscr, bits[i]); ones += bits[i]; }
    switch (a->ds) {
    case D_TRAIN:
        if (a->after_mpa == 2) {
            if (ones == 0) {
                /* Ed (8.6.2): two frames of zeros after MP'; B1d after them */
                a->got_ed = 1; a->t_ed = a->now;
                a->m_b1d = m0 + 12;
                set_cons(a, &a->cp, a->m_b1d);
                a->after_mpa = 3;
                return;
            }
            a->after_mpa = 0;
        } else if (a->after_mpa == 3) {
            /* the second frame of Ed: B1d next, everything from zero (8.6.1) */
            v90adec_init(&a->dec, &a->dmap);
            v90_scr_init(&a->dscr);
            a->b1d_frames = a->b1d_bad = 0;
            a->ds = D_B1D;
            return;
        }
        for (i = 0; i < D && a->state != V90A_ENDED; i++) mp_bit(a, bits[i]);
        if (a->t_mp < 0 && a->mbn == 0) {
            /* TRN2d: scrambled ones, the descrambler in step from the start */
            a->trn2d_frames++;
            if (ones != D) a->trn2d_bad++;
        }
        if (a->after_mpa == 1) a->after_mpa = 2;
        break;
    case D_B1D:
        a->b1d_bad += D - ones;
        if (++a->b1d_frames >= 48) {
            if (a->b1d_bad * 8 > 48 * D) { end(a, V90A_ERR_B1D); return; }
            a->ds = D_DATA; a->rx_ready = 1;
            a->down_rate = (a->cp.drn + 20) * 4000 / 3;
            a->mse_ref = (uint64_t)a->rx.pq;   /* (the equaliser's input power) */
            a->rd_frames = 0; a->bad_syms = 0;
            a->dly_cnt = 0;
        }
        break;
    case D_DATA: {
        /* Rd (8.6.4): the largest point of every interval, + + + - - - or
         * the inverse. Delivery runs two frames late so that Rd is never
         * handed on as data. */
        int k, pol = 0;
        for (k = 0; k < 6; k++) if (a->fu[k] != a->dmap.C[k][0]) break;
        if (k == 6) {
            int p = 1, q = 1;
            for (k = 0; k < 6; k++) { if (a->fp[k] != (k < 3)) p = 0; if (a->fp[k] != (k >= 3)) q = 0; }
            pol = p ? 1 : q ? -1 : 0;
        }
        if (pol && (a->rd_frames == 0 || pol == a->rd_pol)) { a->rd_pol = pol; a->rd_frames++; }
        else a->rd_frames = 0;
        if (a->rd_frames >= 2) {
            /* 9.6.2.2: the digital modem renegotiates; S, S-bar, CP from us */
            a->rx_ready = 0; a->tx_ready = 0;
            a->rd_units = 2;
            a->state = V90A_RENEG; a->t = 0;
            ugo(a, U_RS);
            a->ds = D_RD;
            return;
        }
        k = a->dly_pos & 1;
        if (a->dly_cnt >= 2) deliver(a, a->dly[k], a->dly_n[k]);
        memcpy(a->dly[k], bits, (size_t)D);
        a->dly_n[k] = (uint8_t)D;
        a->dly_pos++;
        if (a->dly_cnt < 2) a->dly_cnt++;
        break;
    }
    default:
        break;
    }
}

static void frame_dec(v90a_t *a, const v90arx_dec_t *d)
{
    int32_t mref = a->ds == D_TRAIN ? a->m_trn2d : a->m_b1d, k;
    if (d->m < mref) return;
    k = (d->m - mref) % 6;
    a->fu[k] = d->ucode; a->fp[k] = d->sign;
    if (a->ds == D_TRAIN && ++a->trn2d_syms == TRN2D_MEAS && !a->cp_ready) design_cp(a);
    if (a->ds == D_DATA) {
        /* the downstream gone: the equaliser's input 12 dB under what it was
         * for 0.5 s (its error says nothing: with nothing coming in, the
         * decision feedback goes on deciding its own echo) */
        if ((uint64_t)a->rx.pq < a->mse_ref / 16) { if (++a->bad_syms > LOSS_SYMS) { end(a, V90A_ERR_CARRIER); return; } }
        else a->bad_syms = 0;
    }
    if (k == 5) frame(a, d->m - 5);
}

/* the digital modem's Rd, Rd-bar in a renegotiation: TRN2d (CPt's points),
 * then MP ... as in Phase 4 */
static void rd_dec(v90a_t *a, const v90arx_dec_t *d)
{
    int32_t m = d->m;
    int k, p = 0, q = 0;
    if ((m - a->m_b1d) % 6 != 5) return;
    for (k = 0; k < 6; k++) {
        int s = a->rsg[(m - 5 + k) & 63];
        p += s == (k < 3); q += s == (k >= 3);
    }
    if ((a->rd_pol > 0 ? q : p) >= 5) {
        a->m_trn2d = m - 5 + 24;
        set_cons(a, &a->cpt, a->m_trn2d);
        v90adec_init(&a->dec, &a->tmap);
        v90_scr_init(&a->dscr);
        a->mbn = a->mones = 0; a->got_mp = a->got_mpa = a->after_mpa = a->got_ed = 0;
        a->trn2d_syms = TRN2D_MEAS;     /* CP stays as it was */
        a->ds = D_TRAIN;
    }
}

static void on_dec(v90a_t *a, const v90arx_dec_t *d)
{
    a->ry[d->m & 63] = d->y; a->rsg[d->m & 63] = d->sign;
    switch (a->ds) {
    case D_JD: jd_bit(a, d); break;
    case D_DIL: case D_DILEND: dil_dec(a, d); break;
    case D_RI: ri_dec(a, d); break;
    case D_TRAIN: case D_B1D: case D_DATA: frame_dec(a, d); break;
    case D_RD: rd_dec(a, d); break;
    default: break;
    }
}

/* time limits and the transitions that do not come with a symbol */
static void watch(v90a_t *a)
{
    int32_t rtd = a->p2.rtd;
    if (a->ds == D_SD) {
        if (a->rx.state != V90ARX_WAIT) {
            /* 9.3.2.4: Sd-bar ends Ja */
            a->t_sd = a->now;
            if (a->us == U_JA) { ugo(a, U_QUIET); a->t_ja_end = a->now; }
            v90_scr_init(&a->jscr); a->jprev = 0; a->jbn = 0; a->jones = 0;
            a->ds = D_JD;
        } else if (a->t_ja >= 0 && a->now - a->t_ja > MS(T_SD_MS)) { end(a, V90A_ERR_SD); return; }
    }
    if (a->rx.state == V90ARX_FAIL) { end(a, V90A_ERR_TRAIN); return; }
    if (a->ds == D_JD && !a->jd_ok && a->t_ja_end >= 0 && a->now - a->t_ja_end > MS(T_JD_MS)) { end(a, V90A_ERR_JD); return; }
    if (a->ds == D_JD && a->jd_ok && a->now - a->t_jd > MS(T_JDP_MS)) { end(a, V90A_ERR_JDP); return; }
    if (a->ds == D_DIL && a->now - a->t_jdp > MS(T_DIL_MS)) {
        if (v90dil_acc_min(&a->acc) >= 2) dil_finish(a); else end(a, V90A_ERR_DIL);
        if (a->state == V90A_ENDED) return;
    }
    if (a->ds == D_DILEND && a->now - a->t_dil_end > MS(T_RI_MS)) { end(a, V90A_ERR_RI); return; }
    if ((a->state == V90A_P3 || a->state == V90A_P4) && a->now - a->t_info1a > MS(T_P4_MS) + 5 * rtd) { end(a, V90A_ERR_PHASE4); return; }
    if (a->state == V90A_RENEG && a->t > MS(T_RENEG_MS) + 2 * rtd) { end(a, V90A_ERR_CARRIER); return; }
    if ((a->state == V90A_P4 || a->state == V90A_RENEG) && a->rx_ready && a->tx_ready && !a->clearing) {
        if (a->state == V90A_RENEG) a->renegs++;
        a->state = V90A_DATA; a->t = 0;
        if (a->t_data < 0) a->t_data = a->now;
    }
}

int16_t v90a_tick(v90a_t *a, int16_t rx)
{
    int32_t y;
    int16_t ref;
    int i, nd;
    a->now++; a->t++;
    if (a->state == V90A_ENDED) return 0;
    if (a->state == V90A_P2) {
        int16_t o = p2call_tick(&a->p2, rx);
        if (a->p2.state == P2L_DONE) start_p3(a);
        else if (a->p2.state == P2L_FAILED) end(a, V90A_ERR_PHASE2);
        return o;
    }
    if (v34mod_due(&a->mod)) {
        int32_t re, im;
        up_symbol(a, &re, &im);
        v34mod_put(&a->mod, re, im);
        if (a->state == V90A_ENDED) return 0;
    }
    y = v34mod_tick(&a->mod);
    if (a->md_on) {
        /* MD: uniform white noise at the transmit level (RMS of a uniform
         * variable is its peak over sqrt 3); the canceller at full step for
         * a quarter of it, then 2^-2, then 2^-4 */
        int32_t v, q = a->md_total / 4, mu;
        a->md_rng = a->md_rng * 1664525u + 1013904223u;
        v = (int32_t)(a->md_rng >> 16) - 32768;
        y += (int32_t)(((int64_t)v * a->cfg.level) / 18919);
        mu = a->md_samples < q ? 0 : a->md_samples < 2 * q ? 2 : 4;
        if (a->md_samples == 0 || a->md_samples == q || a->md_samples == 2 * q) v90arx_ec(&a->rx, mu);
        a->md_samples++;
    }
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    if (a->cfg.law) y = v90_octet_linear(a->cfg.law == 2, pcm_encode(a->cfg.law == 2, (int16_t)y));
    a->ecdl[a->ecpos] = (int16_t)y;
    ref = a->ecdl[(a->ecpos - a->cfg.echo_delay) & (V90A_ECDL - 1)];
    a->ecpos = (a->ecpos + 1) & (V90A_ECDL - 1);
    nd = v90arx_tick(&a->rx, rx, ref);
    if (a->md_on && a->md_samples > a->md_total * 3 / 4) {
        a->erle_rx += (int64_t)rx * rx;
        a->erle_res += (int64_t)a->rx.eout * a->rx.eout;
    }
    for (i = 0; i < nd && a->state != V90A_ENDED; i++) on_dec(a, &a->rx.out[i]);
    if (a->state != V90A_ENDED) watch(a);
    return (int16_t)y;
}

int v90a_tx_rate(const v90a_t *a) { return a->state == V90A_DATA ? a->up_rate : 0; }
int v90a_rx_rate(const v90a_t *a) { return a->state == V90A_DATA ? a->down_rate : 0; }

int v90a_snr_db10(const v90a_t *a)
{
    /* the mean power of the data constellations' levels over the error */
    int64_t pw = 0;
    int s, u, n = 0, v;
    if (a->state != V90A_DATA) return -1;
    for (s = 0; s < 6; s++)
        for (u = 0; u < 128; u++)
            if ((a->cp.mask[a->cp.idx[s]][u >> 3] >> (u & 7)) & 1 && a->res.lv[s][u] > 0) {
                pw += (int64_t)a->res.lv[s][u] * a->res.lv[s][u];
                n++;
            }
    if (!n) return -1;
    v = probe_db10(pw / n, v90arx_mse(&a->rx) > 0 ? (int64_t)v90arx_mse(&a->rx) : 1);
    return v < 0 ? 0 : v > 600 ? 600 : v;
}

int v90a_cleardown(v90a_t *a)
{
    if (a->state != V90A_DATA) return -1;
    a->clearing = 1;
    a->rx_ready = a->tx_ready = 0;
    a->state = V90A_RENEG; a->t = 0;
    ugo(a, U_RS);
    return 0;
}

int v90a_retrain(v90a_t *a) { (void)a; return -1; }
