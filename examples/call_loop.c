/* Example: one modem calls another, both libv90modem, back to back. Only the
 * public API is used.
 *
 *   call_loop [-m MOD] [-s SECONDS]
 *
 * A calling modem (V90M_ROLE_ORIGINATE) and an answering modem (the
 * default role) are joined by a 4-wire line: what one sends, the other
 * hears one sample later, linear both ways (v90m_process_linear: the
 * analogue V.90 / V.92 modem needs its received samples linear, and the
 * answerer's codewords pass as their linear values). A start-stop framer
 * (v90m_async_*) sits on each modem's bit pump; once data moves each end
 * sends a line of text, then the caller clears down where the modulation
 * has a way to.
 *   -m MOD   what the caller offers: v92 (default: V.92, V.90, V.34, V.22),
 *            v90, v34, v32, v22, bell212, bell103
 *   -s SEC   give up after SEC seconds of line (default 40)
 * The answerer offers its defaults, with V.92 and Bell added.
 * Exit status 0 when both lines of text arrived, 1 otherwise.
 *
 * The calling role has been tried in simulation and against this library's
 * own answerer only (as here), never against a real modem. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "v90modem.h"

static const char *const conn_name[] = { "none", "V.21", "V.22", "V.22bis", "V.90", "V.34", "V.32bis", "V.92", "Bell 103", "Bell 212A" };
static const char *const end_name[] = { "none", "start-up failed", "line lost", "far end cleared down", "we cleared down",
                                        "no answer tone", "no JM", "nothing in common", "retrain failed" };

/* one end of the line: a modem and a start-stop framer on its bit pump */
typedef struct { const char *who; v90m_t *m; v90m_async_t *a; char got[64]; size_t n; } end_t;

static int  get_bit(void *u) { return v90m_async_getbit(((end_t *)u)->a); }
static void put_bit(void *u, int bit) { v90m_async_putbit(((end_t *)u)->a, bit); }

/* events come from inside v90m_process_*, after the sample they belong to */
static void on_event(void *u, v90m_event ev)
{
    end_t *e = u;
    v90m_state st;
    v90m_get_state(e->m, &st);
    switch (ev) {
    case V90M_EV_CONNECTED:
        printf("%-8s CONNECT %d/%d %s%s\n", e->who, st.tx_bps, st.rx_bps, conn_name[st.modulation], st.used_v8 ? "" : " (no V.8)");
        break;
    case V90M_EV_RATES:
        printf("%-8s rates now %d/%d %s\n", e->who, st.tx_bps, st.rx_bps, conn_name[st.modulation]);
        break;
    case V90M_EV_ENDED:
        printf("%-8s ended: %s\n", e->who, end_name[st.end]);
        break;
    default:
        break;
    }
}

static int end_open(end_t *e, const char *who, v90m_role role, unsigned mods)
{
    v90m_cfg c;
    int err;
    memset(e, 0, sizeof *e);
    e->who = who;
    if (!(e->a = v90m_async_create(&err))) return err;
    v90m_cfg_default(&c);
    c.role = role;
    c.modulations = mods;
    c.getbit = get_bit; c.putbit = put_bit; c.on_event = on_event; c.user = e;
    if (!(e->m = v90m_create(&c, &err))) return err;
    return V90M_OK;
}

static void end_close(end_t *e) { v90m_destroy(e->m); v90m_async_destroy(e->a); }

int main(int argc, char **argv)
{
    static const struct { const char *name; unsigned mods; } offer[] = {
        { "v92", V90M_MOD_V92 | V90M_MOD_V90 | V90M_MOD_V34 | V90M_MOD_V22 },
        { "v90", V90M_MOD_V90 | V90M_MOD_V34 | V90M_MOD_V22 },
        { "v34", V90M_MOD_V34 | V90M_MOD_V22 },
        { "v32", V90M_MOD_V32BIS | V90M_MOD_V22 },
        { "v22", V90M_MOD_V22 | V90M_MOD_V21 },
        { "bell212", V90M_MOD_BELL212A },
        { "bell103", V90M_MOD_BELL103 },
    };
    static end_t cl, an;
    const char *to_an = "hello from the caller", *to_cl = "hello from the answerer";
    unsigned mods = offer[0].mods;
    long k, seconds = 40, k_sent = -1, k_done = -1;
    int16_t cl_rx = 0, an_rx = 0;
    int i, arg, err, ok;
    v90m_state sc;

    for (arg = 1; arg < argc; arg++) {
        if (!strcmp(argv[arg], "-m") && arg + 1 < argc) {
            for (i = 0; i < (int)(sizeof offer / sizeof offer[0]); i++) if (!strcmp(argv[arg + 1], offer[i].name)) break;
            if (i == (int)(sizeof offer / sizeof offer[0])) break;
            mods = offer[i].mods; arg++;
        } else if (!strcmp(argv[arg], "-s") && arg + 1 < argc) seconds = atol(argv[++arg]);
        else break;
    }
    if (arg < argc) {
        fprintf(stderr, "usage: call_loop [-m v92|v90|v34|v32|v22|bell212|bell103] [-s SECONDS]\n");
        return 2;
    }

    printf("libv90modem %s\n", v90m_version());
    if ((err = end_open(&cl, "caller", V90M_ROLE_ORIGINATE, mods)) != V90M_OK
        || (err = end_open(&an, "answerer", V90M_ROLE_ANSWER, V90M_MOD_V21 | V90M_MOD_V22 | V90M_MOD_V32BIS | V90M_MOD_V34
                           | V90M_MOD_V90 | V90M_MOD_V92 | V90M_MOD_BELL103 | V90M_MOD_BELL212A)) != V90M_OK) {
        fprintf(stderr, "could not make the modems: error %d\n", err);
        return 1;
    }

    for (k = 0; k < 8000 * seconds; k++) {
        int16_t cl_tx, an_tx;
        v90m_process_linear(cl.m, &cl_rx, &cl_tx, 1);
        v90m_process_linear(an.m, &an_rx, &an_tx, 1);
        an_rx = cl_tx; cl_rx = an_tx;               /* the line: one sample each way */

        v90m_get_state(cl.m, &sc);
        if (sc.end != V90M_END_NONE && k_done < 0) break;     /* failed before data moved */
        if (k_sent < 0 && sc.moving) {
            v90m_state sa;
            v90m_get_state(an.m, &sa);
            if (sa.moving) {
                v90m_async_write(cl.a, (const uint8_t *)to_an, strlen(to_an));
                v90m_async_write(an.a, (const uint8_t *)to_cl, strlen(to_cl));
                k_sent = k;
            }
        }
        cl.n += v90m_async_read(cl.a, (uint8_t *)cl.got + cl.n, sizeof cl.got - 1 - cl.n);
        an.n += v90m_async_read(an.a, (uint8_t *)an.got + an.n, sizeof an.got - 1 - an.n);
        if (k_done < 0 && cl.n == strlen(to_cl) && an.n == strlen(to_an)) {
            v90m_stats st;
            k_done = k;
            cl.got[cl.n] = an.got[an.n] = 0;
            printf("caller   got \"%s\"\nanswerer got \"%s\"\n", cl.got, an.got);
            v90m_get_stats(cl.m, &st);
            if (st.snr_db10 >= 0) printf("caller   SNR %.1f dB", st.snr_db10 / 10.0);
            else printf("caller   SNR not measured");
            if (st.rtd >= 0) printf(", round trip %d samples", st.rtd);
            printf("\n");
            /* hang up: an orderly cleardown where the modulation has one,
             * else just stop */
            if (v90m_cleardown(cl.m) != V90M_OK) { printf("caller   hangs up (no cleardown in %s)\n", conn_name[sc.modulation]); break; }
        }
        if (k_done >= 0) {
            /* the cleardown takes a fraction of a second: until both ends have
             * ended, or 2 s */
            v90m_state sa;
            v90m_get_state(an.m, &sa);
            if ((sc.end != V90M_END_NONE && sa.end != V90M_END_NONE) || k > k_done + 8000 * 2) break;
        }
    }
    ok = k_done >= 0;
    if (!ok) printf("no data both ways in %ld s\n", seconds);
    printf("line time %.2f s\n", k / 8000.0);
    end_close(&cl); end_close(&an);
    return ok ? 0 : 1;
}
