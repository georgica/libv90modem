/* Test peer: V.8bis's responding station. See v8bisc.h. */
#include <string.h>
#include <math.h>
#include "v8bisc.h"

#define MS(ms) ((long)(ms) * 8)
#define TWO_PI 6.283185307179586
#define BLOCK 80

enum { S_CRE, S_CRE_IN, S_CRD, S_CL, S_MS, S_ACK, S_OFF };

/* the FCS (V.8bis 7.2.7): x^16 + x^12 + x^5 + 1, preset to ones, octets
 * least significant bit first; the ones' complement is sent, low octet
 * first, and a good frame leaves 0xF0B8 */
static unsigned crc16(const uint8_t *o, int n)
{
    unsigned r = 0xffff;
    int k, j;
    for (k = 0; k < n; k++) {
        r ^= o[k];
        for (j = 0; j < 8; j++) r = (r & 1) ? (r >> 1) ^ 0x8408 : r >> 1;
    }
    return r;
}

static int put(uint8_t *bits, int nb, int bit) { bits[nb] = (uint8_t)bit; return nb + 1; }

/* 40 ones, four flags, the octets and the FCS with a zero after five ones
 * (7.2.8), two flags */
static int frame(const uint8_t *o, int n, uint8_t *bits)
{
    uint8_t f[V8BISC_MAXOCT + 2];
    unsigned fcs;
    int nb = 0, k, j, ones = 0;
    memcpy(f, o, (size_t)n);
    fcs = ~crc16(o, n) & 0xffff;
    f[n] = (uint8_t)(fcs & 0xff); f[n + 1] = (uint8_t)(fcs >> 8);
    for (k = 0; k < 40; k++) nb = put(bits, nb, 1);
    for (k = 0; k < 4; k++) for (j = 0; j < 8; j++) nb = put(bits, nb, (0x7e >> j) & 1);
    for (k = 0; k < n + 2; k++)
        for (j = 0; j < 8; j++) {
            int b = (f[k] >> j) & 1;
            nb = put(bits, nb, b);
            ones = b ? ones + 1 : 0;
            if (ones == 5) { nb = put(bits, nb, 0); ones = 0; }
        }
    for (k = 0; k < 2; k++) for (j = 0; j < 8; j++) nb = put(bits, nb, (0x7e >> j) & 1);
    return nb;
}

static void listen1(v8bisc_t *b)
{
    v21demod_init(&b->dem, V21L_MARK, V21L_SPACE, 8000, 300);
    b->squelch = v21demod_energy_of(&b->dem, 110);
    b->level1 = 1; b->clk = 0;
    b->rbits = b->rones = b->rin = 0;
}

/* one received bit: the octet count of a good frame when its closing flag
 * ends, -1 a bad one, 0 otherwise */
static int rx_bit(v8bisc_t *b, int bit)
{
    int n, r = 0;
    if (bit) {
        if (++b->rones >= 7) { b->rin = 0; b->rbits = 0; return 0; }     /* abort */
    } else {
        if (b->rones == 5) { b->rones = 0; return 0; }    /* an inserted zero */
        if (b->rones == 6) {
            /* a flag: its 0 and six 1s went in as data */
            n = b->rbits - 7;
            if (b->rin && n > 0) {
                if (n % 8 || n < 24 || crc16(b->rbuf, n / 8) != 0xf0b8) r = -1;
                else r = n / 8 - 2;
            }
            b->rin = 1; b->rbits = 0; b->rones = 0;
            return r;
        }
        b->rones = 0;
    }
    if (b->rin) {
        if (b->rbits >= (int)sizeof b->rbuf * 8) { b->rin = 0; b->rbits = 0; return -1; }
        if (bit) b->rbuf[b->rbits >> 3] |= (uint8_t)(1 << (b->rbits & 7));
        else b->rbuf[b->rbits >> 3] &= (uint8_t)~(1 << (b->rbits & 7));
        b->rbits++;
    }
    return 0;
}

/* channel 1: the level, a bit clock from its edges, the frame receiver */
static int listen_tick(v8bisc_t *b, int16_t x)
{
    int level = v21demod_tick(&b->dem, x);
    if (b->dem.energy < b->squelch) level = 1;
    if (level != b->level1) { b->level1 = level; b->clk = 4000; }
    b->clk += 300;
    if (b->clk < 8000) return 0;
    b->clk -= 8000;
    return rx_bit(b, level);
}

/* the dual tone 1375 + 2002 Hz in 10 ms blocks: 1 at the end of a block
 * with both (each at least a quarter of its power), 0 without, -1 within
 * a block; quiet: the block's RMS below 50 */
static int dual_block(v8bisc_t *b, int16_t x, int *quiet)
{
    static const double f[2] = { 1375, 2002 };
    int k, on = 1;
    for (k = 0; k < 2; k++) {
        b->gr[k] += x * cos(b->gph[k]); b->gi[k] += x * sin(b->gph[k]);
        b->gph[k] += TWO_PI * f[k] / 8000;
        if (b->gph[k] > TWO_PI) b->gph[k] -= TWO_PI;
    }
    b->gpw += (double)x * x;
    if (++b->gn < BLOCK) return -1;
    for (k = 0; k < 2; k++) {
        double sh = 2 * (b->gr[k] * b->gr[k] + b->gi[k] * b->gi[k]) / (BLOCK * (b->gpw + 1));
        if (sh < 0.25) on = 0;
        b->gr[k] = b->gi[k] = 0;
    }
    *quiet = b->gpw < (double)BLOCK * 2500;
    if (*quiet) on = 0;
    b->gpw = 0; b->gn = 0;
    return on;
}

void v8bisc_init(v8bisc_t *b, const uint8_t *ms, int n, double level)
{
    memset(b, 0, sizeof *b);
    if (n < 1) n = 1;
    if (n > V8BISC_MAXOCT) n = V8BISC_MAXOCT;
    memcpy(b->ms, ms, (size_t)n); b->ms_len = n;
    b->level = level;
    b->st = S_CRE;
}

int v8bisc_got(const v8bisc_t *b) { return b->got; }

double v8bisc_tick(v8bisc_t *b, int16_t x)
{
    int r, quiet = 0, done;
    double y = 0;
    b->now++;
    switch (b->st) {
    case S_CRE:
        r = dual_block(b, x, &quiet);
        if (r >= 0) b->gblocks = r ? b->gblocks + 1 : 0;
        if (b->gblocks >= 10) { b->got_cre = 1; b->st = S_CRE_IN; }
        break;
    case S_CRE_IN:
        if (dual_block(b, x, &quiet) >= 0 && quiet) { b->at = b->now + MS(175); b->st = S_CRD; }
        break;
    case S_CRD:
        if (b->now >= b->at) {
            long k = b->now - b->at;
            if (k < MS(400)) {
                /* each tone of RMS level / sqrt 2: the pair's RMS is level */
                y = b->level * (sin(b->ph[0]) + sin(b->ph[1]));
                b->ph[0] += TWO_PI * 1529 / 8000; b->ph[1] += TWO_PI * 2225 / 8000;
            } else if (k < MS(500)) {
                y = b->level * 1.41421356 * sin(b->ph[2]);
                b->ph[2] += TWO_PI * 1900 / 8000;
            } else {
                listen1(b); b->st = S_CL; b->at = b->now + MS(3000);
            }
        }
        break;
    case S_CL:
        r = listen_tick(b, x);
        if (r > 0 && (b->rbuf[0] & 15) == 2) {
            b->got_cl = 1;
            b->nbits = frame(b->ms, b->ms_len, b->bits); b->pos = 0;
            v21mod_init(&b->mod, V21H_MARK, V21H_SPACE, 8000, 300, (int16_t)lround(b->level * 1.41421356));
            b->at = b->now + MS(10);
            b->st = S_MS;
        } else if (b->now >= b->at) {
            b->st = S_OFF;
        }
        break;
    case S_MS:
        if (b->now < b->at) break;
        y = v21mod_tick(&b->mod, b->bits[b->pos], &done);
        if (done && ++b->pos >= b->nbits) { b->sent_ms = 1; listen1(b); b->at = b->now + MS(2000); b->st = S_ACK; }
        break;
    case S_ACK:
        r = listen_tick(b, x);
        if (r == 1 && (b->rbuf[0] == 0x14 || ((b->rbuf[0] & 15) >= 8 && (b->rbuf[0] & 15) <= 11))) {
            b->got = b->rbuf[0]; b->st = S_OFF;
        } else if (r != 0 || b->now >= b->at) {
            b->st = S_OFF;
        }
        break;
    default:
        break;
    }
    return y;
}
