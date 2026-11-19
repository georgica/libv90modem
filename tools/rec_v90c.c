/* Record what the calling modem's A/D receives in a simulated V.90 call:
 * the library's caller (orig, V.90 offered) against the library's answerer
 * over test/v90link.h's line (2 km, the given clock offset, hybrid echo
 * -16 dB), until SECONDS seconds after data mode. rig/bench_v90c replays it
 * into the caller alone: what the caller does depends on nothing else (its
 * upstream data is the same 2^23 - 1 sequence there). Host only.
 *
 *   rec_v90c PPM SECONDS OUT [v92] [rt|rn]
 *
 * v92: V.92 on both sides (PCM upstream; the upstream through the
 * answerer's own V.92 line, test/v90link.h's up_line 1).
 * rt / rn: 2 s into data mode the caller retrains (orig_retrain) or
 * renegotiates its rate (orig_renegotiate, no ceiling); SECONDS then
 * counts from there.
 * OUT: "V90C0001" ("V92C0001" with v92), the sample at which the caller
 * reached data mode (32-bit little-endian), then every A/D sample as a
 * 16-bit little-endian value. With rt / rn the magic ends in 2 ("V90C0002")
 * and two more 32-bit values follow the first: the event (1 retrain, 2
 * renegotiation) and the sample before whose tick the caller was asked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "v90link.h"

static vl_t L;

static void put32(FILE *f, uint32_t v) { int i; for (i = 0; i < 4; i++) fputc((int)((v >> (8 * i)) & 0xff), f); }

int main(int argc, char **argv)
{
    vl_cfg c;
    vl_t *l = &L;
    FILE *f;
    int64_t t_data = -1, end;
    int32_t secs;
    int v92 = argc > 4 && strcmp(argv[4], "v92") == 0, ev = 0, ai;
    int64_t t_ev = -1;
    if (argc < 4) { fprintf(stderr, "usage: rec_v90c PPM SECONDS OUT [v92] [rt|rn]\n"); return 2; }
    for (ai = 4; ai < argc; ai++) {
        if (strcmp(argv[ai], "rt") == 0) ev = 1;
        if (strcmp(argv[ai], "rn") == 0) ev = 2;
    }
    secs = (int32_t)atoi(argv[2]);
    memset(&c, 0, sizeof c);
    c.loop_km = 2; c.noise_dbm0 = -75; c.ppm = atof(argv[1]); c.phase = 0.3; c.echo_db = -16;
    c.up_loss_db = 6; c.up_noise_dbm0 = -70; c.full = 1; c.caller_orig = 1; c.seed = 5;
    if (v92) { c.v92 = 1; c.caller_v92 = 1; c.up_line = 1; }
    vl_init(l, &c);
    f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 1; }
    fputs(v92 ? (ev ? "V92C0002" : "V92C0001") : (ev ? "V90C0002" : "V90C0001"), f);
    put32(f, 0);
    if (ev) { put32(f, 0); put32(f, 0); }
    end = (int64_t)8000 * 40;
    while (l->k < end) {
        uint16_t u;
        if (ev && t_data >= 0 && t_ev < 0 && l->k == t_data + 16000) {
            /* (the bench asks the same before the same tick) */
            if (ev == 1) (void)orig_retrain(&l->org); else (void)orig_renegotiate(&l->org, 0);
            t_ev = l->k;
            end = l->k + (int64_t)8000 * secs;
        }
        vl_tick(l);
        u = (uint16_t)l->last_rx;
        fputc(u & 0xff, f); fputc(u >> 8, f);
        if (t_data < 0 && orig_moving(&l->org)) { t_data = l->k; end = l->k + (int64_t)8000 * secs; }
        if (l->org.phase == ORIG_FAILED) break;
    }
    fseek(f, 8, SEEK_SET);
    put32(f, (uint32_t)(t_data < 0 ? 0 : t_data));
    if (ev) { put32(f, (uint32_t)ev); put32(f, (uint32_t)(t_ev < 0 ? 0 : t_ev)); }
    fclose(f);
    printf("rec_v90c: %" PRId64 " samples (%.2f s), %+g ppm, %s, data mode at sample %" PRId64 ", %d / %d bit/s down / up\n",
           l->k, (double)l->k / 8000.0, c.ppm, l->org.modulation == ORIG_MOD_V92 ? "V.92" : "V.90", t_data,
           l->org.rx_bitrate, l->org.bitrate);
    return t_data < 0;
}
