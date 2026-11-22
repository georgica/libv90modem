/* x2's training script, the server's side (docs/history/x2-facts.md, "Phase
 * 3 script", "Records", "Mapping"): G.711 mu-law octets, one per sample,
 * from the client's J to data mode.
 *
 *   silence (7f) until x2tx_start(), then without a gap:
 *   lead       20 octets 7f
 *   preamble   363: 22 octets 16 times, then their first 11 with the sign turned
 *   TRN        20004 scrambled ones (GPC from state 0), b1 / 31
 *   J words    1200: 09B0 99 times, then 099F, 12 bits LSB first, scrambled, b1 / 31
 *   ones       6000 scrambled ones, c1 / 41
 *   alignment  1152: 192 frames of 9e 28 a1 21 a8 1e, frame f turned f left
 *   sync       mapped frames of ones in levels A: 1667 frames at least, until
 *              x2tx_go() (the client's MP)
 *   record     our record in levels A, whole records (4 frames each), until
 *              x2tx_go() again (the client's E)
 *   start      681 frames of ones: one in levels A, then 680 in the data levels
 *   data       mapped frames of the owner's bits
 *
 * A rate change of the client's ("Rate change"): x2tx_data() with the index
 * its MP asks for, then x2tx_go(), in data mode; after the frame being sent:
 *   marker     8 frames +++--- and one ---+++, each position at its largest
 *              level in the rate in use, no differential signs
 *   records    GPC from zero, the sign positive: our record in index 1's
 *              data levels, 10 times without the acknowledge (733C), 33
 *              times with it
 *   start      682 frames of ones: one in levels B, then 681 in the new data levels
 *   data       on from the owner's next bit
 * From the MP on, all of it is sent for the pad that MP reports.
 *
 * GPC runs on from TRN's first octet through every mapped frame (not the
 * alignment). Fixed-point, no allocation. */
#ifndef V90D_MODEM_X2TX_H
#define V90D_MODEM_X2TX_H
#include <stdint.h>
#include "x2map.h"
#include "../v90/v90_scr.h"

typedef enum {
    X2TX_SILENCE, X2TX_LEAD, X2TX_PRE, X2TX_TRN, X2TX_JW, X2TX_ONES, X2TX_ALIGN,
    X2TX_SYNC, X2TX_REC, X2TX_START, X2TX_DATA,
    X2TX_MARK, X2TX_RREC        /* a rate change's marker and records */
} x2tx_seg;

/* segment lengths in octets (the facts' table) */
#define X2TX_LEAD_N   20
#define X2TX_PRE_N    363
#define X2TX_TRN_N    20004
#define X2TX_JW_N     1200
#define X2TX_ONES_N   6000
#define X2TX_ALIGN_N  1152
#define X2TX_SYNC_MIN 10002     /* octets of sync frames at least */
#define X2TX_START_FRAMES 681
#define X2TX_REC_BITS 96        /* our record, padded with zeros to 4 frames */
#define X2TX_MARK_FRAMES 9      /* a rate change: the marker's frames (8 + 1) */
#define X2TX_RREC_NAK 10        /* ... our records without the acknowledge, */
#define X2TX_RREC_N   43        /* ... of all (x2-facts.md "Rate change") */
/* our record's words: word 0 and 2 as the I-modem sends them (word 0 bit
 * 15 the acknowledge); word 1 the downstream rate indices we have level
 * banks for, bit n - 1 for index n as in the MP's word 1: all 15, in each
 * pad mode (x2tab.h), as the I-modem's 7FFF. The Courier takes 0001 as it
 * takes 7FFF and hangs up on 0002 (x2-facts.md "Records"); what else it
 * does with the field is not known. */
#define X2TX_REC_W0   0xF33C
#define X2TX_REC_W1   0x7FFF
#define X2TX_REC_W2   0x0000

typedef int (*x2tx_getbit_fn)(void *user);

typedef struct {
    x2tx_seg  seg;
    int32_t   n;                /* octets sent in this segment */
    int       go;               /* leave SYNC / REC / DATA at the next chance */
    int       again;            /* the start segment is a rate change's */
    v90_scr_t scr;              /* GPC */
    x2map_t   map;
    uint8_t   frame[X2MAP_N]; int fpos;     /* the frame being sent */
    uint8_t   rec[X2TX_REC_BITS], rec0[X2TX_REC_BITS];    /* with the acknowledge, without */
    int       rec_frame;        /* frame of the record being sent, 0..3 */
    int32_t   records;          /* records begun */
    int       index, pad_mode;  /* the data levels (x2tx_data) */
    x2tx_getbit_fn getbit;
    void     *user;
} x2tx_t;

/* silent until x2tx_start(); the data levels index 1, no pad, data all ones */
void     x2tx_init(x2tx_t *t);
/* the client's J has come: the script begins with the next octet */
void     x2tx_start(x2tx_t *t);
/* the data levels (rate index, pad mode) and the source of the data; given
 * before the start segment */
void     x2tx_data(x2tx_t *t, int index, int pad_mode, x2tx_getbit_fn getbit, void *user);
/* SYNC: on to our record once 1667 frames are out; REC: on to the start
 * segment after the record being sent; DATA: a rate change, to the levels
 * of the last x2tx_data(), after the frame being sent */
void     x2tx_go(x2tx_t *t);
uint8_t  x2tx_tick(x2tx_t *t);
/* frames of 6 octets per second times the bits of each: the data rate in
 * bit/s of the data levels (0 for a bank that does not exist) */
int      x2tx_rate(const x2tx_t *t);
/* 1 once the next mapped frame is data: in data mode, and from the start
 * segment's last frame on (the first bit of data is taken when it ends) */
int      x2tx_data_next(const x2tx_t *t);

#endif
