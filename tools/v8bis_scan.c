/* v8bis-scan: V.8bis messages (V.8bis 7.2: V.21, flags, FCS; modem/v8bismsg)
 * in what the answering modem heard in recorded calls, for Quick Connect's
 * Task 8 evidence (the 0.7.4 plan's Global Constraints, replay; ruling
 * F13): a recording whose heard side holds a valid MS without the K56flex
 * block that asks for ACK(1) may replay otherwise once our V.8bis
 * acknowledges such an MS.
 *
 *   v8bis-scan [-a] FILE...        (make v8bis-scan RECS=<dir>: every recording)
 *
 * FILE: as v90m-replay reads it (per 10 ms, 80 codewords heard then 80
 * sent, mu-law, or A-law with -a; or "V90DLIN1", then 80 16-bit linear
 * samples heard and 80 codewords sent).
 *
 * The heard side goes through both V.21 channels' demodulators (as
 * modem/v8bis listens: squelch, a bit clock from the level's edges) into the
 * frame receiver, over the whole recording. Each frame with a good FCS is
 * printed with its time, channel, type and octet count; an MS also with
 * "K56flex block" (v8bis_ms_is_k56) or "for V.8" (v8bis_ms_v8: 1, or 2 when
 * it asks for ACK(1)). Nothing else of the call is printed. Exits 0, 2 on a
 * file it cannot read. Host tool, not installed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../modem/v8bis.h"
#include "../v90/v90_pcm.h"

#define FS 8000
#define MAXS (2L << 20)                 /* samples at most: 262 s */
#define SQUELCH_AMP 110                 /* as modem/v8bis.c */

static int16_t heard[MAXS];
static long nheard;
static uint8_t file[(24 << 20) + 64];

static void load_rec(long len, int alaw)
{
    const uint8_t *d = file;
    long bs = 160, nb, i, j;
    int lin = 0;
    if (len >= 8 && !memcmp(file, "V90DLIN1", 8)) { lin = 1; bs = 240; d = file + 8; len -= 8; }
    nb = len / bs;
    if (nb * 80 > MAXS) nb = MAXS / 80;
    for (i = 0; i < nb; i++) {
        const uint8_t *b = d + i * bs;
        for (j = 0; j < 80; j++)
            heard[i * 80 + j] = lin ? (int16_t)(uint16_t)(b[2 * j] | b[2 * j + 1] << 8)
                                    : (int16_t)v90_octet_linear(alaw, b[j]);
    }
    nheard = nb * 80;
}

/* one channel; returns the MSs without the K56flex block that v8bis_ms_v8
 * takes and that ask for ACK(1) */
static int scan(const char *name, int high, int *frames)
{
    v21demod_t dem;
    v8bis_rx_t rx;
    int64_t squelch;
    uint8_t o[V8B_MAXOCT];
    int lv = 1, hits = 0;
    int32_t clk = 0;
    long i;
    v21demod_init(&dem, high ? V21H_MARK : V21L_MARK, high ? V21H_SPACE : V21L_SPACE, FS, 300);
    squelch = v21demod_energy_of(&dem, SQUELCH_AMP);
    v8bis_rx_init(&rx);
    for (i = 0; i < nheard; i++) {
        int level = v21demod_tick(&dem, heard[i]), n;
        if (dem.energy < squelch) level = 1;
        if (level != lv) { lv = level; clk = 4000; }
        clk += 300;
        if (clk < 8000) continue;
        clk -= 8000;
        n = v8bis_rx_bit(&rx, level, o);
        if (n <= 0) continue;
        (*frames)++;
        printf("%s: %.3f s, V.21(%c): type %d, %d octets", name, i / (double)FS, high ? 'H' : 'L', o[0] & 15, n);
        if ((o[0] & 15) == V8B_MSG_MS) {
            int v8 = v8bis_ms_v8(o, n);
            if (v8bis_ms_is_k56(o, n)) printf(", MS with the K56flex block");
            else if (v8) printf(", MS for V.8%s", v8 == 2 ? ", asks for ACK(1)" : "");
            else printf(", MS (other)");
            hits += v8 == 2;
        }
        printf("\n");
    }
    return hits;
}

int main(int argc, char **argv)
{
    int arg = 1, alaw = 0, hits = 0, n = 0, frames = 0;
    if (arg < argc && !strcmp(argv[arg], "-a")) { alaw = 1; arg++; }
    if (arg >= argc) { fprintf(stderr, "usage: v8bis-scan [-a] FILE...\n"); return 2; }
    for (; arg < argc; arg++) {
        FILE *f = fopen(argv[arg], "rb");
        long len;
        int h;
        if (!f) { fprintf(stderr, "v8bis-scan: cannot open %s\n", argv[arg]); return 2; }
        len = (long)fread(file, 1, sizeof file, f);
        fclose(f);
        load_rec(len, alaw);
        h = scan(argv[arg], 1, &frames);
        h += scan(argv[arg], 0, &frames);
        hits += h > 0;
        n++;
    }
    printf("v8bis-scan: %d recordings, %d V.8bis frames, %d with an MS for V.8 asking for ACK(1)\n", n, frames, hits);
    return 0;
}
