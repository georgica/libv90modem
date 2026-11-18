/* How this C library's printf rounds a double to a few decimals.
 * 0.25 and 0.75 are ties, exactly representable: correct rounding (to the
 * nearest, ties to even, as glibc, musl and mingw's own do) prints 0.2 and
 * 0.8. 5.835 and 0.145 are not ties: the doubles nearest them are just below
 * (5.83499999999999996447..., 0.14499999999999999001...), so the correct
 * output is 5.83 and 0.14. uClibc-ng 1.0.43 prints 0.3 0.8 5.84 0.14 on
 * ARM and 0.3 0.8 5.84 0.15 on m68k: it rounds ties, and some values just
 * below them, up (away from zero), from a decimal string of limited
 * precision. tools/check-ports.sh runs this on each
 * target and, where the answer is not the correct one, allows the
 * difference that rounding makes (a number one unit larger in its last
 * printed digit) and counts it apart. */
#include <stdio.h>

int main(void)
{
    printf("%.1f %.1f %.2f %.2f\n", 0.25, 0.75, 5.835, 0.145);
    return 0;
}
