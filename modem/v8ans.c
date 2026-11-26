#include "v8ans.h"

#define MS(v, ms) ((int32_t)((int64_t)(ms) * (v)->fs / 1000))

/* carrier squelch: below roughly -46 dBm0 the V.21(L) receiver reads idle mark
 * (V.21 carrier detector: ON above -43 dBm, OFF below -48 dBm) */
#define SQUELCH_AMP 110

void v8ans_init(v8ans_t *v, int fs, int16_t amp, const v8_menu_t *local)
{
    v->state = V8A_SILENCE; v->fs = fs; v->t = 0;
    ansam_init(&v->ansam, fs, amp, 1);          /* V.90 9.1.3.1: with phase reversals */
    v21mod_init(&v->mod, V21H_MARK, V21H_SPACE, fs, 300, amp);
    v21demod_init(&v->dem, V21L_MARK, V21L_SPACE, fs, 300);
    v8rx_init(&v->rx, fs);
    v->squelch = v21demod_energy_of(&v->dem, SQUELCH_AMP);
    v->local = *local;
    v->njm = v->jmpos = 0;
    v->result = V8A_RES_NONE; v->selected = 0;
    v->x2_offer = 0; v->x2 = 0; v->jm_sign = 1;
}

void v8ans_offer_x2(v8ans_t *v, int alaw) { v->x2_offer = !alaw; }
int v8ans_x2(const v8ans_t *v) { return v->x2; }

/* x2 (docs/history/x2-facts.md "Phase 1"): the client's CM offers V.34 and
 * no PCM (RE (D-Modem)), and V.34 is what the JM selects. The JM octets are
 * V.34's; the CM's PSTN access octet comes back with the digital-network
 * bit (the Courier's CM ends 0d, the I-modem's JM 8d: RE (courier-emu)). */
static void x2_jm(v8ans_t *v)
{
    v->x2 = v->x2_offer && v->selected == V8_SEL_V34 && (v->cm.modes & V8_MOD_V34)
            && !v->cm.has_pcm;
    if (!v->x2 || !v->cm.has_access) return;
    v->jm.has_access = 1;
    v->jm.access = (uint8_t)(V8_ACC_DIGITAL | (v->cm.access & V8_ACC_CALL_CELL) | (v->local.access & V8_ACC_ANS_CELL));
}

static void go(v8ans_t *v, v8a_state s) { v->state = s; v->t = 0; }

v8a_state v8ans_tick(v8ans_t *v, int16_t rx, int16_t *tx)
{
    int level, ev = 0, done;
    uint8_t oct[V8_MAX_OCTETS];

    *tx = 0;
    if (v->state == V8A_ANSAM || v->state == V8A_JM) {
        level = v21demod_tick(&v->dem, rx);
        if (v->dem.energy < v->squelch) level = 1;
        ev = v8rx_tick(&v->rx, level);
    }

    switch (v->state) {
    case V8A_SILENCE:
        if (++v->t >= MS(v, V8A_SILENCE_MS)) go(v, V8A_ANSAM);
        break;
    case V8A_ANSAM:
        ansam_run(&v->ansam, tx, 1);
        v->t++;
        if ((ev & V8RX_MENU) && v8_decode(v->rx.msg, v->rx.nmsg, &v->cm) == 0) {
            int n;
            v8_build_jm(&v->cm, &v->local, &v->jm);
            v->selected = v8_selected(&v->jm);
            x2_jm(v);
            n = v8_encode(&v->jm, oct, V8_MAX_OCTETS);
            v->njm = v8_frame(V8_SYNC_CM_JM, oct, n, v->jmbits, V8A_MAXBITS);
            v->jmpos = 0;
            go(v, V8A_JM);
        } else if (v->t >= MS(v, V8A_ANSAM_MS)) {
            v->result = V8A_RES_NO_CM;
            go(v, V8A_POST);
        }
        break;
    case V8A_JM:
        *tx = v21mod_tick(&v->mod, v->jmbits[v->jmpos], &done);
        /* x2: the carrier's sign inverted after each complete JM, a 180
         * degree step; the octets are not changed by it (x2-facts.md
         * "Phase 1": RE (D-Modem), RE (courier-emu); our disassembly of
         * Ie030002, DSP dd62-dd64). The amplitude is far below full scale.
         * Only for V8A_X2_FLIP_MS: the live USR x2 client sent CJ 0.773 s
         * after the JM began (the recorded corpus 0.606-0.641 s); a strict
         * V.8 receiver (a hardware V.34 modem) never decodes it and goes on
         * sending CM, and non-x2 callers that do take it were slow (WebTV
         * 1.47-3.87 s), so the JMs that begin later are plain, of one sign. */
        if (v->jm_sign < 0) *tx = (int16_t)-*tx;
        if (done && ++v->jmpos >= v->njm) {
            v->jmpos = 0;
            if (v->x2) v->jm_sign = v->t < MS(v, V8A_X2_FLIP_MS) ? (int16_t)-v->jm_sign : 1;
        }
        v->t++;
        if (ev & V8RX_CJ) {                      /* JM stops at once (8.2.3) */
            v->result = V8A_RES_OK;
            go(v, V8A_POST);
        } else if (v->t >= MS(v, V8A_JM_MS)) {
            v->result = V8A_RES_NO_CJ;
            go(v, V8A_POST);
        }
        break;
    case V8A_POST:
        if (++v->t >= MS(v, V8A_POST_MS)) go(v, V8A_DONE);
        break;
    case V8A_DONE:
        break;
    }
    return v->state;
}
