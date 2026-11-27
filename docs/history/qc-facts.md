# V.92 Quick Connect on the answering side: the facts

2026-11-26. The single source of values for the 0.7.4 Quick Connect (QC)
work (design `docs/superpowers/specs/2026-11-26-v92-qc-design.md`, scope
`...-scope.md`, plan `docs/superpowers/plans/2026-11-26-v92-qc.md`). Later
tasks take their values from here, by section name. No code of BinModem
(GPL-3.0) or D-Modem (AGPL-3.0) is in this repository; what follows are
facts, each with where it came from, in our own words.

## Tags and sources

- [V.92 n]: ITU-T V.92 (11/2000), clause or table n, read from
  `/home/georgica/v90d/refs/itu/T-REC-V.92-200011-I.txt` (the text, read
  only).
- [BM file:line]: BinModem's behaviour, read from its source at
  `/home/georgica/modem-research/ext/BinModem`, commit `2a1bfcc`. Read for
  what it does, never copied. Paths are relative to that tree.
- [OURS]: a reading or a choice this plan makes, with its reason, for the
  owner to overrule ("Readings").

Bit numbering is the Recommendation's: in Tables 2 to 5, 11 to 24, bit
patterns are sent left-most bit first in time and integers least
significant bit first (8, first paragraph) [V.92 8].

## Short Phase 1

Three sequences matter on our side, all 10-bit V.8-type frames at 300 bit/s
[V.92 8.2.1, 8.3.2, 8.3.4]:

| Bits | QC1a (Table 2), V.21(L), from the caller | QC1d (Table 11), V.21(L) | QCA1d (Table 13), V.21(H), ours |
| --- | --- | --- | --- |
| 0:9 | ten ONEs | ten ONEs | ten ONEs |
| 10:19 | 0101010101 (sync) | 0101010101 | 0101010101 |
| 20 | 0, start bit | 0 | 0 |
| 21 | 0: analogue modem | 1: digital modem | 1: digital modem |
| 22 | 0: QC | 0: QC | 1: QCA |
| 23 | P: 1 asks for LAPM (9.2.5) | P | P |
| 24:29 | W 0 X Y Z 1 | 0 0 0 L M 1 | 0 0 0 L M 1 |
| 30:39 | ten ONEs | ten ONEs | ten ONEs |
| 40:49 | bits 10:19 again | again | again |
| 50:59 | 000PW0XYZ1 | 010P000LM1 | 011P000LM1 |
| 60:69 | (none: 60 bits; CM follows at once) | (none) | ten ONEs (70 bits) |

- QC1a is sent once and "is followed immediately by CM" [V.92 8.2.1]; QCA1d
  is sent once [V.92 8.3.4].
- Bits 20:29 are one start-stop character: start bit 20, eight data bits 21
  to 28 (the first in time is bit 0 of the octet), stop bit 29. So the
  information octet has: bit 0 analogue/digital, bit 1 QC/QCA, bit 2 P, bit 3
  W, bit 4 always 0 (character bit 25), bit 5 X, bit 6 Y, bit 7 Z (QC1a); or
  bits 3:5 zero, bit 6 L, bit 7 M (QC1d, QCA1d). The sync character
  0101010101 is the octet 0x55.
- WXYZ and LM read most significant bit first in time: W is bit 24, X 26, Y
  27, Z 28; L is bit 27, M bit 28. WXYZ = 8W + 4X + 2Y + Z, LM = 2L + M.
- Examples: QC1a with WXYZ 0101 and P 1 is
  `1111111111 0101010101 0001001011` twice, octet 0xA4; WXYZ 0111 P 0 gives
  `0000001111`, octet 0xE0 (equal to V.8's CM/JM sync octet); WXYZ 1110 P 1
  gives `0001101101`, octet 0x6C. QCA1d with LM 01 and P 1 is
  `0111000011`, octet 0x87.
- LM, the ANSpcm level named in QC1d and QCA1d [V.92 Table 11]: 00 −9.5,
  01 −12, 10 −15, 11 −18 dBm0.
- On ANSam, the answering digital modem listens for QC1a, QC1d or CM; on
  QC1a it "shall transmit QCA1d followed by silence for 75 ± 5 ms and then
  QTS, QTS\ and ANSpcm" [V.92 9.2.4.1]. QC1d (a digital caller) "may" be
  answered as an analogue modem; CM gets normal V.8.
- While ANSpcm is sent it listens for TONEq; on TONEq, 75 ± 5 ms of silence
  and Phase 2; with no TONEq "during the 2 s following transmission of
  QCA1d" it sends ANSam and proceeds by V.8 [V.92 9.2.4.3].
- The caller: on ANSam heard for 1 s, QC1a then CM, listening for QCA1d,
  QCA1a and JM; on QCA1d it stops CM "without completing the current octet",
  is silent, and listens for QTS, QTS\ and ANSpcm [V.92 9.2.1.1]. When it has
  heard ANSam for 1 s it may send TONEq as soon as ANSpcm is detected; it
  ends TONEq when ANSpcm is no longer detected, is silent 75 ± 5 ms, and
  goes to Phase 2 [V.92 9.2.1.3]. TONEq is 980 Hz [V.92 8.2.5], the V.21(L)
  mark frequency.
- 9.2.4.1 applies "even when a previous V.8 bis session has timed out".
- ODP/ADP bypass: "If both modems have indicated LAPM capability, the V.42
  ODP/ADP exchange shall be bypassed" [V.92 9.2.5]; under QC that is the two
  P bits (there is no CM).
- QC2a/QCA2d (after CRe, V.8bis framing, Tables 3 and 14) exist [V.92
  9.2.4.2]; out of scope here (design, "What stays").

## UQTS

[V.92 Table 2] WXYZ → the Ucode of QTS's V:

| WXYZ | 0000 | 0001 | 0010 | 0011 | 0100 | 0101 | 0110 | 0111 | 1000 | 1001 | 1010 | 1011 | 1100 | 1101 | 1110 | 1111 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Ucode | 61 | 62 | 63 | 66 | 67 | 70 | 71 | 74 | 75 | 78 | 79 | 82 | 83 | 86 | 87 | cleardown from on-hold |

1111 is not a Ucode: it ends a modem-on-hold (9.10), out of scope; we treat
such a QC1a as no QC1a ("Readings").

## ANSpcm

[V.92 8.3.1, Table 6, Tables 7 to 10]

- A repeating sequence of PCM codewords giving about 2100 Hz; it repeats
  every 301 symbols and has a phase reversal added every 3612 symbols
  (3612 = 12 × 301).
- Ucode sequence: x = scl × √2 × cos(2πk × 79/301 + ϑ) + 0.5, k = 0 .. 300,
  ϑ = 0.25π/301, x quantised to a linear PCM value by G.711; the result
  "shall equal" Tables 7 to 10.
- scl by level: −9.5 dBm0 µ 1334, A 667; −12 µ 1000, A 500; −15 µ 708,
  A 354; −18 µ 500, A 250 (Table 6). Tables 7, 8, 9, 10 are −9.5, −12, −15
  and −18 dBm0, each with a µ-law and an A-law column, octets as sent.
- The µ-law scale is the 14-bit one (±8159) and the A-law the 13-bit one
  (±4096): Table 8 k 0, x = 1414.7, is µ 0xA9 (Ucode 0x56 positive, segment
  5) and A 0x83 (code 0xD6, 704..735).
- Convention that reproduces the tables: x is floored to an integer, then
  G.711-encoded (the encoder truncating, as G.711's does). Checked on
  2026-11-26 against all 2408 entries parsed from the V.92 text: floor 0
  differ, truncation toward zero 96, rounding 118 [OURS, computed]. `sim/qca`
  uses floor; Task 2's `tools/gen_anspcm.py --check` repeats the comparison.
- Transcribed values used by the tests [V.92 Table 8]: k 0..5 µ `A9 5D 29 C9
  AA 3D`, A `83 49 00 E1 80 15`; k 300 µ `5E`, A `4F`. Table 7 k 0..2 µ
  `A1 58 22`, A `88 76 08`, k 300 `59`/`74`; Table 9 k 0 `AF`/`9A`, k 300
  `64`/`46`; Table 10 k 0..5 µ `B8 69 39 D7 B9 4C`, A `93 5B 10 F1 90 64`.
- The phase reversal, read as the sign of every codeword inverted (bit 7 of
  the octet, in both laws) in blocks 1, 3, 5, ... of 3612 [OURS: the text
  says "a phase reversal added to it every 3612 symbols"; inverting the sign
  is a reversal of the tone and keeps the octets those of the tables]:
  µ −12 dBm0 n 3612 is 0x29, n 7224 0xA9.
- "Some network equipment are known to alter the characteristics of the
  channel in response to ANSpcm" (its NOTE).

## QTS

[V.92 8.3.6]

- QTS: 128 repetitions of {+V, +0, +V, −V, −0, −V} (768 symbols), V the PCM
  codeword of Ucode UQTS and 0 the codeword of Ucode 0.
- QTS\: 8 repetitions of {−V, −0, −V, +V, +0, +V} (48 symbols).
- "The first symbol of QTS is defined to be transmitted in data frame
  interval 0. The digital modem shall keep data frame alignment from this
  point on." This replaces V.90's alignment from the first symbol of Sd for
  a call that had short Phase 1.
- Octets with UQTS 70: µ-law `B9 FF B9 39 7F 39` (QTS\ `39 7F 39 B9 FF B9`);
  A-law `93 D5 93 13 55 13` (QTS\ `13 55 13 93 D5 93`). µ-law Ucode u is
  0xFF − u positive, 0x7F − u negative; A-law the code u with bit 7 the
  sign, even bits inverted (V.90 Table 1's columns).

## Timing

| What | Value | Source |
| --- | --- | --- |
| ANSam heard before QC1a | 1 s | [V.92 9.2.1.1, Figure 3] |
| QCA1d to QTS | 75 ± 5 ms of silence | [V.92 9.2.4.1, Figure 3] |
| QTS, QTS\ | 768T, 48T | [V.92 8.3.6, Figure 3] |
| TONEq after ANSpcm detected | ≤ 1 s in Figure 3; at once if ANSam was heard for 1 s, else after 1 s of ANSpcm, then ≥ 50 ms | [V.92 9.2.1.3, Figure 3] |
| TONEq missing: ANSam again | 2 s after QCA1d was sent | [V.92 9.2.4.3] |
| TONEq to Phase 2 | 75 ± 5 ms of silence | [V.92 9.2.4.3, 9.2.1.3] |
| Short Phase 2: Tone B before our reversal | ≥ 50 ms, after Tone A is heard; Tone B 10 ms after the reversal | [V.92 9.4.1.1.3, Figure 9] |
| The caller's Tone A reversal | 40 ± 1 ms after ours, both at the line terminals; Tone A 10 ms more | [V.92 9.4.2.1.3, Figure 9] |
| No Tone A reversal, or no INFO1a | 2500 ms from our Tone B reversal → full Phase 2 | [V.92 9.4.1.2.2, 9.4.1.2.3] |
| The caller with no Tone B reversal | 2500 ms from the end of INFO0a → retrain (9.7.2.1) | [V.92 9.4.2.2.2] |

## Short Phase 2

[V.92 9.4, Figure 9, Tables 15, 16, 18, 19]

- Used when both modems say V.92 and ask for it: INFO0d bit 26 (with bit 27
  V.92) and INFO0a bit 27 (with bit 26 V.92). The two tables put the bits
  the other way round (Table 15: 26 short Phase 2, 27 V.92; Table 16: 26
  V.92, 27 short Phase 2). The caller asks only if it means PCM upstream or
  V.90 data [V.92 9.4].
- Figure 9: INFO0d, Tone B (> 50 ms); the caller's INFO0a, Tone A; our Tone
  B reversal and 10 ms of Tone B; the caller's Tone A reversal 40 ms after,
  10 ms of Tone A, INFO1a. No L1/L2 probing in either direction and no
  INFO1d.
- Our side [V.92 9.4.1.1]: in the 75 ms that end Phase 1, listen for INFO0a
  and Tone A; send INFO0d with bit 28 = 0 then Tone B; after Tone A has been
  heard and Tone B sent ≥ 50 ms, reverse Tone B, send it 10 ms more, then
  silence; on the Tone A reversal, RTDEd = the interval between our reversal
  at our line terminals and theirs, minus 40 ms; then receive INFO1a and go
  to the Phase 3 it names.
- Recovery [V.92 9.4.1.2]: Tone A before a good INFO0a, or repeated INFO0a:
  send INFO0d again (bit 28 = 1 once INFO0a is in), as in V.90; no Tone A
  reversal within 2500 ms of ours: listen for Tone A, send Tone B on it, full
  Phase 2; no INFO1a within 2500 ms of our reversal: send Tone B, listen for
  Tone A, full Phase 2.
- INFO1a after short Phase 2: Table 18 (PCM upstream; bits 34:36 and 37:39
  both 6) or Table 19 (V.34 upstream):

| Table 19 bits | Content |
| --- | --- |
| 0:3 | fill 1111 |
| 4:11 | frame sync 01110010 (left-most first) |
| 12:17 | reserved, 0 |
| 18:24 | MD length, × 35 ms |
| 25:31 | UINFO (> 66) |
| 32 | reserved, 0 |
| 33 | 1: high carrier for the caller's transmitter |
| 34:36 | the caller's symbol rate, 3..5 = 3000, 3200, 3429 |
| 37:39 | the digital modem's: 6 |
| 40:49 | frequency offset, 0.02 Hz, two's complement, bit 49 the sign; −512 = ignore |
| 50:65 | CRC (V.34 10.1.2.3.2) |
| 66:69 | fill 1111 |

- Table 19 differs from V.90's Table 10 only in bit 33 (reserved there).
- Phase 3 after QC: nothing new; the caller may ask for a zero-length DIL
  (Figure 11; 9.5.1.1.11, 9.5.1.1.13).

## BinModem

Behaviour facts for the live runs (Task 11) and for what our side must
expect. [BM] at `2a1bfcc`.

- Its QC switch, `BINMODEM_V92_QC`, read when the modem is made: `0`, `off`,
  `no` or `false` for never (no QC1a sent, none answered); `always` or `2`
  for a QC1a on every V.92 call; anything else or unset for "recognised
  connections" only [BM crates/modem/src/lib.rs:86-95].
- `BINMODEM_V92_TONEQ_MS`, read when a call is placed: end TONEq after that
  many milliseconds even while ANSpcm goes on, a test switch; unset, empty
  or not a positive number: TONEq lasts until ANSpcm stops [BM
  crates/modem/src/lib.rs:117-124]. It is never shorter than 50 ms
  (`TONEQ_MINIMUM`) [BM crates/datapump/src/v8.rs:531-533,
  crates/datapump/src/v92/anspcm.rs:522].
- When it sends QC1a: only with `+MS=V92` and QC not off; with a memo of an
  earlier call to the same dial string, with that memo's UQTS; with
  `always` and no memo, with the default UQTS 70 (WXYZ 0101); P set when its
  own menu asks for LAPM [BM crates/modem/src/lib.rs:3681-3692;
  crates/datapump/src/v92/memo.rs:25; crates/datapump/src/v8.rs:476-479].
  The QC1a goes out after ANSam has been believed (0.25 s held) and Te (1 s)
  of silence, so about 1.25 s after ANSam began, and the CM follows it
  directly [BM crates/datapump/src/v8.rs:74, 128, 1125-1133].
- On QCA1d during CM: CM abandoned at once, silence, QTS and ANSpcm
  listened for [BM crates/datapump/src/v8.rs:994-1003]. TONEq on ANSpcm
  detected (or 50 ms of 2100 Hz behind QTS), as 9.2.1.3's "upon detection"
  [BM v8.rs:1214-1228]; TONEq ends 12 ms after ANSpcm is gone (after its
  50 ms minimum), or at the cap, or after 2 s with ANSpcm still there
  (then CM) [BM v8.rs:209, 234, 1238-1256]. With no QTS/ANSpcm 3 s after
  QCA1d, or ANSam heard again, it sends CM [BM v8.rs:229, 1229-1235,
  1018-1027].
- Short Phase 2: asked for (INFO0a bit 27) only when the memo says its call
  had PCM upstream and this call wants PCM upstream; so never with `always`
  and no memo [BM crates/modem/src/lib.rs:4149-4155]. With a memo it skips
  its own round-trip measurement and uses the memo's [BM lib.rs:4159].
- The rescue: after a hand-over (QC accepted) it waits 2.5 s
  (`QUICK_PHASE2_WAIT`) plus the round trip (the memo's, or 1.5 s
  `QUICK_ROUND_TRIP` with none) for INFO0d or Tone B; with neither it
  forgets the memo, and sends CM with no QC1a, giving up 4.0 s
  (`QUICK_RESCUE_PATIENCE`) plus the round trip later without two JMs [BM
  crates/modem/src/lib.rs:137, 142, 150, 2320-2328, 4166-4169, 4353-4370].
- Its memos are kept by the dial string (white space removed; `-` for no
  number), one line each in a text file, `<dial string> uqts=N law=mu|a
  rtd=MS pcm_up=0|1` [BM crates/modem/src/memos.rs:1-13, 55-60;
  crates/datapump/src/v92/memo.rs:60-68]. A memo is written when a V.92 call
  it placed reaches V.90 or V.92 data, with `pcm_up` = V.92 (PCM upstream)
  data; its UQTS is the one it sent (it never measures one) [BM lib.rs:
  2767-2786]. It is read at `ATD` [BM lib.rs:3434-3436] and forgotten by the
  rescue [BM lib.rs:4355-4357]. Who keeps the file: the `modem-sip` binary
  has a `--memos <file>` option ("keeps what V.92 calls teach in a file, by
  the string dialled") [BM crates/modem/src/bin/modem-sip.rs:88, 137,
  316-317]; the GUI keeps its own file [BM crates/gui/src/live.rs:1250]. So
  the plan's "no `--memos` flag" was wrong: the flag exists in `modem-sip`;
  without it, no memo is kept between runs and only `always` gives QC.
  A stale memo for Task 11 run 5 is made by editing that line (`uqts=`,
  `rtd=`).
- `+A8E`: `AT+A8E=,,,1` (or `1,1,0,1`) turns V.8bis on in front of V.8; the
  first two fields only accept 1 [BM crates/at/src/lib.rs:805-830]. With it,
  the calling station answers our CRe with CRd, reads our CL and, since our
  CL's data block names no modulation, sends an MS "for data with the
  modulation left to V.8" [BM crates/datapump/src/v8bis.rs:806-820;
  crates/v8/src/bis.rs:911-920]. Its octets, as encoded by its tree writer
  [BM crates/v8/src/bis.rs:29, 164, 243-256, 336-341, 439-451, 510-519,
  546-550, 622-630, 808-813; its capabilities lib.rs:3825-3852, with error
  control wanted by default lib.rs:1344]:

| Octet | Value | Meaning |
| --- | --- | --- |
| 0 | 0x21 | MS (type 0001), **revision 2** (its `REVISION`) |
| 1 | 0x89 | I NPar(1): V.8 follows (bit 1), transmit ACK(1) (bit 4), last octet |
| 2 | 0x80 | I SPar(1): none (no network type: the caller is on an analogue line) |
| 3 | 0x80 | S NPar(1): none |
| 4 | 0x81 | S SPar(1): data |
| 5 | 0xC2 | data NPar(2): V.42 (both lists' protocols; ours has no transparent), last |

  So the MS is `21 89 80 80 81 c2`, not `11 89 80 80 81 c2` as the plan
  expected: BinModem writes revision 2 in every message, whatever the CL's
  revision. It does ask for ACK(1) (bit 4 of I NPar(1)). Task 8 uses
  `21 89 80 80 81 c2` (and, as the Recommendation's revision field is to be
  ignored by a receiver of a known message, should accept the revision-1
  form as well) [OURS].
- BinModem as the answering digital modem answers a QC1a "whoever sends it"
  at −12 dBm0 (`DEFAULT_LEVEL`) [BM lib.rs:3681-3685, memo.rs:29]; not
  something we meet as a server.

## Readings

[OURS] The choices this plan makes where the text leaves room, each with its
reason. The owner may overrule any.

1. QC is offered exactly when the answerer's modes have both V.90 and V.92
   and K56flex was not agreed in this call; no setting (design 3). A K56flex
   caller is not a V.92 caller.
2. QC1a is recognised as Table 2 has it, with these bounds: twice, at least
   8 mark bits before the sync (10 are sent; the first copy follows ANSam,
   so its first marks may be lost to the receiver's settling), the character
   0x55, one information character with bits 0, 1 and 4 clear, then marks;
   both information characters equal. QC1d (bit 0 set) is not answered (the
   "may" of 9.2.4.1 is not taken). A UQTS of 1111 is treated as no QC1a: the
   CM behind it decides (no modem-on-hold).
3. LM is the level nearest our nominal Phase 2 transmit power among −9.5,
   −12, −15 and −18 dBm0 (ties to the lower), moved down while above the
   maximum INFO0d advertises (−0.5 − 0.5 × code dBm0), never below −18: the
   design asks for the Phase 2 power, and an ANSpcm louder than INFO0d's
   maximum would contradict our own INFO0d.
4. Our P bit in QCA1d mirrors the caller's (as our JM mirrors its CM); both
   P set → V.42's ODP/ADP bypassed (9.2.5).
5. TONEq is accepted only after ≥ 100 ms of 980 Hz, and the 2 s of 9.2.4.3
   applies to the start of that run: 980 Hz is also V.21(L)'s mark, and a
   caller that missed QCA1d goes on sending CM, whose ten-ONE preambles are
   33 ms runs of 980 Hz. With no TONEq: ANSam again 2 s after QCA1d's last
   sample (16000 samples), then V.8.
6. The 6-symbol data-frame grid starts at QTS's first symbol (8.3.6) and is
   kept through retrains in that call.
7. INFO0d bit 26 (short Phase 2) is set only in calls that came through
   short Phase 1, so no other call's INFO0d changes.
8. An INFO1a short Phase 2 cannot serve (for example Table 19 naming 3429,
   which we do not receive) is handled as a missing one (9.4.1.2.3): Tone B,
   full Phase 2.
9. One failed QC start-up (short Phase 2 recovery, or a PCM upstream that
   then fails) does not take PCM upstream away; 0.7.3's rule (bit 70 cleared
   after the second failure) stands.
10. V.8bis: ACK(1) only to a valid MS without the K56flex block that asks
    for it (V.8bis 9.9); the CL stays byte for byte as it is.
11. ANSpcm codewords: x floored, then G.711 (see "ANSpcm"); the reversal as
    a sign inversion.
12. The simulated caller (`sim/qca`, `sim/v92call` with `cfg.qc`), where it
    has to choose: it sends QC1a 1 s after ANSam began (9.2.1.1, so earlier
    than BinModem's 1.25 s); TONEq at its V.21 level from 20 ms of matching
    ANSpcm (`toneq_delay_ms` moves it); ANSpcm "stops" at the first octet
    that is not the expected codeword, and a stop followed within 50 ms by
    anything louder than Ucode 16 is the digital modem's ANSam (it then
    waits for 1 s of ANSam and sends CM); after its 75 ms it sends INFO0a.
    In short Phase 2, a Tone B reversal that does not come within 2500 ms
    sends it to the full Phase 2 instead of 9.4.2.2.2's retrain, a simpler
    peer that still exercises our recovery.
13. The ANSpcm generator exists three times on purpose (`tools/gen_anspcm.py`,
    `sim/qca`, the table `modem/qc_tab.c`), and the Table 2/13/19 bit writers
    twice (`sim/` and `modem/`): the simulated caller is written from the
    V.92 text, never from library code, so that each checks the other. They
    are not to be merged.
