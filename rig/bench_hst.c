/* HST's back channel on the target (modem/hst_back.c): for the instruction
 * counts of `make cost-hst-back`.
 *
 *   bench_hst -g FILE [300]
 *                       write the recording: "HSTB0001", then 16-bit
 *                       little-endian samples at 8000 Hz: the caller's back
 *                       channel at 450 bit/s, or 300 (our transmitter, its preamble
 *                       then pseudo-random data) at -26 dBm0, with noise 20
 *                       dB below it over 0-4 kHz, for 4 s. Integer only, so
 *                       the host and the target write the same file.
 *   bench_hst -b FILE   the two back receivers (450 and 300) on one
 *                       band-pass, as the pump runs them while it holds the
 *                       forward channel (hst_brx_tick on the 450 receiver,
 *                       hst_brx_band on the 300 one)
 *   bench_hst -d FILE   the detector of ANS_HST_WAIT (hst_det_tick): the two
 *                       receivers, its band and its high band
 *   bench_hst -n FILE   the loop alone (each sample to a volatile): what the
 *                       others' counts include besides the library
 *
 * Each prints one line: the samples, the bits each receiver gave and their
 * hash (FNV-1a, one bit at a time), and what it detected; the host's and
 * the target's must agree. bench_hst_mark() is called at 2 s and at 3 s of
 * the recording: in qemu's instruction trace that function's address marks
 * off the third second, the receivers locked (or the detector run on). */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "hst.h"

#define SECS 4
static uint32_t gen = 0x2468ACEu;
static uint32_t lcg(void) { gen = gen * 1664525u + 1013904223u; return gen; }
static int getbit(void *u) { (void)u; return (int)(lcg() >> 31); }

/* not inlined (called through a volatile pointer): its address marks the trace */
volatile int bench_hst_marked;
void bench_hst_mark(int i);
void bench_hst_mark(int i) { bench_hst_marked = i; }
static void (*volatile mark)(int) = bench_hst_mark;

static hst_brx_t r450, r300;
static hst_det_t det;

static int record(const char *path, int bps)
{
    hst_btx_t tx;
    uint32_t ns = 0x13579BDu;
    FILE *f = fopen(path, "wb");
    long n;
    if (!f) { fprintf(stderr, "bench_hst: cannot write %s\n", path); return 1; }
    fwrite("HSTB0001", 1, 8, f);
    hst_btx_init(&tx, bps, 790, getbit, NULL);               /* -26 dBm0: 15770 x 10^(-26/20) */
    hst_btx_preamble(&tx);
    for (n = 0; n < (long)SECS * 8000; n++) {
        /* noise: the sum of four uniform values, RMS 79 (-46 dBm0) */
        int32_t v = hst_btx_tick(&tx), w = 0, k;
        uint8_t b[2];
        for (k = 0; k < 4; k++) { ns = ns * 1103515245u + 12345u; w += (int32_t)((ns >> 16) & 0xFFFF) - 32768; }
        v += w / 479;                                         /* 2 x 18919 RMS / 479 = 79 */
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        b[0] = (uint8_t)((uint16_t)v & 0xFF); b[1] = (uint8_t)((uint16_t)v >> 8);
        fwrite(b, 1, 2, f);
    }
    fclose(f);
    printf("bench_hst: %ld samples written\n", (long)SECS * 8000);
    return 0;
}

static uint32_t fnv_bits(uint32_t h, const uint8_t *bits, int n)
{
    int i;
    for (i = 0; i < n; i++) h = (h ^ (uint32_t)(bits[i] & 1)) * 16777619u;
    return h;
}

static int16_t rec[SECS * 8000];
volatile int16_t bench_hst_sink;

int main(int argc, char **argv)
{
    uint8_t hdr[8], b[2];
    uint32_t h450 = 2166136261u, h300 = 2166136261u;
    int32_t k, n = 0, n450 = 0, n300 = 0, fired_at = -1;
    int mode, fired = 0;
    FILE *f;
    if (argc < 3 || argv[1][0] != '-' || !strchr("gbdn", argv[1][1])) {
        fprintf(stderr, "usage: bench_hst -g|-b|-d|-n FILE\n");
        return 2;
    }
    mode = argv[1][1];
    if (mode == 'g') return record(argv[2], argc > 3 && !strcmp(argv[3], "300") ? 300 : 450);
    f = fopen(argv[2], "rb");
    if (!f || fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "HSTB0001", 8) != 0) {
        fprintf(stderr, "bench_hst: not a recording of bench_hst -g\n");
        return 1;
    }
    /* the whole recording first: the counted second is the receivers' alone */
    while (n < SECS * 8000 && fread(b, 1, 2, f) == 2) rec[n++] = (int16_t)(uint16_t)(b[0] | (unsigned)b[1] << 8);
    fclose(f);
    hst_brx_init(&r450, 450);
    hst_brx_init(&r300, 300);
    hst_det_init(&det);
    for (k = 0; k < n; k++) {
        int16_t x = rec[k];
        int nb;
        if (k == 2 * 8000) mark(1);
        if (k == 3 * 8000) mark(2);
        if (mode == 'b') {
            nb = hst_brx_tick(&r450, x);
            h450 = fnv_bits(h450, r450.bits, nb); n450 += nb;
            if (r450.dec == 0) {
                nb = hst_brx_band(&r300, r450.y);
                h300 = fnv_bits(h300, r300.bits, nb); n300 += nb;
            }
        } else if (mode == 'd') {
            int r = hst_det_tick(&det, x);
            if (r && !fired) { fired = r; fired_at = k; }
        } else
            bench_hst_sink = x;
    }
    if (mode == 'b')
        printf("bench_hst -b: %" PRId32 " samples; 450: %" PRId32 " bits, hash %08" PRIx32 ", locked %d; 300: %" PRId32
               " bits, hash %08" PRIx32 ", locked %d\n", n, n450, h450, hst_brx_locked(&r450), n300, h300, hst_brx_locked(&r300));
    else if (mode == 'n')
        printf("bench_hst -n: %" PRId32 " samples\n", n);
    else
        printf("bench_hst -d: %" PRId32 " samples; detected %s at sample %" PRId32 "\n", n,
               fired == HST_DET_450 ? "450" : fired == HST_DET_300 ? "300" : "nothing", fired_at);
    return 0;
}
