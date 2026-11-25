/* HST on the target: the instruction counts of `make cost-hst-back` (the
 * back channel, modem/hst_back.c), `make cost-hst-fwd` (the forward
 * receiver, modem/hst_fwd.c) and `make cost-hst` (a whole call).
 *
 *   bench_hst -c FILE   a whole call: tools/rec_hst's recording (v90m-replay's
 *                       format, mu-law) replayed into the answerer alone, through
 *                       the public API as rig/hst_side.h runs it (HST offered, the
 *                       endpoint linked, MNP on, text sent once MNP is up), from
 *                       the recording's second second on (rec_hst's first is the
 *                       lead before the answer). Prints the samples, where data at
 *                       14400 began, how many codewords we sent differ from the
 *                       recording's (0: the replay is the call), the octets the
 *                       endpoint read; then "tx hash" (FNV-1a over the codewords
 *                       sent) and "rx hash" (over the octets read) for
 *                       tools/bench-hashes.sh. bench_hst_mark() is called 1 s and
 *                       2 s into data at 14400: the counted second holds the
 *                       forward channel at 14400 (`make cost-hst`).
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
 *   bench_hst -f FILE   write a recording of the forward channel: our
 *                       transmitter (hst_ftx) after 200 ms of silence, the
 *                       long training at 14400 (code 6), then pseudo-random
 *                       data (data mode from about 1.6 s), at -14.4 dBm0
 *                       (RMS 3000), with noise 30 dB below it over 0-4 kHz,
 *                       for 4 s; integer only, as -g
 *   bench_hst -r FILE   the forward receiver (hst_frx_tick at code 6) on it;
 *                       after the run (not counted) its data bits against
 *                       those sent: where they were found, and the errors
 *                       from the 300th bit on
 *   bench_hst -s FILE   write a recording of a switch by marks: as -f, but
 *                       the long training at 12000 (code 5), and at 2.5 s
 *                       hst_ftx_marks_switch to 14400 (code 6)
 *   bench_hst -p FILE   the forward receiver (at code 5) on it, armed for
 *                       code 6 at 2.4 s; a first run on a copy finds the
 *                       sample where both codes' decoders start (the new
 *                       one on the points kept: the dearest symbol), and
 *                       the trace's marks are there and 20 ms (160 samples)
 *                       on: the parallel decode's peak, the switch within
 *                       it. After the run, its code and state, and its bits
 *   bench_hst -n FILE   the loop alone (each sample to a volatile): what the
 *                       others' counts include besides the library
 *
 * Each prints one line: the samples, the bits each receiver gave and their
 * hash (FNV-1a, one bit at a time), and what it detected; the host's and
 * the target's must agree. bench_hst_mark() is called (but under -p) at 2 s and at 3 s of
 * the recording: in qemu's instruction trace that function's address marks
 * off the third second, the receivers locked (or the detector run on; or the forward receiver in
 * data mode). */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "hst.h"
#include "hst_side.h"

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
static hst_frx_t frx, frx2;

/* the forward channel's data: its own generator, so that -r can make the
 * same bits again */
#define FWD_SEED 0x5EED5u
#define FWD_LEVEL 3000                /* -14.4 dBm0 */
#define FWD_NB (SECS * 14400)         /* more than the data bits of the recording */
static uint32_t fgen = FWD_SEED;
static int fwd_bit(void *u) { (void)u; fgen = fgen * 1664525u + 1013904223u; return (int)(fgen >> 27) & 1; }

#define SW_MARKS_AT (8000 * 5 / 2)   /* -s: the marks from 2.5 s */
#define SW_ARM_AT   (8000 * 12 / 5)  /* -p: armed at 2.4 s */
static int record_fwd(const char *path, int sw)
{
    static hst_ftx_t tx;
    uint32_t ns = 0x2B4D6F1u;
    FILE *f = fopen(path, "wb");
    long n;
    if (!f) { fprintf(stderr, "bench_hst: cannot write %s\n", path); return 1; }
    fwrite("HSTB0001", 1, 8, f);
    hst_ftx_init(&tx, FWD_LEVEL, fwd_bit, NULL);
    for (n = 0; n < (long)SECS * 8000; n++) {
        int32_t v, w = 0, k;
        uint8_t b[2];
        if (n == 8000 / 5) hst_ftx_train(&tx, sw ? 5 : 6, 1);
        if (sw && n == SW_MARKS_AT) hst_ftx_marks_switch(&tx, 6);
        v = hst_ftx_tick(&tx);
        /* noise: the sum of four uniform values, RMS 95 (30 dB under 3000) */
        for (k = 0; k < 4; k++) { ns = ns * 1103515245u + 12345u; w += (int32_t)((ns >> 16) & 0xFFFF) - 32768; }
        v += w / 398;                                         /* 2 x 18919 RMS / 398 = 95 */
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        b[0] = (uint8_t)((uint16_t)v & 0xFF); b[1] = (uint8_t)((uint16_t)v >> 8);
        fwrite(b, 1, 2, f);
    }
    fclose(f);
    printf("bench_hst: %ld samples written\n", (long)SECS * 8000);
    return 0;
}

/* the forward receiver's bits against those sent (as test_hst_fwd finds
 * them: the last 64 received among those sent), errors from bit 300 on */
static uint8_t ftxb[FWD_NB], frxb[FWD_NB];
static void fwd_compare(long nrx, long *at, long *cmp, long *errs)
{
    long i0 = nrx - 64, k, j;
    *at = -1; *cmp = *errs = 0;
    fgen = FWD_SEED;
    for (k = 0; k < FWD_NB; k++) ftxb[k] = (uint8_t)fwd_bit(NULL);
    if (i0 < 0 || nrx > FWD_NB) return;
    for (k = 0; k + 64 <= FWD_NB; k++) {
        for (j = 0; j < 64 && frxb[i0 + j] == ftxb[k + j]; j++) ;
        if (j == 64) break;
    }
    if (k + 64 > FWD_NB) return;
    *at = i0 - k;                     /* the received bit that is sent bit 0 */
    for (j = 300; j < FWD_NB && *at + j < nrx; j++) {
        if (*at + j < 0) continue;
        (*cmp)++;
        if (frxb[*at + j] != ftxb[j]) (*errs)++;
    }
}

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

/* -c: the whole call (rig/hst_side.h) */
#define CALL_MAX (2L << 20)              /* octets of a recording at most: 109 s */
#define CALL_LEAD 8000L                  /* rec_hst's second before the answer */
static uint8_t call[CALL_MAX];
static hst_side CS;
static int bench_call(const char *path)
{
    FILE *f = fopen(path, "rb");
    long len, nb, i, diff = 0, first = -1;
    uint32_t h = 2166136261u;
    v90m_state st;
    if (!f) { fprintf(stderr, "bench_hst: cannot read %s\n", path); return 1; }
    len = (long)fread(call, 1, sizeof call, f);
    fclose(f);
    nb = len / 160;
    if (nb * 80 <= CALL_LEAD) { fprintf(stderr, "bench_hst: %s is not a recording of rec_hst\n", path); return 1; }
    if (hst_side_init(&CS)) { fprintf(stderr, "bench_hst: no answerer\n"); return 1; }
    memset(&st, 0, sizeof st);
    for (i = CALL_LEAD / 80; i < nb; i++) {
        const uint8_t *b = call + i * 160;
        uint8_t o[HS_BLOCK];
        int k;
        if (CS.data14400 >= 0 && CS.n == CS.data14400 + 8000) mark(1);
        if (CS.data14400 >= 0 && CS.n == CS.data14400 + 16000) mark(2);
        hst_side_block(&CS, b, o, &st);
        for (k = 0; k < HS_BLOCK; k++) {
            h = (h ^ o[k]) * 16777619u;
            if (o[k] != b[80 + k]) { if (first < 0) first = i * 80 + k; diff++; }
        }
        if (st.end != V90M_END_NONE) { i++; break; }
    }
    printf("bench_hst -c: %ld samples; data at 14400 from sample %ld; our transmit differs from the recording in %ld "
           "(first at %ld); %lu octets received; end %d\n", i * 80, CS.data14400 >= 0 ? CS.data14400 + CALL_LEAD : -1L, diff, first,
           CS.got, (int)st.end);
    printf("tx hash %08" PRIx32 "\nrx hash %08" PRIx32 "\n", h, CS.h);
    hst_side_free(&CS);
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t hdr[8], b[2];
    uint32_t h450 = 2166136261u, h300 = 2166136261u;
    int32_t k, n = 0, n450 = 0, n300 = 0, fired_at = -1;
    long nrx = 0, data_at = -1;
    int mode, fired = 0;
    FILE *f;
    int32_t m1 = 2 * 8000, m2 = 3 * 8000;
    if (argc < 3 || argv[1][0] != '-' || !argv[1][1] || !strchr("gbdnfrcsp", argv[1][1])) {
        fprintf(stderr, "usage: bench_hst -g|-b|-d|-n|-f|-r|-c|-s|-p FILE\n");
        return 2;
    }
    mode = argv[1][1];
    if (mode == 'c') return bench_call(argv[2]);
    if (mode == 'g') return record(argv[2], argc > 3 && !strcmp(argv[3], "300") ? 300 : 450);
    if (mode == 'f') return record_fwd(argv[2], 0);
    if (mode == 's') return record_fwd(argv[2], 1);
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
    hst_frx_init(&frx);
    hst_frx_set_code(&frx, mode == 'p' ? 5 : 6);
    if (mode == 'p') {
        /* where the parallel decode starts: a first run, on a copy */
        hst_frx_init(&frx2);
        hst_frx_set_code(&frx2, 5);
        m1 = m2 = -1;
        for (k = 0; k < n && m1 < 0; k++) {
            if (k == SW_ARM_AT) hst_frx_arm_switch(&frx2, 6);
            (void)hst_frx_tick(&frx2, rec[k]);
            if (frx2.sw_win) m1 = k;
        }
        if (m1 >= 0) m2 = m1 + 160;
    }
    for (k = 0; k < n; k++) {
        int16_t x = rec[k];
        int nb;
        if (k == m1) mark(1);
        if (k == m2) mark(2);
        if (mode == 'b') {
            nb = hst_brx_tick(&r450, x);
            h450 = fnv_bits(h450, r450.bits, nb); n450 += nb;
            if (r450.dec == 0) {
                nb = hst_brx_band(&r300, r450.y);
                h300 = fnv_bits(h300, r300.bits, nb); n300 += nb;
            }
        } else if (mode == 'r' || mode == 'p') {
            int i;
            if (mode == 'p' && k == SW_ARM_AT) hst_frx_arm_switch(&frx, 6);
            nb = hst_frx_tick(&frx, x);
            for (i = 0; i < nb; i++) { if (nrx < FWD_NB) frxb[nrx] = frx.bits[i]; nrx++; }
            if (data_at < 0 && hst_frx_state(&frx) == HST_FRX_DATA) data_at = k;
        } else if (mode == 'd') {
            int r = hst_det_tick(&det, x);
            if (r && !fired) { fired = r; fired_at = k; }
        } else
            bench_hst_sink = x;
    }
    if (mode == 'b')
        printf("bench_hst -b: %" PRId32 " samples; 450: %" PRId32 " bits, hash %08" PRIx32 ", locked %d; 300: %" PRId32
               " bits, hash %08" PRIx32 ", locked %d\n", n, n450, h450, hst_brx_locked(&r450), n300, h300, hst_brx_locked(&r300));
    else if (mode == 'r') {
        long at, cmp, errs;
        fwd_compare(nrx, &at, &cmp, &errs);
        printf("bench_hst -r: %" PRId32 " samples; data mode from sample %ld, state %d at the end; %ld bits, "
               "the data found at bit %ld, %ld errors in %ld bits\n", n, data_at, hst_frx_state(&frx), nrx, at, errs, cmp);
    } else if (mode == 'p')
        printf("bench_hst -p: %" PRId32 " samples; both codes decoded from sample %" PRId32 "; at the end code %d, armed %d, "
               "state %d; %ld bits, hash %08" PRIx32 "\n", n, m1, (int)frx.code, hst_frx_armed(&frx), hst_frx_state(&frx),
               nrx, fnv_bits(2166136261u, frxb, (int)(nrx < FWD_NB ? nrx : FWD_NB)));
    else if (mode == 'n')
        printf("bench_hst -n: %" PRId32 " samples\n", n);
    else
        printf("bench_hst -d: %" PRId32 " samples; detected %s at sample %" PRId32 "\n", n,
               fired == HST_DET_450 ? "450" : fired == HST_DET_300 ? "300" : "nothing", fired_at);
    return 0;
}
