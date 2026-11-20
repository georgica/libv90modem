/* x2-peer bridge: our answering modem as the far end of an emulated
 * U.S. Robotics Courier (courier-emu, run outside this repository; see
 * README.md). Only the public API is used.
 *
 *   x2-bridge SOCKET [-p PAD] [-m MODS] [-s SECONDS]
 *
 * courier-emu's `run --line-link SOCKET --line-listen` puts the Courier's
 * telephone line on a UNIX socket: frames of a header (little-endian u32
 * instruction stamp, off-hook, ringing, call state, u16 sample count) and
 * that many 16-bit samples at 8000 Hz, each side sending its frame and then
 * taking the other's. With COURIER_LINE_DIGITAL=1 the samples are the
 * Courier's codec's, unscaled, and the far end sets the levels.
 *
 * This end is the network and our modem behind it: the Courier's loop
 * reaches a mu-law codec at the exchange (one sample for one octet, no
 * resampling), and the exchange reaches us digitally. So
 *   - Courier -> us: the codec sample scaled to the mu-law side, encoded
 *     to the nearest octet, given to v90m_process_g711;
 *   - us -> Courier: our octet through the optional digital pad (decoded,
 *     scaled in Q15 by 23198 (3 dB) or 16423 (6 dB) with rounding, encoded
 *     again to the nearest octet: tools/x2_pad.c's model), then decoded and
 *     scaled to the Courier's codec.
 * The levels: the Courier's codec (TI AC01) puts its full-scale sine at
 * +8.75 dBm, its DAA loses 11.4 dB on transmit ("-9 dBm maximum") and
 * nothing on receive; mu-law's full scale (32124 decoded) is +3.21 dBm0.
 * So the codec's samples reach the mu-law side 8.75 - 3.21 - 11.4 =
 * -5.86 dB, and mu-law reaches the codec 3.21 - 8.75 = -5.54 dB.
 *
 * The call: the Courier dials; when it says the line is ringing we go off
 * hook and say the call is answered, and our modem starts answering. A
 * V.42 endpoint (v90m_ec_*: LAPM, or start-stop when the Courier has no
 * error control) on our bit pump, started at connection, sends a line of
 * text once it is up and counts what arrives.
 *   -p PAD   the digital pad on our transmit path: 0 (default), 3 or 6 dB
 *   -m MODS  the modulations to offer beyond the defaults: "" or "none"
 *            (default), or "x2" (V90M_MOD_X2)
 *   -s SEC   give up after SEC seconds of line (default 600)
 * The log goes to stdout. Exit status 0 when our modem reached data mode
 * and its V.42 endpoint got data moving (LAPM or start-stop), 1 otherwise, 2 on a usage or socket error. */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "v90modem.h"

enum { CALL_IDLE, CALL_SEIZED, CALL_RINGING, CALL_ANSWERED, CALL_CLEARED };
static const char *const call_name[] = { "idle", "seized", "ringing", "answered", "cleared" };
static const char *const conn_name[] = { "none", "V.21", "V.22", "V.22bis", "V.90", "V.34", "V.32bis", "V.92", "Bell 103", "Bell 212A",
                                         "x2" };
static const char *const end_name[] = { "none", "start-up failed", "line lost", "far end cleared down", "we cleared down",
                                        "no answer tone", "no JM", "nothing in common", "retrain failed", "x2 failed" };

#define HDR      9          /* <IBBBH */
#define MAXN     4096       /* samples in a frame, at most */
#define RATE     8000
#define FRAME    160        /* 20 ms */
#define IPS      4348000L   /* courier-emu's instructions per second, for the stamp */

/* Q15 gains: Courier codec -> mu-law (-5.86 dB), mu-law -> codec (-5.54 dB),
   and the pads (0, -3, -6 dB) */
#define G_TO_ULAW   16690
#define G_TO_CODEC  17316
static const int32_t pad_gain[3] = { 32768, 23198, 16423 };

static long frames;
static v90m_t *modem;
static v90m_ec_t *ec;
static int ec_on, was_connected;
static const char *const ec_name[] = { "detecting", "establishing", "LAPM", "start-stop", "releasing", "disconnected" };

static double line_s(void) { return frames * (double)FRAME / RATE; }

static int16_t scale(int32_t v, int32_t q15)
{
    int32_t a = v < 0 ? -v : v;
    a = (int32_t)(((int64_t)a * q15 + 16384) >> 15);
    if (a > 32767)
        a = 32767;
    return (int16_t)(v < 0 ? -a : a);
}

static int  get_bit(void *u) { (void)u; return ec_on ? v90m_ec_tx_bit(ec) : 1; }
static void put_bit(void *u, int bit) { (void)u; if (ec_on) v90m_ec_rx_bit(ec, bit); }

static void on_event(void *u, v90m_event ev)
{
    v90m_state st;
    (void)u;
    v90m_get_state(modem, &st);
    switch (ev) {
    case V90M_EV_CONNECTED:
        printf("[%7.2f] ours: CONNECT %d/%d %s%s\n", line_s(), st.tx_bps, st.rx_bps, conn_name[st.modulation],
               st.used_v8 ? "" : " (no V.8)");
        if (!ec_on) {
            v90m_ec_cfg ecc;
            v90m_ec_cfg_default(&ecc, 0);
            ecc.detect = !st.odp_bypass;
            v90m_ec_reset(ec, &ecc);
            ec_on = 1;
        }
        was_connected = 1;
        break;
    case V90M_EV_RATES:
        printf("[%7.2f] ours: rates now %d/%d %s\n", line_s(), st.tx_bps, st.rx_bps, conn_name[st.modulation]);
        break;
    case V90M_EV_DATA_STOPPED:
        printf("[%7.2f] ours: data stopped\n", line_s());
        break;
    case V90M_EV_DATA_MOVING:
        printf("[%7.2f] ours: data moving\n", line_s());
        if (ec_on)
            v90m_ec_line_restored(ec);
        break;
    case V90M_EV_ENDED:
        printf("[%7.2f] ours: ended: %s\n", line_s(), end_name[st.end]);
        break;
    }
    fflush(stdout);
}

static int io_all(int fd, void *buf, size_t n, int out)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t r = out ? write(fd, p, n) : read(fd, p, n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* connect, retrying while courier-emu starts (it binds before it listens) */
static int line_open(const char *path, int wait_s)
{
    struct sockaddr_un a;
    struct timespec ts = { 0, 50000000 };
    int i;
    if (strlen(path) >= sizeof a.sun_path) {
        fprintf(stderr, "x2-bridge: socket path too long\n");
        return -1;
    }
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, path);
    for (i = 0; i < wait_s * 20; i++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0)
            return fd;
        close(fd);
        nanosleep(&ts, NULL);
    }
    fprintf(stderr, "x2-bridge: no line at %s\n", path);
    return -1;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

int main(int argc, char **argv)
{
    static uint8_t hdr[HDR], body[2 * MAXN], obuf[HDR + 2 * MAXN];
    static int16_t tx_lin[MAXN];
    uint8_t rx_oct[MAXN], tx_oct[MAXN], text[256];
    v90m_ec_cfg ecc;
    int ec_shown = -1, ec_up = 0;
    const char *sock, *mods = "";
    const char *hello = "\r\nlibv90modem x2-peer bridge here\r\n";
    int pad = 0, seconds = 600, fd, answered = 0, peer = CALL_IDLE, shown = -1, said = 0, err, i, ntx = FRAME;
    long got = 0, printable = 0;
    uint32_t stamp;
    double e_pre = 0, e_post = 0, e_n = 0;   /* 0 dBm0: RMS 15872 (mu-law's sine at 32124 is +3.17 dBm0) */
    v90m_cfg cfg;
    v90m_state st;

    for (i = 2; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-p"))
            pad = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-m"))
            mods = argv[i + 1];
        else if (!strcmp(argv[i], "-s"))
            seconds = atoi(argv[i + 1]);
        else
            break;
    }
    if (argc < 2 || i != argc || (pad != 0 && pad != 3 && pad != 6) || seconds <= 0) {
        fprintf(stderr, "usage: x2-bridge SOCKET [-p 0|3|6] [-m MODS] [-s SECONDS]\n");
        return 2;
    }
    if (*mods && strcmp(mods, "none") && strcmp(mods, "x2")) {
        fprintf(stderr, "x2-bridge: MODS=%s: \"\", \"none\" or \"x2\"\n", mods);
        return 2;
    }
    sock = argv[1];
    setvbuf(stdout, NULL, _IOLBF, 0);

    v90m_ec_cfg_default(&ecc, 0);
    ec = v90m_ec_create(&ecc, &err);
    v90m_cfg_default(&cfg);
    cfg.role = V90M_ROLE_ANSWER;
    cfg.getbit = get_bit;
    cfg.putbit = put_bit;
    cfg.on_event = on_event;
    if (!strcmp(mods, "x2"))
        cfg.modulations |= V90M_MOD_X2;
    modem = ec ? v90m_create(&cfg, &err) : NULL;
    if (!modem) {
        fprintf(stderr, "x2-bridge: v90m_create: %d\n", err);
        return 2;
    }
    if ((fd = line_open(sock, 120)) < 0)
        return 2;
    printf("x2-bridge: %s, pad %d dB, offering the defaults%s\n", v90m_version(), pad,
           cfg.modulations & V90M_MOD_X2 ? " and x2" : " (x2 not offered)");
    memset(tx_lin, 0, sizeof tx_lin);

    for (;;) {
        unsigned n, state;
        /* our frame: the samples worked out from the last one received */
        state = answered && peer < CALL_CLEARED ? CALL_ANSWERED : (unsigned)peer;
        stamp = (uint32_t)((int64_t)frames * FRAME * IPS / RATE);   /* wraps, as the field does */
        obuf[0] = (uint8_t)stamp;
        obuf[1] = (uint8_t)(stamp >> 8);
        obuf[2] = (uint8_t)(stamp >> 16);
        obuf[3] = (uint8_t)(stamp >> 24);
        obuf[4] = (uint8_t)answered;
        obuf[5] = 0;
        obuf[6] = (uint8_t)state;
        put16(obuf + 7, (unsigned)ntx);
        for (i = 0; i < ntx; i++)
            put16(obuf + HDR + 2 * i, (uint16_t)tx_lin[i]);
        if (io_all(fd, obuf, HDR + 2 * (size_t)ntx, 1))
            break;
        /* theirs */
        if (io_all(fd, hdr, HDR, 0))
            break;
        peer = hdr[6];
        n = hdr[7] | (unsigned)hdr[8] << 8;
        if (n > MAXN || io_all(fd, body, 2 * (size_t)n, 0))
            break;
        frames++;
        if (peer != shown && peer <= CALL_CLEARED) {
            printf("[%7.2f] line: %s (Courier %s hook)\n", line_s(), call_name[peer], hdr[4] ? "off" : "on");
            shown = peer;
        }
        if (!answered && peer >= CALL_RINGING && peer < CALL_CLEARED) {
            answered = 1;
            printf("[%7.2f] line: we answer\n", line_s());
        }
        ntx = (int)n;
        if (answered && peer < CALL_CLEARED) {
            for (i = 0; i < (int)n; i++)
                rx_oct[i] = v90m_g711_encode(0, scale((int16_t)(uint16_t)(body[2 * i] | body[2 * i + 1] << 8), G_TO_ULAW));
            v90m_process_g711(modem, rx_oct, tx_oct, n);
            for (i = 0; i < (int)n; i++) {
                uint8_t o = tx_oct[i];
                if (pad)
                    o = v90m_g711_encode(0, scale(v90m_g711_decode(0, o), pad_gain[pad / 3]));
                if (was_connected) {   /* our signal before and after the pad, in data mode */
                    double a = v90m_g711_decode(0, tx_oct[i]), b = v90m_g711_decode(0, o);
                    e_pre += a * a;
                    e_post += b * b;
                    e_n++;
                }
                tx_lin[i] = scale(v90m_g711_decode(0, o), G_TO_CODEC);
            }
        } else
            memset(tx_lin, 0, sizeof tx_lin);
        v90m_get_state(modem, &st);
        if (ec_on) {
            size_t k, r;
            int es;
            v90m_ec_tick(ec, n * 1000 / RATE);
            es = v90m_ec_state(ec);
            if (es != ec_shown) {
                v90m_ec_status ecs;
                v90m_ec_get_status(ec, &ecs);
                printf("[%7.2f] ours: V.42 %s", line_s(), es >= 0 && es <= V90M_EC_DISCONNECTED ? ec_name[es] : "?");
                if (es == V90M_EC_CONNECTED)
                    printf(" (compression %d)", ecs.comp_alg);
                if (es == V90M_EC_DISCONNECTED)
                    printf(" (reason %d)", ecs.reason);
                printf("\n");
                ec_shown = es;
            }
            if (es == V90M_EC_CONNECTED || es == V90M_EC_ASYNC) {
                ec_up = 1;
                if (!said) {
                    v90m_ec_write(ec, (const uint8_t *)hello, strlen(hello));
                    said = 1;
                }
            }
            while ((r = v90m_ec_read(ec, text, sizeof text)) > 0) {
                got += (long)r;
                for (k = 0; k < r; k++)
                    printable += text[k] >= 32 && text[k] < 127;
            }
        }
        if (peer == CALL_CLEARED && answered) {
            printf("[%7.2f] line: the Courier hung up\n", line_s());
            break;
        }
        if (line_s() >= seconds) {
            printf("[%7.2f] x2-bridge: %d s of line, giving up\n", line_s(), seconds);
            break;
        }
    }
    close(fd);
    v90m_get_state(modem, &st);
    printf("[%7.2f] ours: %s, %s %d/%d, %s, V.42 %s; %ld octets received (%ld printable), %s\n", line_s(),
           was_connected ? "data mode reached" : "no data mode", conn_name[st.modulation], st.tx_bps, st.rx_bps,
           st.used_v8 ? "V.8" : "no V.8", ec_up ? "up" : "never up", got, printable, end_name[st.end]);
    v90m_destroy(modem);
    if (e_pre > 0 && e_post > 0)
        printf("ours: in data mode, sent %.1f dBm0 before the pad, %.1f dBm0 after it\n",
               10 * log10(e_pre / e_n) - 83.96, 10 * log10(e_post / e_n) - 83.96);
    v90m_ec_destroy(ec);
    return was_connected && ec_up ? 0 : 1;
}
