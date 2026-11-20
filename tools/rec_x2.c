/* Record a simulated x2 call for the bench (rig/bench_v90 -x): the x2
 * client of sim/x2call calls the answering modem with x2 offered over the
 * link of test/test_answer_x2.c (our octets to it through the network's
 * pad, its samples back through a mu-law codec with our own echo and
 * noise), and the codewords the answering modem receives go to a file.
 *
 *   rec_x2 [SECONDS [PAD [FILE]]]      (defaults 40, 0, /dev/null)
 *
 * Prints the transmit hash the bench compares with, and what the call did. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "answer.h"
#include "x2call.h"
#include "g711.h"
#include "noise.h"

/* the bench's data: an LCG per direction, the same seeds as rec_v90 */
typedef struct { uint32_t s; long n, bad; } prbs;
static int nx(prbs *p) { p->s = p->s * 1103515245u + 12345u; return (int)((p->s >> 16) & 1); }
static int get(void *u) { prbs *p = ((void **)u)[0]; p->n++; return nx(p); }
static void put(void *u, int b) { prbs *p = ((void **)u)[1]; p->n++; if (nx(p) != (b & 1)) p->bad++; }

#define XD 4096
int main(int argc, char **argv)
{
    static answer_t a; static x2call_t x;
    static prbs dsrc, dsink, asrc, asink; static void *aio[2], *dio[2];
    static uint8_t down[XD]; static int16_t up[XD]; static double dl[XD];
    int secs = argc > 1 ? atoi(argv[1]) : 40, pad = argc > 2 ? atoi(argv[2]) : 0;
    FILE *dump = fopen(argc > 3 ? argv[3] : "/dev/null", "wb");
    const int d_ac = 37, d_ca = 41, e = 300;
    uint32_t hash = 2166136261u;
    answer_cfg ac; x2call_cfg xc; rng_t rng;
    uint8_t arx = 0xff;
    long n, data_at = -1;
    if (!dump) { perror("rec_x2"); return 2; }
    memset(down, 0x7f, sizeof down);
    rng_seed(&rng, 4242);
    dsrc.s = asink.s = 5; asrc.s = dsink.s = 9;
    dio[0] = &dsrc; dio[1] = &dsink; aio[0] = &asrc; aio[1] = &asink;
    memset(&ac, 0, sizeof ac);
    ac.level = 3000; ac.modes = ANS_MODE_V90 | V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21 | ANS_MODE_X2;
    ac.getbit = get; ac.putbit = put; ac.user = dio;
    answer_init(&a, &ac);
    answer_v90_line(&a, -1, 0, 0);
    memset(&xc, 0, sizeof xc);
    xc.level = 3000; xc.pad_mode = pad == 6 ? 2 : pad == 3 ? 1 : 0; xc.max_index = 1; xc.up_mask = 0x03FE;
    xc.getbit = get; xc.putbit = put; xc.user = aio;
    x2call_init(&x, &xc);
    for (n = 0; n < 8000L * secs; n++) {
        int i = (int)(n & (XD - 1));
        double v;
        uint8_t atx;
        fputc(arx, dump);
        atx = answer_tick_pcm(&a, arx);
        hash = (hash ^ atx) * 16777619u;
        down[i] = atx;
        dl[i] = (double)ulaw_to_linear(atx);
        up[i] = x2call_tick(&x, down[(n - d_ac) & (XD - 1)]);
        v = 0.5 * up[(n - d_ca) & (XD - 1)]
            + 0.12 * dl[(n - e - 2) & (XD - 1)] + 0.08 * dl[(n - e - 4) & (XD - 1)] - 0.05 * dl[(n - e - 9) & (XD - 1)]
            + 2.0 * rng_gauss(&rng);
        arx = linear_to_ulaw((int32_t)lround(v));
        if (data_at < 0 && a.connected) data_at = n;
        if (a.phase == ANS_FAILED) { printf("FAILED at %.2f s, x2 error %d\n", n / 8000.0, a.u.x2.error); break; }
    }
    fclose(dump);
    printf("transmit hash %08x, connected %d (data mode from %.2f s), bits to line %ld, bits from line %ld (%ld bad)\n",
           (unsigned)hash, a.connected, data_at / 8000.0, dsrc.n, dsink.n, dsink.bad);
    printf("x2 %d down / %d up; down %ld bits %ld bad\n", a.bitrate, a.rx_bitrate, asink.n, asink.bad);
    return a.connected ? 0 : 1;
}
