/* k56enc_dump DIR: our K56flex data encoder (modem/k56enc) on the oracle's
 * pipeline (docs/history/k56-facts.md "Oracle": xorshift32 from 1, GPA from
 * state 0, 600 frames) at every rate code (1..13), pad (0..2) and robbed
 * mask (0..63), written as DIR/r<R>p<PAD>m<MASK>.oct (the mu-law octets)
 * and .bits (the data bits before the scrambler, one per octet), MASK in
 * decimal. For SM56's decoder harness, outside the repository
 * (~/modem-research/k56-oracle/sm56dec/check.sh DIR). Host only. */
#include <stdio.h>
#include "k56enc.h"
#include "v90_scr.h"

#define NFRAMES 600

static int xs_bit(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return (int)(*x & 1u);
}

static int dump(const char *dir, int r, int pad, int mask)
{
    char name[512];
    FILE *fo, *fb;
    k56enc_t e;
    v90_scr_t s;
    uint32_t x = 1;
    uint8_t data[64], bits[64], out[K56ENC_N];
    int n, i, k, err;

    snprintf(name, sizeof name, "%s/r%dp%dm%d.oct", dir, r, pad, mask);
    fo = fopen(name, "wb");
    snprintf(name, sizeof name, "%s/r%dp%dm%d.bits", dir, r, pad, mask);
    fb = fopen(name, "wb");
    if (!fo || !fb) {
        perror(name);
        if (fo) fclose(fo);
        if (fb) fclose(fb);
        return -1;
    }
    k56enc_init(&e, r, pad, (uint8_t)mask);
    v90_scr_init(&s);
    for (n = 0; n < NFRAMES; n++) {
        k = k56enc_bits(&e);
        for (i = 0; i < k; i++) {
            data[i] = (uint8_t)xs_bit(&x);
            bits[i] = (uint8_t)v90_scramble_gpa(&s, data[i]);
        }
        k56enc_frame(&e, bits, out);
        fwrite(data, 1, (size_t)k, fb);
        fwrite(out, 1, K56ENC_N, fo);
    }
    err = ferror(fo) || ferror(fb);
    err |= fclose(fo) != 0;
    err |= fclose(fb) != 0;
    return err ? -1 : 0;
}

int main(int argc, char **argv)
{
    int r, pad, mask, files = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: k56enc_dump DIR\n");
        return 2;
    }
    for (r = 1; r <= 13; r++)
        for (pad = 0; pad < 3; pad++)
            for (mask = 0; mask < 64; mask++) {
                if (dump(argv[1], r, pad, mask))
                    return 1;
                files++;
            }
    printf("k56enc_dump: %d rate x pad x mask, %d frames each, in %s\n", files, NFRAMES, argv[1]);
    return 0;
}
