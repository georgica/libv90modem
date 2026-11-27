/* v90tx_align: the 6-symbol data frame grid set before the first signal
 * (V.92 8.3.6). Silence runs on that grid, and the first selected signal
 * starts at the next interval 0; align 0 is the unaligned start. */
#include <string.h>
#include "test.h"
#include "v90_tx.h"
#include "v90_pcm.h"

#define N 1000

static void run(int alaw, int p)
{
    static v90tx_t a, b;
    uint8_t ref[N], got[N + 6];
    const uint8_t zero = alaw ? 0xD5 : 0xFF;
    int i, lead = (6 - p) % 6, bad = 0, badz = 0;

    /* reference: unaligned, Sd selected at tick 0 */
    CHECK_EQ_I(v90tx_init(&b, alaw, 80), 0);
    CHECK_EQ_I(v90tx_select(&b, V90TX_SD), 0);
    for (i = 0; i < N; i++) ref[i] = v90tx_tick(&b);

    CHECK_EQ_I(v90tx_init(&a, alaw, 80), 0);
    CHECK_EQ_I(v90tx_align(&a, p), 0);
    CHECK_EQ_I(v90tx_phase(&a), p);
    CHECK_EQ_I(v90tx_signal(&a), V90TX_SILENCE);
    CHECK_EQ_I(v90tx_select(&a, V90TX_SD), 0);
    CHECK_EQ_I(v90tx_align(&a, 0), -1);              /* after a selection */
    CHECK_EQ_I(v90tx_phase(&a), p);
    for (i = 0; i < lead + N; i++) {
        got[i] = v90tx_tick(&a);
        if (i < lead) {
            if (got[i] != zero) badz++;
            if (v90tx_signal(&a) != V90TX_SILENCE) badz++;
        } else if (got[i] != ref[i - lead]) bad++;
        if (i == lead) CHECK_EQ_I(v90tx_signal(&a), V90TX_SD);
    }
    CHECK_EQ_I(badz, 0);
    CHECK_EQ_I(bad, 0);
    CHECK_EQ_I(v90tx_phase(&a), N % 6);
}

/* silence first, then the selection: Sd still starts at interval 0 */
static void late(int alaw, int p)
{
    static v90tx_t a;
    int i, first = -1;
    CHECK_EQ_I(v90tx_init(&a, alaw, 80), 0);
    CHECK_EQ_I(v90tx_align(&a, p), 0);
    for (i = 0; i < 20; i++) (void)v90tx_tick(&a);
    CHECK_EQ_I(v90tx_select(&a, V90TX_SD), 0);
    for (i = 0; i < 30 && first < 0; i++) {
        int ph = v90tx_phase(&a);
        (void)v90tx_tick(&a);
        if (v90tx_signal(&a) == V90TX_SD) first = ph;
    }
    CHECK_EQ_I(first, 0);
}

static void refuse(void)
{
    static v90tx_t a, b;
    int i;
    CHECK_EQ_I(v90tx_init(&a, 0, 80), 0);
    CHECK_EQ_I(v90tx_align(&a, 6), -1);
    CHECK_EQ_I(v90tx_align(&a, -1), -1);
    CHECK_EQ_I(v90tx_phase(&a), 0);
    CHECK_EQ_I(v90tx_align(&a, 3), 0);
    CHECK_EQ_I(v90tx_align(&a, 2), 0);               /* still before a selection: set again */
    CHECK_EQ_I(v90tx_phase(&a), 2);
    /* a selection first, then align refused: the transmitter runs as unaligned */
    CHECK_EQ_I(v90tx_init(&a, 0, 80), 0);
    CHECK_EQ_I(v90tx_init(&b, 0, 80), 0);
    CHECK_EQ_I(v90tx_select(&a, V90TX_SD), 0);
    CHECK_EQ_I(v90tx_select(&b, V90TX_SD), 0);
    CHECK_EQ_I(v90tx_align(&a, 4), -1);
    for (i = 0; i < 200; i++) CHECK_EQ_I(v90tx_tick(&a), v90tx_tick(&b));
    /* the signal under way, nothing pending: refused too, nothing changes */
    CHECK(v90tx_signal(&a) == V90TX_SD && a.pending == V90TX_NONE);
    CHECK_EQ_I(v90tx_align(&a, 0), -1);
    CHECK_EQ_I(v90tx_align(&a, 3), -1);
    for (i = 0; i < 200; i++) CHECK_EQ_I(v90tx_tick(&a), v90tx_tick(&b));
}

int main(void)
{
    int alaw, p;
    for (alaw = 0; alaw < 2; alaw++)
        for (p = 0; p < 6; p++) { run(alaw, p); late(alaw, p); }
    refuse();
    T_REPORT("txalign");
}
