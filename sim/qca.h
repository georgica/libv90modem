/* Test peer: the analogue modem's half of V.92's short Phase 1 (8.2, 8.3,
 * 9.2.1.1, 9.2.1.3), as far as a test of the digital modem needs it.
 *
 * Written from the V.92 text (11/2000) alone, never from library code: its
 * own Table 2 writer, its own Table 13 reader and its own QTS and ANSpcm
 * codewords (8.3.6; 8.3.1's formula with Table 6's scl and sim/g711). The
 * library has its own copies of these; the two are kept apart on purpose,
 * so that each checks the other (docs/history/qc-facts.md). It uses the V.21
 * demodulator the other peers use (modem/v21). Host only. */
#ifndef V90D_SIM_QCA_H
#define V90D_SIM_QCA_H
#include <stdint.h>
#include "../modem/v21.h"

#define QCA_QC1A_BITS 60
#define QCA_QTS_LEN   768                   /* QTS: 128 x 6 symbols */
#define QCA_QTSB_LEN  48                    /* QTS\: 8 x 6 symbols */
#define QCA_ANSPCM_PERIOD 301
#define QCA_ANSPCM_REV    3612              /* a phase reversal every 3612 symbols */

/* QC1a of Table 2, bit 0 first in time (1 = mark): ten ONEs, the
 * synchronisation sequence 0101010101, the character 000PW0XYZ1, and the
 * three again. wxyz is UQTS's code (0..15, W the most significant), lapm the
 * P bit. Returns 60. */
int qca_qc1a_bits(int wxyz, int lapm, uint8_t *bits);

/* UQTS's Ucode for WXYZ 0..14 (Table 2), -1 for 15 (cleardown from on-hold)
 * and anything else */
int qca_uqts_ucode(int wxyz);

/* QCA1d receiver (Table 13), V.21(H). Fed the line sample by sample; the bit
 * clock is taken from the level changes. Returns 1 on the sample at which the
 * second of two equal copies of {ten ONEs, 0101010101, 011P000LM1} has been
 * read, once; r->oct is then the information octet (its eight data bits,
 * the first in time in bit 0). */
typedef struct {
    v21demod_t dem;
    int64_t  squelch;       /* in-band energy below this is no signal */
    int      level;         /* the line level of the last sample, 1 = mark */
    int32_t  clk;           /* bit clock, in 1/fs units of 1/300 s */
    uint32_t hist;          /* the last 32 bits read, the latest in bit 0 */
    int      n;             /* bits read since the first copy */
    int      got;           /* 0 hunting, 1 one copy read, 2 done */
    uint8_t  oct;
} qca_rx_t;

void qca_rx_init(qca_rx_t *r);
int  qca_rx_tick(qca_rx_t *r, int16_t x);
/* LM of a QCA1d octet (Table 13, bits 27:28): 0 -9.5, 1 -12, 2 -15, 3 -18 dBm0 */
int  qca_lm(uint8_t oct);

/* QTS (i 0..767) and QTS\ (i 768..815) of 8.3.6 for the Ucode ucode, as the
 * octet the digital modem sends */
uint8_t qca_qts_octet(int alaw, int ucode, int i);
/* ANSpcm's codeword n (0 = the first) at level lm (Table 6): Ucode sequence
 * from 8.3.1's formula, x floored to an integer and quantised by G.711; the
 * sign inverted in every second block of 3612 */
uint8_t qca_anspcm_octet(int alaw, int lm, long n);
/* the Ucode (0..127) of an octet the digital modem sent */
int qca_octet_ucode(int alaw, uint8_t o);

#endif
