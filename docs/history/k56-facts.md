# K56flex on the answering side: the facts

2026-11-21. The single source of values for the K56flex work
(`docs/superpowers/specs/2026-11-21-k56flex-answering-design.md`, the plan
`docs/superpowers/plans/2026-11-21-k56flex-answering.md`). Later tasks take
their values from here, by section name. No code, table file or prose of
D-Modem (AGPL-3.0) is in this repository, and no recording, SM56 binary or
firmware; what follows are facts, each with where it came from, in our own
words.

## Tags and sources

Every value carries one of the plan's four tags:

- [doc]: in a standard (ITU-T V.8 bis, V.34, T.35), a patent, or a modem
  maker's manual.
- [RE: who, from what]: reverse-engineered or observed by someone, named
  with what it was read from. Most values are [RE: D-Modem, ...]: D-Modem's
  reading of Motorola's SM56 soft modem (`sm56.ko`: a K56flex client plus an
  unused digital-side transmitter), checked by D-Modem against that client.
  "code `x`" means we read the value from D-Modem's source, nothing else;
  "notes N" means section N of its `docs/dsp/k56flex-details.md`. Gough
  Lui's published `AT&V` print-out is [RE: Gough Lui, ...], and our own
  decoding of his recording ("Recording") is [RE: ours, the recording].
- [inf]: inferred or computed by us from tagged values; the computation or
  argument is given, and a weak one is called weak.
- [unk]: no source gives it. Such values are collected in "Unsettled".

Decisions the controller took on these facts are marked **Ruling Cn**
(the ledger `.superpowers/sdd/2026-11-21-k56flex-answering/progress.md`).

Sources, as read:

| Short name | What | Version read |
| --- | --- | --- |
| V.8bis | ITU-T V.8 bis (11/2000), https://www.itu.int/rec/T-REC-V.8bis-200011-I/en | PDF SHA-256 `1eddb79f4dabac26c17531693e9dd45e30e7a9961d867bfad79181cccade2806` (kept in `~/modem-research/itu/`) |
| V.34 | ITU-T V.34 (02/98), as already implemented in `v34/`, `modem/info.c`, `modem/v34mp.c` | |
| D-Modem notes | `docs/dsp/k56flex-details.md` of github.com/D-Modem/D-Modem | `c06a380d86105b2309c8cb8958e41120856bb84b` (2026-11-20); identical to `~/modem-research/sources/dmodem-k56flex-details.md` |
| D-Modem code | `dsp/k56flex.{h,cpp}`, `dsp/k56flex_train.cpp`, `dsp/k56flex_tables.cpp`, `dsp/v34_k56.h`, `dsp/v8bis.{h,cpp}`, `dsp/bits.h`, `dsp/v34_modem.cpp`, `dsp/test/netsim.cpp`, read for values only | same commit, cloned to `~/modem-research/D-Modem` |
| D-Modem PRs | #39 (encoder against SM56's decoder), #44 (server), #122 (pads, robbed bits, renegotiation, retrains), #142 (V.8bis levels), github.com/D-Modem/D-Modem/pull/N | as summarised in `~/modem-research/research_notes/x2 and K56flex protocols/k56flex_technical.md` |
| Patents | US 6,081,555 and US 6,259,742 (shell mapping for PCM modems), US 6,278,744 (sign against the running sum), patents.google.com | as summarised in the research note above |
| Telenetics | Telenetics 56 manual, `AT&V` "Flex" field, https://www.manualsdir.com/manuals/211413/telenetics-56.html?page=75 | as quoted in the research note above |
| Gough Lui | "Project: Record a K56flex Dial-Up Modem Connection", https://goughlui.com/2018/06/10/project-record-a-k56flex-dial-up-modem-connection/, and its recording `k56flex-48000.wav` (© Gough Lui) | WAV SHA-256 `e45f73cc0fa8d0defc9c1e39a5ec093d050095163ed3dab32fd41b050306b2cf`, kept in `~/modem-research/goughlui-k56/`, never in the repository |

Conventions used below:

- A **code** is a downstream PCM symbol: bit 7 set is positive, bits 0 to 6
  the magnitude index m (0 the smallest, 127 the largest). As a mu-law
  octet on the line it is `0xff - m` (positive) or `0x7f - m` (negative);
  as an A-law octet, the code xor `0x55`. Its value, in 14-bit mu-law units,
  is `((2*(m & 15) + 33) << (m >> 4)) - 33` (`0x4f` is 975). [RE: D-Modem,
  notes 3 and 5; code `k56_g711`, `k56_value`]. SM56's own unused
  transmitter used the opposite sign sense; the client's decoder and
  D-Modem's encoder use the one above, and data sent with the opposite
  sense reached SM56 with every sign inverted. [RE: D-Modem, notes 3].
- The **symbol** downstream is one 8 kHz code. A **frame** is 8 symbols.
- The **scrambler** is V.34's, `1 + x^-5 + x^-23` (GPA) downstream and
  `1 + x^-18 + x^-23` (GPC) upstream (see "Upstream and J24"): each output
  bit is the input bit xor the register's bits 4 and 22 (GPA; 17 and 22 for
  GPC), then shifted into the register's bit 0. [doc] (V.34 7.2),
  [RE: D-Modem, code `bits.h` `scramble`].
- "Running sum" is the sum of the values of the codes sent, as defined
  where it is used.

## V.8bis signals

The answering server is the V.8bis initiating station. [doc]
(V.8bis 9.3: on automatic answering the answering station is the initiating
station). The K56flex exchange is V.8bis transaction 10 (MRe, CRd, CL, MS,
ACK) or 12 (CRe, CRd, CL, MS, ACK) of V.8bis Table 7. [doc]; the
real server used 12, and so do we (Ruling C1) ([RE: ours, the recording], "Recording").

| Signal | Segment 1 (dual tone) | Segment 2 | Sent by |
| --- | --- | --- | --- |
| MRe | 1375 + 2002 Hz | 650 Hz | answering station |
| CRe | 1375 + 2002 Hz | 400 Hz | answering station |
| CRd | 1529 + 2225 Hz | 1900 Hz | calling station (responding) |
| MRd | 1375 + 2002 Hz (initiating) or 1529 + 2225 Hz (responding) | 1150 Hz | |
| ESi / ESr | 1375 + 2002 / 1529 + 2225 Hz | 980 / 1650 Hz | |

- Frequencies: V.8bis Tables 1 and 2, tolerance ±250 ppm. [doc].
- Durations: segment 1 400 ms nominal, segment 2 100 ms nominal, ±2 %; for
  MRe and CRe segment 1 may be shortened to 285 ms. [doc] (7.1.2,
  7.1.3).
- Levels: MRe and CRe 12 to 15 dB below the nominal permitted transmit
  power (the power of continuous signals); every other signal and the
  messages preferably at the highest permitted level. [doc]
  (7.1.4). D-Modem sends MRe with each tone at half its V.34 tone amplitude
  (the dual tone 3 dB below one full tone) and its messages at the full
  amplitude, so its MRe is not 12 dB below its messages. [RE: D-Modem,
  code `dp_v34.cpp` `V34V8::v8_step`, `v34_k56.h` `K8_MRE`]. The real
  server's CRe (both segments) is 14.6 dB below its V.21 messages and
  11.6 dB below its ANSam, in rms. [RE: ours, the recording].
- Silence before MRe or CRe on answering: at least 400 ms. [doc]
  (10.2.2). D-Modem sends MRe after 200 ms of V.8's silence. [RE: D-Modem,
  code `V34V8::v8_step`].
- **Ruling C1:** we send CRe, as the real server did (spec decision 4
  amended from MRe). **Ruling C6:** at least 400 ms of silence before it,
  as 10.2.2 asks.
- If no V.8bis signal is detected within 3 s after MRe or CRe, the
  answering station may repeat it or clear down. [doc] (10.2.2).
- The client answers MRe with CRd, and also answers CRe. [RE: D-Modem,
  notes 1]. Before its blind-dial wait (S6, 2 s from ATD) has run out SM56
  ignores initiating signals: an MRe 0.3 s after ATD went unanswered, the
  same MRe 3 s after ATD was answered. [RE: D-Modem, notes 1]. With `+MS`
  set to V.92 SM56 still replies to MRe (a quick-connect message plus CRd)
  but never follows with an MS. [RE: D-Modem, notes 1].
- SM56 timeline (seconds in one run): MRe 3.040-3.545, CRd 3.720-4.225, CL
  4.345-4.985, MS 4.995-5.630, ACK 5.815-6.135. [RE: D-Modem, notes 1].
- D-Modem's server: CRd detected as its dual tone in at least 15 blocks of
  10 ms; CL 220 ms after the dual tone ends (about 120 ms after CRd ends);
  no CRd 1500 ms after MRe ends: V.8; no MS 2500 ms after the CL starts:
  V.8; ACK(1) 180 ms after the MS. [RE: D-Modem, code `v34_k56.h`
  `k8_rx_sample`, `k8_step`].
- The real server and client (gaps measured where the recording was made,
  so they include unknown network delays): CRe segment 1 about 290 ms,
  segment 2 about 105 ms; CRd begins about 70 ms after CRe ends; CRd segment
  1 about 393 ms, segment 2 about 110 ms; the CL begins about 405 ms after
  CRd ends; the MS begins about 220 ms after the CL ends; ACK(1) begins
  about 345 ms after the MS ends; ANSam begins about 400 ms after ACK(1)
  ends. [RE: ours, the recording].

## V.8bis messages

Line coding (V.8bis 7.2). [doc] unless marked:

- V.21: the initiating station (our server) sends on V.21(L), mark 980 Hz,
  space 1180 Hz; the responding station (the client) on V.21(H), mark 1650
  Hz, space 1850 Hz; 300 bit/s; ±0.01 %. (7.2)
- Preamble: 100 ms ±2 % of continuous mark (30 bits) before each message.
  (7.2.4). D-Modem sends 32 ones. [RE: D-Modem, code `v34_k56.h`
  `k8_frame`]. The real server sent about 32 to 35, the real client about
  41. [RE: ours, the recording].
- Flags `01111110`: 2 to 5 before the frame, 1 to 3 after the FCS. (7.2.5).
  D-Modem: 3 before, 2 after. [RE: D-Modem, code]. The real server: 3
  before, 1 after (CL and ACK); the real client: 4 before, 2 after (MS).
  [RE: ours, the recording].
- Octets in ascending order, bit 1 (the least significant) first. (7.2.2)
- Zero insertion after five contiguous ones, over everything between the
  flags (information field and FCS). (7.2.8)
- FCS: 16 bits, ISO/IEC 3309: x^16 + x^12 + x^5 + 1, register preset to
  ones, over the octets least significant bit first, the ones' complement
  sent; the receiver's remainder over frame and FCS is the fixed residue
  (in the reflected register form `0xF0B8`). (7.2.7). The FCS goes low
  octet first. [RE: D-Modem, code `bits.h` `hdlc_fcs`, `fax_hdlc.h`],
  confirmed: the three recorded frames' FCS octets equal this computation
  (below). [RE: ours, the recording].
- Invalid frame: not bounded by flags; fewer than three octets between
  flags; not a whole number of octets; or an FCS error. (7.2.9). On an
  invalid frame a station sends NAK(1) and returns to its initial state;
  any state other than telephony or MS mode held over 5 s returns to the
  initial state. (9.8)
- CL then MS (a CL-MS combination) has no gap; MS carries the minimum
  octets to select the mode. (9.1 Notes 1, 2)

Information field (V.8bis clause 8). [doc]:

- Three components: identification field I, standard information field S,
  non-standard field NS (optional). (8.1)
- Octet 0 (the I field's first): bits 1-4 the message type (Table 3: MS
  `0001`, CL `0010`, CLR `0011`, ACK(1) `0100`, ACK(2) `0101`, NAK(1) to
  NAK(4) `1000` to `1011`); bits 5-8 the revision (Table 4: revision 1
  `0001`, revision 2 `0010`). (8.3.1, 8.3.2)
- Then the I field's parameter tree, then the S field's. Each tree: the
  {NPar(1)} block, the {SPar(1)} block, then one Par(2) block for each
  SPar(1) bit set, in bit order. Level 1 octets carry 7 parameter bits and
  bit 8 is the delimiter (1 = last octet of the block). A Par(2) block is
  {NPar(2)} octets, optionally followed by {SPar(2)} and one {NPar(3)} block
  per SPar(2) bit set; level 2 and 3 octets carry 6 bits, bit 7 delimits
  each block, and bits 7 and 8 both set in the last NPar(2) octet means
  the Par(2) block has NPar(2) only. Receivers parse every block and ignore
  what they do not understand. (8.2.2, 8.2.3)
- ACK and NAK: octet 0 alone (no parameter field, no S field). (8.3.3,
  8.4.1)
- I field {NPar(1)} (Table 5-1): bit 1 V.8, bit 2 short V.8, bit 3
  additional information available, bit 4 transmit ACK(1), bit 7
  non-standard field. I field {SPar(1)} (Table 5-2): bit 1 network type
  (absent = analogue PSTN).
- S field {NPar(1)} (Table 6-1): bit 7 non-standard capabilities. S field
  {SPar(1)} octet 1 (Table 6-2a): bit 1 data, bit 2 simultaneous voice and
  data, ... Data {NPar(2)} octet 1 (Table 6-3a): bit 1 transparent data,
  bit 2 V.42, bit 3 V.42 bis, bit 4 V.14, bit 5 T.120, bit 6 non-standard
  capabilities.
- NS field: one or more blocks; each block is a length octet (the octets
  that follow: K + L + M + 1), a T.35 country code (K octets), a provider
  code length L (one octet), the provider code (L octets), and M octets of
  non-standard information. (8.5, Figure 10). T.35 country code `0xB5` is
  the United States. [doc] (T.35).
- Maximum information field: 64 octets. (8.6)
- V.8 bit: in a CL, "supports a V.8 start-up"; in an MS, "a V.8 start-up
  is requested". After an MS with "transmit ACK(1)" set, positively
  acknowledged, the station that received the MS sends ACK(1) then ANSam;
  with the bit clear it sends ANSam at once. The station that receives the
  MS configures its modem as the answer modem. V.8's CM and JM take
  priority over the MS's fields. (9.7, 9.9, 9.9.1)

**Field layout, octet by octet** (step 4 of task 1: each octet checked
against the V.8bis text above). [inf] from the tables above:

| Octet | CL we planned | Real server's CL | SM56's MS | Real client's MS | Meaning |
| --- | --- | --- | --- | --- | --- |
| 0 | `12` | `12` | `11` | `11` | type CL (`0010`) / MS (`0001`), revision 1 |
| 1 | `c9` | `c9` | `c9` | `c9` | I {NPar(1)}: V.8 (bit 1), transmit ACK(1) (bit 4), NS field (bit 7), last octet (bit 8) |
| 2 | `80` | `80` | `80` | `80` | I {SPar(1)}: none (analogue PSTN), last; no I Par(2) blocks |
| 3 | `80` | `80` | `80` | `80` | S {NPar(1)}: none, last |
| 4 | `80` | `81` | `80` | `81` | S {SPar(1)}: none / data (bit 1), last |
| 5 | (NS) | `c2` | (NS) | `c2` | the data Par(2) block: {NPar(2)} V.42 (bit 2), bits 7 and 8 (last, NPar(2) only) |
| next | `09` or `07` | `09` | `07` | `09` | NS block length |
| | `b5` | `b5` | `b5` | `b5` | T.35 country: USA |
| | `02` | `02` | `02` | `02` | provider code length 2 |
| | `00 94` | `00 94` | `00 94` | `00 94` | provider code |
| | `81 83 42 46 e0` or `81 83 61` | `81 83 42 46 e0` | `81 83 21` | `81 81 02 47 85` | non-standard information (see "NS block") |

The 9-octet block's length `09` = 1 (country) + 1 (L) + 2 (provider) + 5;
the 7-octet block's `07` = 1 + 1 + 2 + 3. Both agree with Figure 10.
[inf].

ACK(1) is the single octet `14`: type `0100`, revision 1. [doc]
(Table 3), [RE: D-Modem, notes 1], [RE: ours, the recording] (the real server's
ACK).

FCS of each form (low octet first; the recorded three match the frames as
received). [inf] (computed), [RE: ours, the recording] (recorded):

| Information field | FCS |
| --- | --- |
| `12 c9 80 80 81 c2 09 b5 02 00 94 81 83 42 46 e0` (real server's CL) | `23 50` (recorded `23 50`) |
| `12 c9 80 80 80 09 b5 02 00 94 81 83 42 46 e0` | `05 c8` |
| `12 c9 80 80 80 07 b5 02 00 94 81 83 61` (D-Modem's CL) | `d6 36` |
| `11 c9 80 80 80 07 b5 02 00 94 81 83 21` (SM56's MS) | `25 7a` |
| `11 c9 80 80 81 c2 09 b5 02 00 94 81 81 02 47 85` (real client's MS) | `71 d8` (recorded `71 d8`) |
| `14` (ACK(1)) | `dd a6` (recorded `dd a6`) |

**Ruling C2:** the CL we send by default is the real server's, `12 c9 80
80 81 c2 09 b5 02 00 94 81 83 42 46 e0` [RE: ours, the recording]. SM56's
acceptance of it is the gate once `glibc-devel.i686` exists; if SM56
refuses it, the 7-octet form (`12 c9 80 80 80 07 ...`) takes its place.

## NS block

The K56flex block, octet by octet, with `o[0]` the length octet (SM56's and
D-Modem's indexing):

| Index | Value(s) seen | Meaning | Tag |
| --- | --- | --- | --- |
| `o[0]` | `09` (real server, real client), `07` (SM56) | block length | [doc] (V.8bis 8.5) |
| `o[1]` | `b5` | T.35 country, USA | [doc] (T.35) |
| `o[2]` | `02` | provider code length | [doc] (V.8bis 8.5) |
| `o[3] o[4]` | `00 94` | provider code; `94` = "K56flex" | [doc] (Telenetics `AT&V`: "second byte of manufacturer id, 94 = K56flex") |
| `o[5]` | `81` | licensee (`81` = Conexant, formerly Rockwell) | [doc] (Telenetics) |
| `o[6]` | `83` (real server, D-Modem server), `81` (Telenetics example `9481814347C4`, a server), `81` (real client, forced K56flex), `83` (SM56 client with V.90 on) | product capabilities; bit 0 K56flex, bit 1 V.90 | [doc] (Telenetics: "manufacturer's product capabilities"); the bit meanings [inf] (SM56 sets bits 0 and 1 when its V.90 is on, [RE: D-Modem, notes 1]; the real client forced to K56flex sends `81`, [RE: ours, the recording]) |
| `o[7]` | `42` (real server), `43` (Telenetics example), `61` (D-Modem server), `21` (SM56 client), `02` (real client) | K56flex version; bit 6 (`0x40`) set by a digital modem | [doc] (Telenetics: "K56flex version number"); `0x40` = digital modem [inf]: set in every server's, clear in both clients' ([RE: ours, the recording] and [RE: D-Modem]) |
| `o[8]` | `46` (real server), `47` (real client) | pump code version | [doc] (Telenetics) |
| `o[9]` | `e0` (real server), `85` (real client), `c4` (Telenetics example) | x-law and controller version: bit 6 1 = law not forced, bit 5 1 = mu-law, bits 4:0 controller version | [doc] (Telenetics) |

The default block (decision 3, confirmed by the recording): `09 b5 02 00 94
81 83 42 46 e0`. It is exactly the real server's block, and the Flex field
`9481834246E0` Gough Lui's modem printed after the call ([RE: Gough Lui,
`AT&V1`]). The compile-time alternative (decision 3): SM56's proven `07
b5 02 00 94 81 83 61`. No third form: the recording's block is the
9-octet one. [RE: ours, the recording].

Notes:

- The Rockwell `AT&V` "Flex" octets are the server's block octets 4 to 9
  (`o[4]` to `o[9]`), and Telenetics numbers them "octets 13 to 18". With
  the real server's CL, `94` is octet 13 when the octets are numbered as in
  V.8bis Figure 4, the two mandatory opening flags being octets 1 and 2.
  [inf] (consistent, not proof).
- The real server's x-law octet said mu-law (`e0`) while the call ran
  A-law (INFO1, "Phase 2"); the client's said A-law, forced (`85`). The law
  is settled by INFO1, not by this octet. [RE: ours, the recording].
- SM56's own test of a CL's block (it is the client): `L > 6`, `o[1] ==
  0xb5`, `o[2] == 0x02`, `o[6] & 1`, `(o[7] & 0x2f) >= 2`, `o[7] & 0x40`; it
  does not test `o[3]`, `o[4]`. A block with `o[6]` bit 0 clear gets an MS
  without the block, and the call is V.34. [RE: D-Modem, notes 1]. Both
  candidate blocks and the real server's pass it: `o[7]` = `0x42` gives
  `0x02`, `0x61` gives `0x21`, both `>= 2`, both with `0x40`. [inf].

## MS test

What makes a received message an MS that selects K56flex (we then send
ACK(1); decision 2). From D-Modem's server check (`k8_ms_k56()` with
`k8_msg_k56()`, [RE: D-Modem, code `v34_k56.h`]), stated as conditions,
with one correction from the recording:

1. The frame is valid (V.8bis 7.2.9: FCS good, at least 3 octets between
   the flags, whole octets).
2. Octet 0's low four bits are 1 (MS). The revision (high four bits) is not
   tested.
3. The I field's {NPar(1)} first octet (octet 1) has bit 7 (`0x40`, "NS
   field") set.
4. The NS field is found after the I and S fields. **D-Modem skips exactly
   four level 1 blocks (each up to an octet with bit 8 set); the real
   client's MS has a data Par(2) block in its S field (`81 c2`), which that
   skip reads as an NS block of length `0xc2` and rejects.** The NS field
   must be found by parsing the two trees as V.8bis 8.2.3 says ("V.8bis
   messages"): for each SPar(1) bit set, one Par(2) block. [inf]
   (from the recording and 8.2.3).
5. Among the NS blocks (each a length octet `L` then `L` octets), one has
   `L > 6`, `o[1] == 0xb5`, `o[2] == 0x02` and `o[6] & 1`, and lies whole
   inside the message. (D-Modem accepts a block whose last octet is
   missing; we require the whole block.) [inf].
6. No condition on `o[7]`: the client clears `0x40` in its MS (SM56 sends
   `21`, the real client `02`). [RE: D-Modem, notes 1], [RE: ours, the recording].

The SM56 MS `11 c9 80 80 80 07 b5 02 00 94 81 83 21` and the real client's
`11 c9 80 80 81 c2 09 b5 02 00 94 81 81 02 47 85` both pass. [inf].

An MS that fails (no block, or a block without K56flex) is not answered
with K56flex: V.8 follows as for no MS. [RE: D-Modem, code `k8_step`].

**Ruling C3:** the MS is parsed by the V.8bis 8.2.3 trees (condition 4),
not by D-Modem's fixed block skip. **Ruling C6:** NAK(1) only for an
invalid frame (bad FCS, or a field that does not parse); ACK(1) to every
valid MS carrying our block (conditions 1 to 5).

## Phase 2

Phase 1 continues as plain V.8 after ACK(1): our ANSam, its CM, our JM, its
CJ. [doc] (V.8bis 9.9.1). The JM must select V.34, not PCM: a JM
selecting V.90 makes the client run V.90 and drop K56flex. [RE: D-Modem,
notes 1].

- SM56's CM: `e0 c1 45` with `+MS=K56FLEX,0`; the V.90 CM `e0 c1 65 13 94
  27 0d` in automode or V.90. [RE: D-Modem, notes 1] (the sync octet `e0`
  first, then the information octets, each least significant bit first).
- The real client's CM (forced K56flex): `e0 c1 45 13 94 2a 0d`: data call,
  V.34, V.32, V.22, V.23, V.21, no PCM category, LAPM, an access category
  with no bits. The real server's JM is the same seven octets: V.34
  selected, and no "digital network" access bit (unlike the x2 server's
  JM). [RE: ours, the recording] (decoded with `modem/v8msg.c`).

V.34 Phase 2 with the server as the V.34 **answer** modem (INFO0a, tone A,
INFO1a on 2400 Hz) and the client as the call modem (INFO0c, tone B, INFO1c
on 1200 Hz); INFO0 and the probing are standard. [RE: D-Modem, notes 2],
[RE: ours, the recording] (INFO1a found on 2400 Hz, INFO1c on 1200 Hz).

Frequency offset field, 10 bits, two's complement, 0.02 Hz units, sent
least significant bit first (`modem/info.c`): [doc] (V.34 Tables
14, 15). K56flex uses it:

- INFO1c bits 79:88: `0x001` when the client accepts K56flex, is mu-law and
  its tandem-codec check passed; `0x200` (the V.34 "ignore" value)
  otherwise. [RE: D-Modem, notes 2]. Bit 3 is the client's law (1 =
  A-law), bit 4 the x of an earlier INFO1a. [RE: D-Modem, notes 2:
  inferred there]. The real (A-law) client sent `0x009`: bit 0 and bit 3.
  [RE: ours, the recording], which confirms bit 3 = A-law.
- INFO1a bits 40:49: `1 | law << 2 | x << 3`: 1 or 9 mu-law, 5 or 13 A-law.
  Against SM56 (mu-law): 1 and 9 kept K56flex; 0, 2, 3, 5, 13, `0x11` and
  `0x200` made the call V.34. The meaning of x is unknown; send 1 to a
  mu-law client. [RE: D-Modem, notes 2]. The real server sent `0x005`
  (A-law, x = 0) to the A-law client. [RE: ours, the recording], which confirms
  the law bit and x = 0 from a real server.
- D-Modem's own client accepts an INFO1a offset whose low two bits are `01`
  (bit 2 its PCM law). [RE: D-Modem, notes 6]. Hence decision 6's nudge
  of a measured offset whose low bits are `01` in an agreed call that goes
  V.34. [inf].
- The client tests for a second codec in the path using our L1/L2 probe:
  with P(f) the received power of the probe tone near f, it requires
  P3300 >= 0.1 P2100 and P3750 >= about 0.018 to 0.020 P3300. If either
  fails it sends INFO1c `0x200`, so our L1/L2 have to be V.34's unmodified
  probing. [RE: D-Modem, notes 2].
- The real server's INFO1a, all fields (`modem/info.c` `info1a_parse`):
  minimum power reduction 1, additional 3, MD length 0, low carrier,
  pre-emphasis 1, maximum rate 13, call-to-answer symbol rate 3200 (index
  4), answer-to-call 3200 (4), offset 5. The real client's INFO1c: power
  reductions 0, MD 0, maximum rates per symbol rate 2400: 9, 2743: 0, 2800:
  10, 3000: 11, 3200: 13, 3429: 14, offset 9. [RE: ours, the recording].
- The upstream is at 3200 baud (the client's codec runs at 9600 Hz). [RE: D-Modem, notes 3]; the real server selected 3200. [RE: ours, the recording].
- After a flagged INFO1a the client skips V.34 Phase 3 and waits for the
  server's PCM; against V.34's Phase 3 it is silent about 0.7 s, then
  retrains. [RE: D-Modem, notes 3].

## Training down

The server's sequence that SM56 accepts (decision 8), segment by segment,
in symbols (8 kHz codes). [RE: D-Modem, code `k56flex_train.cpp`
`K56Train`, `v34_k56.h` `k56_step`; notes 3] unless marked.

One scrambler (GPA) runs from R1 through E: its register is cleared at R1's
first symbol and its input is given per segment. "Hist" is the scrambler's
output bits so far, the newest (this symbol's last) in bit 0. One running
sum (the values of the codes sent, mu-law 14-bit units, or A-law 13-bit)
is cleared at R1's first symbol; R's and silence's codes are not added,
every other segment's are. In the segments marked "alternating", the
symbols are counted from 0 at the start of TRN1B and of TRN2A, and the
count runs on, without restarting, across TRN1B-TRN1C-Js and
TRN2A-MARK-TRN2B-MP-MP'-E (TRN1B, TRN1C, TRN2A and MARK have even lengths,
so TRN1C, Js, MARK and TRN2B start on an even symbol; TRN2B's length can be
odd, so MP may start on an odd one); the odd ones are the running sum's:
magnitude as given, **negative when the running sum (before this symbol)
is above 0, else positive**.

| Segment | Length | Codes |
| --- | --- | --- |
| Silence 1 | 512 | code `0x80` (magnitude 0, positive: mu-law octet `ff`) |
| R1 | 288 | magnitude `0x54`, signs `+ + - -` repeating from the first symbol; from symbol 264 on the pattern is shifted by two (inverted): the last 24 symbols are R-bar |
| TRN1A | 16368 | input ones, 1 scrambled bit a symbol; positive when the scrambled bit is 1; magnitude `0x4f` when hist bit 24 is 1, else `0x4e` |
| TRN1B | 8448 | alternating. Even symbols and odd alike scramble 3 input ones (b0 first); magnitude by `b0 + 2 b1 + 4 b2` from `40 41 4b 4c 50 51 50 51`; even symbols: positive when hist bit 23 is 1; odd: the running sum's |
| TRN1C | 8448 | alternating. Even: 1 scrambled input one, magnitude `0x4f`, positive when it is 1. Odd: `0x4f`, the running sum's (no scrambler step) |
| Js | 128 | alternating. Even: 1 scrambled bit of J (`0x89b0`, least significant bit first, 16 bits round and round: 4 Js), `0x4f`, positive when the scrambled bit is 1. Odd: as TRN1C |
| Silence 2 | until the client's J24, at least 12288, rounded up to a multiple of 8 | code `0x80` |
| R2 | 288 | as R1, but the scrambler and running sum go on |
| TRN2A | 16336 (2048 in a renegotiation) | as TRN1C (alternation restarts at 0), but see "Bits a symbol" below for a renegotiation |
| MARK | 32 | as TRN1C with input zeros |
| TRN2B | until the client's MP, at least 2048, then until the grid below | alternating. Even and odd scramble 2 input bits, **a one then a zero** (the 2-bit value 1, lowest bit first; corrected in k56 8, see below), b0 first; magnitude `pad2[b0 + 2 b1]`; even: **negative when hist bit 24 is 1**; odd: the running sum's |
| MP | 704 | as TRN2B, input the 88 bits of our MP (11 octets, each least significant bit first) round and round: 16 MPs at 2 bits a symbol (8 at 1 bit) |
| MP' | 704 | the same with our MP' (the acknowledge bit set) |
| E | 12 (24 in a renegotiation) | as TRN2B, input ones: 24 ones |
| data | | B1 first (see "Phase 4") |

- **Corrections (k56 8, 2026-11-21)**, found when our transmitter was
  compared with the training oracle ("Oracle") and confirmed in
  `K56Train::next` / `level` / `segment` (`dsp/k56flex_train.cpp`).
  [RE: D-Modem, code], [our observation: the oracle]:
  - TRN2B's input is not ones: each symbol scrambles the 2-bit value 1,
    lowest bit first, so a one and then a zero. E's input is ones (the
    value 3).
  - **Bits a symbol.** The even symbols of TRN1C, Js, TRN2A and MARK
    scramble as many bits as the last TRN2B, MP, MP' or E symbol did: the
    input value (1, J's bit, or 0 for MARK) lowest bit first, the sign the
    first scrambled bit. The count is 1 from the start of the training; 2
    from the first TRN2B on; 1 after a renegotiation's MP. So the first
    renegotiation's TRN2A and MARK scramble 2 bits an even symbol (a one
    and a zero; two zeros), a later one's 1; a retrain (the training from
    silence 1) is back to 1. The oracle's renegotiation equals ours only
    this way.
  - The client is not disturbed by it: GPA's output taken every second bit
    obeys the same recurrence (squaring maps the roots of 1 + x^5 + x^23
    onto themselves over GF(2)), so the even signs of TRN2A still
    descramble one bit a symbol to ones, and MARK's to zeros, after 23
    bits. [derived]; checked in `test/test_k56tx.c` for the call, a first
    and a second renegotiation [our observation].
- Pad magnitudes ("pad2", 2 bits a symbol, `d0 + 2 d1` = 0..3; "pad1", 1
  bit a symbol, 0 then 1). TRN2B, MP, MP' and E use the pad the client's
  J reports; the data uses the rate's constellation for that pad. [RE: D-Modem, notes 4, 5; code `k56_trn2b_mag`, `k56_trn2b_mag1`]:

  | Pad | pad2 | pad1 |
  | --- | --- | --- |
  | 0 dB | `2f 45 51 57` | `3d 55` |
  | 3 dB | `35 4d 58 60` | `44 5d` |
  | 6 dB | `3d 54 61 67` | `4c 65` |

  The 1-bit order is the client's: the first magnitude is 0. In the first
  Phase 4, 2 bits a symbol; in a renegotiation, 1 bit (MP, MP', E). [RE: D-Modem, notes 4].
- The client accepts only a 16-level Js: its detector reads 16
  descrambled bits from the even signs, most significant first, and moves
  on only on `0xd91` (`0x89b0` bit-reversed); `0x991` (4-level) is
  dropped. [RE: D-Modem, notes 3].
- TRN1B is required. SM56's unused transmitter lacks it; a training sent
  that way puts the client's Js search 8448 symbols ahead of the real Js,
  which it then misses. [RE: D-Modem, notes 3].
  The client's reference for TRN1C, TRN2A is one level `0x4f`, corrected
  for robbed bits and loss; TRN1A's reference ignores the `0x4e`/`0x4f`
  choice. [RE: D-Modem, notes 3].
- The client's patience for R2, counted from its J24, is about 0.75 s:
  R2 has to be triggered by detecting the J24, a timeout would be too late.
  [RE: D-Modem, notes 3].
- The client's front end, after Js, waits 12273 symbols and then looks for
  R2. [RE: D-Modem, notes 3].
- The client looks for the end of TRN2A as 16 descrambled ones followed by
  16 zeros (TRN2A's last ones and MARK). [RE: D-Modem, notes 4].
- In D-Modem's server PCM starts 40 ms after INFO1a has been sent (its
  converters' delay; not a protocol value). The real server's R1 starts
  about 80 ms (about 640 symbols) after its INFO1a ends. [RE: D-Modem,
  code `k56_step`], [RE: ours, the recording].
- The real server's segments differ from these lengths ("Recording"). The
  client of that call accepted them; decision 8 keeps SM56's.

## Upstream and J24

The client's upstream, at 3200 baud as a V.34 call modem. [RE: D-Modem,
notes 3]:

- Phase 3 up: the client stays silent until it has decoded our Js, then
  sends V.34's start of Phase 3 with fixed lengths in T (S 128, S-bar 16,
  PP 288) and TRN, whose length varies with the echo delay and the rate
  (echo delay + 4000 to 6430 T; D-Modem's own client uses 2048). J24 then
  repeats until it sees our TRN2A.
- Phase 4 up: J' (`0x899f`) directly after the last complete J24, then TRN,
  MP, MP', E and B1 as a V.34 call modem sends them. Because our Js asked
  for 16 levels, TRN, MP and E are 16-point (4 bits a symbol, V.34
  10.1.3.8).

- The upstream descrambler is GPC (tap 18): D-Modem's server starts its
  K56flex upstream receiver with `prx.init(baud_rx, high_rx, 0)` ("not
  caller", `v34_k56.h` `k56_start`), and `V34Rx::init` takes `tap = caller
  ? 5 : 18`. [RE: D-Modem, code `v34_k56.h`, `v34_modem.cpp`]. This
  settles decision 9's GPC; the SM56 call of Task 9 is to confirm it as our
  observation. **Confirmed (k56 9, 2026-11-21):** against SM56 (`make
  test-k56-sm56 MODS=k56`), our receiver, descrambling with GPC, found
  SM56's S, its J24 (`0x89b0`, octet `00`) twice 24 bits apart, and its J'
  24 bits after the last J word. [our observation: the SM56 call].
- **J24**: a 24-bit word sent least significant bit first, 2 bits a symbol
  (12 symbols), 4-point, scrambled and differentially encoded as V.34's J:
  bits 0-15 the V.34 J (`0x89b0` asks for 16-point Phase 4, `0x8990` for
  4-point), bits 16-23 the octet. SM56 sends `0x89b0`. [RE: D-Modem, notes
  3; code `v34_modem.cpp` J transmit and receive].
- The octet. [RE: D-Modem, notes 5]:

  | Bits | Meaning |
  | --- | --- |
  | 0 to 5 | bit k set: symbol k of the 6-symbol cycle is robbed, k counted from TRN1B's first symbol |
  | 6, 7 | pad: 0 none, 1 3 dB, 2 6 dB (3 not defined) |

  D-Modem's tests: a 3 dB pad gave `0x40`, robbed `0x24` on the line gave
  `0x12`, a 6 dB pad with robbed bits `0x98`, SM56 with robbed bits `0x04`.
  [RE: D-Modem, notes 6].
- Receiving it: two equal J words 24 bits apart are the J; the octet is the
  8 bits between them. J' is the J' word 24 bits after the last J. [RE: D-Modem, code `V34Rx`].
- The J word also sets our downstream Phase 4 density: 2 bits a symbol
  after `0x89b0`, 1 after `0x8990`. [RE: D-Modem, notes 3].
- The real call: the client printed "RBS Pattern 00", "Digital Loss None";
  its J24 octet is then presumably `0x00`. [RE: Gough Lui, `AT&V1`],
  [inf]; the J24 itself was not decoded from the recording.
- The server's echo canceller can train on its own TRN1A to Js, while the
  client is silent. [RE: D-Modem, PR #44].

## Phase 4

The server's MP and MP' are V.34's type 0 (88 bits: frame sync, start bits,
fields, CRC, fill, as `modem/v34mp.c` builds them). [RE: D-Modem, notes 4;
code `k56_make_mp`] for the K56flex meanings; [doc] (V.34 Table
20) for the layout:

| Field | V.34 bits | K56flex meaning |
| --- | --- | --- |
| type | 18 | 0 |
| `rate_ca` (max call to answer) | 20:23 | the upstream rate N (N x 2400) |
| `rate_ac` (max answer to call) | 24:27 | the downstream rate code r, 1..13: 30000 + 2000 r bit/s (not N x 2400) |
| trellis, non-linear, shaping | 29:32 | for the client's transmitter (decision 9: 16 states, linear, minimum) |
| ack | 33 | 0 in MP, 1 in MP' |
| rate mask | 35:48 (49 reserved) | non-zero; D-Modem's server sends `0x3fff` |
| asymmetric | 50 | 1 |

- The client accepts the MP (V.34's MP detector on the descrambled bits) only
  when `rate_ca`, `rate_ac` and the mask are all non-zero; else it clears
  down. It sets its upstream to min(`rate_ca`, 12 or 13) and r to
  min(`rate_ac`, its own). [RE: D-Modem, notes 4]. r = 1 is 32000, r =
  13 is 56000: SM56 calls `pstn_init_rx(30 + 2 r)`. [RE: D-Modem, notes
  4, 5].
- The client's MP: type 0, acknowledge bit set, mask `0x7fff` (bit 49 set
  too), asymmetric, bits 52 to 59 ones and 60 to 67 zeros (not V.34's
  zeros), V.34's CRC at bits 69 to 84. [RE: D-Modem, notes 4]. Our
  `v34_mp_parse` keeps 14 mask bits and does not test bits 49 or 52 to 67.
  [inf] (read in `modem/v34mp.c`).
- Rates: the server takes r = min(the client's `rate_ac`, its own maximum);
  upstream the highest N set in the client's mask (`& 0x3fff`) up to
  min(the client's `rate_ca`, its own maximum) that exists at the symbol
  rate. [RE: D-Modem, code `k56_data_setup`]. SM56 connected at 44000
  down, 31200 up; D-Modem's client at 56000 down, 31200 up. [RE: D-Modem,
  notes]. The real call: 48000 down, 28800 up. [RE: Gough Lui, `AT&V1`].
- After our MP the client accepts MP' (ack 1), then E (24 ones), waits 14
  symbols, takes B1 (48 symbols, content not checked), then data. [RE: D-Modem, notes 4].
- **B1**: the first 6 data frames, their data bits all ones, through the
  data encoder ("Data"); the client discards the first 48 symbols. [RE: D-Modem, code `k56_sample`; notes 5].
- **The grid of 24**: the client counts symbols modulo 24 from TRN1B's first
  symbol into data mode and takes the robbed pattern from the first data
  symbol. So the number of symbols from TRN1B's first symbol to the first
  data symbol is a multiple of 24. The server lengthens TRN2B: MP starts
  at the first symbol (after the client's MP and 2048 symbols of TRN2B) for
  which (symbols from TRN1B's first symbol to MP's first) + 1420 (1432 in a
  renegotiation) is a multiple of 24. With robbed bits on the line SM56's
  data was right only with this. [RE: D-Modem, notes 5; code
  `k56_step`].
- **Data start**: 1420 symbols after TRN2B ends (MP 704 + MP' 704 + E 12),
  1432 in a renegotiation (E 24). [RE: D-Modem, notes 4, 5].
- D-Modem's waits: the client's J within 3000 ms of silence 2 (else R2
  anyway); the client's MP within 8000 ms of TRN2B's start (else fail).
  [RE: D-Modem, code `k56_step`].
- The upstream's Phase 4 is the V.34 caller's: J', TRN, MP, MP', E, B1;
  our E follows its whole MP' once our MP' is in. [RE: D-Modem, notes 3,
  6].

## Data

Frames of 8 symbols, R/1000 data bits each on a clean line, R = 30000 +
2000 r. [RE: D-Modem, notes 5]. Per frame, in this order. [RE: D-Modem,
code `K56Enc::frame`; notes 5]:

1. Take the frame's data bits (time order), scramble each through GPA (one
   bit at a time; octets least significant bit first), and number them 0
   upward: bit i of a word w.
2. The shell index S is w's low K' bits, shifted left by the robbed
   adjustment ("Robbed bits"; 0 on a clean line, K' = K); U is the rest of
   w (shifted down by K').
3. V.34's shell mapper (9.4, `v34_shell_map`, g2/g4/z8 for M rings) maps S
   to rings m0..m7; ring m0 goes to symbol 0, and so on.
4. For each symbol i (0..7): its group g = `k56_ring[pad][rate][m_i]`; its
   sign and q = u - 1 bits come from U, lowest first:
   - a shaping symbol (every r-th symbol, see below): no sign bit; the q
     bits are the point's low q bits; **positive when the running sum is 0
     or below**;
   - otherwise: the first bit is the sign (1 positive), the next q bits are
     the point's low q bits.
   The point p = g x 2^q + (q bits); the magnitude index m =
   `k56_bios[pad][rate][p]`; the code = m, with bit 7 for positive.
5. Each code's value is added to the data's running sum as it is made.

- **Shaping symbol**: a counter of data symbols since the last shaping one;
  the r-th symbol (the counter starting at 0 at the data start, so the
  first shaping symbol is data symbol r - 1) and every r-th after it,
  across frame boundaries. [RE: D-Modem, code `shaping_next`].
- **What restarts at each data start** (**Ruling C4**) (the first, and after every
  renegotiation and retrain): the data scrambler (register 0), the running
  sum (0), the shaping counter (0) and the 3-frame cycle (frame 0 first).
  D-Modem builds a new encoder on each MP received (`k56_data_setup` calls
  the encoder's init, which clears all of it), and SM56 re-initialises its
  decoder (`pstn_init_rx`) on each MP it receives (`SC_Phase4_Rec_MPs`).
  [RE: D-Modem, code; notes 5]. So they do **not** carry across a
  renegotiation; the training's scrambler and running sum are separate from
  the data's (see "Rate renegotiation"). The descrambler synchronises
  itself within 23 bits, inside B1. [inf].
- Bits per frame = K + 8 q + 8 - 8/r (the K shell bits, q bits per symbol,
  8 signs less the 8/r shaping ones), = R/1000 at every rate ("Tables").
  [inf]; [doc] in form (US 6,081,555: b = R/1000 + r/8 with
  the redundancy counted the other way).
- The rings are ordered by cost (`k56_ring` maps the shell mapper's ring
  to the constellation group); the shell mapper is unchanged by the sign
  choice because a point and its mirror share a ring. [RE: D-Modem, notes
  5], [doc] (US 6,278,744, US 6,081,555).
- On the line: mu-law `0xff - m` or `0x7f - m`; A-law code xor `0x55`.
  [RE: D-Modem, notes 5].
- Bit order follows SM56's decoder (least significant bit of each octet
  first). SM56's unused encoder takes octets the other way round and the
  decoder does not compensate, so the encoder is not the reference.
  [RE: D-Modem, notes 5].
- D-Modem's encoder, fed into SM56's own decoder (`pstn_init_rx`, `putBIOS`)
  at every rate, pad and 9 robbed-bit patterns: all 351 combinations
  decode. [RE: D-Modem, PR #39].
- SM56 and Zoom (Lucent) also have 58000 and 60000 tables (r = 16, K one
  less in every second frame); no client selects them. [RE: D-Modem, notes
  5, 7]. Not used (non-goal).

## Robbed bits

[RE: D-Modem, code `K56Codec::init`, `shell_bits`, `frame_bits`, `frame`;
notes 5] unless marked:

- Only above 34000 bit/s (at 32000 and 34000 q = 0: no bit to rob).
- The 6-symbol mask comes from the J24 octet (bits 0-5), symbol k of the
  cycle counted from TRN1B's first symbol; on the grid of 24 the data's
  first symbol is cycle position 0. Data symbol j of the 3-frame cycle (j =
  0..23, frame j / 8) is robbed when mask bit (j mod 6) is set.
- n6 = the number of robbed symbols in 6; 5 is treated as 6 (all six
  robbed). The loss per frame L(n6) = `0 2 4 4 6 8 8` for n6 = 0..6.
- n = the robbed symbols in this frame (it depends on the frame's place in
  the 3-frame cycle).
- When n6 is not 3: the frame takes R/1000 - L data bits; the shell mapper
  takes K' = K - (L - n) bits, and the index is shifted left by L - n (so
  its range is that of K bits).
- When n6 is 3: K' = K, no shift, and the frame takes R/1000 - n data bits.
  (The general rule would give a negative shift for a frame with 5 robbed
  symbols.)
- A robbed symbol's lowest q bit (bit 0 of the point) is 0: it takes one
  bit fewer from U (sign then q - 1 bits into point bits 1..q-1; a shaping
  robbed symbol takes q - 1 bits). The decoder treats a code and the code
  with its last bit inverted alike.
- The line model of a robbed bit (D-Modem's netsim `NET_RBS=<mask>`): in
  the samples whose count mod 6 has its bit set in the mask, the G.711
  octet's least significant bit is forced to 1 (after the pad, if any), as
  T1 trunks rob it every sixth frame. [RE: D-Modem, code
  `dsp/test/netsim.cpp` `Net::digital`]. For a positive mu-law code (octet
  `0xff - m`) that clears bit 0 of m; for a negative one (`0x7f - m`) it
  also clears bit 0 of m. [inf].
- The pad on the line (netsim `NET_PAD`): each code re-encoded that many dB
  lower. [RE: D-Modem, code `netsim.cpp`].
- The client's detector (`MOD_Det_DL_RBS`, `mask_RBS`) averages, for each
  of the 24 cycle positions and each of TRN1B's 6 magnitudes, what it
  receives after skipping TRN1B's first 240 symbols, and fits a model of
  pad plus robbed bit (before or after the pad). The 24 positions fold to
  6: position k of 6 is reported robbed if any of its 4 occurrences is.
  [RE: D-Modem, notes 6].
- SM56 cannot train behind a pad (its TRN2B thresholds are the pre-pad
  ones), with any server. [RE: D-Modem, notes 6].

## Tables

Per rate (i = r - 1 = (R - 32000) / 2000). [RE: D-Modem, code
`k56flex_tables.cpp` `k56_rates`; notes 5]:

| R (kbit/s) | 32 | 34 | 36 | 38 | 40 | 42 | 44 | 46 | 48 | 50 | 52 | 54 | 56 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| K | 28 | 30 | 24 | 26 | 26 | 28 | 30 | 24 | 25 | 27 | 29 | 31 | 25 |
| u (q = u - 1) | 1 | 1 | 2 | 2 | 2 | 2 | 2 | 3 | 3 | 3 | 3 | 3 | 4 |
| r (shaping period) | 2 | 2 | 2 | 2 | 4 | 4 | 4 | 4 | 8 | 8 | 8 | 8 | 8 |
| M, 0 dB | 13 | 15 | 9 | 11 | 11 | 13 | 15 | 9 | 10 | 11 | 13 | 16 | 9 |
| M, 3 dB | 13 | 15 | 9 | 11 | 11 | 13 | 15 | 9 | 10 | 11 | 14 | 16 | 10 |
| M, 6 dB | 13 | 15 | 9 | 11 | 11 | 13 | 15 | 9 | 10 | 11 | 13 | 16 | 10 |

Checks. [inf] (computed):

- K + 8 q + 8 - 8/r = R/1000 at all 13 rates (32: 28 + 0 + 8 - 4; 56: 25 +
  24 + 8 - 1).
- M^8 >= 2^K at every rate and pad (the tightest: 54000, 16^8 = 2^32 >=
  2^31; 32000, 13^8 = 2^29.6 >= 2^28; 52000 at 0 and 6 dB, 13^8 = 2^29.6 >=
  2^29). At M = 16, z8's total (16^8 = 2^32) does not fit 32 bits, but the
  31-bit index stays below it.
- The robbed-bit loss `0 2 4 4 6 8 8`: [RE: D-Modem, code
  `k56_rbs_loss`].

The ring orders (`k56_ring`, M entries per rate and pad) and the
constellations (`k56_bios`, M x 2^q magnitude indexes per rate and pad) are
D-Modem's reading of SM56's tables. [RE: D-Modem, code
`k56flex_tables.cpp`]. They are irregular (0 dB, 32000: `2 3 4 5 6 7 1 8
0 10 9 11 12`), and the patents say the reorder tables were built
iteratively under a power limit. [doc] (US 6,081,555). The
patents' own tables (1996) differ from SM56's at several rates (for
example 52000 M 14 against 13, 56000 r 16 against 8); the shipped client
follows SM56's. [doc] (the patents' tables), [inf] (the
comparison). Rebuild or include: decision 11, task "Tables".

The rule tried (k56 3, 2026-11-21, a scratch script outside the
repository, against D-Modem's lists at `c06a380d`). [our observation]:

- **M per pad**: no rule tried beyond the checks above; M^8 >= 2^K leaves
  several M at each rate (52000: 13 at 0 and 6 dB, 14 at 3 dB). Included
  as the facts' table.
- **`k56_ring`**, rings in order of their group's energy (the sum of the
  squared mu-law values of its 2^q points, with or without the pad): every
  constellation is increasing, so this order is 0, 1, ..., M - 1, and none
  of the 39 lists is that. 0 of 39. Included.
- **`k56_bios`**, a minimum-distance ladder in linear terms (each point
  the smallest magnitude whose value is at least d above the last):
  - before the pad, the first point the smallest with 2 v >= d and d the
    table's own smallest gap: 13 of 39 (the 0 dB tables but 56000's,
    and 6 dB at 32000); with d the largest that fits the table's top
    magnitude: 11 of 39 (0 dB, 34000 to 54000);
  - behind the pad (each value re-quantised to mu-law after a gain of
    -3 or -6 dB), from the table's own first point with d its smallest
    gap: 25 of 39 (0 dB but 56000; 6 dB but 56000; 3 dB only at 32000).
    No d at all reproduces the other 14 from their first points; the
    3 dB tables depart within a few points of the top (38000: ours 83
    84 86 88, the table's 84 87 88 89).
  So even where the ladder matches it needs each table's first point and
  spacing given; the patents' power-limited iterative design is not
  reproduced. Included.

So all three are included: `modem/k56tab.c`, generated by
`tools/gen_k56_tables.py` from D-Modem's `dsp/k56flex_tables.cpp`
(`docs/limits.md`, "Included tables (K56flex)").

## Rate renegotiation

[RE: D-Modem, notes 5; code `v34_k56.h` `k56_reneg_start`, `k56_sample`,
`k56_step`] unless marked:

- Client-started: S (as V.34's) appears in the upstream during data mode;
  we answer with R2 at the next frame boundary, never inside a frame.
- Down (server): R2 288, TRN2A 2048, MARK 32, TRN2B until the client's MP
  (and the grid), MP 704 and MP' 704 at **1 bit a symbol** (pad1
  magnitudes; 8 MPs each), E 24 (24 ones), then data (B1 first) on the grid
  of 24. 1432 symbols from MP to data.
- Up (client): S 128, S-bar 16, TRN (4 points), MP until the server's MP,
  MP', E, B1, data.
- The training's scrambler, hist and running sum go on from where E left
  them in the last Phase 4 (only R1 clears them); the alternation restarts
  at TRN2A. The symbol count for the grid goes on through data mode. [RE: D-Modem, code `K56Train::segment`, `k56_sample`].
- The data's scrambler, running sum, shaping counter and 3-frame cycle
  restart ("Data").
- SM56's MARK search runs only between about symbol 924 and symbol 5000
  of TRN2A, which rules out the first Phase 4's 16336 and is why TRN2A is
  2048 here. In a renegotiation SM56's receiver starts straight at its R
  search (no silence, no first R) and adapts over 4096 symbols of TRN2B.
- SM56's triggers (every 501 interrupts, from the mean error L(r) each rate
  accepts): 4 checks in a row above (L(r) + L(r-1))/2: down one rate (none
  at 32000); 3 checks above 4 times that: retrain; 17 checks below 0.8
  L(r+1) + 0.2 L(r+2): up (none at the top, nor to the rate the last
  renegotiation went down from); more than 5 renegotiations down: retrain.
- Server-started (we never do this, decision 13): R replaces the data
  and the client, on spotting R, replies with S. SM56 survives the
  exchange but its data error then runs about 10 times higher and it
  retrains about 4 s later; the likely cause is its adapted level table.
- A client MP with rate 0 both ways is V.34's cleardown (11.7). [doc]
  (V.34); no K56flex source covers it.

## Retrain

[RE: D-Modem, notes 5 "Retrains"]:

- The mechanism is V.34's retrain (11.5): silence and then tone B from a
  client that starts it, tone A from a server. The far end, spotting the
  tone in data mode, joins in. Both repeat Phase 2 (INFO1)
  and then the whole training from silence 1 with the scrambler and sums
  cleared; there is no second CONNECT.
- A tone A from the server is misread by SM56: it hunts for a
  renegotiation's R, times out (causes `0x26`, `0x27`), and only then
  repeats Phase 2 from its V.34 side.
- SM56 retrains (cause `0x10`, `0x23`) when its data error exceeds 4 times
  the down limit 3 times in a row, or on its 6th renegotiation down. In the
  training of a retrain SM56 fails about half the time (stays in TRN2A over
  840 handler calls, cause `0x21`).
- D-Modem's server: tone B detected in at least 6 blocks in data mode
  starts the retrain; a failed phase in a retrain retrains again; after 4
  retrains it ends the call; the retrain's time limit is 30 s. [RE: D-Modem, code `k56_step`, `k56_fail`; notes 5].
- No K56flex cleardown is known; V.42's DISC comes first in practice. [RE: D-Modem: not covered]; the design's decision 15.

## Recording

Gough Lui's recording of a real K56flex call, decoded by us outside the
repository (`~/modem-research/goughlui-k56/`, throwaway scripts in its
`scripts/`). [RE: ours, the recording] throughout, unless marked.

The call (Gough Lui's page, [RE: Gough Lui]): a NetComm Roadster II
(Rockwell RCV56DPF L8570A, firmware `V2.210-K56_2M_DLS`) forced to K56flex
(`+MS=56`), through a Linksys PAP2T ATA to an Australian point of presence
(server equipment not identified). `AT&V1`: RX 48000, TX 28800, LAPM,
V.42bis, "RBS Pattern 00", "Rate Drop 00", "Digital Loss None", retrain
counts 0, "Flex 9481834246E0". Its `AT&V2` dump has `ra43 rb148 rc129
rd131 re66 rf70 rg224`: `rb` to `rg` are `94 81 83 42 46 e0`, the Flex
octets; `ra` = `0x2b` is unexplained.

The file: stereo, 16-bit, 8000 Hz, 218498 frames (27.3 s); left the
digital modem (server), right the analogue modem (client). The samples are
not G.711 values (an analogue or resampled path), so codes cannot be read
back; segment boundaries were found from tones, levels and the energy
below 125 Hz (which the running-sum signs suppress).

Phase 1 (times in the file):

| Time (s) | Side | What |
| --- | --- | --- |
| 1.420-1.710 | server | CRe segment 1, 1375 + 2002 Hz (about 290 ms: the shortened form) |
| 1.710-1.815 | server | CRe segment 2, 400 Hz |
| 1.885-2.278 | client | CRd segment 1, 1529 + 2225 Hz |
| 2.278-2.390 | client | CRd segment 2, 1900 Hz |
| 2.415-2.445 | client | a 1650 Hz burst (unexplained) |
| 2.795-3.510 | server | CL on V.21(L): `12 c9 80 80 81 c2 09 b5 02 00 94 81 83 42 46 e0`, FCS `23 50` good |
| 3.725-4.535 | client | MS on V.21(H): `11 c9 80 80 81 c2 09 b5 02 00 94 81 81 02 47 85`, FCS `71 d8` good |
| 4.875-5.200 | server | ACK(1) on V.21(L): `14`, FCS `dd a6` good |
| 5.600- | server | ANSam (phase reversals every 450 ms) |
| 7.28-8.05 | client | CM `e0 c1 45 13 94 2a 0d` (repeated) |
| 8.06-8.95 | server | JM `e0 c1 45 13 94 2a 0d` |

So a real server sent **CRe, not MRe**, a **CL** (not CLR) whose S field
offers data with V.42, and the 9-octet block; the real client answered with
CRd and an MS echoing the S field and its own block (version `02`, no
`0x40`); the server sent ACK(1); V.8 selected V.34.

Phase 2: INFO1c (right, 1200 Hz) at 10.987 s, 109 bits, CRC good, offset
`0x009`; INFO1a (left, 2400 Hz) at 11.320 s, 70 bits, CRC good, offset
`0x005` (all fields in "Phase 2"). The call is **A-law** (offsets 9 and 5;
Australia), a K56flex call no other source has seen.

Phases 3 and 4, left channel, in samples at 8000 Hz (± about 40 where the
boundary comes from a level or low-band step, ± about 5 at onsets):

| Segment | Samples | Length | SM56-accepted (decision 8) |
| --- | --- | --- | --- |
| silence after INFO1a | 91515-92152 | about 637 | 512 + converter delay |
| R1 | 92152-about 92440 | about 288, a reversal about 270 in (last about 24 reversed) | 288, last 24 reversed |
| TRN1A | about 92440-109100 | about 16650 | 16368 |
| TRN1B | about 109100-117280 | about 8180 | 8448 |
| TRN1C and Js | about 117280-135747 | about 18470 | 8448 + 128 |
| silence 2 | 135747-151504 | 15757 | until the J24, at least 12288 |
| R2 | 151504-about 151792 | about 288, reversal as R1 | 288 |
| TRN2A and MARK | about 151792-160170 | about 8380 | 16336 + 32 |
| TRN2B, MP, MP', E | about 160170-178960 | about 18790 | until the MP and the grid, + 1420 |
| data (48000) | about 178960-215750 | about 36790 (to the end of the file) | |

- TRN1A + TRN1B together (about 24830) are within about 15 of SM56's 16368 +
  8448 = 24816, but the boundary is about 270 symbols later than SM56's.
- The client's upstream (right) starts at sample about 134200, about 1550
  symbols (194 ms) before the server's TRN1C/Js ends, and runs on through
  the rest of the file. The server's Js is therefore not simply the last
  128 symbols before a fixed silence; the server may repeat Js (or TRN1C)
  until it hears the client ([inf], weak: the network delays between
  the recording point and each modem are unknown).
- The training levels are about 0.6 to 1 dB apart as the segments predict
  (TRN1B's mixed magnitudes below TRN1A's and TRN1C's `0x4f`), and the
  energy below 125 Hz drops by 18 dB at TRN1B (the running-sum signs
  begin). The data's low band is 10 dB above TRN2B's (at 48000 only every
  8th sign controls the DC).
- The J24 and the client's MP were not decoded (a 3200-baud QAM receiver on
  a recording; not attempted).

## Oracle

The oracles are D-Modem's encoder and training, built outside the
repository and run on our inputs; their output is pinned in our tests as
data. D-Modem is cloned at `c06a380d` in `~/modem-research/D-Modem` (`git
log -1`: `c06a380d86105b2309c8cb8958e41120856bb84b`, 2026-11-20) and built
there unmodified ("Harness"). The drivers are ours, in
`~/modem-research/k56-oracle/` (`enc.cpp`, `train.cpp`, its `README.md`),
never in the repository (k56 2, 2026-11-21).

- Encoder oracle: `K56Enc` (`dsp/k56flex.cpp`): data bits (one per byte,
  time order) to 8 codes per frame, for a `K56Line` {rate, pad 0..2, law,
  robbed mask}; `frame_bits()` gives the bits the next frame takes. Its
  scrambler starts at 0 and its 3-frame cycle at frame 0. [RE: D-Modem,
  code]. It is D-Modem's reading of SM56's tables; SM56's own decoder
  took its octets in 351 cases (PR #39). It is not independent truth (the
  echo chamber of the spec).
  - **`K56Enc::frame` scrambles internally** (GPA, one bit at a time, before
    the shell index is taken), so `enc` feeds it the unscrambled bits.
    [RE: D-Modem, code]. The pipeline Task 7's test must reproduce, for an
    encoder that takes scrambled bits: for each rate code r (1..13), pad
    (0..2) and mask (0..63), a fresh xorshift32 from 1 (`x ^= x << 13; x ^=
    x >> 17; x ^= x << 5`, bit = `x & 1`, one step a bit), each bit through
    a GPA scrambler from state 0, into an encoder at frame 0, running sum 0,
    shaping counter 0, mu-law; 600 frames, each taking the encoder's bit
    count for it; FNV-1a (32 bits) over the 4800 mu-law octets
    (`test/data/k56enc_oracle.h`: `k56enc_oracle_fnv[r-1][pad][mask]`, and
    `k56enc_oracle_frames[r-1][pad][k]` with mask 0 for frames 0, 1 and 599).
    [our observation: the oracle run].
  - At 32000 and 34000 every mask gives the same octets (no robbed bits
    below 36000, "Robbed bits"); 1920 different FNVs in the 2496. [our
    observation].
- Training oracle: `K56Train` (`dsp/k56flex_train.cpp`) with `K56Enc` for
  B1 and `V34Mp::build` (`dsp/v34_modem.cpp`, from the built
  `libdmodem.a`) for our MP, driven as `v34_k56.h` drives them, at fixed
  symbols instead of on a received J and MP: `init(law)`, `js = 0x89b0`,
  silence 2 held until the J, the pad from the J's octet, silence 2 ended
  at max(12288, J rounded up to 8), TRN2B held until the MP, MP once TRN2B
  has 2048 and the grid holds, MP and MP' as `k56_make_mp()` builds them
  (rate_ca, rate_ac, mask `0x3fff`, asymmetric; ack 0 and 1), a new
  encoder at each MP, B1 (6 frames of ones) first. [RE: D-Modem, code].
  `K56Train` can be driven alone; `open_k56flex` was not needed.
  - Cases (`test/data/k56tx_oracle.h`, `k56tx_oracle_case[]`): J octets
    `00`, `40`, `80`, `04`, `12`, `98`, each with the J after 12000 and
    13000 symbols of silence 2 and the MP after 3000 of TRN2B; and a
    renegotiation: J `12` after 12000, the first MP after 3000, 1001 data
    frames, then R2 at that frame boundary and the client's MP after 1500
    of the new TRN2B. Our MP: rate_ca 13, rate_ac 13 (56000); its 11 octets
    (and MP''s) are in the header as `k56tx_oracle_mp`. Recorded per
    segment from silence 1 (from R2 in the renegotiation) to the first
    data frame: length, FNV-1a of its mu-law octets, the first 8 octets.
    [our observation].
  - "After j_at symbols": the J is taken before symbol j_at of silence 2 is
    sent; when j_at is already a multiple of 8 above 12288 (13000), silence
    2 ends there (13000 symbols). `v34_k56.h` sets the length after the
    symbol is sent, which would give 13001 for a J seen right after symbol
    13000; the driver does not follow that. [inf] (read in `k56_step` and
    `K56Train::next`).
  - Seen in the output: TRN2B 3012 symbols with the J after 12000 (silence
    2 12288) and 3020 after 13000, the grid then holding (48980 + 1420 =
    2100 x 24; 49700 + 1420 = 2130 x 24), whatever the J's octet; in
    the renegotiation TRN2B is 2064 (2048 and the grid); TRN2B, MP, MP' and
    E use the J's pad's magnitudes, 1 bit a symbol in the renegotiation
    (`c2 aa 2a 42` at pad 0); R1 and R2 are the same octets; the first data
    frame after the renegotiation equals the first one of the call (Ruling
    C4: the encoder restarts). [our observation].
- SM56's decoder as a check (`~/modem-research/k56-oracle/sm56dec/`, ours,
  32-bit): `sm56.ko` linked with its imports renamed by objcopy,
  `pstn_reset_rx`, the octet callback at state + `0x20`, the pad at
  `0x1110`, per cycle symbol k `0x10f8` + 4 k (0 robbed, 2 not),
  `pstn_init_rx(st, 30 + 2 r)`, `putBIOS` per code. [RE: D-Modem, code
  `dsp/test/k56flex_sm56.cpp`] for the calls. On `K56Enc`'s output (600
  frames): 32000 pad 0 mask 0, 44000 pad 1 mask `0x12`, 56000 pad 2 mask
  `0x3f`: all three decode to the data bits sent, one frame short (SM56
  decodes a frame when the next starts); a corrupted octet and a wrong mask
  are caught. [our observation].
  - **Our encoder** (`modem/k56enc`, k56 7, 2026-11-21): `make k56-dump`
    (`tools/k56enc_dump.c`, the pipeline above at every rate, pad and mask,
    600 frames) then `sm56dec/check.sh build/k56dump`: **2496 of 2496**
    decode to the data sent. Its FNVs equal `k56enc_oracle_fnv` at all 2496
    and its frames 0, 1 and 599 equal `k56enc_oracle_frames` at all 39
    (`test/test_k56enc.c`). [our observation].

## Harness

- SM56: `dsp/test/sm56/sm56.ko` in the D-Modem clone, SHA-256
  `a757fd85bc27cc4bd816073efa9d03591307973de5c5aabcb37e0bb225ba516a`
  (sm56-06.09.05); D-Modem's `dsp/test/half_sm56.cpp` runs it in user
  space; `dsp/test/k56flex_sm56.cpp` feeds encoder output to its decoder.
  `dsp/test/NOTICE.md` grants no rights to the binaries. [RE: D-Modem].
- `glibc-devel.i686` (with `libxcrypt-devel.i686`) installed by the owner on
  2026-11-21: the SM56 gates run.
- D-Modem built (k56 2, unmodified):

      cd ~/modem-research/D-Modem
      cmake -B build -DCMAKE_C_FLAGS=-mstackrealign -DCMAKE_CXX_FLAGS=-mstackrealign
      cmake --build build -j16

  Built with the plain `cmake -B build` of its README first, `half_sm56`
  faulted at once (`movapd` to a misaligned stack, GCC 15.3.1, in a
  callback from the module, which keeps 4-byte alignment); `-mstackrealign`
  cures it. Then `ctest -R k56flex -L sm56` passes: `sm56_k56flex`,
  `sm56_k56flex_rbs`, `k56flex_sm56`. [our observation].
- **The reference call** (D-Modem's server, `half_open8`, against SM56,
  `ATX3;AT+MS=K56FLEX`, netsim at 8000 Hz mu-law with `NET_LOOP=0.45`; log
  in `~/modem-research/k56-oracle/ref-call/`). Times in D-Modem's line ms
  from its ATA: MRe 200; CRd at 700, our CL from 1639; SM56's MS `11 c9 80
  80 80 07 b5 02 00 94 81 83 21` at 2950; ACK(1) and V.8 from 3650; CM `e0
  c1 65 13 94 27 0d` at 5235; INFO1c (offset `0x1`) and the flagged INFO1a
  at about 7800; Phase 3 from 8002 (SM56 at 3200 baud); its J24 (16 point,
  octet `00`) at 14420; R2 at once; its MP (rate_ca 13, rate_ac 7, mask
  `3fff`) at 18672; E at 18875: **CONNECT 44000/31200**, SM56 `CONNECT
  44000/LAPM`; 22.7 s of the test's clock to CONNECT. [our observation:
  D-Modem's log]. This is SM56 with D-Modem's MRe; Ruling C1's CRe is
  untried with SM56 until Task 5.
- SM56 answers V.8bis only after its blind-dial wait (2 s from ATD): the
  bridge answers 3 s after ATD. With `+MS=K56FLEX,0` it offers no V.90.
  [RE: D-Modem, notes 1].
- **Our bridge** (`tools/k56-peer/`, `make test-k56-sm56`): with
  `AT+MS=K56FLEX,0` and K56flex not offered by us, SM56 gets no V.8bis
  answer, sends a CM for V.34 and connects **V.34 28800/28800** with LAPM to
  our answerer; 4 KiB each way identical; its `AT&V1` says V.34 / V.34,
  3200 baud, 1829 / 1920 Hz carriers, 35/33 good EC packets, none bad.
  Also so with `RBS=0x08`. So SM56 falls back from K56flex without
  automode (Ruling F2's automode is not needed for a fall-back to V.34).
  [our observation].
- With automode (`AT+MS=K56FLEX,1`) or `AT+MS=V90`, SM56's CM offers V.90,
  we select it, and our V.90 answerer failed against SM56 in Phase 2 in the
  bridge (k56 2): our INFO1d sent, no INFO1a from SM56 within the 1 s wait,
  SM56 NO CARRIER. **k56 9b: this depends on the line model, not on our
  modem**: through D-Modem's netsim (our answerer as a half program, below)
  our V.90 connects with SM56 at 45333 and 50666 (`NET_LOOP` 0.45 and 0.2);
  in the bridge it passed with the k56 2 high pass at fraction 0 (50666)
  and fails with the k56 9b default. Not investigated further. In automode
  our answerer, offered both, connects V.90 (29333 with `DOWN=32000`), not
  K56flex. [our observation].
- **SM56's TRN2A depends on the line, not on the server** (k56 9b,
  2026-11-21). [our observation: the cross-experiment, below]:
  - The cross-experiment. Our answerer, wrapped as a "half" program
    (`half_ours`, outside the repository: line on fd 3 / 4, AT commands on
    a pty) in D-Modem's `interop_call` with its netsim (`NET=8000:8000:ulaw`,
    `NET_LOOP=0.45`, SM56 `ATX3;AT+MS=K56FLEX,0`): **K56flex connects**,
    SM56 `CONNECT 32000`, front end to state 25, our MP from it rate_ca 13,
    rate_ac 7. So the k56 9 failure was not in our modem.
  - Then, through the same netsim: with `NET_LOOP` unset or 0, **D-Modem's
    own server fails exactly as ours did** (SM56 stays in front end state
    19, no MP, cause `0x21` after 841 calls); with `AT+MS=K56FLEX,0` it
    fails at 0.45, 0.6 and 0.75 too and passes at 0.15, 0.3 and 0.9 (two
    calls each); its own test (`AT+MS=K56FLEX`, 0.45) passes. Ours, same
    netsim, `,0`: fails at 0, 0.75, passes at 0.3, 0.45, 0.6, mixed at
    0.15 and 0.9. Both servers' outcomes move with the line model and with
    the call's timing alike.
  - In the bridge (deterministic, lock step), fraction swept 0 to 0.95 with
    the k56 2 model (second-order 300 Hz Butterworth high pass): pass 0 to
    0.35 and 0.65 to 0.95, fail 0.40 to 0.60 (0.45, the k56 2 default,
    among them); `LOOP=none` fails. Extra delay toward SM56 (0 to 300
    samples) moves the failing region irregularly; at fraction 0 with a
    first-order high pass: pass with 0 to 13 extra samples, fail with 30,
    50, 100, 192, 250, pass with 150. Not explained by: the codec's noise
    (`SM56_NOISE` 0, 1, 20: same), a slip of one sample in silence 2 (+1 or
    -1: no outcome changes), or a modulus of the delay we could find.
  - The second-order high pass also spoiled SM56's data above 32000 bit/s
    (error about 80 at 48000, 140 at 44000, then it drops the call); a
    first-order one, or none, gives about 5 to 7 at 50000 and 52000.
  - SM56's codec runs at 9600 Hz in K56flex (`codec rate 9600 Hz` before its
    front end state 4); half_sm56 resamples the 8000 Hz line to it.
  - D-Modem's notes see the same failure (state 19, cause `0x21`) in about
    half of its retrains and suspect SM56's adapted code-level table
    (`State_TRN2A_DecDir`, MOD + 0x3840); we see it in first trainings,
    which use the power-up table, so that cannot be the whole cause. [RE:
    D-Modem, `docs/dsp/k56flex-details.md` "Retrains"].
  - The bridge's default is now a one-sample delay and a first-order 300 Hz
    high pass, robbed bits through it as well (`tools/k56-peer/README.md`
    "Known"); with it `make test-k56-sm56 MODS=k56` passes at 32000 and
    52000, with robbed bits `0x08` and `0x24`.
- netsim's robbed-bit model is in "Robbed bits"; `NET_RBS_TO=a` (or `b`)
  applies the pad and robbed bits to the codes toward an 8000 Hz PCM client
  with no codec filter (as SM56 takes them). Without it, D-Modem's SM56
  calls put `NET_LOOP=0.45` on the codes toward SM56 (a 0.45-sample delay
  and a 300 Hz high pass); the bridge models both. [RE: D-Modem, code
  `netsim.cpp`; `interop_test.cpp`].

## Unsettled

Not settled by any source, or settled against the spec's wording. Each with
what the work assumes.

Settled by the controller (ledger, Rulings C1 to C6), kept here for the
record: CRe, not MRe (C1); the real server's full CL as the default, SM56's
acceptance its gate (C2); the MS parsed by V.8bis 8.2.3 (C3); every MP
restarts the data encoder (C4); the A-law call is facts only, mu-law only
(C5); at least 400 ms of silence before CRe, NAK(1) only for an invalid
frame (C6).

Still [unk]:

- **What in SM56 makes TRN2A fail** (k56 9 found it, k56 9b placed it,
  [our observation]): SM56 stays in front end state 19 during our TRN2A,
  sends no MP and gives up with cause `0x21`, for some line models and
  delays toward it, against D-Modem's server and ours alike ("Harness").
  So it is SM56's (as half_sm56 runs it), not our modem's; which condition
  inside it fails (its decision-directed TRN2A check, its timing, its
  echo canceller while it sends J' and TRN) is not known. Whether a real
  SM56, or a hardware K56flex client, is as sensitive is not known.

- Whether SM56 accepts the real server's CL (`81 c2` S field) and our CRe:
  the harness runs now (k56 2), the V.8bis side comes in Task 5.
- Why our V.90 answerer fails against SM56's V.90 in Phase 2 with some
  bridge line models and not through D-Modem's netsim ("Harness").
- (Settled, k56 7: the repository's `v34_shell_map` handles M = 16 at
  54000, z8's total 2^32 saturated; index 2^31 - 1 maps and unmaps, and
  54000 equals the oracle at every pad and mask, `test/test_k56enc.c`.)
- **The real server's training lengths** (TRN1A about 16650, TRN1B about
  8180, TRN1C and Js about 18470, TRN2A and MARK about 8380) differ from
  SM56's; whether a hardware client takes SM56's (decision 8) is unknown.
- The meaning of INFO1a's x, of NS octets `o[3]`, `o[4]`, `o[8]`, and of
  `AT&V2`'s `ra`.
- Whether a hardware client accepts a 4-level Js; the 1650 Hz burst after
  the real CRd.
- Lucent clients' tables; a K56flex cleardown; how a real client takes a
  server's renegotiation or retrain.
- A-law (non-goal): the recording shows a real A-law call (INFO1c `0x009`,
  INFO1a `0x005`), but no A-law tables or levels were checked.
