/* The calling modem in V.90 (modem/orig with modem/v90a) on the target:
 * replays what tools/rec_v90c recorded and prints a hash of everything it
 * sent (integer only, so it must equal the host's), when it reached data
 * mode, its rates, and the bits it received.
 *
 *   bench_v90c RECORDING [-w N]
 *
 * -w N also lists the N ticks that took the longest here (host time), for
 * an instruction count of each under qemu (`make cost-v90c`). For counts
 * per second of line run it under qemu on truncated copies of the recording
 * (the header says where data mode begins). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include "orig.h"

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* the recorder's upstream data: 2^23 - 1 (test/v90link.h, bert_get, seed 5) */
typedef struct { uint32_t gen; int64_t got; } src_t;
static int get(void *u)
{
    src_t *s = (src_t *)u;
    int bit = (int)(((s->gen >> 17) ^ (s->gen >> 22)) & 1);
    s->gen = ((s->gen << 1) | (uint32_t)bit) & 0x7fffff;
    return bit;
}
static void put(void *u, int bit) { (void)bit; ((src_t *)u)->got++; }

static orig_t o;

#define NW 8

int main(int argc, char **argv)
{
    uint8_t hdr[12], b[2];
    orig_cfg c;
    src_t s;
    uint32_t hash = 2166136261u, t_rec;
    int64_t k = 0, t_data = -1;
    int nw = argc > 3 && strcmp(argv[2], "-w") == 0 ? atoi(argv[3]) : 0, i;
    double t0, t, wt[NW]; int64_t wk[NW];
    FILE *f;

    if (argc < 2) { fprintf(stderr, "usage: bench_v90c RECORDING [-w N]\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f || fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "V90C0001", 8) != 0) {
        fprintf(stderr, "bench_v90c: not a recording of rec_v90c\n");
        return 1;
    }
    t_rec = (uint32_t)hdr[8] | (uint32_t)hdr[9] << 8 | (uint32_t)hdr[10] << 16 | (uint32_t)hdr[11] << 24;
    if (nw > NW) nw = NW;
    for (i = 0; i < NW; i++) { wt[i] = 0; wk[i] = -1; }
    memset(&s, 0, sizeof s); s.gen = (0x4321u + 5u) | 1u;
    memset(&c, 0, sizeof c);
    c.level = 3000; c.modes = V8_MOD_V34 | V8_MOD_V22 | V8_MOD_V21 | ORIG_MODE_V90;
    c.echo_delay = 0; c.law = 0;
    c.getbit = get; c.putbit = put; c.user = &s;
    orig_init(&o, &c);
    t0 = now();
    while (fread(b, 1, 2, f) == 2) {
        int16_t rx = (int16_t)(uint16_t)(b[0] | (unsigned)b[1] << 8), tx;
        double ta = nw ? now() : 0;
        tx = orig_tick(&o, rx);
        if (nw) {
            double d = now() - ta;
            int j = nw - 1;
            if (d > wt[j]) {
                while (j > 0 && d > wt[j - 1]) { wt[j] = wt[j - 1]; wk[j] = wk[j - 1]; j--; }
                wt[j] = d; wk[j] = k;
            }
        }
        hash = (hash ^ (uint32_t)((uint16_t)tx & 0xff)) * 16777619u;
        hash = (hash ^ (uint32_t)((uint16_t)tx >> 8)) * 16777619u;
        k++;
        if (t_data < 0 && orig_moving(&o)) t_data = k;
    }
    t = now() - t0;
    fclose(f);
    printf("bench_v90c: %" PRId64 " samples (%.2f s of line), data mode at sample %" PRId64 " (recorded %" PRIu32 "), "
           "%d / %d bit/s, %" PRId64 " bits received, hash %08" PRIx32 "\n",
           k, (double)k / 8000.0, t_data, t_rec, o.rx_bitrate, o.bitrate, s.got, hash);
    if (!nw) fprintf(stderr, "bench_v90c: %.3f s of CPU here (%.1f %% of real time)\n", t, 100.0 * t / ((double)k / 8000.0 + 1e-9));
    for (i = 0; i < nw; i++) if (wk[i] >= 0) printf("slow tick %" PRId64 " (%.3f s): %.1f us here\n", wk[i], (double)wk[i] / 8000.0, wt[i] * 1e6);
    return 0;
}
