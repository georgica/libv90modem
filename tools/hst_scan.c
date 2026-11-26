/* hst-scan: what is on each side of a recorded call, for HST's evidence
 * (docs/history/hst-facts.md, "Evidence"), and the back-channel detector
 * of ANS_HST_WAIT (modem/hst_back.c, hst_det) over what the answerer heard.
 *
 *   hst-scan [-a] [-b | -f] FILE...     (make hst-scan RECS=<dir>: -b over
 *   hst-scan -w [-b | -f] FILE.wav...    every recording)
 *
 * FILE: as v90m-replay reads it (per 10 ms, 80 codewords heard then 80
 * sent, mu-law, or A-law with -a; or "V90DLIN1", then 80 16-bit linear
 * samples heard and 80 codewords sent); with -w, a 16-bit PCM WAV at 8000
 * or 16000 Hz (16000 is halved by a low-pass), mono (heard) or stereo
 * (heard, then sent).
 *
 * Without -b, for each channel: its tones (one frequency holding 60 ms or
 * more: where, how long, at what level) and its silences (100 ms or more
 * under -50 dBm0).
 *
 * With -b, on the heard channel: the detector from the first sample (and
 * again after each detection), each detection as "back channel detected"
 * with its time and speed, and the time after the end of the answer tone
 * we sent, when there is one; then the receiver of that speed, on: how long
 * its first bits are ones (the preamble), and the HDLC frames in its bits,
 * as headers and parameters only (an LR's parameters; an LT's N(S) and
 * length, never its data). Exits 1 when anything was detected.
 *
 * With -f, on each channel: the forward channel (modem/hst_fwd.c's
 * receiver, hst_frx, one at each rate code 2..6 on the same samples).
 * Each training it hears, its segments by the statistics of the points
 * the receiver decides, 8 symbols at a time: an alternation of A and B
 * (AB, V.32's S), of C and D (CD, S-bar), the 4 points A..D at random
 * (TRN), or points off those 4 (data); each with its start and length in
 * symbols (AB from where the receiver heard its two tones: it needs 40 ms
 * of them before it starts). Then the data's constellation: the rate code
 * whose receiver decodes the data best (the highest signal to error 256
 * symbols in, among those still in data mode), its size in points and its
 * rate; and how the receiver left it (carrier lost, unlocked, the next
 * training). A training heard in data starts the receivers again from the
 * equaliser they had. Exits 1 when a training was heard.
 *
 * Host tool, not installed. Its analysis of tones is in double precision;
 * the detector and the receivers are the library's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../modem/hst.h"
#include "../v90/v90_pcm.h"
#include "../v42/v42_hdlc.h"
#include "../v42/mnp_frame.h"

#define FS 8000
#define DBM0 15770.0
#define MAXS (2L << 20)                 /* samples a channel at most: 262 s */
#define PI 3.14159265358979323846

static int16_t ch[2][MAXS];        /* heard, sent */
static long nch[2];
static int nchan;

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint8_t file[(24 << 20) + 64];

/* a recording of v90m-replay's formats */
static int load_rec(long len, int alaw)
{
    const uint8_t *d = file;
    long bs = 160, nb, i, j;
    int lin = 0;
    if (len >= 8 && !memcmp(file, "V90DLIN1", 8)) { lin = 1; bs = 240; d = file + 8; len -= 8; }
    nb = len / bs;
    if (nb * 80 > (long)(sizeof ch[0] / sizeof ch[0][0])) nb = (long)(sizeof ch[0] / sizeof ch[0][0]) / 80;
    for (i = 0; i < nb; i++) {
        const uint8_t *b = d + i * bs;
        for (j = 0; j < 80; j++) {
            ch[0][i * 80 + j] = lin ? (int16_t)le16(b + 2 * j) : (int16_t)v90_octet_linear(alaw, b[j]);
            ch[1][i * 80 + j] = (int16_t)v90_octet_linear(alaw, b[(lin ? 160 : 80) + j]);
        }
    }
    nch[0] = nch[1] = nb * 80;
    nchan = 2;
    return 0;
}

/* a 16-bit PCM WAV, 8000 or 16000 Hz, mono or stereo */
static int load_wav(long len, const char *name)
{
    long p = 12, rate = 0, chans = 0, bits = 0, i, n, cap = (long)(sizeof ch[0] / sizeof ch[0][0]);
    const uint8_t *data = NULL;
    long dlen = 0;
    if (len < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4)) {
        fprintf(stderr, "hst-scan: %s: not a WAV file\n", name);
        return 1;
    }
    while (p + 8 <= len) {
        long sz = (long)le32(file + p + 4);
        if (!memcmp(file + p, "fmt ", 4) && sz >= 16) {
            if (le16(file + p + 8) != 1) break;
            chans = le16(file + p + 10); rate = (long)le32(file + p + 12); bits = le16(file + p + 22);
        } else if (!memcmp(file + p, "data", 4)) {
            data = file + p + 8; dlen = sz < len - p - 8 ? sz : len - p - 8;
            break;
        }
        p += 8 + sz + (sz & 1);
    }
    if (!data || bits != 16 || (chans != 1 && chans != 2) || (rate != 8000 && rate != 16000)) {
        fprintf(stderr, "hst-scan: %s: not 16-bit PCM, mono or stereo, at 8000 or 16000 Hz\n", name);
        return 1;
    }
    n = dlen / (2 * chans);
    nchan = (int)chans;
    if (rate == 8000) {
        if (n > cap) n = cap;
        for (i = 0; i < n; i++) {
            ch[0][i] = (int16_t)le16(data + 2 * chans * i);
            if (chans == 2) ch[1][i] = (int16_t)le16(data + 2 * chans * i + 2);
        }
        nch[0] = nch[1] = n;
    } else {
        /* halved: a 63-tap windowed-sinc low-pass at 3600 Hz, then every second sample */
        static double h[63];
        int c, k;
        for (k = 0; k < 63; k++) {
            int t = k - 31;
            double v = t == 0 ? 2 * 3600.0 / 16000 : sin(2 * PI * 3600.0 * t / 16000) / (PI * t);
            h[k] = v * (0.42 - 0.5 * cos(2 * PI * k / 62) + 0.08 * cos(4 * PI * k / 62));
        }
        if (n / 2 > cap) n = 2 * cap;
        for (c = 0; c < chans; c++) {
            for (i = 0; 2 * i < n; i++) {
                double acc = 0, v;
                for (k = 0; k < 63; k++) {
                    long s = 2 * i - k + 31;
                    if (s >= 0 && s < n) acc += h[k] * (int16_t)le16(data + 2 * chans * s + 2 * c);
                }
                v = acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc;
                ch[c][i] = (int16_t)lrint(v);
            }
            nch[c] = i;
        }
    }
    return 0;
}

static double dbm0(double ms) { return 10 * log10(ms / (DBM0 * DBM0) + 1e-30); }

/* ---- tones and silences: 10 ms frames ---- */
#define FR 80
typedef struct { int kind; double hz, ms; } frame_t;      /* kind 0: other, 1: silence, 2: tone */

static frame_t frame_of(const int16_t *x)
{
    frame_t f = { 0, 0, 0 };
    double r0 = 0, num = 0, den = 0, c, res = 0;
    int i;
    for (i = 0; i < FR; i++) r0 += (double)x[i] * x[i];
    f.ms = r0 / FR;
    if (dbm0(f.ms) < -50) { f.kind = 1; return f; }
    for (i = 2; i < FR; i++) { num += (double)x[i - 1] * (x[i] + x[i - 2]); den += 2.0 * x[i - 1] * x[i - 1]; }
    c = den > 0 ? num / den : 0;
    if (c > 1) c = 1; else if (c < -1) c = -1;
    for (i = 2; i < FR; i++) { double e = x[i] - 2 * c * x[i - 1] + x[i - 2]; res += e * e; }
    f.hz = acos(c) * FS / (2 * PI);
    if (res < 0.02 * r0 && f.hz > 100 && f.hz < 3800) f.kind = 2;
    return f;
}

static void tones(int c, const char *name, const char *side)
{
    long nf = nch[c] / FR, i = 0;
    while (i < nf) {
        frame_t f = frame_of(ch[c] + i * FR);
        long j = i + 1;
        double sum = f.hz, ms = f.ms;
        if (f.kind == 0) { i++; continue; }
        for (; j < nf; j++) {
            frame_t g = frame_of(ch[c] + j * FR);
            if (g.kind != f.kind || (f.kind == 2 && fabs(g.hz - sum / (j - i)) > 15)) break;
            sum += g.hz; ms += g.ms;
        }
        if (f.kind == 2 && j - i >= 6)
            printf("%s: %s: tone %.0f Hz at %.3f s for %.3f s, %.1f dBm0\n", name, side, sum / (j - i),
                   i * FR / (double)FS, (j - i) * FR / (double)FS, dbm0(ms / (j - i)));
        if (f.kind == 1 && j - i >= 10)
            printf("%s: %s: silence at %.3f s for %.3f s\n", name, side, i * FR / (double)FS, (j - i) * FR / (double)FS);
        i = j;
    }
}

/* the end of the answer tone we sent: the end of the first run of 2100 Hz
 * (+-25) frames holding 1 s or more, through gaps of up to 3 frames (its
 * phase reversals); -1 when none */
static long answer_tone_end(void)
{
    long nf = nch[1] / FR, i, run = 0, gap = 0, last = -1;
    if (nchan < 2) return -1;
    for (i = 0; i < nf; i++) {
        frame_t f = frame_of(ch[1] + i * FR);
        if (f.kind == 2 && fabs(f.hz - 2100) <= 25) { run += gap + 1; gap = 0; last = i; continue; }
        if (run > 0 && ++gap <= 3) continue;
        if (run >= 100) return (last + 1) * FR;
        run = gap = 0;
    }
    return run >= 100 ? (last + 1) * FR : -1;
}

/* ---- -b ---- */
static void frame_out(const char *name, double t, const uint8_t *f, int len)
{
    mnp_hdr_t h;
    mnp_lr_t lr;
    int r;
    memset(&h, 0, sizeof h);
    r = mnp_hdr_parse(f, len, MNP_LAYOUT_STD, -1, &h);
    printf("%s: %.3f s: frame, %d octets", name, t, len);
    if (r == 0 && h.type == MNP_LR && mnp_lr_parse(f, len, &lr) >= 0) {
        printf(": LR, framing %d, k %d, N401 %d", lr.framing, lr.k, lr.n401);
        if (lr.have_opt) printf(", optimisation 0x%02X", (unsigned)lr.opt);
        if (lr.have_c0) printf(", 0xC0 flags 0x%02X rates 0x%02X", lr.c0_flags, lr.c0_rates);
    } else if (r == 0) {
        switch (h.type) {
        case MNP_LT: printf(": LT, N(S) %d, %d octets of data", h.ns, h.dlen); break;
        case MNP_LA: printf(": LA, N(R) %d, N(k) %d", h.nr, h.nk); break;
        case MNP_LN: printf(": LN, N(S) %d, type %d", h.ns, h.code); break;
        case MNP_LNA: printf(": LNA, N(R) %d", h.nr); break;
        case MNP_LD: printf(": LD, reason %d", h.reason); break;
        default: printf(": type %d", h.type); break;
        }
    } else
        printf(": not an MNP header (LI 0x%02X, type 0x%02X)", f[0], len > 1 ? f[1] : 0);
    printf("\n");
}

/* ---- -f ---- */
enum { SEG_NONE, SEG_AB, SEG_CD, SEG_ALT, SEG_TRN, SEG_DATA };
static const char *seg_n[] = { "none", "AB", "CD", "alternation", "TRN", "data" };
static const int set_size[7] = { 0, 0, 4, 16, 32, 64, 128 };

/* nearest of A B C D (as modem/hst_fwd.c decides them in training) */
static int slice4(int32_t zr, int32_t zi)
{
    int64_t a = 2 * (int64_t)zr - zi, b = 2 * (int64_t)zi + zr;
    if (a >= 0) return b >= 0 ? 2 : 1;
    return b >= 0 ? 3 : 0;
}

static double snr_db(const hst_frx_t *r)
{
    int64_t num = 0, den = 0;
    if (!hst_frx_snr(r, &num, &den) || den <= 0 || num <= 0) return 0;
    return 10 * log10((double)num / (double)den);
}

static int scan_f(const char *name, int c, const char *side)
{
    static hst_frx_t rx[7];
    int k, cur = SEG_NONE, cand = SEG_NONE, ncand = 0, trainings = 0, lst = -1, pending = 0;
    long n, seg_at = 0, cand_at = 0, nsym = 0, data_sym = -1, ab_at = -1, restart_at = -1;
    int lab[8], off[8], i;
    long last_nsym = -1;
    for (k = 2; k <= 6; k++) { hst_frx_init(&rx[k]); hst_frx_set_code(&rx[k], k); }
    for (i = 0; i < 8; i++) lab[i] = off[i] = 0;
    for (n = 0; n < nch[c]; n++) {
        int st6;
        for (k = 2; k <= 6; k++) (void)hst_frx_tick(&rx[k], ch[c][n]);
        st6 = hst_frx_state(&rx[6]);
        /* the receiver started: AB was heard for abrun samples before
         * (HST_FRX_AB_MS, or less for a short AB that ended) */
        if ((st6 == HST_FRX_TRAIN || st6 == HST_FRX_DATA) && lst != HST_FRX_TRAIN && lst != HST_FRX_DATA) {
            ab_at = restart_at >= 0 ? restart_at : n - rx[6].abrun;
            trainings++;
            printf("%s: %s: forward training heard%s: AB from about %.3f s\n", name, side,
                   restart_at >= 0 ? " in data" : "", ab_at / (double)FS);
            restart_at = -1;
            cur = SEG_AB; seg_at = ab_at; cand = SEG_NONE; ncand = 0; data_sym = -1; nsym = 0; pending = 0;
            last_nsym = rx[6].nsym;
        }
        if ((lst == HST_FRX_TRAIN || lst == HST_FRX_DATA) && st6 != HST_FRX_TRAIN && st6 != HST_FRX_DATA) {
            static const char *why[] = { "", "", "", "", "the carrier lost", "unlocked", "no data mode (training failed)" };
            printf("%s: %s:   %s from %.3f s, then the receiver stopped at %.3f s: %s\n", name, side, seg_n[cur],
                   seg_at / (double)FS, n / (double)FS, st6 >= 0 && st6 <= HST_FRX_TRAINFAIL ? why[st6] : "?");
            cur = SEG_NONE;
        }
        lst = st6;
        if ((st6 != HST_FRX_TRAIN && st6 != HST_FRX_DATA) || rx[6].nsym == last_nsym) continue;
        last_nsym = rx[6].nsym;
        /* a symbol: its decision among A..D and whether it is off them */
        {
            int32_t zr = rx[6].hzr[0], zi = rx[6].hzi[0];
            int x, y, kind;
            int64_t dr, di, er, ei;
            int na = 0;
            int st = slice4(zr, zi);
            v32_state_point(st, &x, &y);
            dr = (int64_t)x * HST_FRX_U; di = (int64_t)y * HST_FRX_U;
            er = zr - dr; ei = zi - di;
            for (i = 7; i > 0; i--) { lab[i] = lab[i - 1]; off[i] = off[i - 1]; }
            lab[0] = st;
            off[0] = 10 * (er * er + ei * ei) > dr * dr + di * di;          /* error over -10 dB */
            nsym++;
            if (nsym < 8) continue;
            for (i = 0; i < 8; i++) na += off[i];
            if (na >= 4) kind = SEG_DATA;
            else {
                int alt = lab[0] != lab[1];
                for (i = 2; i < 8 && alt; i++) alt = lab[i] == lab[i - 2];
                if (!alt) kind = SEG_TRN;
                else {
                    int lo = lab[0] < lab[1] ? lab[0] : lab[1], hi = lab[0] ^ lab[1] ^ lo;
                    kind = lo == 0 && hi == 1 ? SEG_AB : lo == 2 && hi == 3 ? SEG_CD : SEG_ALT;
                }
            }
            if (kind == cur) ncand = 0;
            else {
                if (kind != cand) { cand = kind; ncand = 0; cand_at = n - 7 * FS / 2400; }
                if (++ncand >= 8) {
                    /* a new segment, 8 symbols long by now: the last one's length */
                    printf("%s: %s:   %s from %.3f s, %ld symbols\n", name, side, seg_n[cur], seg_at / (double)FS,
                           (long)((cand_at - seg_at) * 2400 / FS));
                    if (cur == SEG_DATA && (cand == SEG_AB || cand == SEG_CD || cand == SEG_ALT)) {
                        /* a training in data: every receiver listens again,
                         * from its equaliser, for the rest of the AB */
                        hst_eqstore_t eq;
                        for (k = 2; k <= 6; k++) { hst_frx_store(&rx[k], &eq); hst_frx_restore(&rx[k], &eq); }
                        restart_at = cand_at;
                        lst = HST_FRX_IDLE;
                        cur = SEG_NONE; pending = 0;
                        continue;
                    }
                    cur = cand; seg_at = cand_at; ncand = 0;
                    if (cur == SEG_DATA) { data_sym = nsym; pending = 1; }
                }
            }
        }
        /* 256 symbols into data: the code that decodes it best */
        if (pending && nsym - data_sym >= 256) {
            int best = 0;
            double bs = -100;
            pending = 0;
            printf("%s: %s:   the data's signal to error by rate code:", name, side);
            for (k = 2; k <= 6; k++) {
                int in = hst_frx_state(&rx[k]) == HST_FRX_DATA;
                double v = snr_db(&rx[k]);
                printf(" %d: %s%.1f dB", k, in ? "" : "(not in data) ", v);
                if (in && v > bs) { bs = v; best = k; }
            }
            printf("\n");
            if (best) printf("%s: %s:   constellation: %d points, code %d, %d bit/s\n", name, side, set_size[best], best, 2400 * best);
            else printf("%s: %s:   constellation: none of codes 2..6 decodes it\n", name, side);
        }
    }
    if (cur != SEG_NONE)
        printf("%s: %s:   %s from %.3f s to the end, %ld symbols\n", name, side, seg_n[cur], seg_at / (double)FS,
               (long)((n - seg_at) * 2400 / FS));
    printf("%s: %s: %.1f s, %d forward trainings heard\n", name, side, nch[c] / (double)FS, trainings);
    return trainings;
}

static int scan_b(const char *name)
{
    static hst_det_t d;
    static v42hdlc_rx_t hr;
    long n, ans = answer_tone_end(), det_at = -1, ones = 0, nbits = 0;
    int got = 0, k = 0, hits = 0, in_pre = 1;
    hst_det_init(&d);
    for (n = 0; n < nch[0]; n++) {
        int r = hst_det_tick(&d, ch[0][n]);
        if (!got && r) {
            got = r; k = r == HST_DET_450 ? 0 : 1; det_at = n; hits++;
            printf("%s: back channel detected at %.3f s, %s bit/s", name, n / (double)FS, r == HST_DET_450 ? "450" : "300");
            if (ans >= 0) printf(", %.0f ms after the end of our answer tone", (n - ans) * 1000.0 / FS);
            printf("\n");
            v42hdlc_rx_init(&hr);
            v42hdlc_rx_set_fcs(&hr, V42HDLC_FCS16);
            ones = nbits = 0; in_pre = 1;
            continue;
        }
        if (got) {
            int b, nb = d.nb[k];
            for (b = 0; b < nb; b++) {
                int bit = d.rx[k].bits[b], len;
                nbits++;
                if (in_pre) {
                    if (bit) ones++;
                    else {
                        in_pre = 0;
                        printf("%s: preamble: the first %ld bits given (from the lock) ones, %.0f ms\n", name, ones,
                               ones * 1000.0 / (k ? 300 : 450));
                    }
                }
                len = v42hdlc_rx_bit(&hr, bit);
                if (len > 0) frame_out(name, n / (double)FS, hr.buf, len);
            }
            /* the receiver lost: detect again */
            if (!hst_brx_locked(&d.rx[k]) && n - det_at > 2 * FS) {
                printf("%s: lock lost at %.3f s after %ld bits\n", name, n / (double)FS, nbits);
                got = 0;
                hst_det_init(&d);
            }
        }
    }
    printf("%s: %.1f s heard", name, nch[0] / (double)FS);
    if (ans >= 0) printf(", our answer tone ends at %.3f s", ans / (double)FS);
    printf(", %d back-channel detections\n", hits);
    return hits;
}

int main(int argc, char **argv)
{
    int arg = 1, alaw = 0, wav = 0, back = 0, fwd = 0, hits = 0;
    for (; arg < argc && argv[arg][0] == '-'; arg++) {
        if (!strcmp(argv[arg], "-a")) alaw = 1;
        else if (!strcmp(argv[arg], "-w")) wav = 1;
        else if (!strcmp(argv[arg], "-b")) back = 1;
        else if (!strcmp(argv[arg], "-f")) fwd = 1;
        else break;
    }
    if (arg >= argc || (back && fwd)) { fprintf(stderr, "usage: hst-scan [-a] [-w] [-b | -f] FILE...\n"); return 2; }
    for (; arg < argc; arg++) {
        FILE *f = fopen(argv[arg], "rb");
        long len;
        if (!f) { fprintf(stderr, "hst-scan: cannot open %s\n", argv[arg]); return 2; }
        len = (long)fread(file, 1, sizeof file, f);
        fclose(f);
        if (wav ? load_wav(len, argv[arg]) : load_rec(len, alaw)) return 2;
        if (back) hits += scan_b(argv[arg]) > 0;
        else if (fwd) {
            int t = scan_f(argv[arg], 0, "heard");
            if (nchan > 1) t += scan_f(argv[arg], 1, "sent");
            hits += t > 0;
        }
        else {
            tones(0, argv[arg], "heard");
            if (nchan > 1) tones(1, argv[arg], "sent");
        }
    }
    return hits ? 1 : 0;
}
