/* The library's V.92 Quick Connect signals (modem/qc) against the V.92 text
 * and against the simulated caller (sim/qca, written from the text on its
 * own): Table 2's UQTS and QC1a octet, Table 13's QCA1d, the ANSpcm level,
 * 8.3.6's QTS, 8.3.1's ANSpcm (Tables 7 to 10 transcribed, and all 2408
 * entries against sim/qca's generator), TONEq's 980 Hz watch, and Table 19's
 * bit 33 in INFO1a. */
#include <string.h>
#include "test.h"
#include "../modem/qc.h"
#include "../modem/tonedet.h"
#include "../modem/info.h"
#include "../sim/qca.h"

#define FS 8000
#define PI 3.14159265358979323846

static void bits_of(const char *s, uint8_t *b, int *n)
{
    for (; *s; s++) if (*s == '0' || *s == '1') b[(*n)++] = (uint8_t)(*s - '0');
}

/* a[0..] equals the bit string s, and s has want bits */
static int same_bits(const uint8_t *a, const char *s, int want)
{
    uint8_t b[128]; int n = 0, i;
    bits_of(s, b, &n);
    if (n != want) return 0;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void uqts(void)
{
    static const int want[15] = { 61, 62, 63, 66, 67, 70, 71, 74, 75, 78, 79, 82, 83, 86, 87 };
    int w, ok = 1;
    printf("uqts\n");
    for (w = 0; w < 15; w++) ok &= qc_uqts_ucode(w) == want[w];
    CHECK(ok);
    CHECK_EQ_I(qc_uqts_ucode(15), -1);
    CHECK_EQ_I(qc_uqts_ucode(-1), -1);
    CHECK_EQ_I(qc_uqts_ucode(16), -1);
}

static void qc1a_one(uint8_t oct, int ret, int wxyz, int lapm)
{
    int w = -7, p = -7, r = qc1a_parse(oct, &w, &p);
    CHECK_EQ_I(r, ret);
    if (ret == 0) { CHECK_EQ_I(w, wxyz); CHECK_EQ_I(p, lapm); }
}

static void qc1a(void)
{
    int w, p, ok = 1;
    uint8_t b[64];
    printf("qc1a\n");
    qc1a_one(0xA4, 0, 5, 1);
    qc1a_one(0xE0, 0, 7, 0);        /* the CM sync octet as well */
    qc1a_one(0x00, 0, 0, 0);
    qc1a_one(0x6C, 0, 14, 1);
    qc1a_one(0xE8, 0, 15, 0);
    qc1a_one(0xEC, 0, 15, 1);
    qc1a_one(0xF8, -1, 0, 0);       /* bit 4 (character bit 25) set */
    qc1a_one(0xA5, -1, 0, 0);       /* QC1d: digital modem */
    qc1a_one(0xB4, -1, 0, 0);
    qc1a_one(0xA6, -1, 0, 0);       /* QCA */
    /* every (WXYZ, P): the sim's Table 2 writer, read back by the library */
    for (w = 0; w < 16; w++)
        for (p = 0; p < 2; p++) {
            uint8_t lb[80];
            int i, oct = 0, rw = -1, rp = -1;
            (void)qca_qc1a_bits(w, p, b);
            for (i = 0; i < 8; i++) oct |= b[21 + i] << i;
            ok &= qc1a_parse((uint8_t)oct, &rw, &rp) == 0 && rw == w && rp == p;
            /* and the library's frame of that octet is the sim's QC1a */
            ok &= qc_frame_bits((uint8_t)oct, 0, lb) == 60 && memcmp(lb, b, 60) == 0;
        }
    CHECK(ok);
}

static void qca1d(void)
{
    uint8_t b[80];
    printf("qca1d\n");
    CHECK_EQ_I(qca1d_octet(1, 1), 0x87);
    CHECK_EQ_I(qca1d_octet(0, 0), 0x03);
    CHECK_EQ_I(qca1d_octet(3, 1), 0xC7);
    CHECK_EQ_I(qca1d_octet(2, 0), 0x43);
    CHECK_EQ_I(qca_lm(qca1d_octet(2, 0)), 2);
    CHECK_EQ_I(qc_frame_bits(0x87, 1, b), 70);
    CHECK(same_bits(b, "1111111111 0101010101 0111000011 1111111111 0101010101 0111000011 1111111111", 70));
    CHECK_EQ_I(qc_frame_bits(0xA4, 0, b), 60);
    CHECK(same_bits(b, "1111111111 0101010101 0001001011 1111111111 0101010101 0001001011", 60));
    CHECK_EQ_I(QC_SYNC, 0x55);
    CHECK_EQ_I(QC_OCT_DIGITAL | QC_OCT_ACK | QC_OCT_LAPM | QC_OCT_ZERO, 0x17);
}

static void lm(void)
{
    printf("lm\n");
    CHECK_EQ_I(qc_lm(-12, 23), 1);
    CHECK_EQ_I(qc_lm(-6, 23), 1);
    CHECK_EQ_I(qc_lm(-6, 18), 0);
    CHECK_EQ_I(qc_lm(-10, 23), 1);
    CHECK_EQ_I(qc_lm(-13, 23), 1);
    CHECK_EQ_I(qc_lm(-14, 23), 2);
    CHECK_EQ_I(qc_lm(-21, 23), 3);
    CHECK_EQ_I(qc_lm(-21, 31), 3);
    CHECK_EQ_I(qc_lm_db10(0), -95);
    CHECK_EQ_I(qc_lm_db10(1), -120);
    CHECK_EQ_I(qc_lm_db10(2), -150);
    CHECK_EQ_I(qc_lm_db10(3), -180);
}

static void qts(void)
{
    static const uint8_t mu0[6] = { 0xB9, 0xFF, 0xB9, 0x39, 0x7F, 0x39 };
    static const uint8_t a0[6] = { 0x93, 0xD5, 0x93, 0x13, 0x55, 0x13 };
    int i, u, law, ok;
    printf("qts\n");
    for (i = 0; i < 6; i++) {
        CHECK_EQ_I(qc_qts_octet(0, 70, i), mu0[i]);
        CHECK_EQ_I(qc_qts_octet(0, 70, 768 + i), mu0[(i + 3) % 6]);
        CHECK_EQ_I(qc_qts_octet(1, 70, i), a0[i]);
        CHECK_EQ_I(qc_qts_octet(1, 70, 768 + i), a0[(i + 3) % 6]);
    }
    CHECK_EQ_I(qc_qts_octet(0, 61, 0), 0xC2);
    CHECK_EQ_I(qc_qts_octet(0, 87, 0), 0xA8);
    CHECK_EQ_I(qc_qts_octet(1, 61, 0), 0xE8);
    CHECK_EQ_I(qc_qts_octet(1, 87, 0), 0x82);
    /* every symbol of QTS and QTS\, both laws, every UQTS: as the sim's */
    ok = 1;
    for (law = 0; law < 2; law++)
        for (u = 0; u < 15; u++)
            for (i = 0; i < 816; i++)
                ok &= qc_qts_octet(law, qc_uqts_ucode(u), i) == qca_qts_octet(law, qc_uqts_ucode(u), i);
    CHECK(ok);
}

/* Tables 7 to 10/V.92, transcribed (mu-law / A-law) */
static void anspcm(void)
{
    static const uint8_t t7m[3] = { 0xA1, 0x58, 0x22 }, t7a[3] = { 0x88, 0x76, 0x08 };
    static const uint8_t t8m[6] = { 0xA9, 0x5D, 0x29, 0xC9, 0xAA, 0x3D }, t8a[6] = { 0x83, 0x49, 0x00, 0xE1, 0x80, 0x15 };
    static const uint8_t t10m[6] = { 0xB8, 0x69, 0x39, 0xD7, 0xB9, 0x4C }, t10a[6] = { 0x93, 0x5B, 0x10, 0xF1, 0x90, 0x64 };
    int i, law, l, k, ok, diff;
    printf("anspcm\n");
    for (i = 0; i < 3; i++) { CHECK_EQ_I(qc_anspcm[0][0][i], t7m[i]); CHECK_EQ_I(qc_anspcm[1][0][i], t7a[i]); }
    CHECK_EQ_I(qc_anspcm[0][0][300], 0x59); CHECK_EQ_I(qc_anspcm[1][0][300], 0x74);
    for (i = 0; i < 6; i++) { CHECK_EQ_I(qc_anspcm[0][1][i], t8m[i]); CHECK_EQ_I(qc_anspcm[1][1][i], t8a[i]); }
    CHECK_EQ_I(qc_anspcm[0][1][300], 0x5E); CHECK_EQ_I(qc_anspcm[1][1][300], 0x4F);
    CHECK_EQ_I(qc_anspcm[0][2][0], 0xAF); CHECK_EQ_I(qc_anspcm[1][2][0], 0x9A);
    CHECK_EQ_I(qc_anspcm[0][2][300], 0x64); CHECK_EQ_I(qc_anspcm[1][2][300], 0x46);
    for (i = 0; i < 6; i++) { CHECK_EQ_I(qc_anspcm[0][3][i], t10m[i]); CHECK_EQ_I(qc_anspcm[1][3][i], t10a[i]); }
    /* the sequence in time: period 301, the sign inverted in odd blocks of 3612 */
    CHECK_EQ_I(qc_anspcm_octet(0, 1, 0), 0xA9);
    CHECK_EQ_I(qc_anspcm_octet(0, 1, 301), 0xA9);
    CHECK_EQ_I(qc_anspcm_octet(0, 1, 3611), 0x5E);
    CHECK_EQ_I(qc_anspcm_octet(0, 1, 3612), 0x29);
    CHECK_EQ_I(qc_anspcm_octet(1, 1, 3612), 0x03);
    CHECK_EQ_I(qc_anspcm_octet(0, 1, 7224), 0xA9);
    /* all 2408 entries against the sim's generator (8.3.1's formula) */
    diff = 0;
    for (law = 0; law < 2; law++)
        for (l = 0; l < 4; l++)
            for (k = 0; k < 301; k++)
                diff += qc_anspcm[law][l][k] != qca_anspcm_octet(law, l, k);
    CHECK_EQ_I(diff, 0);
    printf("  %d of 2408 entries differ from sim/qca\n", diff);
    /* and over three reversal blocks, both laws, every level */
    ok = 1;
    for (law = 0; law < 2; law++)
        for (l = 0; l < 4; l++)
            for (i = 0; i < 3 * 3612; i += 7)
                ok &= qc_anspcm_octet(law, l, i) == qca_anspcm_octet(law, l, i);
    CHECK(ok);
}

/* the first sample on which the watch says "on", within n samples, or -1 */
static int tone_on_at(double f1, double a1, double f2, double a2, int n)
{
    tonedet_t t;
    int i;
    tonedet_init(&t, TONEDET_980);
    for (i = 0; i < n; i++) {
        double v = a1 * cos(2 * PI * f1 * i / FS) + a2 * cos(2 * PI * f2 * i / FS);
        if (tonedet_tick(&t, (int16_t)floor(v + 0.5))) return i;
    }
    return -1;
}

static void tonedet980(void)
{
    /* peaks on the 16-bit scale: 0 dBm0 is 22300 */
    int at;
    printf("tonedet980\n");
    CHECK_EQ_I(TONEDET_980, 0x1F5C28F6u);
    at = tone_on_at(980, 2230, 2100, 705, 4000);   /* -20 dBm0 + -30 dBm0 */
    CHECK_EQ_I(at, TONEDET_BLOCKS * TONEDET_BLOCK - 1);
    CHECK_EQ_I(tone_on_at(1180, 2230, 0, 0, 8000), -1);
    CHECK_EQ_I(tone_on_at(2100, 2230, 0, 0, 8000), -1);
}

static void put_bits(uint8_t *p, int *n, unsigned v, int nb)
{
    int k;
    for (k = 0; k < nb; k++) p[(*n)++] = (uint8_t)((v >> k) & 1u);
}

/* Table 19/V.92 payload (bit 12 onwards), bit by bit, with its CRC */
static void t19_payload(int bit33, uint8_t *p)
{
    int n = 0;
    uint16_t c;
    put_bits(p, &n, 0, 6);                  /* 12:17 reserved */
    put_bits(p, &n, 0, 7);                  /* 18:24 MD */
    put_bits(p, &n, 100, 7);                /* 25:31 UINFO */
    put_bits(p, &n, 0, 1);                  /* 32 */
    put_bits(p, &n, (unsigned)bit33, 1);    /* 33 high carrier */
    put_bits(p, &n, 4, 3);                  /* 34:36 */
    put_bits(p, &n, 6, 3);                  /* 37:39 */
    put_bits(p, &n, 0x200, 10);             /* 40:49 -512 */
    c = info_crc(p, n);
    put_bits(p, &n, c, 16);                 /* 50:65 */
}

static void table19(void)
{
    uint8_t p[64];
    info1a_t i;
    printf("table19\n");
    t19_payload(1, p);
    CHECK(info_check(p, 54));
    memset(&i, 0x5A, sizeof i);
    info1a_parse(p, &i);
    CHECK_EQ_I(i.v90, 1); CHECK_EQ_I(i.pcm, 0); CHECK_EQ_I(i.up_sr, 4);
    CHECK_EQ_I(i.uinfo, 100); CHECK_EQ_I(i.hi_carrier, 1); CHECK_EQ_I(i.md_len, 0);
    CHECK_EQ_I(i.freq_off, -512);
    CHECK_EQ_I(info1a_kind(&i), INFO1A_V90);
    t19_payload(0, p);
    info1a_parse(p, &i);
    CHECK_EQ_I(i.hi_carrier, 0); CHECK_EQ_I(i.up_sr, 4);
}

int main(void)
{
    uqts();
    qc1a();
    qca1d();
    lm();
    qts();
    anspcm();
    tonedet980();
    table19();
    T_REPORT("test_qcsig");
}
