/* qprof: ARM instruction counts per tick and per function, from qemu's log.
 *
 *   qemu-arm -d in_asm,exec,nochain -D /dev/stdout PROG ... | qprof MARK [FROM TO] [-top N] [-tb N]
 *
 * qemu logs each translated block once (in_asm: its address and its code,
 * four octets per ARM instruction) and each execution of a block (exec,
 * with nochain: every one). A block's instructions are counted each time it
 * runs and charged to the function its first instruction is in (the name
 * qemu prints on the Trace line). MARK is the address (hex) of a function
 * the program calls once before each tick: the instructions between one
 * call and the next are that tick's (the program's own loop included).
 *
 * Prints the total, the N heaviest ticks, and the instructions per function
 * over ticks FROM to TO - 1 (all ticks without them). A block cut short by
 * an exception is counted whole; ARM code only (no Thumb). Not installed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#define HB 22                      /* blocks: 4 M slots */
#define NF 4096                    /* functions */
#define NT (1 << 21)               /* ticks */

static uint64_t bpc[1u << HB];
static uint32_t bn[1u << HB];
static int16_t bf[1u << HB];       /* function, -1 not yet named */
static char fname[NF][64];
static uint64_t fcnt[NF];
static uint64_t bcnt[1u << HB];    /* instructions of each block in the window */
static int nf;
static uint32_t *tcnt;

static uint32_t slot(uint64_t pc)
{
    uint32_t h = (uint32_t)((pc * 0x9e3779b97f4a7c15u) >> (64 - HB));
    while (bpc[h] && bpc[h] != pc + 1) h = (h + 1) & ((1u << HB) - 1);
    return h;
}

static int fid(const char *s)
{
    int i;
    for (i = 0; i < nf; i++) if (strcmp(fname[i], s) == 0) return i;
    if (nf == NF) return NF - 1;
    snprintf(fname[nf], sizeof fname[nf], "%s", s);
    return nf++;
}

static int cmpf(const void *a, const void *b)
{
    uint64_t x = fcnt[*(const int *)a], y = fcnt[*(const int *)b];
    return x < y ? 1 : x > y ? -1 : 0;
}

int main(int argc, char **argv)
{
    static char line[4096];
    uint64_t mark, pc, cur = 0, total = 0, win = 0;
    int64_t tick = -1, from = -1, to = -1, ntick;
    int top = 10, ntb = 0, i, ai = 2, inb = 0;
    uint32_t h;

    if (argc < 2) { fprintf(stderr, "usage: qprof MARK [FROM TO] [-top N] < qemu-log\n"); return 2; }
    mark = strtoull(argv[1], NULL, 16);
    if (argc > 3 && argv[2][0] != '-') { from = atoll(argv[2]); to = atoll(argv[3]); ai = 4; }
    for (; ai < argc - 1; ai++) if (strcmp(argv[ai], "-top") == 0) top = atoi(argv[++ai]);
        else if (strcmp(argv[ai], "-tb") == 0) ntb = atoi(argv[++ai]);
    tcnt = calloc(NT, sizeof *tcnt);
    if (!tcnt) return 1;
    while (fgets(line, sizeof line, stdin)) {
        if (line[0] == 'T' && strncmp(line, "Trace ", 6) == 0) {
            char *p = strchr(line, '['), *q;
            uint32_t n;
            if (!p || !(p = strchr(p, '/'))) continue;
            pc = strtoull(p + 1, &q, 16);
            h = slot(pc);
            n = bpc[h] ? bn[h] : 0;
            if (pc == mark) tick++;
            if (bpc[h] && bf[h] < 0) {
                char *s = strchr(q, ']');
                size_t l;
                s = s ? s + 1 : q;
                while (*s == ' ') s++;
                l = strcspn(s, "\r\n");
                s[l] = 0;
                bf[h] = (int16_t)fid(l ? s : "?");
            }
            total += n;
            if (tick >= 0 && tick < NT) tcnt[tick] += n;
            if (from < 0 || (tick >= from && tick < to)) {
                win += n;
                if (bpc[h]) { fcnt[bf[h]] += n; bcnt[h] += n; }
            }
        } else if (strncmp(line, "IN:", 3) == 0) {
            inb = 1;
        } else if (inb == 1 && line[0] == '0' && line[1] == 'x') {
            cur = strtoull(line, NULL, 16);
            h = slot(cur);
            bpc[h] = cur + 1; bn[h] = 0; bf[h] = -1;
            inb = 2;
        } else if (inb == 2 && strncmp(line, "OBJD-T: ", 8) == 0) {
            bn[slot(cur)] += (uint32_t)(strcspn(line + 8, "\r\n") / 8);
        } else if (inb == 2 && (line[0] == '\n' || line[0] == '-')) {
            inb = 0;
        }
    }
    ntick = tick + 1;
    printf("total %" PRIu64 " instructions, %" PRId64 " ticks\n", total, ntick);
    {
        static int64_t tk[64];
        int nt = top < 64 ? top : 64, j;
        for (j = 0; j < nt; j++) tk[j] = -1;
        for (tick = 0; tick < ntick && tick < NT; tick++) {
            j = nt - 1;
            if (tk[j] >= 0 && tcnt[tick] <= tcnt[tk[j]]) continue;
            while (j > 0 && (tk[j - 1] < 0 || tcnt[tick] > tcnt[tk[j - 1]])) { tk[j] = tk[j - 1]; j--; }
            tk[j] = tick;
        }
        for (j = 0; j < nt; j++) if (tk[j] >= 0)
            printf("tick %" PRId64 " (%.3f s): %" PRIu32 "\n", tk[j], (double)tk[j] / 8000.0, tcnt[tk[j]]);
    }
    if (getenv("QPROF_TICKS")) {
        FILE *f = fopen(getenv("QPROF_TICKS"), "wb");
        if (f) { fwrite(tcnt, sizeof *tcnt, (size_t)(ntick < NT ? ntick : NT), f); fclose(f); }
    }
    {
        static int ord[NF];
        for (i = 0; i < nf; i++) ord[i] = i;
        qsort(ord, (size_t)nf, sizeof ord[0], cmpf);
        printf("window: %" PRIu64 " instructions", win);
        if (from >= 0) printf(" over ticks %" PRId64 " to %" PRId64 " (%.0f per second of line)", from, to, (double)win * 8000.0 / (double)(to - from));
        printf("\n");
        for (i = 0; i < nf && fcnt[ord[i]]; i++)
            printf("%12" PRIu64 " %5.1f%%  %s\n", fcnt[ord[i]], 100.0 * (double)fcnt[ord[i]] / (double)(win ? win : 1), fname[ord[i]]);
    }
    /* the heaviest blocks in the window: address, instructions, function */
    for (i = 0; i < ntb; i++) {
        uint32_t b, bi = 0;
        uint64_t bm = 0;
        for (b = 0; b < (1u << HB); b++) if (bcnt[b] > bm) { bm = bcnt[b]; bi = b; }
        if (!bm) break;
        printf("block %08" PRIx64 " %3" PRIu32 " insns %12" PRIu64 "  %s\n", bpc[bi] - 1, bn[bi], bm, bf[bi] >= 0 ? fname[bf[bi]] : "?");
        bcnt[bi] = 0;
    }
    return 0;
}
