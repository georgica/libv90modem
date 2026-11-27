/* Test peer: a V.8 CALLING modem (V.8 8.1). Detects ANSam, waits Te, sends CM
 * on V.21(L) until 2 identical JM arrive on V.21(H), then CJ and 75 ms silence.
 * Built from the modem library's own primitives; x86 test side. */
#ifndef V90D_SIM_V8CALL_H
#define V90D_SIM_V8CALL_H
#include "../modem/v21.h"
#include "../modem/v8rx.h"

typedef enum { V8C_WAIT, V8C_TE, V8C_CM, V8C_CJ, V8C_POST, V8C_DONE } v8c_state;

typedef struct {
    v8c_state  state;
    int        fs, t;
    v21mod_t   mod;
    v21demod_t dem;
    v8rx_t     rx;
    double     gi, gq, gph; int gn, ghits;   /* 2100 Hz detector */
    uint8_t    cmbits[200]; int ncm, pos;
    int        want_cj;
    v8_menu_t  jm; int got_jm;
    int        cm_sent;                       /* full CM sequences transmitted */
    uint8_t    pre[64]; int npre, ppos;       /* v8call_prefix: bits sent once before the first CM */
    int        te;                            /* samples of Te after the 2100 Hz detector holds */
} v8call_t;

void v8call_init(v8call_t *c, int fs, int16_t amp, const v8_menu_t *cm);
v8c_state v8call_tick(v8call_t *c, int16_t rx, int16_t *tx);
/* After v8call_init: send these n bits (1 = mark, n <= 64) once, ahead of the
 * first CM, as V.92's QC1a (9.2.1.1: "If signal ANSam is detected for 1 s,
 * the analogue modem shall transmit signal QC1a followed by CM"). Te is then
 * cut so that the first bit goes out 1 s after ANSam began, the 200 ms the
 * detector takes counted in. c->ppos == c->npre once they have all gone. */
void v8call_prefix(v8call_t *c, const uint8_t *bits, int n);

#endif
