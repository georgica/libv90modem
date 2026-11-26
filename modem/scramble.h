/* Self-synchronising scrambler / descrambler, generating polynomial
 * 1 + x^-14 + x^-17: V.22bis clause 5 with its 64-consecutive-ones guard
 * (v22_*), and HST's back channel without it (scr17_*; hst-facts.md, "Back
 * channel"). */
#ifndef V90D_MODEM_SCRAMBLE_H
#define V90D_MODEM_SCRAMBLE_H
#include <stdint.h>

typedef struct { uint32_t reg; int ones; int invert; } scr_t;

void scr_init(scr_t *s);
int  v22_scramble(scr_t *s, int di);     /* returns Ds */
int  v22_descramble(scr_t *s, int ds);   /* returns Do */
/* the polynomial alone: Ds = Di ^ Ds.x^-14 ^ Ds.x^-17 (only `reg` is used) */
int  scr17_scramble(scr_t *s, int di);   /* returns Ds */
int  scr17_descramble(scr_t *s, int ds); /* returns Do */

#endif
