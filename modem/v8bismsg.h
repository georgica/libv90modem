/* The V.8bis messages of K56flex's start-up: CL, MS and ACK(1), their
 * information-field trees (V.8bis clause 8) and framing (7.2), with the
 * frame check sequence of 7.2.7. The values are in docs/history/k56-facts.md
 * ("V.8bis messages", "NS block", "MS test"). The framing is its own: the
 * ones preamble and flags here are V.8bis's, not V.42's HDLC.
 *
 * A message is its information-field octets (octet 0 first), without the
 * FCS. A frame is a bit string, one bit a byte (0 or 1), the first bit
 * to send first. */
#ifndef V90D_MODEM_V8BISMSG_H
#define V90D_MODEM_V8BISMSG_H
#include <stdint.h>

/* octet 0, low nibble: the message type (Table 3) */
#define V8B_MSG_MS   1
#define V8B_MSG_CL   2
#define V8B_MSG_ACK1 4

/* the NS block of our CL: the real server's 9 octets, or SM56's proven 7 */
#define V8B_NS9 0
#define V8B_NS7 1
#ifndef V8B_NS_DEFAULT
#define V8B_NS_DEFAULT V8B_NS9
#endif

#define V8B_MAXOCT  32    /* longest message handled, FCS not counted */
#define V8B_MAXBITS 512   /* longest frame v8bis_frame() writes */

/* the FCS of 7.2.7 (CRC-16/X.25) over n octets; sent low octet first */
uint16_t v8bis_fcs16(const uint8_t *o, int n);

/* The NS block's version octet (blk[7]; docs/history/k56-facts.md "NS
 * block", and "Versions"): bit 6 (0x40) set by the digital modem (the
 * server), clear in a client's; bits 0 to 4 a mask of K56flex versions, bit 0
 * version 1, bit 1 version 2; bit 5 (0x20) a flag of the sender's, no
 * version. A client keeps the highest version bit its mask and our offer
 * share, and drops a CL that shares none without a word. */
#define V8B_VER_DIGITAL 0x40
#define V8B_VER_FLAG    0x20
#define V8B_VER_MASK    0x1f
#define V8B_VERS_1      0x01
#define V8B_VERS_2      0x02
#define V8B_VERS_BOTH   (V8B_VERS_1 | V8B_VERS_2)

/* our CL, FCS not included: the real server's, with the NS block of the
 * given form; returns the octet count (16 or 13). vers: the versions the
 * 9-octet block offers (V8B_VERS_*: its version octet is 0x40 | vers; the
 * real server's was 0x42, version 2 alone); the 7-octet block keeps SM56's
 * 0x61 whatever vers says */
int v8bis_cl_octets(int ns_form, int vers, uint8_t *o);

/* ACK(1): one octet; returns 1 */
int v8bis_ack1_octets(uint8_t *o);

/* the frame of n octets (1..V8B_MAXOCT): a preamble of 32 ones, three
 * flags, the octets and the FCS least significant bit first with a zero
 * inserted after five ones, one closing flag. Returns the bit count. */
int v8bis_frame(const uint8_t *o, int n, uint8_t *bits);

/* the receiver: bits in, whole messages out */
typedef struct {
    uint8_t buf[V8B_MAXOCT + 3];
    int nbits;      /* bits since the last flag (a closing flag's first seven included) */
    int ones;       /* the run of ones now */
    int in_frame;   /* a flag has been seen */
    int overflow;
} v8bis_rx_t;

void v8bis_rx_init(v8bis_rx_t *r);

/* One bit. Returns the octet count (FCS removed, the octets in o, room for
 * V8B_MAXOCT) when the closing flag of a frame with a good FCS arrives; -1
 * when a frame ends invalid (7.2.9: fewer than three octets, not whole
 * octets, FCS error, too long, or aborted by seven ones); 0 otherwise. */
int v8bis_rx_bit(v8bis_rx_t *r, int bit, uint8_t *o);

/* SM56's test of a CL's NS block (blk[0] is its length L, len the octets
 * available): L > 6, blk[1] == 0xb5, blk[2] == 2, blk[6] & 1, (blk[7] &
 * 0x2f) >= 2, blk[7] & 0x40. 1 or 0. */
int v8bis_cl_is_k56(const uint8_t *blk, int len);

/* 1 when the n octets are an MS (type 1) whose NS field, found by parsing
 * the identification and standard fields' trees (8.2.3), holds a whole
 * block with L > 6, blk[1] == 0xb5, blk[2] == 2 and blk[6] & 1; 0 for
 * anything else, a message that does not parse included. */
int v8bis_ms_is_k56(const uint8_t *o, int n);

/* the version octet (blk[7]) of the block v8bis_ms_is_k56 finds, 0..255;
 * -1 when there is none */
int v8bis_ms_version(const uint8_t *o, int n);

/* the version a version octet selects: the highest of its version bits
 * (bits 0 to 4: 1 to 5), 0 with none of them set */
int v8bis_version_of(int octet);

#endif
