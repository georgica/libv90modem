/* V.34 clause 7 scrambler. See v90_scr.h: the scrambler of v34.h, its
 * register kept here (bit 0 the previous bit, x^-k is bit k-1). */
#include "v90_scr.h"
#include "v34.h"

void v90_scr_init(v90_scr_t *s) { s->reg = 0; }

static int scr(v90_scr_t *s, int bit, int poly, int descramble)
{
    v34_scr_t t;
    int out;
    v34_scr_init(&t, poly);
    t.sr = s->reg;
    out = descramble ? v34_descramble(&t, bit) : v34_scramble(&t, bit);
    s->reg = t.sr;
    return out;
}

int v90_scramble_gpc(v90_scr_t *s, int bit)   { return scr(s, bit, V34_GPC, 0); }
int v90_descramble_gpc(v90_scr_t *s, int bit) { return scr(s, bit, V34_GPC, 1); }
int v90_scramble_gpa(v90_scr_t *s, int bit)   { return scr(s, bit, V34_GPA, 0); }
int v90_descramble_gpa(v90_scr_t *s, int bit) { return scr(s, bit, V34_GPA, 1); }
