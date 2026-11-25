/* Record a simulated HST call in the format v90d records real calls in
 * (tools/README.md), from the answering side: per 10 ms the 80 mu-law
 * codewords the answerer's codec received and the 80 it sent. The caller
 * is sim/hstcall (written from docs/history/hst-facts.md, not from the
 * library); the answerer is the library's through the public API, run by
 * rig/hst_side.h as a host runs it, in 10 ms blocks (a block's delay
 * between the line and the modem), its endpoint linked as v90m-replay -H
 * links it and kept full of text once MNP is up. rig/bench_hst -c replays
 * the recording into the answerer alone (`make bench-hst`, `make
 * cost-hst`, tools/bench-hashes.sh); v90m-replay -H follows it until the
 * endpoint starts (at a block's end there, at the sample in the replay).
 * Host only: the line and the caller are the tests' simulations, in
 * double.
 *
 *   rec_hst [-e ERL] [-b DBM0] [-n DBM0] [-p BPS] [-r SEED] [-x DB@MS]... [-q] SECONDS OUT
 *
 * The line (sim/v32line): mu-law at the answerer, 6 dB loss each way, 25
 * ms each way, +1 Hz, +30 ppm on the caller's clock, the caller's own
 * hybrid 20 dB down.
 *   -e ERL      our hybrid's echo return loss: our forward signal comes
 *               back this far under its level (default 20)
 *   -b DBM0     the caller's back channel as it reaches us (default -18:
 *               the caller at -12)
 *   -n DBM0     the noise at our receiver and at the caller's (default -55)
 *   -p BPS      the caller's back channel, 450 or 300, from its start
 *               (default 450)
 *   -r SEED     the line's noise (default 19)
 *   -x DB@MS    a level step of the caller's signal: DB (negative: down)
 *               from MS ms after our data at 14400 begins, held (the real
 *               line's "a lasting level drop"); up to four
 *   -q          no summary of the back receivers' locks
 *
 * The call: the recording begins a second before the answer (200 ms of it
 * silent, as v90d's are: the replay finds our answer tone after it); then
 * ANSam, the HST wait, the caller's back channel, our long training at the
 * opening rate, MNP, the retrain to 14400, and SECONDS seconds of data at
 * 14400 both ways. It stops early when the modem ends the call; at most
 * 60 s more.
 *
 * Printed: the samples, the transmit hash (FNV-1a over the codewords we
 * sent, as bench_hst -c prints it), where data at 14400 began, the
 * octets each end received and whether each was right, and with -x, each
 * back receiver's locks and losses of lock from the first step on, the
 * modem's end (code and time) and the octets the endpoint got in each
 * second after the step. Exit 1 when no data moved at 14400. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "v90modem.h"
#include "v90modem_priv.h"
#include "pcmenc.h"
#include "v90_pcm.h"
#include "hstcall.h"
#include "v32line.h"
#include "../rig/hst_side.h"

static FILE *out;
static long nrec;
static uint32_t txh = 2166136261u;

static hst_side S;
static hstcall_t HC;
static v32line_t L;

/* the caller's signal, scaled by the level steps */
static double cgain = 1.0;
static int16_t sat(double v) { long q = lrint(v); return (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q); }
static double peer(void *u, double rx) { return cgain * hstcall_tick((hstcall_t *)u, sat(rx)); }

/* the caller's data: the same sentence; what it gets back checked */
static size_t cw, cr; static long cbad;
static void caller_data(void)
{
    size_t k = sizeof hs_sentence - 1, w;
    uint8_t buf[256];
    if (HC.linked)
        for (;;) {
            size_t off = cw % k;
            w = hstcall_write(&HC, (const uint8_t *)hs_sentence + off, k - off);
            cw += w;
            if (w < k - off) break;
        }
    while ((w = hstcall_read(&HC, buf, sizeof buf)) > 0) {
        size_t i;
        for (i = 0; i < w; i++, cr++) if (buf[i] != (uint8_t)hs_sentence[cr % k]) cbad++;
    }
}

/* a block, 10 ms: the line run with what we sent in the block before (the
 * host's block of delay), then the answerer on what it brought; recorded */
static uint8_t atx[HS_BLOCK] = { 0xFF };
static void block(v90m_state *st)
{
    uint8_t heard[HS_BLOCK], blk[2 * HS_BLOCK];
    int k;
    for (k = 0; k < HS_BLOCK; k++) {
        heard[k] = pcm_encode(0, v32line_tick(&L, (int16_t)v90_octet_linear(0, atx[k]), peer, &HC));
        if (k % 8 == 7) caller_data();
    }
    hst_side_block(&S, heard, atx, st);
    for (k = 0; k < HS_BLOCK; k++) txh = (txh ^ atx[k]) * 16777619u;
    memcpy(blk, heard, HS_BLOCK); memcpy(blk + HS_BLOCK, atx, HS_BLOCK);
    fwrite(blk, 1, sizeof blk, out);
    nrec += HS_BLOCK;
}

int main(int argc, char **argv)
{
    v32line_cfg lc;
    hstcall_cfg hc;
    double erl = 20, back = -18, noise = -55;
    int nx = 0, quiet = 0, arg, k, bps = 450;
    long seed = 19;
    struct { double db; long ms; } xs[4];
    long secs, end, d0;
    int lk[2] = { -1, -1 }, xi = 0, ended = 0;
    unsigned long got_at[64], g0 = 0;
    long nsec = 0;
    answer_t *a;
    for (arg = 1; arg < argc && argv[arg][0] == '-' && argv[arg][1]; arg++) {
        const char *o = argv[arg];
        if (!strcmp(o, "-q")) quiet = 1;
        else if (arg + 1 < argc && !strcmp(o, "-e")) erl = atof(argv[++arg]);
        else if (arg + 1 < argc && !strcmp(o, "-b")) back = atof(argv[++arg]);
        else if (arg + 1 < argc && !strcmp(o, "-n")) noise = atof(argv[++arg]);
        else if (arg + 1 < argc && !strcmp(o, "-p")) bps = atoi(argv[++arg]) == 300 ? 300 : 450;
        else if (arg + 1 < argc && !strcmp(o, "-r")) seed = atol(argv[++arg]);
        else if (arg + 1 < argc && !strcmp(o, "-x") && nx < 4) {
            if (sscanf(argv[++arg], "%lf@%ld", &xs[nx].db, &xs[nx].ms) != 2) break;
            nx++;
        } else break;
    }
    if (argc - arg != 2 || (secs = atol(argv[arg])) <= 0) {
        fprintf(stderr, "usage: rec_hst [-e ERL] [-b DBM0] [-n DBM0] [-p BPS] [-r SEED] [-x DB@MS]... [-q] SECONDS OUT\n");
        return 2;
    }
    out = fopen(argv[arg + 1], "wb");
    if (!out) { perror(argv[arg + 1]); return 1; }
    if (hst_side_init(&S)) { fprintf(stderr, "rec_hst: no answerer\n"); return 1; }
    a = v90m__answer(S.m);

    memset(&lc, 0, sizeof lc);
    lc.mulaw = 1; lc.loss_db = 6; lc.delay = 200; lc.near_db = erl; lc.c_near_db = 20;
    lc.far_db = lc.c_far_db = 300; lc.noise_a_dbm0 = lc.noise_c_dbm0 = noise;
    lc.freq_offset = 1; lc.ppm = 30; lc.seed = (uint32_t)seed;
    v32line_init(&L, &lc);
    hstcall_defaults(&hc);
    hc.level_dbm0 = (int)lrint(back + lc.loss_db);
    hc.back_bps = hc.start_bps = bps;
    hstcall_init(&HC, &hc);

    /* a second before the call is answered, as v90d's recordings have:
     * the replay takes for the answer tone the first sound after 200 ms of
     * silence, more than a second in (the modem's own 200 ms), so what is
     * "sent" here is not silent */
    {
        uint8_t lead[2 * HS_BLOCK];
        memset(lead, 0xFF, HS_BLOCK); memset(lead + HS_BLOCK, 0xFE, HS_BLOCK);
        memset(atx, 0xFF, sizeof atx);
        for (k = 0; k < 8000 / HS_BLOCK; k++) { fwrite(lead, 1, sizeof lead, out); nrec += HS_BLOCK; }
    }
    d0 = nrec;
    memset(got_at, 0, sizeof got_at);
    end = d0 + 8000L * 60;
    while (nrec < end) {
        v90m_state st;
        block(&st);
        if (S.data14400 >= 0 && end > S.data14400 + d0 + secs * 8000) end = S.data14400 + d0 + secs * 8000;
        if (S.data14400 >= 0) {
            long t = S.n - S.data14400;                       /* samples into data at 14400 */
            while (xi < nx && t >= xs[xi].ms * 8) {
                cgain *= pow(10, xs[xi].db / 20);
                printf("rec_hst: level step %+.1f dB at %ld ms into data at 14400\n", xs[xi].db, xs[xi].ms);
                xi++;
            }
            if (nx && t >= xs[0].ms * 8 && t < xs[0].ms * 8 + HS_BLOCK) g0 = S.got;
            if (nx && t >= xs[0].ms * 8) {
                long s = (t - xs[0].ms * 8) / 8000;
                if (s >= 0 && s < 64) { got_at[s] = S.got; nsec = s + 1; }
                if (a->phase == ANS_HST)
                    for (k = 0; k < 2; k++) {
                        int l = hst_brx_locked(&a->u.hst.det.rx[k]);
                        if (l != lk[k]) {
                            if (lk[k] >= 0 && !quiet)
                                printf("rec_hst: back receiver %d %s at %ld ms after the step\n", k ? 300 : 450,
                                       l ? "locked" : "lost its lock", (t - xs[0].ms * 8) / 8);
                            lk[k] = l;
                        }
                    }
            }
        }
        if (st.end != V90M_END_NONE) {
            ended = 1;
            printf("rec_hst: the modem ended the call (end %d) at %.3f s", (int)st.end, S.n / 8000.0);
            if (S.data14400 >= 0 && nx) printf(", %ld ms after the first step", (S.n - S.data14400 - xs[0].ms * 8) / 8);
            printf("\n");
            break;
        }
    }
    fclose(out);
    {
        v90m_ec_status es;
        v90m_ec_get_status(S.ec, &es);
        printf("rec_hst: %ld samples; data at 14400 from sample %ld (%.3f s of the call); transmit hash %08lx\n", nrec,
               S.data14400 >= 0 ? S.data14400 + d0 : -1L, S.data14400 / 8000.0, (unsigned long)txh);
        printf("rec_hst: MNP %s, hst %d rates 0x%02X back300 %d; the answerer got %lu octets, the caller %lu (%ld wrong)\n",
               es.protocol == V90M_EC_PROTO_MNP ? "up" : "not up", es.hst, es.hst_rates, es.hst_back300, S.got,
               (unsigned long)cr, cbad);
    }
    if (nx && S.data14400 >= 0) {
        long s;
        printf("rec_hst: octets the answerer got in each second from the first step:");
        for (s = 0; s < nsec; s++) printf(" %lu", got_at[s] - (s ? got_at[s - 1] : g0));
        printf("%s\n", ended ? " (ended)" : "");
    }
    hst_side_free(&S);
    return S.data14400 < 0;
}
