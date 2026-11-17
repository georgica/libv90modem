/* The V.90 analogue modem's downstream receiver (modem/v90arx) on the
 * target: replays what tools/rec_v90a recorded, with the same echo
 * canceller schedule, and prints a hash of the decisions (integer only, so
 * it must equal the host's), the last error and the clock offset found.
 *
 *   bench_v90a RECORDING
 *
 * For instruction counts run it under qemu on two truncated copies of the
 * recording (see `make cost-v90a`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "v90arx.h"

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static v90arx_t rx;
static v90arx_cons_t cons;

int main(int argc, char **argv)
{
    int32_t hdr[5];
    uint8_t mask[16];
    int16_t pr[2];
    v90arx_cfg_t cfg;
    uint32_t hash = 2166136261u;
    long k = 0, ndec = 0, ndata = 0;
    int i, mu = -2;
    double t0, t;
    FILE *f;

    if (argc < 2) { fprintf(stderr, "usage: bench_v90a RECORDING\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f || fread(hdr, sizeof hdr, 1, f) != 1 || fread(mask, 16, 1, f) != 1 || hdr[0] != 0x41303956) {
        fprintf(stderr, "bench_v90a: not a recording of rec_v90a\n");
        return 1;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.alaw = hdr[1]; cfg.uinfo = hdr[2];
    v90arx_init(&rx, &cfg);
    for (i = 0; i < 6; i++) v90arx_cons_mask(&cons, cfg.alaw, i, mask);
    v90arx_set_cons(&rx, &cons, hdr[4]);
    t0 = now();
    while (fread(pr, sizeof pr, 1, f) == 1) {
        int m = k < hdr[3] / 4 ? 0 : k < hdr[3] / 2 ? 2 : k < hdr[3] ? 4 : -1, n;
        if (m != mu) { v90arx_ec(&rx, m); mu = m; }
        n = v90arx_tick(&rx, pr[0], pr[1]);
        for (i = 0; i < n; i++) {
            const v90arx_dec_t *d = &rx.out[i];
            hash = (hash ^ (uint32_t)(d->ucode | d->sign << 7)) * 16777619u;
            ndec++;
            if (d->m >= hdr[4]) ndata++;
        }
        k++;
    }
    t = now() - t0;
    fclose(f);
    printf("bench_v90a: %ld samples (%.2f s of line), %ld decisions (%ld of data), hash %08x, "
           "mse %.1f dB re UINFO^2, %+.2f ppm; %.3f s of CPU here (%.1f %% of real time)\n",
           k, k / 8000.0, ndec, ndata, (unsigned)hash,
           10 * log10((double)v90arx_mse(&rx) / 256.0 / ((double)rx.L * rx.L) + 1e-30),
           v90arx_ppm_q8(&rx) / 256.0, t, 100.0 * t / (k / 8000.0 + 1e-9));
    return 0;
}
