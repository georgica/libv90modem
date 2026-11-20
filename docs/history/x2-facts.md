# x2 on the answering side: the facts

2026-11-20. The single source of values for the x2 work
(`docs/superpowers/specs/2026-11-20-x2-answering-design.md`). Later tasks
take their values from here, by section name. No code or table file of
D-Modem (AGPL-3.0) or courier-emu (no licence) is in this repository; what
follows are facts, each with where it came from.

## Tags and sources

Every value carries one tag:

- **documented**: in a USR document or a standard (ITU-T V.34, G.711).
- **RE (D-Modem)** / **RE (courier-emu)**: reverse-engineered by that
  project, by running the original USR firmware in courier-emu and watching
  the line octets or the DSP.
- **our disassembly**: read by us from the firmware image at the address
  given. Nothing was transcribed as code.
- **derived**: computed by us from the values above (the computation is
  stated, and where it is a tool or test, named).

Sources, as read:

| Short name | What | Version read |
| --- | --- | --- |
| D-Modem docs | `docs/dsp/x2-details.md`, `docs/dsp/x2.md` of github.com/D-Modem/D-Modem | `main` at `bbeb8741e6c1` (2026-11-20) |
| D-Modem code | `dsp/x2.cpp`, `dsp/x2.h`, `dsp/x2_tables.cpp`, `dsp/v34_x2.h`, `dsp/k56flex.cpp`, read for values only | same commit |
| courier-emu | github.com/cryan209/courier-emu: `docs/x2-v90-protocol-selection.md`, `artifacts/x2-spec-20261004/x2-technical-specification.docx` ("Draft 0.33"), `artifacts/x2-server-script-20261004/script.json`, `artifacts/x2-server-decomp-20261004/README.md` | `186d653f5624` |
| x2shoot4 | USR, "x2 Understanding the Issues / Troubleshooting Problems" v1.0, 1997 (ftp.desync.com/pub/files/USRobotics_3Com/Modems/x2shoot4.pdf) | |
| x2comref | USR, "x2 Commands & Technical Reference", 1997 (unicom.usr.com/support/s-modem/s-modem-docs/x2comref.pdf) | |

Firmware images (kept outside the repository, in
`~/modem-research/sources/fw/`):

| Image | Role | Origin | SHA-256 |
| --- | --- | --- | --- |
| `Ie030002.nac` | I-modem 3.00.02 server; 386EX host, TMS320C5x DSP | courier-emu `firmware/legacy-usrobotics/2332-sdl302.zip` | `3f9a7aa8033323c28eebfee950b53267a67b7ad9141ebf30271918d0babbe358` |
| `QF060003.NAC` | Total Control Quad x2 NAC 6.0.3; 80186 host, TMS320C50 DSP | courier-emu `docs/x2/Qf060003.zip` | `0eafde5a534c2270d9cd58755ad5af461eacc2d671a5422005d0471d5860e69a` |
| `IDSDL302.ROM` | Courier (client) SDL 3.02 | courier-emu repository root | `49f4182cc961aef983ff43468b7b7e55c03205c9dba80e9689fe20aa6ff2ccc5` |

"Ie030002 DSP `xxxx`" below is a C5x program-word address. The `.nac` was
flattened with courier-emu's `NacImage` (load base `40000`); the DSP
overlays were laid down as courier-emu's `tools/imodem_x2_map.py`
(`V90_IMAGES`) names them for this build: image 5 (flat offset `A8690`) at
`8000`, image 6 (`90D60`) at `A000`, image 10 (`9CEB0`) at `D100`. The x2
training script and level tables are in image 6; the V.8 code in image 10.
The disassembler was courier-emu's `tools/c5x_disasm.py`, run locally.
"IDSDL302.ROM +`xxxxx`" is a byte offset in that file (16-bit words, little
endian).

The octets below are G.711 mu-law octets as on the line (bit 7 set:
positive; `ff`/`7f` zero; `80` the largest positive). "Linear" values are on
the V.90 Table 1 scale of `sim/g711.c` (`ulaw_to_linear`); D-Modem's
"14-bit" values are that divided by 4.

## Phase 1

V.8, the server's JM:

- The JM is V.34's JM. After each complete JM the server inverts the sign
  of the V.21 carrier (a 180 degree phase step). The JM octets are not
  changed by it. **RE (D-Modem)**, x2-details.md "Phase 1: V.8";
  **RE (courier-emu)**, x2-v90-protocol-selection.md.
  **Our disassembly**: Ie030002 DSP `dd62`-`dd64` negates `@63` (the
  carrier amplitude cell `03e3`) after a message unless `@70` bit 5 is set;
  `dd74`-`dd76` negates it again between the following CJ octets.
- Observed octets (courier-emu notation, after the sync octet): the
  Courier's CM `e0 c1 45 10 10 0d`, the I-modem's JM `e0 c1 45 10 10 8d`.
  The JM's last octet is the CM's with the PSTN-access "DCE on digital
  network" bit set. **RE (courier-emu)**. The client's CM offers V.34 and
  no PCM. **RE (D-Modem)**.
- x2 needs V.8 and the 3200 baud symbol rate. **documented**, x2shoot4
  ("S32=0 ... disables V.8, which effectively disables x2"; "3200 symbol
  rate -- an x2 requirement").

## Phase 2 marker

INFO0a (the server's):

- The Courier recognises an x2 server when its own INFO0 has ITU bit 18
  (3200 baud on the high carrier) and the remote INFO0 has **bit 23 or bit
  24** set. **Our disassembly**: IDSDL302.ROM +`2b174`-`2b192`: the remote
  INFO0 word (`ff00`) is loaded, the local word (`ff18`) bit 5 is tested,
  then `and #0c00` on the remote word, failing when zero. The packing that
  puts ITU bit 13 + j at word bit j (so word bits 10, 11 are ITU 23, 24, and
  word bit 5 is ITU 18) is **RE (courier-emu)**, x2-v90-protocol-selection.md
  "And it explains the peer test"; it is consistent with bit 5 being the
  3200-high-carrier capability that x2 needs.
- The I-modem sends bits 20 to 25 as `1 0 1 1 1 1`: power reduction 1,
  `max_sr_diff` = 6 (bits 22, 23), CME = 1, bit 25 = 1. The Courier's INFO0c
  has bits 21 to 24 clear. **RE (courier-emu / D-Modem)**: observed bodies,
  bits 12-28, I-modem `11111111101111000`, Courier `11111111100001000`.
- **Settled**: our INFO0a, when x2 is offered, sets `max_sr_diff` = 4
  (bit 23 alone of bits 21-23) and `cme` = 1 (bit 24). Either bit alone
  satisfies the Courier's test; both are set as the design asks. The
  I-modem's 6 is not used because V.34 allows 0 to 5 in that field and a
  non-x2 V.34 caller also reads it. Everything else of INFO0a is as today.
  **derived**.

After our L1:

- Our L1 is sent as today (the I-modem's: about 160 ms, at L2's level;
  **RE (D-Modem)**, **RE (courier-emu)**: "an I-modem server sent a V.34
  multitone L1 probe of about 160 ms").
- The client sends no L1, L2 or INFO1c. It sends one INFO frame (the
  marker), then goes to Phase 3. On the bearer of a Courier to I-modem call:
  I-modem L1 about 5.01-5.17 s, Courier marker 5.27-5.35 s; D-Modem's client
  sends it 170 ms after the server's L1 began. **RE (courier-emu, D-Modem)**.
- If no marker comes within **800 ms** (counted from the end of our L1),
  the server goes on as V.34 (waits for the caller's L2). **RE (D-Modem)**,
  x2-details.md "Phase 2"; D-Modem `dsp/v34_x2.h` lines 306-316 (the state `P2A_X2MARK`, "silence after L1 until the client's marker", `dsp/dp_v34.cpp` line 233).
- **For our receiver (derived from the three above):** our L1 lasts
  160 ms. The marker frame begins **170 to 260 ms after the start of our
  L1**, that is **10 to 100 ms after its end** (D-Modem's client: 170 ms
  after the start = 10 ms after the end; the Courier against the I-modem:
  5.27 s against L1 5.01-5.17 s = 260 ms after the start, 100 ms after the
  end; courier-emu's times are bearer times read to about 10 ms, at the
  server's side of the line, so they include the round trip). The frame
  (2 + 35 + 4 bits at 600 bit/s, about 68 ms) has ended by about 170 ms
  after the end of L1 (5.35 s observed: 180 ms). We **accept it until
  800 ms after the end of our L1** (960 ms after its start); with nothing
  by then the call goes on as V.34. That leaves more than four times the
  latest observed arrival for a long round trip.

The marker frame (as V.34's INFO frames, DPSK at 600 bit/s, from the
client):

| Part | Bits | Value |
| --- | --- | --- |
| lead-in | 2 | phase reference before the frame (courier-emu: two ones; D-Modem: "two symbols of our tone") |
| fill | 4 | `1111` |
| sync | 8 | `01110010` (as INFO0/INFO1, `info.c`) |
| body | 7 | LSB first |
| CRC | 16 | V.34 INFO CRC over the 7 body bits (`info_crc`: from FFFF, 0x8408 reflected), LSB first |
| closing fill | 4 | `1111` |

- 35 bits from the fill to the end of the CRC. **RE (D-Modem)**; the 2 + 12
  + 7 + 16 + 4 layout **RE (courier-emu)**, specification 5.1.
- Body: bit 0 the carrier (1 high); bits 1-3 the first modulation index;
  bits 4-6 the second. For a server holding the answering role, bits 1-3
  must be **6** (the PCM direction) for x2; bits 4-6 are the client's V.34
  symbol-rate index (4 = 3200 baud). Any other value of bits 1-3: not x2,
  go on as V.34. **RE (courier-emu)**, specification 4.2 and 5.2 (the
  Quad's parse executed for all 128 bodies and both roles). D-Modem's server
  does not check bits 1-3.
- The Courier sends `4d` (high carrier, 6, 3200 baud). `69` is the answerer
  form, `48`/`49` are plain V.34 (not x2). **RE (courier-emu)**.
- The CRC of body `4d` is `24dd`. **derived** (`info_crc` over the 7 bits).
- The server does not answer the marker on the line; it is silent until the
  client's J. **RE (D-Modem, courier-emu)**.

## Phase 3 script

The client sends V.34 S (128), S-bar (16), PP (288), TRN (about 6400
symbols on 4 points) and J at its symbol rate and carrier, scrambled with
**GPA** (1 + x^-5 + x^-23, the answerer's). **RE (courier-emu)** spec 3.4;
**RE (D-Modem)**. The server's PCM is digital silence (`7f`) until the
client's J; then it sends, in G.711 octets, without a gap:

| Segment | Octets | Content |
| --- | --- | --- |
| lead | 20 | `7f` |
| Preamble | 363 | the 22 octets below 16 times (352), then their first 11 with bit 7 inverted |
| TRN | 20004 | scrambled ones, GPC from state 0, one bit per octet: `b1` for a scrambled 0, `31` for a 1 |
| J words | 1200 | the 12-bit word `09B0` 99 times then `099F` once, each LSB first, scrambled (GPC continuing), at `b1`/`31` |
| Ones | 6000 | scrambled ones (GPC continuing) at `c1`/`41` |
| Alignment | 1152 | 192 frames; frame f is `9e 28 a1 21 a8 1e` rotated f places left |
| Sync | >= 10002, whole frames | mapped frames of ones in levels A ("Mapping"), until the client's MP, ending on a frame |
| Record | whole records | our record ("Records") in levels A, until the client's E |
| Start | 4086 (681 frames) | 1 frame of ones in levels A, then 680 frames of ones in the data levels |
| Data | | mapped data frames |

- The script, segment lengths and order: **our disassembly**, Ie030002 DSP
  `c915`-`c935` is the script (state, parameter, count): `c919` 20 x `7f`;
  `c91c` 352 and `c91f` 11 (parameter `0080`: the sign turn); `c922` 20004;
  `c925` `09B0`, a halt word, `c929` `099F`; `c92c` 6000; `c92f` 1152;
  `c932` 10002 with a halt after it. Matches **RE (D-Modem)** x2-details.md
  "Phase 3" and **RE (courier-emu)** script.json.
- The 20 lead octets: **our disassembly** (`c919`). D-Modem's segment table
  starts at the preamble; whether its silence covers them does not matter to
  the client.
- TRN has a fixed length because the client starts its Phase 4 on the J
  words. **RE (D-Modem)**.

The preamble's 22 octets (**RE (D-Modem)**, `dsp/x2.cpp` line 28;
rule **RE (courier-emu)**):

```
ab bd ab 2b 3d 2b ab 2b 3d bd 3d bd ab bd 3d 2b 3d bd 3d 2b ab 2b
```

- Rule: signs from the 11-bit register `05b8`, LSB first, a 1 negative
  (`+++---+--+-`, Barker-11), the same each 11 octets; magnitude from
  `0712`, LSB first, 0 `ab`, 1 `bd`, complemented every other 11 octets.
  **Our disassembly**: Ie030002 DSP `c9d9` `splk @62,#0712`; `ca1b`
  `splk @63,#05b8`; `ca1d`-`ca21` complements `@62` each cycle; `c9d7`
  copies the two levels `ab`, `bd` from the table at `d235`/`d236`. The 22
  octets follow from the rule exactly (**derived**).

Levels (**settled**, see "Disagreements settled" 1):

- TRN and J words: `b1` / `31` (magnitude 3772 linear). **Our
  disassembly**: `c9db` `splk @69,#00b1`; TRN state `ca2e` copies `@69` to
  the level cell `04bc`. **RE (D-Modem)** `X2_TRN_LEVEL 0xb1`.
- Ones: `c1` / `41` (magnitude 1820 linear, 6.3 dB below TRN). **Our
  disassembly**: ones state `ca22`-`ca24` copies the table word at `d237`
  (`00c1`) to `04bc`. **RE (D-Modem)** `X2_ONES_LEVEL 0xc1`.
- The sign: the octet sent is the level with bit 7 inverted by the
  scrambled bit (`ca4c`-`ca53`: the scrambler bit is shifted to bit 7 and
  XORed with the level). A scrambled 0 is positive. **RE (D-Modem)**, the
  XOR **our disassembly**.

The alignment pattern: `9e 28 a1 21 a8 1e`, frame f (from 0) sends entry
`(k + f) mod 6` at position k; the period is 36 octets. **Our
disassembly**: table at `cb06`-`cb0b`; state `cb0c`-`cb22` steps the index
by one each octet and once more each frame. **RE (D-Modem)**, x2.cpp
lines 35, 258.

The scrambler: GPC (1 + x^-18 + x^-23), from state 0 at TRN's first octet,
running on through the J words, the ones, and every mapped frame after
the alignment (the alignment does not use it), until a rate change resets
it. **RE (D-Modem)** (`X2Map::scr` shared with `X2Train`); the TX
scrambler entry is GPC **RE (courier-emu)** (`8faa` in IM020104).

## Records

Format, both directions (**RE (D-Modem, courier-emu)**; **our
disassembly**: Ie030002 DSP `b54c`-`b589` is the server's serializer):

- 17 ones (`b54c`: a count of `0x11`), a zero;
- each 16-bit word, LSB first, followed by a zero (`b557`);
- the CRC-16 of the word bits only (not the zeros): register from `FFFF`
  (`b552`), polynomial `8408` reflected (`b571`), no final inversion, sent
  LSB first (`b57f`).

Check values (**derived**, the CRC above):

| Words | CRC |
| --- | --- |
| `0344 03FE 0000 0500` (Courier MP) | `14AD` (also published by courier-emu: confirms the model) |
| `8344 03FE 0000 0500` (its MP') | `92B0` |
| `F33C 7FFF 0000` (server record, acknowledged) | `73F3` |
| `733C 7FFF 0000` (server record, not acknowledged) | `F9A6` |

The client's MP: 4 words; after each, 2 zeros (104 bits); E is 20 ones in
place of the next sync. **RE (D-Modem, courier-emu)**. Fields (**RE
(D-Modem)**, Courier `0344 03FE 0000 0500`):

| Field | Bits | Courier |
| --- | --- | --- |
| highest downstream rate index accepted (n) | word 0, 2-5 | 1 at start-up; 12, 14 ... at a renegotiation |
| (courier-emu: N2, 13) | word 0, 6-9 | 13 |
| digital pad measured (mode) | word 0, 11-14 | 0 none, 1 3 dB (`0B44`), 2 6 dB (`1344`) |
| acknowledge | word 0, 15 | MP' `8344` |
| upstream rates it can send | word 1, 0-11 (bit n-1: n x 2400) | `03FE` (up to 24000); `1FFE` at a renegotiation |
| downstream signs MD | word 3, high byte | 5 |
| (behind a pad) | words 2, 3 | `8888 0588` |

The server's record: 3 words `F33C 7FFF 0000` (bit 15 of word 0 the
acknowledge; `733C` without it), padded with zeros to 96 bits (4 frames of
24 bits); the meaning of the other bits is not known. **RE (D-Modem,
courier-emu)**. The words are not in the DSP image (the host supplies
them); **our disassembly** found no `f33c`/`733c` word there.

Sequence (**RE (D-Modem)**): the client sends Phase 4 (S, S-bar, TRN at 4
then 16 points), its MP until our record has been decoded, then MP', then
E, then V.34 data. The server sends sync frames until the MP, then its
record, whole records, until E, then the start segment.

## Mapping

Each 6-octet frame takes MD + B scrambled bits (GPC). **RE (D-Modem)**,
x2-details.md "Data"; **RE (courier-emu)** spec 10, 11.

1. The first MD bits are the signs, in order; the next B bits, LSB first,
   are a number x.
2. Mixed radix: position i (0 to 5) gets digit `x mod n_i`, then
   `x = x / n_i`; position 0 is the lowest digit.
3. The digit selects the level in the position's list, sorted largest
   magnitude first (smallest octet first).
4. The MD signs go to the MD positions of the smallest selected levels (by
   level, not by digit, where the lists differ; of equal levels, the first
   position), in position order. Each is sent as a change: `last ^= bit`;
   the octet is positive when `last` is 1. `last` starts at 1 (positive) at
   the first sync frame and again after a rate change's scrambler reset.
5. The remaining position (the largest level; of equal levels the last)
   carries the free sign ("Free sign").

At index 1 (32000 bit/s): B = 19, MD = 5, 9 levels in each position.

- Levels A (sync, records, the start's first frame):
  `a5 a7 ad af b7 bd c5 cf e5`. **RE (D-Modem)**; **our disassembly**: the
  table at Ie030002 DSP `d238`-`d240`, loaded by `d229` when `ffd9` bit 2
  is clear. With bit 2 set, `d241`-`d249` is used instead:
  `95 97 9d 9f a7 ad b5 bf d5` (each 16 codes, one segment, louder). What
  sets bit 2 is not known (courier-emu: "chosen from one of two tables by a
  flag set during the exchange"); the Courier calls show the first.
- Levels B (data at 32000): `a5 a8 ab ae b3 b9 bf cb df`. **RE (D-Modem,
  courier-emu)**, line octets of the I-modem after E. Not a stored table in
  either image (**our disassembly**: no such 9-word run in Ie030002 or
  QF060003); the bank constructor builds it.

## Free sign

The sixth sign carries no data; the client does not check it. **RE
(D-Modem)**: the Courier sent the same data with the I-modem's octets and
with only these signs changed.

- In levels A (sync, records, first start frame): negative when the sum of
  the other five, as signed values in D-Modem's 14-bit units (our linear /
  4), is more than **805** (our linear: 3220); otherwise positive. Agrees
  with every frame the I-modem sent. **RE (D-Modem)**, `X2_FRAME_DC`.
- In the data levels: **unsettled** (see "Disagreements settled" 2). Use a
  running sum: `rds` is the sum of all signed values sent (from the first
  sync frame, the five data values of this frame included); the free sign
  is negative when `rds > 0`, then its value is added. **RE (D-Modem)**,
  x2.cpp `X2Map::frame`; agrees with the I-modem about 87% of the time.

## Rate change

Client-initiated only; the server never sends the marker first. **RE
(D-Modem)**, x2-details.md "The rate change", `dsp/v34_x2.h`.

1. The client, in data mode, sends S, S-bar, a short TRN on 4 points and
   its MP (word 0 bits 2-5 the rate index n it asks for, word 1 `1FFE`).
2. The server answers, on a frame boundary, at the current data rate's
   frame clock:
   - 8 frames `a5 a5 a5 25 25 25` and 1 frame `25 25 25 a5 a5 a5`, with no
     differential signs (`a5`: the largest level of index 1; at another
     current rate its largest level, e.g. `9e`/`9f` at 52000);
   - the scrambler reset to 0 and `last` to positive;
   - its record in levels B (the start-up's data levels, at every rate),
     10 times `733C 7FFF 0000` and 33 times `F33C 7FFF 0000` (4 frames
     each);
   - 1 more frame of ones (levels B), then the levels of rate n: 681 frames
     of ones, then data. In all: 9 + 172 + 1 = 182 frames to the switch.
3. The client acknowledges (MP') during the records, then E; its upstream
   then goes up (28800 at the Courier).

- The new rate is exactly the n the MP asks for; a client given another
  rate's levels renegotiates and hangs up. A marker sent before the
  client's S makes the Courier retrain on 16 points without an MP and hang
  up. **RE (D-Modem)**.
- The I-modem itself sends the marker at frame 16882 of the data levels
  (12.66 s, frames from the start segment); D-Modem waits for the MP and
  sends its own only 16000 frames later if none came. Our design answers
  the MP only. **RE (D-Modem)**.

## Pads

The network's digital pad, as modelled (the owner's ruling): decode the
octet (G.711, `ulaw_to_linear`), multiply the magnitude in Q15 by 23198
(-3 dB) or 16423 (-6 dB), round (`+16384 >> 15`), encode to the nearest
octet (`linear_to_ulaw`). The client reports the pad in MP word 0 bits
11-14 (mode 0, 1, 2); the Quad builds banks only for these. **RE
(D-Modem)**.

Index 1, the level the client receives and the octet the server sends (the
Quad's, **RE (D-Modem)** `dsp/x2_tables.cpp` lines 218-225, 602-605, read
by D-Modem from QF060003's bank constructor in courier-emu):

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| mode 0: received = sent | a5 | a8 | ab | ae | b3 | b9 | bf | cb | df |
| mode 1 (3 dB): received | a5 | a8 | ab | ae | b4 | ba | c0 | cc | e0 |
| mode 1: sent | 9d | 9f | a3 | a7 | ac | b2 | ba | c5 | db |
| mode 2 (6 dB): received | a5 | a8 | ab | ae | b3 | b9 | bf | cb | df |
| mode 2: sent | 95 | 98 | 9b | 9e | a4 | aa | b0 | bc | d3 |

- **All 18 pairs are derived** by the model (`tools/x2_pad.c`,
  `x2_pad_octet`; `test/test_x2_pad.c` checks each against the Quad's).
  None has to be taken from the Quad. **derived**.
- Two octets arrive as the same level in two entries (3 dB: `b1` and `b2`
  both give `ba`; 6 dB: `d2` and `d3` both give `df`). The Quad sends the
  one of smaller magnitude (`b2`, `d3`); `x2_pad_octet` does the same.
- Behind 3 dB, levels B's `b3` cannot arrive (no octet pads to it), which
  is why the Quad's 3 dB list is not levels B. Behind 6 dB the client
  receives levels B. **derived**.
- `x2_pad_octet(0x7f, m)` returns -1 for m = 1, 2: `7f` is mu-law's
  negative zero; it decodes to 0, the pad keeps 0, and the encoder gives
  `ff` (positive zero), so no octet arrives as `7f`. Harmless: no x2
  level list contains a zero (the smallest are `e5`, `df`, `e0`, `f1`),
  and `7f` and `ff` are the same value to the client. **derived**.
- The levels A of the sync segment are sent unchanged whatever the pad
  (the client measures the pad from them). **RE (D-Modem)**, "The pad" of
  its client.

## Banks

Per rate index n (MP word 0 bits 2-5), MD = 5. **RE (D-Modem)**,
x2-details.md "The rate change", and `dsp/x2_tables.cpp`:

| n | B | bit/s | levels per position (mode 0) |
| --- | --- | --- | --- |
| 1 | 19 | 32000 | 9 each (levels B) |
| 2 | 22 | 36000 | 12, then 13 |
| 3 | 25 | 40000 | 18 each |
| 4 | 26 | 41333 | 21, then 20 |
| 5 | 27 | 42666 | 27, then 22 |
| 6 | 28 | 44000 | 28, then 25 |
| 7 | 29 | 45333 | 28, then 29 |
| 8 | 30 | 46666 | 32 each |
| 9 | 31 | 48000 | 41, then 35 |
| 10 | 32 | 49333 | 42, then 40 |
| 11 | 33 | 50666 | 47, then 45 |
| 12 | 34 | 52000 | 50, then 51 |
| 13 | 35 | 53333 | 52, then 62 |
| 14 | 36 | 54666 | 64 each |
| 15 | 37 | 56000 | 82, then 70 |

- B = 19, 22, 25 for n = 1-3, 22 + n above; the payload rate is
  (B + MD) x 8000 / 6. USR's CONNECT figures (x2comref: 33333 ... 57333) are
  one 1333 step higher. **RE (D-Modem)**; **documented** (x2comref).
- Mode 0 lists for n = 1, 8, 11, 12, 13, 14 match the I-modem's line
  octets; the other nine were only compared with the Quad's mapper. The
  level counts per position differ in modes 1 and 2 (e.g. mode 1, n = 4:
  28, then 19). **RE (D-Modem)**.
- For the later bank work (**derived**, our check of the pad model over all
  of D-Modem's published mode 1 and 2 pairs, by a script outside the
  repository): every mode 2 entry is reachable by the model; 15 mode 1
  entries are not (received `e2` sent `dc`, received `f1` sent `ed`, in
  several banks), so those must come from the Quad. The smaller-magnitude
  rule for two candidates holds at index 1 but not in every bank (mode 1:
  46 entries take the other candidate; mode 2: 25). A bank rule has to be
  found or the bank taken as data, as the design says.

## Upstream

V.34 data at the client's symbol rate (its marker's bits 4-6; 3200 baud
for the Courier), with GPA, expanded shaping, the 64-state trellis code
and non-linear encoding (theta 0.3125). The rate: the highest rate of the
client's MP word 1 mask, at most 12 (28800). The Courier sent 24000, then
28800 after its renegotiation. V.42 detection after E waits for two
flags in a row. **RE (D-Modem)**. USR's own figures for the upstream: up
to 31200 (secondhand, the Sportster manual) — not settled, a server
takes what the mask says.

## Disagreements settled

1. **TRN at +-0x31 and ones at +-0x41, against courier-emu's "lower
   amplitude".** No disagreement in fact: the octets are `b1`/`31` (TRN,
   J words) and `c1`/`41` (ones) in both sources, and in mu-law a larger
   octet is a smaller magnitude: `b1` is 3772 linear, `c1` 1820, 6.3 dB
   lower. courier-emu's "lower amplitude" is right; D-Modem's "ones at a
   higher level" (x2.h) means the octet number. Settled by **our
   disassembly** (`c9db`, `ca2e`, `ca22`-`ca24`, table `d237`).
2. **The free-sign rule.** Levels A: D-Modem's frame-sum rule (> 805)
   reproduces every I-modem frame; taken as fact. Data levels: **not
   settled by the disassembly.** The I-modem's sign choice in data mode
   (Ie030002 DSP `cd37`-`ce14`, the loop `cd94`-`cdd4`) appears to try
   candidate sign patterns and keep the one that minimises a filtered
   energy term (taps with `0e81` at `cdb2`-`cdb5`) against a running value
   at `@45:@46`: a shaping search, not a frame sum. We did not lift it
   fully. The Quad's rule (courier-emu: minimum of 8 x a running monitor of
   eighths, over the sign masks) agrees with the I-modem half the time;
   D-Modem's running sum 87%. Since the client does not check the free
   sign, the safe choice for a server is D-Modem's running sum (it keeps the
   DC down). What each says is above ("Free sign").
3. **INFO0a bits 23 and 24.** The client needs only one of them
   (IDSDL302.ROM +`2b18a`: `and #0c00` of the remote word). D-Modem's
   server sets both; the I-modem sends `max_sr_diff` 6 and CME 1. We set
   `max_sr_diff` = 4 and `cme` = 1 ("Phase 2 marker").

## Unsettled, and the choice made

- The data-mode free sign: running sum (above).
- What sets the I-modem's alternate levels A table (`d241`): not used.
- The meaning of the server record's `7FFF 0000` and of word 0's other bits:
  sent as the I-modem sends them.
- The client's full retrain during x2: unknown; the design clears down.
- A-law: no client is known to have done it; not offered.
- Robbed bits: no MP field for them is known; not handled.
