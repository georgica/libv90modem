/* qc-scan: V.92's QC1a (Table 2) in recorded calls, for Quick Connect's
 * evidence (docs/history/qc-facts.md; the plan's Global Constraints,
 * replay): a recording whose heard side holds a QC1a replays otherwise
 * once our answerer takes it (V90M_OPT_NO_QC, v90m-replay -Q, answers it
 * as it was answered).
 *
 *   qc-scan [-a] FILE...        (make qc-scan RECS=<dir>: every recording)
 *
 * FILE: as v90m-replay reads it (per 10 ms, 80 codewords heard then 80
 * sent, mu-law, or A-law with -a; or "V90DLIN1", then 80 16-bit linear
 * samples heard and 80 codewords sent).
 *
 * On the heard side, two ways: modem/v8ans with QC offered, started 200 ms
 * before the answer tone the recording sent (found as tools/v90m_replay.c
 * finds it: the first sound after 200 ms of silence, more than a second in;
 * from the first sample when there is none), so that its ANSam window is
 * the recorded answerer's; and the QC1a
 * tracker of modem/v8rx (v8rx_tick_qc) on the V.21(L) demodulator over the
 * whole recording, wherever the answer tone was. Each QC1a found is printed
 * as "QC1a heard" with its time and information octet. Exits 0 whether
 * or not one was found (real V.92 callers send it: 0.7.4 answers it, and
 * the replay proof runs with v90m-replay -Q), 2 on a file it cannot read. Host tool, not installed; nothing
 * but these lines and a count is printed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../modem/v8ans.h"
#include "../modem/qc.h"
#include "../v90/v90_pcm.h"

#define FS 8000
#define MAXS (2L << 20)                 /* samples at most: 262 s */
/* as modem/v8ans.c: below this the V.21(L) receiver reads idle mark */
#define SQUELCH_AMP 110

static int16_t heard[MAXS];
static uint8_t sent[MAXS];
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
        for (j = 0; j < 80; j++) {
            heard[i * 80 + j] = lin ? (int16_t)(uint16_t)(b[2 * j] | b[2 * j + 1] << 8)
                                    : (int16_t)v90_octet_linear(alaw, b[j]);
            sent[i * 80 + j] = b[(lin ? 160 : 80) + j];
        }
    }
    nheard = nb * 80;
}

/* the answer tone sent (tools/v90m_replay.c recorded_answer); -1: none */
static long answer_tone(int alaw)
{
    long i, run = 0;
    for (i = 0; i < nheard; i++) {
        if (alaw ? (sent[i] & 0x7F) == 0x55 : (sent[i] & 0x7F) == 0x7F) run++;
        else { if (run >= 1590 && run <= 1612 && i > 8000) return i; run = 0; }
    }
    return -1;
}

static int scan(const char *name, int alaw)
{
    static v8ans_t v;
    v8_menu_t local;
    v21demod_t dem;
    v8rx_t rx;
    int64_t squelch;
    uint8_t st = 0, oct = 0;
    int16_t tx;
    long i, k0 = answer_tone(alaw) - FS / 5;
    int hits = 0;

    memset(&local, 0, sizeof local);
    local.call_function = V8_CALLF_DATA;
    local.modes = V8_MOD_V34 | V8_MOD_V32 | V8_MOD_V22 | V8_MOD_V21;
    local.pcm = V8_PCM_DIGITAL; local.prot = V8_PROT_LAPM; local.access = V8_ACC_DIGITAL;
    v8ans_init(&v, FS, 5600, &local);
    v8ans_offer_qc(&v, 1);
    if (k0 < 0) k0 = 0;
    for (i = k0; i < nheard && v8ans_tick(&v, heard[i], &tx) != V8A_DONE; i++) {}
    if (v.qc.flags & V8A_QC_HEARD) {
        printf("%s: QC1a heard by v8ans at %.3f s (%.3f s after the answer tone), octet 0x%02X%s\n", name,
               i / (double)FS, (i - k0 - FS / 5) / (double)FS, v.qc.oct, v.qc.aux & V8A_QCX_ONHOLD ? " (WXYZ 1111)" : "");
        hits++;
    }

    v21demod_init(&dem, V21L_MARK, V21L_SPACE, FS, 300);
    squelch = v21demod_energy_of(&dem, SQUELCH_AMP);
    v8rx_init(&rx, FS);
    for (i = 0; i < nheard; i++) {
        int level = v21demod_tick(&dem, heard[i]);
        if (dem.energy < squelch) level = 1;
        if (v8rx_tick_qc(&rx, level, &st, &oct) & V8RX_QC1A) {
            printf("%s: QC1a heard at %.3f s, octet 0x%02X\n", name, i / (double)FS, oct);
            hits++;
        }
    }
    return hits;
}

int main(int argc, char **argv)
{
    int arg = 1, alaw = 0, hits = 0, n = 0;
    if (arg < argc && !strcmp(argv[arg], "-a")) { alaw = 1; arg++; }
    if (arg >= argc) { fprintf(stderr, "usage: qc-scan [-a] FILE...\n"); return 2; }
    for (; arg < argc; arg++) {
        FILE *f = fopen(argv[arg], "rb");
        long len;
        if (!f) { fprintf(stderr, "qc-scan: cannot open %s\n", argv[arg]); return 2; }
        len = (long)fread(file, 1, sizeof file, f);
        fclose(f);
        load_rec(len, alaw);
        hits += scan(argv[arg], alaw) > 0;
        n++;
    }
    printf("qc-scan: %d recordings, %d with a QC1a\n", n, hits);
    return 0;
}
