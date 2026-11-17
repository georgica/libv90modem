/* modem/anstone: the calling modem's answer tone detector, ANSam (V.8 7.2)
 * against ANS (V.25) and against what is not an answer tone. */
#include <stdio.h>
#include <math.h>
#include "test.h"
#include "../modem/anstone.h"
#include "../modem/ansam.h"
#include "noise.h"

#define FS 8000
#define TWO_PI 6.283185307179586
#define PI 3.141592653589793

/* RMS on the 16-bit scale: 0 dBm0 is 3.17 dB below a full-scale sine of 32124 */
static double rms(double dbm0) { return 32124.0 / sqrt(2.0) * pow(10.0, (dbm0 - 3.17) / 20.0); }

typedef struct {
    double dbm0;        /* average level */
    double depth;       /* AM depth: 0.2 for ANSam, 0 for ANS */
    double freq;        /* carrier, Hz */
    int    reversals;   /* phase reversals every 450 ms */
    double am_phase;    /* starting phase of the 15 Hz, radians */
    double noise_dbm0;  /* white noise added (<= -200: none) */
    /* a transient of the network (V.8 7.2 NOTE 1): at t_step seconds the
     * amplitude is multiplied by step_gain and the phase jumps */
    double t_step, step_gain, step_phase;
    double lead;        /* seconds of silence before the tone */
} tone_cfg;

/* runs the detector over `secs` seconds of the tone; returns the result and
 * the time (s, from the start of the tone) it was decided at */
static anst_result run(const tone_cfg *c, double secs, double *at)
{
    static anst_t d;
    rng_t r;
    long n, total = (long)(secs * FS), lead = (long)(c->lead * FS);
    double a = rms(c->dbm0) * sqrt(2.0), sig = c->noise_dbm0 > -200 ? rms(c->noise_dbm0) : 0;
    anst_result res = ANST_NONE;
    rng_seed(&r, 12345);
    anst_init(&d, 0);
    *at = -1;
    for (n = 0; n < lead + total; n++) {
        double x = 0, t = (double)(n - lead) / FS;
        if (n >= lead) {
            double ph = TWO_PI * c->freq * t, g = 1.0;
            if (c->reversals && ((long)(t / 0.45)) % 2) ph += PI;
            if (c->t_step > 0 && t >= c->t_step) { g = c->step_gain; ph += c->step_phase; }
            x = g * a * (1.0 + c->depth * sin(TWO_PI * 15.0 * t + c->am_phase)) * cos(ph);
        }
        x += sig * rng_gauss(&r);
        if (x > 32767) x = 32767;
        if (x < -32768) x = -32768;
        res = anst_tick(&d, (int16_t)lrint(x));
        if (res != ANST_NONE && *at < 0) *at = (double)(n - lead) / FS;
    }
    return res;
}

static tone_cfg base(double dbm0, double depth)
{
    tone_cfg c = { 0 };
    c.dbm0 = dbm0; c.depth = depth; c.freq = 2100; c.reversals = 1; c.noise_dbm0 = -300;
    return c;
}

static void expect(const char *what, const tone_cfg *c, anst_result want, double max_at)
{
    double at;
    anst_result got = run(c, 2.0, &at);
    printf("  %-58s %s at %.3f s\n", what, got == ANST_ANSAM ? "ANSam" : got == ANST_ANS ? "ANS" : "nothing", at);
    CHECK_EQ_I(got, want);
    if (want != ANST_NONE) CHECK(at >= 0 && at <= max_at);
}

int main(void)
{
    tone_cfg c;
    int i, wrong;

    printf("ANSam, the library's own transmitter (modem/ansam)\n");
    {
        static anst_t d; ansam_t g; int16_t x; long n; int lev;
        static const int amps[] = { 5602, 223 };          /* -12 and -40 dBm0 */
        for (lev = 0; lev < 2; lev++) {
            int rev;
            for (rev = 0; rev < 2; rev++) {
                anst_result res = ANST_NONE;
                anst_init(&d, 0); ansam_init(&g, FS, (int16_t)amps[lev], rev);
                for (n = 0; n < 2 * FS; n++) { ansam_run(&g, &x, 1); res = anst_tick(&d, x); }
                printf("  peak %4d, reversals %d: %s at %.3f s\n", amps[lev], rev,
                       res == ANST_ANSAM ? "ANSam" : res == ANST_ANS ? "ANS" : "nothing", d.at / (double)FS);
                CHECK_EQ_I(res, ANST_ANSAM);
                CHECK(d.at <= (long)(0.8 * FS));
            }
        }
    }

    printf("ANSam and ANS, written here from V.8 7.2 and V.25\n");
    c = base(-12, 0.2); expect("ANSam -12 dBm0, reversals", &c, ANST_ANSAM, 0.8);
    c = base(-12, 0.2); c.reversals = 0; expect("ANSam -12 dBm0, no reversals", &c, ANST_ANSAM, 0.8);
    c = base(-42, 0.2); expect("ANSam -42 dBm0", &c, ANST_ANSAM, 0.8);
    c = base(0, 0.2); expect("ANSam 0 dBm0", &c, ANST_ANSAM, 0.8);
    c = base(-12, 0.2); c.freq = 2110; expect("ANSam 10 Hz high (a carrier shift on the line)", &c, ANST_ANSAM, 0.8);
    c = base(-12, 0.2); c.freq = 2090; expect("ANSam 10 Hz low", &c, ANST_ANSAM, 0.8);
    c = base(-30, 0.2); c.noise_dbm0 = -42; expect("ANSam -30 dBm0 in noise 12 dB below", &c, ANST_ANSAM, 0.8);
    c = base(-12, 0.19); expect("ANSam at the narrowest envelope V.8 allows (0.81..1.19)", &c, ANST_ANSAM, 0.8);
    c = base(-12, 0.2); c.lead = 0.137; c.am_phase = 1.0; expect("ANSam starting off the block grid", &c, ANST_ANSAM, 0.8);
    c = base(-12, 0); expect("ANS -12 dBm0, reversals (V.25)", &c, ANST_ANS, 0.8);
    c = base(-12, 0); c.reversals = 0; expect("ANS -12 dBm0, no reversals", &c, ANST_ANS, 0.8);
    c = base(-40, 0); expect("ANS -40 dBm0", &c, ANST_ANS, 0.8);
    c = base(-30, 0); c.noise_dbm0 = -42; expect("ANS -30 dBm0 in noise 12 dB below", &c, ANST_ANS, 0.8);
    c = base(-12, 0); c.t_step = 0.31; c.step_gain = 0.7; c.step_phase = 1.3;
    expect("ANS with a level step and a phase jump (V.8 7.2 NOTE 1)", &c, ANST_ANS, 1.2);
    c = base(-12, 0.2); c.t_step = 0.31; c.step_gain = 0.7; c.step_phase = 1.3;
    expect("ANSam with the same transient", &c, ANST_ANSAM, 1.2);

    printf("ANS is never taken for ANSam, wherever its reversals fall\n");
    for (i = 0, wrong = 0; i < 24; i++) {
        double at; anst_result r;
        c = base(-20, 0); c.lead = i * 0.0193;
        r = run(&c, 2.0, &at);
        wrong += r != ANST_ANS;
    }
    printf("  24 starting offsets: %d not ANS\n", wrong);
    CHECK_EQ_I(wrong, 0);

    printf("not an answer tone\n");
    c = base(-50, 0.2); expect("ANSam at -50 dBm0, below the floor", &c, ANST_NONE, 0);
    c = base(-12, 0.2); c.freq = 1800; expect("1800 Hz with the same modulation", &c, ANST_NONE, 0);
    c = base(-12, 0.2); c.freq = 2250; expect("2250 Hz (a V.22bis answerer's unscrambled ones)", &c, ANST_NONE, 0);
    c = base(-300, 0); c.noise_dbm0 = -20; expect("white noise at -20 dBm0", &c, ANST_NONE, 0);
    {
        /* dial tone (350 + 440 Hz) and a voice-band mixture with some 2100 Hz in it */
        static anst_t d; long n; anst_result r1 = ANST_NONE, r2 = ANST_NONE; static anst_t d2;
        double a = rms(-13) * sqrt(2.0);
        anst_init(&d, 0); anst_init(&d2, 0);
        for (n = 0; n < 2 * FS; n++) {
            double t = (double)n / FS;
            double dial = a * (cos(TWO_PI * 350 * t) + cos(TWO_PI * 440 * t));
            double mix = a * (cos(TWO_PI * 500 * t) + 0.9 * cos(TWO_PI * 1300 * t) + 0.6 * cos(TWO_PI * 2100 * t));
            r1 = anst_tick(&d, (int16_t)lrint(dial));
            r2 = anst_tick(&d2, (int16_t)lrint(mix));
        }
        printf("  dial tone 350 + 440 Hz: %s; 500 + 1300 + some 2100 Hz: %s\n",
               r1 == ANST_NONE ? "nothing" : "a tone", r2 == ANST_NONE ? "nothing" : "a tone");
        CHECK_EQ_I(r1, ANST_NONE);
        CHECK_EQ_I(r2, ANST_NONE);
    }
    T_REPORT("anstone");
}
