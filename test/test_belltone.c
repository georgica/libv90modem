/* modem/belltone: the 2225 Hz Bell answer tone, told from the 2100 Hz answer
 * tones and from a V.22 answerer's unscrambled ones (2250 Hz). */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "../modem/belltone.h"
#include "../modem/v22bis.h"
#include "../sim/noise.h"

#define FS 8000
#define TWO_PI 6.283185307179586
#define PI 3.141592653589793

/* dBm0 to the RMS of the 16-bit scale (0 dBm0 = 15770) */
static double rms_of(double dbm0) { return 15770.0 * pow(10.0, dbm0 / 20.0); }
static int16_t clip(double x) { long q = lrint(x); return (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q); }

enum { S_TONE, S_ANSAM, S_ANS, S_USB1, S_SB1, S_DIAL, S_NOISE };

typedef struct { int kind; double hz, dbm0, noise_dbm0; int start; } sig_t_;

static v22tx_t TX;
static rng_t R;

static double sample(const sig_t_ *s, long n)
{
    double t = (double)n / FS, a = rms_of(s->dbm0) * sqrt(2.0), x = 0;
    if (n < s->start) return 0;
    switch (s->kind) {
    case S_TONE: x = a * cos(TWO_PI * s->hz * t); break;
    case S_ANSAM: {
        double ph = TWO_PI * 2100 * t + (((long)(t / 0.45)) % 2 ? PI : 0);
        x = a * (1 + 0.2 * sin(TWO_PI * 15 * t)) * cos(ph);
        break;
    }
    case S_ANS: x = a * cos(TWO_PI * 2100 * t + (((long)(t / 0.45)) % 2 ? PI : 0)); break;
    case S_USB1: case S_SB1: x = v22tx_tick(&TX) * rms_of(s->dbm0) / 3000.0; break;
    case S_DIAL: x = a * 0.5 * (cos(TWO_PI * 350 * t) + cos(TWO_PI * 440 * t)); break;
    case S_NOISE: x = 0; break;
    }
    if (s->noise_dbm0 > -200) x += rng_gauss(&R) * rms_of(s->noise_dbm0);
    return x;
}

/* the time of the first decision, seconds; -1 = none in `ms` */
static double run(const sig_t_ *s, long ms)
{
    belltone_t b; long n;
    belltone_init(&b, 0);
    rng_seed(&R, 7);
    if (s->kind == S_USB1 || s->kind == S_SB1) {
        v22tx_init(&TX, V22_FC_HIGH, 3000, NULL, NULL);
        v22tx_set_mode(&TX, s->kind == S_USB1 ? V22_TX_USB1 : V22_TX_SB1_1200);
    }
    for (n = 0; n < ms * FS / 1000; n++)
        if (belltone_tick(&b, clip(sample(s, n)))) return (double)(n - s->start) / FS;
    return -1;
}

static int one(const char *what, int kind, double hz, double dbm0, double noise, int start, int want)
{
    sig_t_ s; double t;
    s.kind = kind; s.hz = hz; s.dbm0 = dbm0; s.noise_dbm0 = noise; s.start = start;
    t = run(&s, 3000);
    printf("  %-52s %s at %.3f s\n", what, t >= 0 ? "2225 Hz" : "nothing", t);
    if (want) { CHECK(t >= 0); CHECK(t <= 0.25); }
    else CHECK(t < 0);
    return t >= 0;
}

int main(void)
{
    printf("the Bell answer tone\n");
    one("2225 Hz -10 dBm0", S_TONE, 2225, -10, -300, 0, 1);
    one("2225 Hz 0 dBm0", S_TONE, 2225, 0, -300, 0, 1);
    one("2225 Hz -40 dBm0", S_TONE, 2225, -40, -300, 0, 1);
    one("2225 Hz 8 Hz high (a carrier shift on the line)", S_TONE, 2233, -20, -300, 0, 1);
    one("2225 Hz 8 Hz low", S_TONE, 2217, -20, -300, 0, 1);
    one("2225 Hz -30 dBm0 in noise 12 dB below", S_TONE, 2225, -30, -42, 0, 1);
    one("2225 Hz starting off the block grid", S_TONE, 2225, -20, -300, 123, 1);
    printf("not the Bell answer tone\n");
    one("2225 Hz -50 dBm0, below the floor", S_TONE, 2225, -50, -300, 0, 0);
    one("2250 Hz -10 dBm0", S_TONE, 2250, -10, -300, 0, 0);
    one("2250 Hz 7 Hz low", S_TONE, 2243, -10, -300, 0, 0);
    one("2200 Hz", S_TONE, 2200, -10, -300, 0, 0);
    one("2025 Hz (Bell 103 answer space)", S_TONE, 2025, -10, -300, 0, 0);
    one("ANSam -10 dBm0 (V.8)", S_ANSAM, 2100, -10, -300, 0, 0);
    one("ANS -10 dBm0 (V.25)", S_ANS, 2100, -10, -300, 0, 0);
    one("V.22 answerer's unscrambled ones, -10 dBm0", S_USB1, 0, -10, -300, 0, 0);
    one("V.22 answerer's scrambled ones, -10 dBm0", S_SB1, 0, -10, -300, 0, 0);
    one("dial tone 350 + 440 Hz", S_DIAL, 0, -10, -300, 0, 0);
    one("white noise -20 dBm0", S_NOISE, 0, -10, -20, 0, 0);
    {
        /* the tone stops: the detector says so within a block */
        belltone_t b; long n; int on = 0, after = 0;
        belltone_init(&b, 0);
        for (n = 0; n < FS; n++) {
            double x = n < FS / 2 ? rms_of(-20) * sqrt(2.0) * cos(TWO_PI * 2225 * n / FS) : 0;
            int d = belltone_tick(&b, clip(x));
            if (n == FS / 2 - 1) on = d;
            if (n == FS / 2 + 2 * BELLTONE_BLOCK) after = d;
        }
        printf("  0.5 s of tone then silence: on %d, 80 ms later %d\n", on, after);
        CHECK_EQ_I(on, 1); CHECK_EQ_I(after, 0);
    }
    T_REPORT("belltone");
}
