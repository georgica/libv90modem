/* k56-peer bridge: our answering modem as the far end of Motorola's SM56
 * soft modem (D-Modem's half_sm56 running sm56.ko, both outside this
 * repository; see README.md). Only the public API is used.
 *
 *   k56-bridge HALF_SM56 OUTDIR [-m MODS] [-a AT] [-r RBS] [-l FRAC|none]
 *              [-d DOWN] [-s SECONDS] [-k KB] [-n]
 *
 * half_sm56 takes the line on its fd 3 and gives it on its fd 4 (16-bit
 * samples, native order, 8000 Hz), says "created sm56: <pty>" on stdout and
 * takes AT commands on that pty. It writes 192 samples first and then one
 * for each it reads; this bridge answers each block it gets with as many
 * samples, so the line runs in lock step, as fast as the two can go.
 *
 * This end is the network and our modem behind it, a digital line with no
 * loss: SM56's samples to the nearest mu-law octet into v90m_process_g711,
 * our octets decoded and, unless -l none, through a model of an analogue
 * path to SM56's codec (-l FRAC, default 0: a delay of 1 + FRAC samples and
 * a first-order high pass at 300 Hz, a line transformer's; our own model,
 * see analog_init). With -r RBS the octets to SM56 are robbed as netsim's
 * NET_RBS_TO: in the samples whose count (from the line's first) mod 6 has
 * its bit in RBS, the octet's least significant bit is set to 1, before
 * the analogue path.
 *
 * The line model decides whether SM56 gets through our TRN2A (README.md,
 * "Known"): with the codes as they are (-l none), with a second-order high
 * pass, or with some fractions and delays, SM56 stays in TRN2A (front end
 * state 19) and gives up with cause 0x21, against D-Modem's own server as
 * well. The default is the model under which every call we tried passed.
 *
 * The call: ATZ, ATX3 (blind dial), the AT+MS command, ATD; 3 s of line
 * after ATD we answer (until then SM56 hears silence and we hear nothing).
 * After the CONNECTs each side sends KB KiB (default 4) of its own pattern
 * (xorshift32, seeds below) through its error control: SM56's through the
 * pty, ours through a V.42 endpoint (v90m_ec) on our bit pump. What each
 * side sent and received goes to OUTDIR (ours-sent.bin, sm56-recv.bin,
 * sm56-sent.bin, ours-recv.bin) for run.sh to compare; OUTDIR/call.rec is
 * the call from our answer on, as v90m-replay takes it (per 10 ms, 80
 * octets heard and the 80 we sent). Then +++ and AT&V1
 * to SM56 (its report of the call), and the bridge ends.
 *   -m MODS   the modulations to offer beyond the defaults: "" or "none"
 *             (default), or "k56" (V90M_MOD_K56, when the library has it)
 *   -a AT     SM56's modulation command (default AT+MS=K56FLEX,0: K56flex,
 *             and V.34 from V.8 when no V.8bis answers it; with automode,
 *             AT+MS=K56FLEX,1, its CM offers V.90 as well)
 *   -r RBS    robbed bits toward SM56 (hex or decimal, 0..63; default 0)
 *   -l FRAC   the loop's fractional delay (0..1), or "none" (default 0)
 *   -d DOWN   our max_down_bps (default 0: none)
 *   -s SEC    give up after SEC seconds of line (default 120)
 *   -k KB     KiB each way after CONNECT (default 4)
 *   -n        strict renegotiation: the call must also see a rate change
 *             (V90M_EV_RATES) after the first burst, and a second burst
 *             of KB KiB each way once data moves again
 * Exit status: 0 when both sides reached data mode and every byte due was
 * received each way (run.sh compares them); 1 otherwise; 2 on a usage,
 * process or pty error. */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "v90modem.h"

#define RATE       8000
#define MAXN       4096          /* samples in one read, at most */
#define SEED_OURS  0x2545F491u   /* our pattern to SM56 */
#define SEED_SM56  0x9E3779B9u   /* SM56's pattern to us */

static const char *const conn_name[] = { "none", "V.21", "V.22", "V.22bis", "V.90", "V.34", "V.32bis", "V.92",
                                         "Bell 103", "Bell 212A", "x2", "K56flex" };
static const char *const end_name[] = { "none", "start-up failed", "line lost", "far end cleared down",
                                        "we cleared down", "no answer tone", "no JM", "nothing in common",
                                        "retrain failed", "x2 failed", "K56flex failed" };
static const char *const ec_name[] = { "detecting", "establishing", "LAPM", "start-stop", "releasing", "disconnected" };
#define NAME(t, i) ((unsigned)(i) < sizeof t / sizeof t[0] ? t[(unsigned)(i)] : "?")

static unsigned long long line_n;   /* samples each way so far */
static v90m_t *modem;
static v90m_ec_t *ec;
static int ec_on, connected, moving, rate_changes;

static double line_s(void) { return (double)line_n / RATE; }

/* The analogue path to SM56's codec (-l): the codes reach it late (1 + frac
 * samples) and without their lowest frequencies. Our model: a 4-point
 * Lagrange interpolator for the delay (at frac 0 a plain one-sample delay),
 * then a first-order high pass at 300 Hz (bilinear transform). A
 * second-order high pass here (k56 2 to k56 9) spoils SM56's PCM decisions
 * above 32000 bit/s (k56 9b). */
static int analog_on;
static double lag_c[4];             /* weights on x[n], x[n-1], x[n-2], x[n-3] */
static double lag_x[4];             /* the last 4 inputs, newest first */
static double hp_b0, hp_a1, hp_in, hp_out;

static void analog_init(double frac)
{
    const double pi = 3.14159265358979323846;
    double d = 1 + frac, t;
    int k, j;
    for (k = 0; k < 4; k++) {   /* Lagrange basis polynomial k at d */
        lag_c[k] = 1;
        for (j = 0; j < 4; j++)
            if (j != k)
                lag_c[k] *= (d - j) / (double)(k - j);
    }
    t = tan(pi * 300 / RATE);           /* y = b0 (x - x[n-1]) - a1 y[n-1] */
    hp_b0 = 1 / (1 + t);
    hp_a1 = (t - 1) / (t + 1);
    analog_on = 1;
}

static int16_t analog_sample(int16_t x)
{
    double v, y;
    lag_x[3] = lag_x[2];
    lag_x[2] = lag_x[1];
    lag_x[1] = lag_x[0];
    lag_x[0] = x;
    v = lag_c[0] * lag_x[0] + lag_c[1] * lag_x[1] + lag_c[2] * lag_x[2] + lag_c[3] * lag_x[3];
    y = hp_b0 * (v - hp_in) - hp_a1 * hp_out;
    hp_in = v;
    hp_out = y;
    y = floor(y + 0.5);
    return (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
}

/* a pattern stream and what came of it */
typedef struct {
    uint32_t seed;
    long sent, recv;                /* octets sent, received */
    FILE *fsent, *frecv;
} stream;
static stream to_sm56, to_ours;

static uint8_t next_octet(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (uint8_t)*s;
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
        printf("[%7.2f] ours: CONNECT %d/%d %s%s\n", line_s(), st.tx_bps, st.rx_bps, NAME(conn_name, st.modulation),
               st.used_v8 ? "" : " (no V.8)");
        if (!ec_on) {
            v90m_ec_cfg ecc;
            v90m_ec_cfg_default(&ecc, 0);
            ecc.detect = !st.odp_bypass;
            v90m_ec_reset(ec, &ecc);
            ec_on = 1;
        }
        connected = moving = 1;
        break;
    case V90M_EV_RATES:
        printf("[%7.2f] ours: rates now %d/%d %s\n", line_s(), st.tx_bps, st.rx_bps, NAME(conn_name, st.modulation));
        rate_changes++;
        break;
    case V90M_EV_DATA_STOPPED:
        printf("[%7.2f] ours: data stopped\n", line_s());
        moving = 0;
        if (ec_on)
            v90m_ec_line_stopped(ec);      /* the falling edge, within its sample */
        break;
    case V90M_EV_DATA_MOVING:
        printf("[%7.2f] ours: data moving\n", line_s());
        moving = 1;
        if (ec_on)
            v90m_ec_line_restored(ec);
        break;
    case V90M_EV_ENDED:
        printf("[%7.2f] ours: ended: %s\n", line_s(), NAME(end_name, st.end));
        break;
    }
    fflush(stdout);
}

static int write_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* SM56's terminal: the text since the last command, and its data */
static int pty = -1;
static char text[8192];
static size_t ntext, shown;     /* text[0 .. shown) printed already */
static int online;                  /* after SM56's CONNECT line */
static char connect_line[256];

static void pty_send(const char *s)
{
    printf("[%7.2f] sm56 <- %s\n", line_s(), s);
    ntext = shown = 0;
    text[0] = 0;
    if (write_all(pty, s, strlen(s)) || write_all(pty, "\r", 1))
        printf("[%7.2f] sm56: the pty refused a command\n", line_s());
}

/* what SM56 gave its terminal: command text, or (online) data */
static void pty_take(const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (online) {
            if (to_sm56.recv < to_sm56.sent) {   /* only what we sent counts as data */
                fputc(b[i], to_sm56.frecv);
                to_sm56.recv++;
            }
            continue;
        }
        if (ntext + 1 < sizeof text) {
            text[ntext++] = (char)b[i];
            text[ntext] = 0;
        }
        if (b[i] == '\n') {   /* a line: show it; CONNECT puts SM56 online */
            char *l = text + shown, *c;
            size_t k = strcspn(l, "\r\n");
            while (*l) {
                if (k)
                    printf("[%7.2f] sm56: %.*s\n", line_s(), (int)k, l);
                l += k;
                l += strspn(l, "\r\n");
                k = strcspn(l, "\r\n");
            }
            c = strstr(text + shown, "CONNECT");
            shown = ntext;
            if (c && !online) {
                k = strcspn(c, "\r\n");
                if (k >= sizeof connect_line)
                    k = sizeof connect_line - 1;
                memcpy(connect_line, c, k);
                connect_line[k] = 0;
                online = 1;
            }
        }
    }
}

static int pty_has(const char *s) { return strstr(text, s) != NULL; }

/* our pattern into the V.42 endpoint, SM56's into its pty */
static void feed(long want)
{
    uint8_t b[256];
    size_t k, n;
    if (ec_on && (v90m_ec_state(ec) == V90M_EC_CONNECTED || v90m_ec_state(ec) == V90M_EC_ASYNC))
        while (to_sm56.sent < want && (n = v90m_ec_write_space(ec)) > 0) {
            if (n > sizeof b)
                n = sizeof b;
            if ((long)n > want - to_sm56.sent)
                n = (size_t)(want - to_sm56.sent);
            for (k = 0; k < n; k++)
                b[k] = next_octet(&to_sm56.seed);
            n = v90m_ec_write(ec, b, n);
            fwrite(b, 1, n, to_sm56.fsent);
            to_sm56.sent += (long)n;
            if (n == 0)
                break;
        }
    while (online && to_ours.sent < want) {
        ssize_t w;
        n = sizeof b;
        if ((long)n > want - to_ours.sent)
            n = (size_t)(want - to_ours.sent);
        {
            uint32_t s = to_ours.seed;
            for (k = 0; k < n; k++)
                b[k] = next_octet(&s);
            w = write(pty, b, n);
            if (w <= 0)
                break;
            for (k = 0; k < (size_t)w; k++)   /* the stream moves on by what the pty took */
                next_octet(&to_ours.seed);
        }
        fwrite(b, 1, (size_t)w, to_ours.fsent);
        to_ours.sent += (long)w;
    }
    if (ec_on) {
        size_t r;
        while ((r = v90m_ec_read(ec, b, sizeof b)) > 0) {
            if (to_ours.recv + (long)r > to_ours.sent)   /* (never more than SM56 was given) */
                r = to_ours.recv < to_ours.sent ? (size_t)(to_ours.sent - to_ours.recv) : 0;
            fwrite(b, 1, r, to_ours.frecv);
            to_ours.recv += (long)r;
        }
    }
}

/* call.rec: blocks of 80 heard and 80 sent */
static FILE *frec;
static uint8_t rec_h[80], rec_s[80];
static int rec_n;
static void rec_put(uint8_t heard, uint8_t sent)
{
    rec_h[rec_n] = heard;
    rec_s[rec_n] = sent;
    if (++rec_n == 80) {
        fwrite(rec_h, 1, 80, frec);
        fwrite(rec_s, 1, 80, frec);
        rec_n = 0;
    }
}

static FILE *open_out(const char *dir, const char *name)
{
    char p[4096];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "wb");
    if (!f)
        perror(p);
    return f;
}

int main(int argc, char **argv)
{
    static int16_t in[MAXN], out[MAXN];
    static uint8_t rbuf[2 * MAXN + 1], rx_oct[MAXN], tx_oct[MAXN];
    const char *half, *outdir, *mods = "", *at = NULL, *loop = NULL;
    int rbs = 0, down = 0, seconds = 120, kb = 4, strict = 0, err, i, to_half[2], from_half[2], said[2];
    int step = 0, answered = 0, ec_shown = -1, pass;
    size_t rpend = 0;
    unsigned long long atd_at = 0, step_at = 0, done_at = 0;
    unsigned long ms_done = 0;
    long want;
    pid_t child;
    char name[256];
    FILE *fc;
    v90m_cfg cfg;
    v90m_ec_cfg ecc;
    v90m_state st;
    struct termios tio;

    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) {
            strict = 1;
            continue;
        }
        if (i + 1 >= argc)
            break;
        if (!strcmp(argv[i], "-m"))
            mods = argv[++i];
        else if (!strcmp(argv[i], "-a"))
            at = argv[++i];
        else if (!strcmp(argv[i], "-r"))
            rbs = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-l"))
            loop = argv[++i];
        else if (!strcmp(argv[i], "-d"))
            down = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s"))
            seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k"))
            kb = atoi(argv[++i]);
        else
            break;
    }
    if (argc < 3 || i != argc || rbs < 0 || rbs > 63 || down < 0 || seconds <= 0 || kb <= 0) {
        fprintf(stderr, "usage: k56-bridge HALF_SM56 OUTDIR [-m none|k56] [-a AT] [-r RBS] [-l FRAC|none] [-d DOWN] "
                        "[-s SECONDS] [-k KB] [-n]\n");
        return 2;
    }
    if (*mods && strcmp(mods, "none") && strcmp(mods, "k56")) {
        fprintf(stderr, "k56-bridge: MODS=%s: \"\", \"none\" or \"k56\"\n", mods);
        return 2;
    }
    if (!loop)
        loop = "0";
    if (strcmp(loop, "none")) {
        double f = atof(loop);
        if (f < 0 || f > 1) {
            fprintf(stderr, "k56-bridge: -l %s: 0..1 or none\n", loop);
            return 2;
        }
        analog_init(f);
    }
    half = argv[1];
    outdir = argv[2];
    setvbuf(stdout, NULL, _IOLBF, 0);
    want = (long)kb * 1024;

    v90m_cfg_default(&cfg);
    cfg.role = V90M_ROLE_ANSWER;
    cfg.getbit = get_bit;
    cfg.putbit = put_bit;
    cfg.on_event = on_event;
    cfg.max_down_bps = down;
    if (!strcmp(mods, "k56")) {
#ifdef V90M_MOD_K56
        cfg.modulations |= V90M_MOD_K56;
#else
        fprintf(stderr, "k56-bridge: MODS=k56: this library has no V90M_MOD_K56 yet\n");
        return 2;
#endif
    }
    if (!at)
        at = "AT+MS=K56FLEX,0";
    v90m_ec_cfg_default(&ecc, 0);
    ec = v90m_ec_create(&ecc, &err);
    modem = ec ? v90m_create(&cfg, &err) : NULL;
    if (!modem) {
        fprintf(stderr, "k56-bridge: v90m_create: %d\n", err);
        return 2;
    }
    to_sm56.seed = SEED_OURS;
    to_ours.seed = SEED_SM56;
    to_sm56.fsent = open_out(outdir, "ours-sent.bin");
    to_sm56.frecv = open_out(outdir, "sm56-recv.bin");
    to_ours.fsent = open_out(outdir, "sm56-sent.bin");
    to_ours.frecv = open_out(outdir, "ours-recv.bin");
    frec = open_out(outdir, "call.rec");
    if (!to_sm56.fsent || !to_sm56.frecv || !to_ours.fsent || !to_ours.frecv || !frec)
        return 2;

    /* half_sm56: our pipes on its fds 3 and 4, its stdout to us (the pty's
       name), its stderr to OUTDIR/sm56.log */
    if (pipe(to_half) || pipe(from_half) || pipe(said)) {
        perror("pipe");
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    child = fork();
    if (child < 0) {
        perror("fork");
        return 2;
    }
    if (child == 0) {
        char p[4096];
        int lg;
        snprintf(p, sizeof p, "%s/sm56.log", outdir);
        lg = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (lg >= 0)
            dup2(lg, 2);
        dup2(said[1], 1);
        dup2(to_half[0], 3);
        dup2(from_half[1], 4);
        for (i = 5; i < 64; i++)
            close(i);
        execl(half, half, (char *)NULL);
        perror(half);
        _exit(127);
    }
    close(to_half[0]);
    close(from_half[1]);
    close(said[1]);
    fc = fdopen(said[0], "r");
    name[0] = 0;
    while (fc && fgets(name, sizeof name, fc) && strncmp(name, "created sm56: ", 14))
        ;
    if (strncmp(name, "created sm56: ", 14)) {
        fprintf(stderr, "k56-bridge: %s did not say its pty (see %s/sm56.log)\n", half, outdir);
        kill(child, SIGKILL);
        return 2;
    }
    name[strcspn(name, "\r\n")] = 0;
    fcntl(said[0], F_SETFL, fcntl(said[0], F_GETFL) | O_NONBLOCK);
    pty = open(name + 14, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (pty < 0 || tcgetattr(pty, &tio)) {
        perror(name + 14);
        kill(child, SIGKILL);
        return 2;
    }
    tio.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tio.c_oflag &= ~(tcflag_t)OPOST;
    tio.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tio.c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    tio.c_cflag |= CS8;
    tcsetattr(pty, TCSANOW, &tio);
    printf("k56-bridge: %s; SM56 (%s) on %s; offering the defaults%s; %s, robbed bits %#04x, loop %s, max_down %d\n",
           v90m_version(), half, name + 14, strcmp(mods, "k56") ? " (K56flex not offered)" : " and K56flex", at,
           rbs, loop, down);

    pass = 0;
    for (;;) {
        struct pollfd p[2];
        ssize_t r;
        int n;
        uint8_t tb[512];
        p[0].fd = from_half[0];
        p[0].events = POLLIN;
        p[1].fd = pty;
        p[1].events = POLLIN;
        if (poll(p, 2, 10000) <= 0) {
            printf("[%7.2f] k56-bridge: SM56 silent for 10 s of wall time\n", line_s());
            break;
        }
        while ((r = read(pty, tb, sizeof tb)) > 0)
            pty_take(tb, (size_t)r);
        while ((r = read(said[0], tb, sizeof tb)) > 0)   /* (half_sm56's stdout after its pty's name) */
            fwrite(tb, 1, (size_t)r, stdout);
        if (!(p[0].revents & (POLLIN | POLLHUP)))
            continue;
        r = read(from_half[0], rbuf + rpend, sizeof rbuf - 1 - rpend);
        if (r <= 0) {
            printf("[%7.2f] k56-bridge: SM56's line ended\n", line_s());
            break;
        }
        rpend += (size_t)r;
        n = (int)(rpend / 2);
        if (n == 0)
            continue;
        memcpy(in, rbuf, 2 * (size_t)n);
        memmove(rbuf, rbuf + 2 * n, rpend - 2 * (size_t)n);
        rpend -= 2 * (size_t)n;
        if (answered) {
            for (i = 0; i < n; i++)
                rx_oct[i] = v90m_g711_encode(0, in[i]);
            v90m_process_g711(modem, rx_oct, tx_oct, (size_t)n);
            for (i = 0; i < n; i++) {
                uint8_t o = tx_oct[i];
                rec_put(rx_oct[i], o);
                if ((rbs >> ((line_n + (unsigned)i) % 6)) & 1)
                    o |= 1;
                out[i] = v90m_g711_decode(0, o);
                if (analog_on)
                    out[i] = analog_sample(out[i]);
            }
        } else
            memset(out, 0, 2 * (size_t)n);
        if (write_all(to_half[1], out, 2 * (size_t)n)) {
            printf("[%7.2f] k56-bridge: SM56's line closed\n", line_s());
            break;
        }
        line_n += (unsigned)n;

        /* error control's timers, in line time */
        if (ec_on) {
            unsigned long ms = (unsigned long)(line_n * 1000 / RATE);
            int es;
            v90m_ec_tick(ec, (unsigned)(ms - ms_done));
            ms_done = ms;
            es = v90m_ec_state(ec);
            if (es != ec_shown) {
                v90m_ec_status s;
                v90m_ec_get_status(ec, &s);
                printf("[%7.2f] ours: V.42 %s", line_s(), NAME(ec_name, es));
                if (es == V90M_EC_CONNECTED)
                    printf(" (compression %d)", s.comp_alg);
                if (es == V90M_EC_DISCONNECTED)
                    printf(" (reason %d)", s.reason);
                printf("\n");
                ec_shown = es;
            }
        } else
            ms_done = (unsigned long)(line_n * 1000 / RATE);

        /* SM56's commands */
        switch (step) {
        case 0: pty_send("ATZ"); step = 1; break;
        case 1: if (pty_has("OK")) { pty_send("ATX3"); step = 2; } break;
        case 2: if (pty_has("OK")) { pty_send(at); step = 3; } break;
        case 3:
            if (pty_has("ERROR")) {
                printf("[%7.2f] k56-bridge: SM56 refused %s\n", line_s(), at);
                step = 99;
            } else if (pty_has("OK")) {
                pty_send("ATD");
                atd_at = line_n;
                step = 4;
            }
            break;
        case 4:   /* we answer 3 s after ATD; then wait for both CONNECTs */
            if (!answered && line_n >= atd_at + 3 * RATE) {
                answered = 1;
                printf("[%7.2f] line: we answer\n", line_s());
            }
            if (pty_has("NO CARRIER") || pty_has("NO DIALTONE") || pty_has("BUSY")) {
                printf("[%7.2f] k56-bridge: SM56 gave up\n", line_s());
                step = 99;
            } else if (online && connected)
                step = 5;
            break;
        case 5:   /* the first burst each way */
            if (moving)
                feed(want);
            if (to_sm56.recv >= want && to_ours.recv >= want) {
                printf("[%7.2f] k56-bridge: %ld octets each way\n", line_s(), want);
                step = strict ? 6 : 8;
                step_at = line_n;
            }
            break;
        case 6:   /* strict: a rate change, then data moving again */
            if (rate_changes > 0 && moving) {
                printf("[%7.2f] k56-bridge: a rate change; the second burst\n", line_s());
                step = 7;
            }
            break;
        case 7:
            if (moving)
                feed(2 * want);
            if (to_sm56.recv >= 2 * want && to_ours.recv >= 2 * want) {
                printf("[%7.2f] k56-bridge: %ld octets each way after the rate change\n", line_s(), want);
                step = 8;
                step_at = line_n;
            }
            break;
        case 8:   /* SM56's report: +++ after its guard time, then AT&V1 */
            if (line_n >= step_at + 3 * RATE / 2) {
                pass = 1;
                online = 0;
                ntext = shown = 0;
                text[0] = 0;
                printf("[%7.2f] sm56 <- +++\n", line_s());
                write_all(pty, "+++", 3);
                step = 9;
                step_at = line_n;
            }
            break;
        case 9:
            if (pty_has("OK") || line_n >= step_at + 3 * RATE) {
                pty_send("AT&V1");
                step = 10;
                step_at = line_n;
            }
            break;
        case 10:
            if (pty_has("OK") || pty_has("ERROR") || line_n >= step_at + 2 * RATE)
                step = 99;
            break;
        }
        if (step == 99 && !done_at)
            done_at = line_n;
        if (done_at && line_n >= done_at + RATE / 2)
            break;
        if (line_s() >= seconds) {
            printf("[%7.2f] k56-bridge: %d s of line, giving up\n", line_s(), seconds);
            break;
        }
    }
    {
        uint8_t tb[512];
        ssize_t r;
        while ((r = read(pty, tb, sizeof tb)) > 0)
            pty_take(tb, (size_t)r);
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    v90m_get_state(modem, &st);
    printf("[%7.2f] sm56: %s\n", line_s(), *connect_line ? connect_line : "(no CONNECT)");
    printf("[%7.2f] ours: %s, %s %d/%d, %s, V.42 %s; end: %s\n", line_s(),
           connected ? "data mode reached" : "no data mode", NAME(conn_name, st.modulation), st.tx_bps, st.rx_bps,
           st.used_v8 ? "V.8" : "no V.8", ec_on ? NAME(ec_name, v90m_ec_state(ec)) : "never on", NAME(end_name, st.end));
    printf("k56-bridge: to SM56 %ld sent, %ld received; to us %ld sent, %ld received; %d rate changes\n",
           to_sm56.sent, to_sm56.recv, to_ours.sent, to_ours.recv, rate_changes);
    fclose(to_sm56.fsent);
    fclose(to_sm56.frecv);
    fclose(to_ours.fsent);
    fclose(to_ours.frecv);
    fclose(frec);
    v90m_destroy(modem);
    v90m_ec_destroy(ec);
    return pass && *connect_line && connected ? 0 : 1;
}
