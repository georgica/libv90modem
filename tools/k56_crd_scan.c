/* k56-crd-scan: run K56flex's CRd detector (modem/v8bis, v8bis_crd_tick)
 * over what the answering modem heard in recorded calls: the callers' side,
 * with our own echo in it. A detection is a false CRd (none of these callers
 * spoke V.8bis), the one thing that could make K56flex take a caller that is
 * not a K56flex client into V.8bis's CL (docs/superpowers/specs/
 * 2026-11-21-k56flex-answering-design.md, "The CRd detector must not fire").
 *
 *   k56-crd-scan [-a] RECORDING...      (make crd-scan RECS=<dir>)
 *
 * RECORDING: as v90m-replay reads it (80 codewords heard, 80 sent, per
 * 10 ms; or "V90DLIN1" and 16-bit linear heard). -a: A-law. Prints each
 * detection (pair held, its 1900 Hz end) and a total; exits 1 when there is
 * any. Host tool, not installed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../modem/v8bis.h"
#include "../v90/v90_pcm.h"

int main(int argc, char **argv)
{
    static uint8_t file[(24 << 20) + 8];
    int arg = 1, alaw = 0, files = 0;
    long tones = 0, ends = 0;
    if (arg < argc && !strcmp(argv[arg], "-a")) { alaw = 1; arg++; }
    if (arg >= argc) { fprintf(stderr, "usage: k56-crd-scan [-a] RECORDING...\n"); return 2; }
    for (; arg < argc; arg++) {
        FILE *f = fopen(argv[arg], "rb");
        const uint8_t *d = file;
        long len, n, i, bs = 160, t = 0, e = 0;
        int lin = 0;
        v8bis_crd_t c;
        if (!f) { fprintf(stderr, "k56-crd-scan: cannot open %s\n", argv[arg]); return 2; }
        len = (long)fread(file, 1, sizeof file, f);
        fclose(f);
        if (len >= 8 && !memcmp(file, "V90DLIN1", 8)) { lin = 1; bs = 240; d = file + 8; len -= 8; }
        n = len / bs * 80;
        v8bis_crd_init(&c);
        for (i = 0; i < n; i++) {
            const uint8_t *b = d + (i / 80) * bs;
            int16_t x = lin ? (int16_t)(uint16_t)(b[2 * (i % 80)] | (b[2 * (i % 80) + 1] << 8))
                            : (int16_t)v90_octet_linear(alaw, b[i % 80]);
            int r = v8bis_crd_tick(&c, x);
            if (r == V8B_CRD_TONE) { t++; printf("%s: %.3f s: the CRd pair held\n", argv[arg], i / 8000.0); }
            if (r == V8B_CRD_END) { e++; printf("%s: %.3f s: its 1900 Hz ended\n", argv[arg], i / 8000.0); }
        }
        printf("%s: %.1f s, %ld CRd detections\n", argv[arg], n / 8000.0, t);
        tones += t; ends += e; files++;
    }
    printf("k56-crd-scan: %d recordings, %ld CRd detections (%ld with the 1900 Hz end)\n", files, tones, ends);
    return tones ? 1 : 0;
}
