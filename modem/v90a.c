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
/* V.92 */
#define TRN1U_SYMS   6000           /* the first TRN1u (>= 2040 T): the canceller learns on it */
#define TRN1U2_MIN   2040           /* the second, before CPt (9.5.2.1.9) */
#define TRN2U_SYMS   12000          /* TRN2u when no SUVd has come (9.6.2.1.1) */
#define TRN2U_RENEG  2400           /* ... in a rate renegotiation (9.8.2.1.2) */
#define T_JP_MS      3000           /* Jp after our Su, Jp' after our second Su-bar */
#define T_P4_92_MS   20000          /* 9.6.2.2.1: B1d within 20 s (+ 6 round trips) of INFO1a */
#define T_RENEG92_MS 12000          /* the digital modem designs CPd again: several seconds */
/* retrains, and the receiver's watch on itself in data mode (data frames: 1333 a second) */
#define RT_FORGET_FRAMES 80000      /* a minute of data mode forgets a retrain */
#define DEG_FRAMES   1333           /* 1 s with the error 3 dB over the design noise: act */
#define COL_FRAMES   333            /* 250 ms with it 12 dB over (the receiver lost): retrain */
#define UP_FRAMES    6667           /* 5 s 6 dB under it, after a renegotiation down: back up */
#define AUTO_GRACE   6667           /* still degraded within 5 s of a renegotiation for the line: retrain */
#define T_SCR_MS     2000           /* 9.6.2.1.2: SCR for no more than 2000 ms */

enum { U_SIL, U_S1, U_SB1, U_MD, U_S2, U_SB2, U_PP, U_TRN, U_JA, U_QUIET, U_SJ, U_SBJ, U_DILQ, U_S4, U_SB4,
       U_CPT, U_SCR, U_CP, U_E, U_DATA, U_RS, U_RSB, U_OFF };
enum { D_SD, D_JD, D_DIL, D_DILEND, D_RI, D_TRAIN, D_B1D, D_DATA, D_RD };
/* the V.92 upstream (the same `us`) */
enum { P_SIL = 64, P_RU, P_RUB, P_TRN1, P_JA, P_QUIET, P_SU1, P_SUB1, P_SU2, P_SUB2, P_TRN1B, P_CPT, P_E1U,
       P_TRN2U, P_SEQ, P_E2U, P_DATA, P_RRU, P_RRUB };

static void end(v90a_t *a, int err)
{
    if (err == V90A_ERR_HANGUP && a->clear_err) err = a->clear_err;
    /* once the call has carried data, a Phase 3 or 4 that fails (a retrain's)
     * and a renegotiation without Ed (9.6.2) are a retrain of ours (v90a_tick) */
    a->rt_check = a->t_data >= 0 && (a->state == V90A_P3 || a->state == V90A_P4 || a->state == V90A_RENEG)
                  && err != V90A_ERR_CLEARDOWN && err != V90A_ERR_HANGUP && err != V90A_ERR_RATE
                  && err != V90A_ERR_RETRAINS && !a->clearing;
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

static void p2_cfg(const v90a_t *a, p2call_cfg *pc)
{
    memset(pc, 0, sizeof *pc);
    pc->level = a->cfg.level; pc->io_delay = a->cfg.echo_delay;
    pc->uinfo = a->cfg.uinfo; pc->md_len = a->cfg.md_len;
    pc->max_rate = a->cfg.max_rx_bps;
    pc->v92 = a->cfg.v92;
}

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
    a->max_rx0 = a->cfg.max_rx_bps;
    p2_cfg(a, &pc);
    p2call_init_v90(&a->p2, &pc);
    tonedet_init(&a->tone_b, TONEDET_1200);
    a->state = V90A_P2;
    a->t_info1a = a->t_ja = a->t_ja_end = a->t_sd = a->t_jd = a->t_jdp = a->t_dil_end = -1;
    a->t_ri = a->t_ribar = a->t_mp = a->t_ed = a->t_data = -1;
    a->dil_m0 = a->dil_end = a->ri_m0 = a->ribar_m0 = a->m_trn2d = a->m_b1d = -1;
    a->t_su = a->t_jp = a->t_cpt = a->t_suvd = -1;
}

static void start_p3_v92(v90a_t *a);
static void jd92_bit(v90a_t *a, const v90arx_dec_t *d);
static void train92(v90a_t *a, const uint8_t *bits, int D, int32_t m0);

/* Phase 2 is over: INFO1a has gone (9.3.2.1) */
static void start_p3(v90a_t *a)
{
    v90arx_cfg_t rc;
    int num, den, k, sr = a->p2.sym_rate;
    if (a->p2.pcm_up) { start_p3_v92(a); return; }
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
            if (a->reneg_ours && !a->clearing) {
                /* ours (9.6.2.1.2): SCR until the digital modem's TRN2d has
                 * shown the noise CP is designed on */
                ugo(a, U_SCR);
                return;
            }
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
        /* our renegotiation: no TRN2d in 2000 ms, CP as it was */
        if (a->reneg_ours && ++a->un >= a->p2.sym_rate * T_SCR_MS / 1000) a->cp_ready = 1;
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

/* a CP of V.90 as the CPt / CPu of V.92 (Table 23: the same fields) */
static void cp_to_cpu(const v90_cp_t *cp, int type, v92_cpu_t *u)
{
    memset(u, 0, sizeof *u);
    u->type = (uint8_t)type;
    u->drn = cp->drn; u->sr = cp->sr; u->ack = cp->ack; u->alaw = cp->alaw; u->ld = cp->ld;
    u->rms_ratio = cp->rms_ratio;
    u->a1 = cp->a1; u->a2 = cp->a2; u->b1 = cp->b1; u->b2 = cp->b2;
    memcpy(u->idx, cp->idx, sizeof u->idx);
    u->codec_differs = cp->codec_differs;
    memcpy(u->mask, cp->mask, sizeof u->mask);
    memcpy(u->codec_mask, cp->codec_mask, sizeof u->codec_mask);
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
    if (a->pcm) { cp_to_cpu(&a->cpt, V92_UP_CPT, &a->cpt92); a->cpt_ready = 1; }   /* V.92: CPt goes out next */
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
    if (r >= 5) { if (++a->ri_units >= 2 && a->pcm) a->ri_seen = 1; return; }
    if (r <= 1 && a->ri_units >= 2) {
        /* Ri-bar, 24 T; TRN2d after it with CPt's constellations (8.6.5) */
        a->ribar_m0 = m - 5; a->m_trn2d = a->ribar_m0 + 24; a->t_ribar = a->now;
        a->ribar_seen = 1;
        set_cons(a, &a->cpt, a->m_trn2d);
        /* the DIL's levels are measured: from here the equaliser leaves the
         * band edge's turning image out (v90arx_edge), TRN2d's noise, which
         * CP is designed on, included */
        v90arx_edge(&a->rx, 1);
        v90arx_timing_hold(&a->rx, 0);
        v90adec_init(&a->dec, &a->tmap);
        v90_scr_init(&a->dscr);
        a->mbn = a->mones = 0; a->got_mp = a->got_mpa = a->after_mpa = a->got_ed = 0;
        a->trn2d_syms = 0;
        if (a->pcm) {
            v92aseq_init(&a->aseq); a->got_suvd = a->got_cpd = a->got_ack = 0;
        }
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
        a->dsn[s] = q.sigma2_q8[s];         /* what the receiver's watch compares with */
    }
    q.rates = a->jd.rates; q.max_bps = a->cfg.max_rx_bps; q.up_rates = a->up_rates;
    if (v90dil_design_cp(&a->res, &q, &a->cp, &a->cp_ch) == 0 || v90_map_from_cp(&a->dmap, &a->cp) != 0) {
        /* nothing fits: CP with drn = 0 is the cleardown (8.5.2; V.92: CPu, 9.11) */
        a->clearing = 1; a->error = V90A_ERR_RATE; a->cpus_left = CLEAR_CPS;
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
        if (a->pcm && a->after_mpa != 3) { train92(a, bits, D, m0); break; }
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
            if (a->state == V90A_P4) a->since_auto = AUTO_GRACE;
            if (a->down_rate > a->best_rate) a->best_rate = a->down_rate;
            a->mse_ref = (uint64_t)a->rx.pq;   /* (the equaliser's input power) */
            a->rd_frames = 0; a->bad_syms = 0;
            a->deg_frames = a->good_frames = 0;
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
        if (a->rd_frames >= 2 && !a->col_frames) {      /* (a lost receiver decides anything) */
            /* 9.6.2.2: the digital modem renegotiates; S, S-bar, CP from us.
             * V.92 (9.8.2.2): our Ru waits for its Rd-bar */
            a->rx_ready = 0; a->tx_ready = 0;
            a->rd_units = 2;
            if (!a->pcm && a->reneg_ours) { if (a->state != V90A_RENEG) { a->state = V90A_RENEG; a->t = 0; } }
            else if (!a->pcm) { a->state = V90A_RENEG; a->t = 0; ugo(a, U_RS); }
            else if (a->state != V90A_RENEG) { a->state = V90A_RENEG; a->t = 0; }
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

/* our rate renegotiation: 1 asked for, 2 for the line (see v90a.h) */
static int renegotiate(v90a_t *a, int kind)
{
    if (a->state != V90A_DATA) return -1;
    a->reneg_ours = kind;
    a->cp_ready = 0;                    /* designed on the digital modem's TRN2d (rd_dec) */
    a->rx_ready = a->tx_ready = 0;
    a->state = V90A_RENEG; a->t = 0;
    if (a->pcm) {
        /* 9.8.2.1.1: Ru on our next data frame boundary; what Phase 4 heard
         * (SUVd, CPd, their acknowledge bits, Ed) is not this one's */
        v92aseq_init(&a->aseq); a->got_suvd = a->got_cpd = a->got_ack = a->got_ed = 0;
        a->ru_due = 1;
    } else ugo(a, U_RS);                /* 9.6.2.1.1: S, S-bar */
    return 0;
}

/* once a data frame in data mode: the receiver's error against the noise
 * CP was designed for (v90a.h). The sum over the intervals: a pad or a
 * robbed bit leaves one interval noisier than the others, by design. */
static void line_watch(v90a_t *a)
{
    uint64_t e = 0, d = 0, p = (uint64_t)a->rx.pq;
    int s;
    if (++a->rt_clock >= RT_FORGET_FRAMES) { a->rt_clock = 0; if (a->rt_recent > 0) a->rt_recent--; }
    if (a->since_auto < AUTO_GRACE) a->since_auto++;
    for (s = 0; s < 6; s++) { e += v90arx_mse_slot(&a->rx, s); d += a->dsn[s]; }
    if (!d) return;
    a->deg_frames = e > 2 * d ? a->deg_frames + 1 : 0;
    a->good_frames = 4 * e < d ? a->good_frames + 1 : 0;
    /* collapsed: decisions on nothing (the error 12 dB over the design). Our
     * V.92 transmitter runs on the clock of this receiver's timing loop,
     * which now follows noise: it stands until the retrain, so that the PCM
     * upstream stays whole meanwhile */
    if (e > 16 * d) {
        if (a->col_frames++ == 0 && a->pcm) v90arx_timing_hold(&a->rx, 1);
    } else if (a->col_frames) {
        a->col_frames = 0;
        if (a->pcm) v90arx_timing_hold(&a->rx, 0);
    }
    if (a->deg_frames >= DEG_FRAMES || a->col_frames >= COL_FRAMES) {
        /* the line's level moved by 3 dB: the DIL's levels no longer hold */
        int moved = p > 2 * a->mse_ref || 2 * p < a->mse_ref;
        a->deg_frames = 0;
        a->auto_down = 1;
        if (moved || a->col_frames || a->since_auto < AUTO_GRACE) {
            if (a->rt_recent < V90A_MAX_RETRAINS) a->rt_due = 1;
            else { a->clear_err = V90A_ERR_RETRAINS; (void)v90a_cleardown(a); }
        } else (void)renegotiate(a, 2);                         /* only the noise rose: CP for it */
    } else if (a->good_frames >= UP_FRAMES && a->auto_down && a->since_auto >= AUTO_GRACE && a->down_rate < a->best_rate
               && (a->cfg.max_rx_bps <= 0 || a->down_rate < a->cfg.max_rx_bps)) {
        /* what brought the rate down has gone again: once more, up */
        if (renegotiate(a, 2) == 0) a->auto_down = 0;
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
    if (k == 5) {
        frame(a, d->m - 5);
        if (a->ds == D_DATA && a->state == V90A_DATA) line_watch(a);
    }
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
        if (a->reneg_ours) { a->trn2d_syms = 0; a->cp_ready = 0; }   /* ours: designed on TRN2d's noise */
        if (a->pcm) {
            /* 9.8.2.2.2: Ru on our next data frame boundary */
            v92aseq_init(&a->aseq); a->got_suvd = a->got_cpd = a->got_ack = 0;
            if (a->us == P_DATA) a->ru_due = 1;
        }
        a->ds = D_TRAIN;
    }
}

static void on_dec(v90a_t *a, const v90arx_dec_t *d)
{
    a->ry[d->m & 63] = d->y; a->rsg[d->m & 63] = d->sign;
    switch (a->ds) {
    case D_JD: if (a->pcm) jd92_bit(a, d); else jd_bit(a, d); break;
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
            if (a->us == P_JA) a->ja_stop = 1;          /* V.92 9.5.2.1.3: at the next 12 bit boundary */
            v90_scr_init(&a->jscr); a->jprev = 0; a->jbn = 0; a->jones = 0;
            a->ds = D_JD;
        } else if (a->t_ja >= 0 && a->now - a->t_ja > MS(T_SD_MS)) { end(a, V90A_ERR_SD); return; }
    }
    if (a->rx.state == V90ARX_FAIL) { end(a, V90A_ERR_TRAIN); return; }
    if (a->ds == D_JD && !a->jd_ok && a->t_ja_end >= 0 && a->now - a->t_ja_end > MS(T_JD_MS)) { end(a, V90A_ERR_JD); return; }
    if (a->ds == D_JD && a->jd_ok && !a->pcm && a->now - a->t_jd > MS(T_JDP_MS)) { end(a, V90A_ERR_JDP); return; }
    if (a->pcm && a->ds == D_JD && a->t_su >= 0 && !a->jp_ok && a->now - a->t_su > MS(T_JP_MS)) { end(a, V90A_ERR_JP); return; }
    if (a->pcm && a->ds == D_JD && a->jp_ok && a->us == P_TRN1B && a->now - a->t_jp > MS(T_JP_MS) + MS(100)) { end(a, V90A_ERR_JP); return; }
    if (a->ds == D_DIL && a->now - a->t_jdp > MS(T_DIL_MS)) {
        if (v90dil_acc_min(&a->acc) >= 2) dil_finish(a); else end(a, V90A_ERR_DIL);
        if (a->state == V90A_ENDED) return;
    }
    if (a->ds == D_DILEND && a->now - a->t_dil_end > MS(T_RI_MS)) { end(a, V90A_ERR_RI); return; }
    if ((a->state == V90A_P3 || a->state == V90A_P4)
        && a->now - a->t_info1a > (a->pcm ? MS(T_P4_92_MS) + 6 * rtd : MS(T_P4_MS) + 5 * rtd)) { end(a, V90A_ERR_PHASE4); return; }
    if (a->state == V90A_RENEG && a->t > (a->pcm ? MS(T_RENEG92_MS) : MS(T_RENEG_MS)) + 2 * rtd) { end(a, V90A_ERR_CARRIER); return; }
    if ((a->state == V90A_P4 || a->state == V90A_RENEG) && a->rx_ready && a->tx_ready && !a->clearing) {
        if (a->state == V90A_RENEG) a->renegs++;
        if (a->reneg_ours == 2) a->since_auto = 0;
        a->reneg_ours = 0;
        a->state = V90A_DATA; a->t = 0;
        if (a->t_data < 0) a->t_data = a->now;
    }
}

/* ==== V.92: PCM upstream ==================================================
 *
 * Phase 3 (9.5.2): silence 70 ms, Ru 384 T, Ru-bar 24 T, TRN1u (the echo
 * canceller learns on it: the digital modem is silent, and TRN1u is our
 * widest signal), Ja (24 ones, the DIL descriptor with our upstream rates)
 * until Sd-bar is heard, to the next 12 bit boundary; silence; the
 * downstream receiver learns on Sd and TRN1d; Jd; Su 144 T, Su-bar 24.5 T,
 * Su until Jp, Su-bar (24 + Jp's fraction) T; TRN1u again (the data frame
 * count begins) while the DIL is measured; CPt (24 ones first) until Ri,
 * E1u.
 * Phase 4 (9.6.2): TRN2u until SUVd (or 12000 T), SUVu; one CPu after the
 * first SUVd (CP of V.90's design, from the DIL and TRN2d's noise), SUVu
 * again, the acknowledge bit once CPd is in; E2u once an acknowledged
 * sequence has gone out and SUVd' / CPd' or Ed has come in; B1u and data
 * through modem/v92_enc with what CPd dictates (precoder, prefilter, gain,
 * moduli, constellations), every symbol on the network clock (pcmtx).
 * Downstream as V.90 but for Jd / Jp / Jp' (8.6.2 to 8.6.4) and SUVd / CPd /
 * Ed in TRN2d (modem/v92aseq).
 * Rate renegotiation begun by the digital modem (9.8.2.2): after its Rd-bar,
 * Ru on our next data frame boundary, Ru-bar, TRN2u (2400 T or until SUVd),
 * then as Phase 4. Cleardown (9.11): the same with a CPu of drn = 0. */

static void start_p3_v92(v90a_t *a)
{
    v90arx_cfg_t rc;
    int r;
    a->pcm = 1;
    a->alaw = a->p2.info0d.alaw != 0;
    a->t_info1a = a->now;
    a->lu = a->cfg.level > 0 ? a->cfg.level : 3000;
    memset(&rc, 0, sizeof rc);
    rc.alaw = a->alaw; rc.uinfo = a->cfg.uinfo;
    rc.known_lv = dil_known; rc.known_ctx = a;
    v90arx_init(&a->rx, &rc);
    v90arx_deaf(&a->rx, 1);             /* until Ja: our own Ru has Sd's period */
    v90arx_clock(&a->rx, a->p2.clk_ppm_q8);     /* the symbol clock from the start (6.2/V.92) */
    a->rx_on = 1;
    v90dil_design(&a->dil, a->cfg.uinfo);
    a->up92_rates = 0;
    for (r = 0; r < V92_UP_NUM_RATES; r++)
        if (a->cfg.max_tx_bps <= 0 || V92_UP_BPS(r + 1) <= a->cfg.max_tx_bps) a->up92_rates |= 1u << r;
    if (!a->up92_rates) a->up92_rates = 1;          /* 24000 bit/s is the least there is */
    pcmtx_init(&a->ptx);
    pcmtx_wide(&a->ptx, 1);             /* the canceller learns on the whole band (pcmtx.h) */
    v92_sgn_init(&a->sgn, 0);
    a->ds = D_SD;
    ugo(a, P_SIL);
    a->state = V90A_P3; a->t = 0;
}

/* Jd (Table 21) repeated, then Jp (Table 22) repeated, then Jp' (12 zeros):
 * the sign of UINFO, differentially, through the GPC descrambler that has run
 * since TRN1d began, as in V.90 */
static void jd92_bit(v90a_t *a, const v90arx_dec_t *d)
{
    int c = d->sign ^ a->jprev, bit, kind;
    a->jprev = d->sign;
    bit = v90_descramble_gpc(&a->jscr, c);
    if (!a->jd_ok) {
        if (a->jbn > 0) {
            a->jb[a->jbn++] = (uint8_t)bit;
            if (a->jbn == V92_J_BITS) {
                if (v92_j_kind(a->jb, V92_J_BITS) == V92_J_JD && v92_jd_parse(a->jb, V92_J_BITS, &a->jd92) == V92_J_BITS) {
                    a->jd_ok = 1; a->t_jd = a->now; a->jdp_zeros = 0;
                    a->jd.rates = a->jd92.rates;
                    if (a->us == P_QUIET) { ugo(a, P_SU1); a->t_su = a->now; }    /* 9.5.2.1.6 */
                }
                a->jbn = 0; a->jones = 0;
            }
            return;
        }
        if (bit) { a->jones++; return; }
        if (a->jones >= 17) { memset(a->jb, 1, 17); a->jb[17] = 0; a->jbn = 18; }
        a->jones = 0;
        return;
    }
    /* in step with the sequences: more Jd, Jp, and after a Jp maybe Jp' */
    if (a->jbn == 0 && !bit && a->jp_ok) {
        a->jdp_m = d->m; a->jdp_zeros = 1;
        a->dil_m0 = d->m + 12; a->dil_mnext = a->dil_m0; a->dil_end = -1;
        v90dil_cur_init(&a->dcur);
        v90arx_hold(&a->rx, 1);
    } else if (a->jdp_zeros > 0) {
        if (bit) { a->jdp_zeros = 0; a->dil_m0 = -1; v90arx_hold(&a->rx, 0); }
        else if (++a->jdp_zeros == 12) {
            int s;
            a->t_jdp = a->now;
            for (s = 0; s < 6; s++) a->sig_jd[s] = (uint32_t)v90arx_mse_slot(&a->rx, s);
            v90dil_acc_init(&a->acc);
            a->acc_on = 1;
            a->ds = D_DIL;
            /* V.92: our transmitter runs on this loop's clock (6.2/V.92), and
             * the DIL's repetitive patterns, on levels still being measured,
             * pull the estimate the loop follows (0.01 to 0.03 sample on the
             * pad and robbed bit lines, back during TRN2d), moving our
             * symbols while the digital modem measures them for CPd. The
             * clock from L2 is within 0.1 ppm: the loop stands until Ri-bar
             * (PROGRESS.md, robust 2b). V.90's clock comes from Sd (off by up
             * to 1.6 ppm) and nothing follows it: its loop runs on. */
            v90arx_timing_hold(&a->rx, 1);
            return;
        }
    }
    a->jb[a->jbn] = (uint8_t)bit;
    if (++a->jbn < V92_J_BITS) return;
    a->jbn = 0;
    kind = v92_j_kind(a->jb, V92_J_BITS);
    if (kind == V92_J_JP && !a->jp_ok && v92_jp_parse(a->jb, V92_J_BITS, &a->jp) == V92_J_BITS) {
        a->jp_ok = 1; a->t_jp = a->now;             /* 9.5.2.1.8: Su-bar with the fraction */
    }
}

/* a data frame of TRN2d (SUVd, CPd, Ed in it) */
static void train92(v90a_t *a, const uint8_t *bits, int D, int32_t m0)
{
    v92_suvd_t sv;
    int ev, i, ones = 0;
    if (!a->got_suvd && !a->got_cpd && !a->aseq.in) {
        for (i = 0; i < D; i++) ones += bits[i];
        a->trn2d_frames++;
        if (ones != D) a->trn2d_bad++;
    }
    ev = v92aseq_frame(&a->aseq, bits, D, &sv, &a->cpd);
    if (ev & V92AS_SUVD) {
        a->got_suvd = 1;
        if (sv.ack) a->got_ack = 1;
        if (a->t_suvd < 0) a->t_suvd = a->now;
    }
    if (ev & V92AS_CPD) {
        a->got_cpd = 1;
        if (a->cpd.ack) a->got_ack = 1;
        if (a->t_mp < 0) a->t_mp = a->now;
        if (a->cpd.drn == 0) { end(a, a->clearing ? V90A_ERR_HANGUP : V90A_ERR_CLEARDOWN); return; }    /* 9.11 */
    }
    if ((ev & V92AS_ED) && (a->got_suvd || a->got_cpd)) {
        /* Ed (8.8.2): this frame and the next; B1d after them, with our CPu */
        a->got_ed = 1; a->t_ed = a->now;
        a->m_b1d = m0 + 12;
        set_cons(a, &a->cp, a->m_b1d);
        a->after_mpa = 3;
    }
}

/* ---- the upstream symbols ---- */

static int16_t two(v90a_t *a, int sbit)
{
    a->sbit = sbit;
    return (int16_t)(sbit ? -a->lu : a->lu);
}

static void start_trn2u(v90a_t *a, int reneg)
{
    v92_ml_init(&a->ml, reneg ? a->jp.pt8_reneg : a->jp.pt8_train, a->sbit);
    a->in_reneg = reneg;
    a->cpu_sent = a->ack_sent = a->seq_ack = 0;
    ugo(a, P_TRN2U);
}

static int enc_get(void *user)
{
    v90a_t *a = (v90a_t *)user;
    if (a->b1_bits > 0) { if (--a->b1_bits == 0) a->tx_ready = 1; return 1; }    /* B1u: ones (8.7.1) */
    return a->cfg.getbit ? a->cfg.getbit(a->cfg.user) & 1 : 1;
}

/* E2u is over: B1u and data with CPd's parameters (8.7.1) */
static void start_b1u(v90a_t *a)
{
    const v92_cpd_t *c = &a->cpd;
    if (!a->got_cpd || c->drn < 1 || c->drn > V92_UP_NUM_RATES || !((a->up92_rates >> (c->drn - 1)) & 1)
        || !c->has_mod || !c->has_filt || !c->has_const
        || v92_enc_init(&a->enc, &a->cpd.map, &a->cpd.filt, enc_get, a) != 0) { end(a, V90A_ERR_CPD); return; }
    a->b1_bits = (int32_t)V92_B1U_FRAMES * c->map.K;
    a->up_rate = V92_UP_BPS(c->drn);
    a->tx_ready = 0;
    ugo(a, P_DATA);
}

/* the next sequence in the TRN2u modulation (9.6.2.1.2 to .4, 9.11) */
static void seq_next(v90a_t *a)
{
    uint8_t bits[V92_CPU_MAX_BITS];
    int unit = a->ml.nbits * 12, n;
    if (a->seq_ack) a->ack_sent = 1;
    a->seq_ack = 0;
    if (a->clearing) {
        /* CPu with drn = 0, CLEAR_CPS of them, then silence */
        v92_cpu_t u;
        if (a->cpus_left-- <= 0) { end(a, V90A_ERR_HANGUP); return; }
        cp_to_cpu(&a->cp, V92_UP_CPU, &u);
        u.drn = 0; u.ack = 0;
        load_bits(a, bits, v92_cpu_build(&u, unit, bits));
        return;
    }
    if (a->ack_sent && (a->got_ack || a->got_ed)) { ugo(a, P_E2U); return; }
    if (a->got_suvd && a->cp_ready && (!a->cpu_sent || (!a->got_ack && a->since_cpu > MS(100) + a->p2.rtd + 600))) {
        v92_cpu_t u;
        cp_to_cpu(&a->cp, V92_UP_CPU, &u);
        u.ack = (uint8_t)(a->got_cpd != 0);
        n = v92_cpu_build(&u, unit, bits);
        load_bits(a, bits, n);
        a->cpu_sent = 1; a->since_cpu = -(n / a->ml.nbits);    /* counted from its end */
        a->seq_ack = u.ack;
        return;
    }
    {
        v92_suvu_t v;
        v.wait_cpu = 0; v.level = V92_SUVU_NO_LEVEL; v.silence = 0; v.ack = (uint8_t)(a->got_cpd != 0);
        load_bits(a, bits, v92_suvu_build(&v, unit, bits));
        a->seq_ack = v.ack;
    }
}

static int16_t ml_sym(v90a_t *a, const uint8_t *b)
{
    int lvl = v92_ml_tx(&a->ml, b);
    return (int16_t)v92_trn2u_level(lvl, a->ml.nbits == 3, a->lu);
}

/* the next symbol; linear */
static int16_t up92_symbol(v90a_t *a)
{
    int32_t n = a->un++;
    int16_t v;
    switch (a->us) {
    case P_SIL:
        if (a->un >= MS(70)) ugo(a, P_RU);                  /* 9.5.2.1.1 */
        return 0;
    case P_RU: case P_RRU:
        v = two(a, v92_ru_sbit((unsigned)n, 0));
        if (a->un >= 384) ugo(a, a->us == P_RU ? P_RUB : P_RRUB);
        return v;
    case P_RUB: case P_RRUB:
        v = two(a, v92_ru_sbit((unsigned)n, 1));
        if (a->un >= 24) {
            if (a->us == P_RRUB) start_trn2u(a, 1);
            else {
                v92_sgn_init(&a->sgn, 0);
                a->ec_n = 0; a->ec_len = TRN1U_SYMS;         /* the canceller learns (v90a_tick) */
                a->erle_rx = a->erle_res = 0;
                ugo(a, P_TRN1);
            }
        }
        return v;
    case P_TRN1:
        v = two(a, v92_sgn_tx_trn(&a->sgn));
        if (a->un >= TRN1U_SYMS) {
            uint8_t bits[V92_JA_MAX_BITS];
            v92_ja_t ja;
            ja.dil = a->dil; ja.rates = a->up92_rates;
            load_bits(a, bits, v92_ja_build(&ja, bits));
            a->t_ja = a->now; a->ja_stop = 0;
            v90arx_deaf(&a->rx, 0);     /* Sd may come now (9.5.2.1.3) */
            ugo(a, P_JA);
        }
        return v;
    case P_JA: {
        int bit = 1;
        if (n >= V92_JA_PREAMBLE) {
            if (a->frpos >= a->frn) a->frpos = 0;
            bit = (int)fr_bit(a, a->frpos++);
        }
        v = two(a, v92_sgn_tx_bit(&a->sgn, bit));
        if (a->ja_stop && a->un % 12 == 0) {
            ugo(a, P_QUIET); a->t_ja_end = a->now;
            pcmtx_wide(&a->ptx, 0);     /* silence: the low-pass from here on */
        }
        return v;
    }
    case P_QUIET:
        return 0;
    case P_SU1: case P_SU2:
        v = (int16_t)v92_su_level((unsigned)n, 0, a->lu);
        if (a->us == P_SU1 ? a->un >= 144 : a->jp_ok && a->un % 12 == 0) ugo(a, a->us == P_SU1 ? P_SUB1 : P_SUB2);
        return v;
    case P_SUB1: case P_SUB2:
        v = (int16_t)v92_su_level((unsigned)n, 1, a->lu);
        if (a->un >= 24) {
            if (a->us == P_SUB1) { a->slip = 0x8000; ugo(a, P_SU2); }      /* 24.5 T (9.5.2.1.7) */
            else {
                a->slip = a->jp.frac;                               /* (24 + fraction) T (9.5.2.1.8) */
                v92_sgn_init(&a->sgn, 0);
                ugo(a, P_TRN1B);
            }
        }
        return v;
    case P_TRN1B:
        v = two(a, v92_sgn_tx_trn(&a->sgn));
        if (a->un % 12 == 0 && a->un >= TRN1U2_MIN && a->cpt_ready) {
            uint8_t bits[V92_CPU_MAX_BITS];
            load_bits(a, bits, v92_cpu_build(&a->cpt92, 12, bits));
            a->t_cpt = a->now;
            ugo(a, P_CPT);
        }
        return v;
    case P_CPT: {
        int bit = 1;
        if (n >= 24) bit = (int)fr_bit(a, a->frpos++);       /* 8.5.1: 24 ones first */
        v = two(a, v92_sgn_tx_bit(&a->sgn, bit));
        if (n >= 24 && a->frpos >= a->frn) {
            if (a->ri_seen) ugo(a, P_E1U);                  /* 9.5.2.1.11: complete the current one */
            else a->frpos = 0;
        }
        return v;
    }
    case P_E1U:
        v = two(a, v92_sgn_tx_bit(&a->sgn, 0));
        if (a->un >= V92_E1U_BITS) { start_trn2u(a, 0); a->state = V90A_P4; }     /* Phase 4 (9.6.2) */
        return v;
    case P_TRN2U:
        v = ml_sym(a, NULL);
        if (a->un % 12 == 0 && (a->un >= (a->in_reneg ? TRN2U_RENEG : TRN2U_SYMS) || a->got_suvd)) {
            seq_next(a);
            if (a->state != V90A_ENDED && a->us == P_TRN2U) ugo(a, P_SEQ);
        }
        return v;
    case P_SEQ: {
        uint8_t b[3];
        int k;
        for (k = 0; k < a->ml.nbits; k++) b[k] = (uint8_t)(a->frpos < a->frn ? fr_bit(a, a->frpos++) : 0);
        v = ml_sym(a, b);
        a->since_cpu++;
        if (a->frpos >= a->frn) seq_next(a);
        return v;
    }
    case P_E2U: {
        static const uint8_t z[3] = { 0, 0, 0 };
        v = ml_sym(a, z);
        if (a->un >= 12 + (a->cpd.e2u_extend ? 1 : 0)) start_b1u(a);
        return v;
    }
    case P_DATA: {
        int64_t x;
        if (a->ru_due && a->enc.i == 0) {                   /* 9.8.2.1.1: on a data frame boundary */
            a->ru_due = 0;
            a->tx_ready = 0;
            ugo(a, P_RRU);
            return up92_symbol(a);
        }
        x = (int64_t)v92_enc_symbol(&a->enc, NULL) * a->lu;   /* G v(n), Q14: 1.0 is LU (reading 35) */
        x = (x + 8192) >> 14;
        return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
    }
    default:
        return 0;
    }
}

static int16_t tick(v90a_t *a, int16_t rx)
{
    int32_t y;
    int16_t ref;
    int i, nd;
    a->now++; a->t++;
    if (a->state == V90A_ENDED) return 0;
    if (a->state == V90A_P2) {
        int16_t o = p2call_tick(&a->p2, rx);
        if (a->p2.state == P2L_DONE) { a->info0d = a->p2.info0d; start_p3(a); }
        else if (a->p2.state == P2L_FAILED) end(a, a->t_data >= 0 ? V90A_ERR_RETRAINS : V90A_ERR_PHASE2);
        return o;
    }
    if (a->pcm) {
        /* V.92: the symbols on the network clock as the receiver keeps it */
        pcmtx_clock(&a->ptx);
        while (pcmtx_due(&a->ptx)) {
            int16_t sy = up92_symbol(a);
            if (a->state == V90A_ENDED) return 0;
            pcmtx_put(&a->ptx, sy, a->rx.step + a->rx.slew);
            if (a->slip) { pcmtx_slip(&a->ptx, a->slip); a->slip = 0; }
        }
        y = pcmtx_out(&a->ptx);
        if (a->ec_len > 0) {
            /* the canceller learns on TRN1u: full step for a quarter of it,
             * then 2^-2, then 2^-4, frozen at its end */
            int32_t q = a->ec_len / 4;
            if (a->ec_n == 0 || a->ec_n == q || a->ec_n == 2 * q) v90arx_ec(&a->rx, a->ec_n == 0 ? 0 : a->ec_n == q ? 2 : 4);
            if (++a->ec_n >= a->ec_len) {
                v90arx_ec(&a->rx, -1);
                a->erle_db10 = probe_db10(a->erle_rx + 1, a->erle_res + 1);
                a->ec_len = 0;
            }
        }
    } else {
        if (v34mod_due(&a->mod)) {
            int32_t re, im;
            up_symbol(a, &re, &im);
            v34mod_put(&a->mod, re, im);
            if (a->state == V90A_ENDED) return 0;
        }
        y = v34mod_tick(&a->mod);
    }
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
    /* the digital modem's retrain: Tone B for more than 50 ms (9.5.2.2 / 9.7.2.2) */
    if (tonedet_tick(&a->tone_b, a->rx.eout)) a->rt_due = 2;
    if ((a->md_on && a->md_samples > a->md_total * 3 / 4) || (a->ec_len > 0 && a->ec_n > a->ec_len * 3 / 4)) {
        a->erle_rx += (int64_t)rx * rx;
        a->erle_res += (int64_t)a->rx.eout * a->rx.eout;
    }
    for (i = 0; i < nd && a->state != V90A_ENDED; i++) on_dec(a, &a->rx.out[i]);
    if (a->state != V90A_ENDED) watch(a);
    return (int16_t)y;
}

/* a retrain, ours or the digital modem's: everything as at the start but the
 * call's counts, our clock and the first INFO0d; 70 ms of silence first */
static void begin_retrain(v90a_t *a, int ours)
{
    v90a_cfg cfg = a->cfg;
    info0_t i0 = a->info0d;
    int32_t now = a->now, t_data = a->t_data, rt_clock = a->rt_clock;
    int retrains = a->retrains, ro = a->retrains_ours, rr = a->rt_recent, renegs = a->renegs, max0 = a->max_rx0;
    int best = a->best_rate, adown = a->auto_down;
    if (rr >= V90A_MAX_RETRAINS) {
        a->rt_due = 0;
        a->state = V90A_P2;             /* (so that end() asks for nothing more) */
        end(a, V90A_ERR_RETRAINS);
        return;
    }
    v90a_init(a, &cfg);
    a->max_rx0 = max0;
    a->now = now; a->t_data = t_data; a->rt_clock = rt_clock; a->info0d = i0;
    a->retrains = retrains + 1; a->retrains_ours = ro + (ours != 0); a->rt_recent = rr + 1;
    a->renegs = renegs; a->best_rate = best; a->auto_down = adown;
    a->state = V90A_RETRAIN; a->t = 0;
}

int16_t v90a_tick(v90a_t *a, int16_t rx)
{
    int16_t y;
    if (a->state == V90A_RETRAIN) {
        /* 9.5.2 / 9.7.2: 70 ms of silence, then Tone A and Phase 2 */
        a->now++; a->t++;
        if (a->t >= MS(70)) {
            p2call_cfg pc;
            p2_cfg(a, &pc);
            p2call_init_v90_retrain(&a->p2, &pc, &a->info0d);
            a->state = V90A_P2; a->t = 0;
        }
        return 0;
    }
    y = tick(a, rx);
    if (a->rt_due || (a->state == V90A_ENDED && a->rt_check)) {
        begin_retrain(a, a->rt_due != 2);
        return 0;
    }
    return y;
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
    if (a->pcm) { a->ru_due = 1; a->cpus_left = CLEAR_CPS; }    /* 9.11: a renegotiation, CPu with drn = 0 */
    else ugo(a, U_RS);
    return 0;
}

int v90a_retrain(v90a_t *a)
{
    if ((a->state != V90A_DATA && a->state != V90A_RENEG) || a->clearing || a->rt_recent >= V90A_MAX_RETRAINS) return -1;
    begin_retrain(a, 1);
    return 0;
}

int v90a_renegotiate(v90a_t *a, int max_rx_bps)
{
    if (a->state != V90A_DATA) return -1;
    if (max_rx_bps > 0) a->cfg.max_rx_bps = a->max_rx0 > 0 && max_rx_bps > a->max_rx0 ? a->max_rx0 : max_rx_bps;
    return renegotiate(a, 1);
}
