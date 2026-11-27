/* V.92 Quick Connect signals. See qc.h. */
#include "qc.h"
#include "../v90/v90_pcm.h"

/* Table 2: WXYZ 0000..1110 */
static const uint8_t uqts_ucode[15] = { 61, 62, 63, 66, 67, 70, 71, 74, 75, 78, 79, 82, 83, 86, 87 };

int qc_uqts_ucode(int wxyz)
{
    return (wxyz >= 0 && wxyz < 15) ? uqts_ucode[wxyz] : -1;
}

int qc1a_parse(uint8_t oct, int *wxyz, int *lapm)
{
    if (oct & (QC_OCT_DIGITAL | QC_OCT_ACK | QC_OCT_ZERO)) return -1;
    *wxyz = ((oct >> 3) & 1) * 8 + ((oct >> 5) & 1) * 4 + ((oct >> 6) & 1) * 2 + ((oct >> 7) & 1);
    *lapm = (oct >> 2) & 1;
    return 0;
}

uint8_t qca1d_octet(int lm, int lapm)
{
    return (uint8_t)(QC_OCT_DIGITAL | QC_OCT_ACK | (lapm ? QC_OCT_LAPM : 0)
                     | (((lm >> 1) & 1) << 6) | ((lm & 1) << 7));
}

/* one 10-bit character: start 0, bits 0..7, stop 1 */
static int put_char(uint8_t *bits, int n, uint8_t c)
{
    int k;
    bits[n++] = 0;
    for (k = 0; k < 8; k++) bits[n++] = (uint8_t)((c >> k) & 1);
    bits[n++] = 1;
    return n;
}

static int put_ones(uint8_t *bits, int n)
{
    int k;
    for (k = 0; k < 10; k++) bits[n++] = 1;
    return n;
}

int qc_frame_bits(uint8_t oct, int trailing_ones, uint8_t *bits)
{
    int n = 0, copy;
    for (copy = 0; copy < 2; copy++) {
        n = put_ones(bits, n);
        n = put_char(bits, n, QC_SYNC);
        n = put_char(bits, n, oct);
    }
    if (trailing_ones) n = put_ones(bits, n);
    return n;
}

static const int16_t lm_db10[4] = { -95, -120, -150, -180 };

int qc_lm_db10(int lm)
{
    return lm_db10[lm & 3];
}

int qc_lm(int tx_dbm0, int max_tx_code)
{
    int32_t want = (int32_t)tx_dbm0 * 10, cap = -5 - 5 * (int32_t)max_tx_code;
    int lm = 0, k;
    int32_t best = -1;
    for (k = 0; k < 4; k++) {
        int32_t d = want - lm_db10[k];
        if (d < 0) d = -d;
        /* the later (lower) level wins a tie */
        if (best < 0 || d <= best) { best = d; lm = k; }
    }
    while (lm < 3 && lm_db10[lm] > cap) lm++;
    return lm;
}

uint8_t qc_qts_octet(int alaw, int ucode, int i)
{
    int k = i % 6, pos = k < 3;
    if (i >= QC_QTS_LEN) pos = !pos;
    return v90_pcm_octet(alaw, (unsigned)(k == 1 || k == 4 ? 0 : ucode), pos);
}

uint8_t qc_anspcm_octet(int alaw, int lm, int32_t n)
{
    uint8_t o = qc_anspcm[alaw != 0][lm & 3][n % QC_ANSPCM_PERIOD];
    if ((n / QC_ANSPCM_REV) & 1) o ^= 0x80;
    return o;
}
