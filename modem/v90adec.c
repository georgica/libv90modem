/* V.90 analogue modem: PCM downstream data decoder. See v90adec.h. */
#include <string.h>
#include "v90adec.h"

int v90adec_init(v90adec_t *d, const v90_map_t *m)
{
    int i, k;
    memset(d, 0, sizeof *d);
    if (v90_map_check(m) != 0) return V90_ERR_RANGE;
    d->K = m->K; d->Sr = m->Sr; d->D = (uint8_t)V90_MAP_D(m);
    memset(d->lab, 255, sizeof d->lab);
    for (i = 0; i < 6; i++) {
        d->M[i] = m->M[i];
        for (k = 0; k < m->M[i]; k++) d->lab[i][m->C[i][k]] = (uint8_t)k;
    }
    return 0;
}

void v90adec_reset(v90adec_t *d)
{
    memset(d->gprev, 0, sizeof d->gprev);
    d->qodd = 0; d->sprev = 0;
}

/* the label of Ucode u in interval i; a non-member is taken as the member
 * nearest by Ucode, and flagged */
static int label(const v90adec_t *d, int i, unsigned u, int *bad)
{
    int a, b;
    if (d->lab[i][u & 127] != 255) return d->lab[i][u & 127];
    *bad = 1;
    for (a = (int)u, b = (int)u; a >= 0 || b < 128; a--, b++) {
        if (a >= 0 && d->lab[i][a] != 255) return d->lab[i][a];
        if (b < 128 && d->lab[i][b] != 255) return d->lab[i][b];
    }
    return 0;
}

int v90adec_frame(v90adec_t *d, const uint8_t u[6], const uint8_t pos[6], uint8_t *bits)
{
    int S = 6 - d->Sr, i, n = 0, bad = 0;
    uint64_t R = 0;

    /* 5.4.3 / 5.4.4 backwards: R0 = K0 + M0 (K1 + M1 (K2 + ...)) */
    for (i = 5; i >= 0; i--) R = R * d->M[i] + (uint64_t)label(d, i, u[i], &bad);
    if (d->K < 64 && (R >> d->K) != 0) { bad = 1; R &= ((uint64_t)1 << d->K) - 1; }
    for (i = 0; i < d->K; i++) bits[S + i] = (uint8_t)((R >> i) & 1);

    /* 5.4.5 backwards: the sign bits */
    if (d->Sr == 0) {
        uint8_t prev = d->sprev;
        for (i = 0; i < 6; i++) { uint8_t g = (uint8_t)(pos[i] & 1); bits[i] = g ^ prev; prev = g; }
        d->sprev = prev;
    } else {
        int F = 6 / d->Sr, j, k;
        for (j = 0; j < d->Sr; j++) {
            uint8_t q[6], x, lastodd = 0;
            for (k = 0; k < F; k++) {
                uint8_t g = (uint8_t)(pos[j * F + k] & 1);
                q[k] = g ^ d->gprev[k];
                d->gprev[k] = g;
            }
            x = q[0];
            for (k = 1; k < F; k++) {
                uint8_t p;
                if (!(k & 1)) p = q[k] ^ x;
                else if (k == 1) p = q[1] ^ d->qodd ^ x;
                else p = q[k] ^ q[k - 2];
                bits[n++] = p;
                if (k & 1) lastodd = q[k];
            }
            d->qodd = lastodd;
        }
    }
    return bad ? -(int)d->D : (int)d->D;
}
