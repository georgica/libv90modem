/* Record what the V.90 analogue modem's A/D would see in a simulated call
 * (sim/dschan), for rig/bench_v90a: our upstream alone while the echo
 * canceller learns (white noise, the downstream silent), then Sd, Sd-bar,
 * TRN1d (4800 T) and B1d and data from the library's own transmitter
 * (v90tx), with our V.34 upstream going on. Host only.
 *
 *   rec_v90a PPM RATE OUT
 *
 * OUT: a header of five little-endian int32 (magic "V90A", alaw, uinfo,
 * samples of canceller training, the first symbol of B1d counted from
 * TRN1d's first) and the 16-byte Ucode mask of the data constellation, then
 * per A/D sample two int16: what the A/D gave and what we sent. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "v90_tx.h"
#include "v90_pcm.h"
#include "v34mod.h"
#include "dschan.h"
#include "noise.h"

#define UINFO   90
#define ECTRAIN 4000
#define SD_AT   (ECTRAIN + 400)
#define TRN_LEN 4800

typedef struct {
    v90tx_t tx; rng_t rng;
    long n, n_trn, n_data;
    int sel;
} src_t;

static int getbit(void *ctx) { src_t *s = (src_t *)ctx; return rng_uniform(&s->rng) < 0.5; }

static uint8_t next(void *ctx)
{
    src_t *s = (src_t *)ctx;
    long n = s->n++;
    uint8_t o;
    if (n == SD_AT) v90tx_select(&s->tx, V90TX_SD);
    o = v90tx_tick(&s->tx);
    if (v90tx_signal(&s->tx) == V90TX_TRN1D) {
        if (s->n_trn < 0) s->n_trn = n;
        if (!s->sel && (long)v90tx_symbols(&s->tx) >= TRN_LEN - 3) { v90tx_select(&s->tx, V90TX_B1D); s->sel = 1; }
    }
    if (s->n_data < 0 && v90tx_signal(&s->tx) == V90TX_B1D) s->n_data = n;
    return o;
}

/* as test/test_v90arx.c: M points spaced as widely as -12 dBm0 allows */
static void design(int rate, uint8_t mask[16], int *K)
{
    int D = rate * 6 / 8000, M = 1, lo = 1, hi = 32768, best = 0, n, u, last;
    double pk = 8159.0 * 4 / pow(10, 3.17 / 20), pmax = pk * pk / 2 * pow(10, -1.2);
    *K = D - 6;
    while (pow(M, 6) < pow(2, *K)) M++;
    while (lo <= hi) {
        int d = (lo + hi) / 2;
        double ps = 0;
        for (n = 0, u = 0, last = -1; u < 128 && n < M; u++) {
            int32_t l = v90_ucode_linear(0, (unsigned)u);
            if (last < 0 || l - last >= d) { last = l; n++; ps += (double)l * l; }
        }
        if (n == M && ps / M <= pmax) { best = d; lo = d + 1; } else hi = d - 1;
    }
    memset(mask, 0, 16);
    for (n = 0, u = 0, last = -1; u < 128 && n < M; u++) {
        int32_t l = v90_ucode_linear(0, (unsigned)u);
        if (last < 0 || l - last >= best) { last = l; n++; mask[u >> 3] |= (uint8_t)(1 << (u & 7)); }
    }
}

static src_t S;
static dschan_t ch;

int main(int argc, char **argv)
{
    v34mod_t up;
    v90_map_t map;
    dschan_cfg cc;
    uint8_t mask[16];
    int32_t hdr[5];
    rng_t ur;
    FILE *f;
    long k, kend, len = (long)ECTRAIN + 400 + 432 + TRN_LEN + 32000;
    int K, i;

    if (argc < 4) { fprintf(stderr, "usage: rec_v90a PPM RATE OUT\n"); return 2; }
    design(atoi(argv[2]), mask, &K);
    memset(&S, 0, sizeof S);
    S.n_trn = S.n_data = -1;
    rng_seed(&S.rng, 7); rng_seed(&ur, 11);
    v90tx_init(&S.tx, 0, UINFO);
    v90tx_set_getbit(&S.tx, getbit, &S);
    memset(&map, 0, sizeof map);
    map.K = (uint8_t)K;
    for (i = 0; i < 6; i++) v90_map_set_points(&map, i, mask);
    if (v90tx_set_map(&S.tx, V90TX_MAP_DATA, &map) != 0) { fprintf(stderr, "bad map\n"); return 1; }
    v34mod_init(&up, 3000, 3, 5, 0, 2000, 0);
    memset(&cc, 0, sizeof cc);
    cc.loop_km = 2; cc.level_db = -12; cc.noise_dbm0 = -75; cc.ppm = atof(argv[1]);
    cc.phase = 0.3; cc.echo_db = -10; cc.seed = 3;
    dschan_init(&ch, &cc, next, &S);

    f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 1; }
    memset(hdr, 0, sizeof hdr);
    fwrite(hdr, sizeof hdr, 1, f);
    fwrite(mask, 16, 1, f);
    kend = len;
    for (k = 0; k < kend; k++) {
        int16_t u, rx, pr[2];
        if (k < ECTRAIN) {
            double v = 2000 * rng_gauss(&ur);
            u = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : floor(v + 0.5));
        } else {
            if (v34mod_due(&up)) {
                int a = (int)(rng_uniform(&ur) * 4), b = (int)(rng_uniform(&ur) * 4);
                v34mod_put(&up, (2 * a - 3) * V34MOD_REF / 2, (2 * b - 3) * V34MOD_REF / 2);
            }
            u = v34mod_tick(&up);
        }
        rx = dschan_tick(&ch, u);
        pr[0] = rx; pr[1] = u;
        fwrite(pr, sizeof pr, 1, f);
    }
    hdr[0] = 0x41303956;        /* "V90A" */
    hdr[1] = 0; hdr[2] = UINFO; hdr[3] = ECTRAIN;
    hdr[4] = (int32_t)(S.n_data - S.n_trn);
    fseek(f, 0, SEEK_SET);
    fwrite(hdr, sizeof hdr, 1, f);
    fclose(f);
    printf("rec_v90a: %ld samples (%.2f s), %+g ppm, %d bit/s, Sd at %d, TRN1d at %ld, B1d at symbol %d\n",
           kend, kend / 8000.0, cc.ppm, atoi(argv[2]), SD_AT, S.n_trn, hdr[4]);
    return 0;
}
