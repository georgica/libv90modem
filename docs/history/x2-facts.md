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
- The client sends its J until our preamble has been there about 150 ms
  (the Courier's), then is quiet until our J words. **RE (D-Modem)**,
  x2-details.md "The client: Phase 3", and `dsp/v34_x2.h`'s server
  ("the client quiet after its J"). Our echo canceller trains on our TRN
  in that quiet (`modem/x2d`); the Courier calls of "Harness" show it is
  there.

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
24 bits); the meaning of the other bits is not known. We send word 1 as the indices we have banks for: see "Our record's word 1" below. **RE (D-Modem,
courier-emu)**. The words are not in the DSP image (the host supplies
them); **our disassembly** found no `f33c`/`733c` word there.

Our record's word 1 (**derived**, and the Courier runs of "Harness"):
the I-modem's `7FFF` has one bit for each of the 15 rate indices, which
makes word 1 the likeliest place for the downstream rates the server
offers. The design asks our record to name only the indices we have banks
for; from x2 6 to x2 7 we sent `0001`: bit n - 1 for index n, as in the
MP's word 1 (its upstream mask). Against the emulated Courier (2026-11-20, PAD=0):
- `0001`: as with `7FFF` in every respect: CONNECT 48000 (50666 at
  PAD=3), V.42 up, its rate change 12.1 s into data mode.
- `0002`: the Courier never prints CONNECT; our data mode starts, V.42
  never comes up, and the Courier hangs up 7.1 s later (NO CARRIER).
So the Courier reads word 1 (`0002` alone fails), and bit 0 is the bit it
needs for index 1, which agrees with the n - 1 convention. What else it
does with the field is not confirmed: its CONNECT figure and its rate
change do not move with it. 48000 and 50666 are exactly the rates of
index 9 and index 11 ("Banks"), so the rate change presumably asks for one
of those from what it measured itself, not from our record.
Since x2 8 we have banks for all 15 indices in each pad mode ("Banks"), so
word 1 is `7FFF`, the I-modem's.

Sequence (**RE (D-Modem)**): the client sends Phase 4 (S, S-bar, TRN at 4
then 16 points), its MP until our record has been decoded, then MP', then
E, then V.34 data. The server sends sync frames until the MP, then its
record, whole records, until E, then the start segment.

- The client's MP, MP' and E are on 16 points: E is five symbols of four
  ones. **RE (D-Modem)**, x2-details.md "Phase 4" ("E (20 ones, 5
  symbols)"); **RE (courier-emu)**, `docs/x2-courier-mp-fields.md`: the
  Courier sets its flag `006F` bit 9 when the J words it received are
  `09B0`, and with it E's ones go to "the same AF7D scrambler/16-point
  transmitter used by MP".
- After E the client sends V.34's B1, then data. **RE (courier-emu)**,
  `artifacts/x2-upstream-b1-20261005` (B1 acquired from a Courier
  recording, with expanded shaping and 64 states).

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

## Oracle: D-Modem's mapper

D-Modem's mapper (`X2Map`, its `dsp/x2.cpp` and `dsp/x2_tables.cpp` at the
commit of "Tags and sources"; unchanged at `c06a380d`) was built and run as
a black box outside the repository (x2 8b, 2026-11-20); none of its code is
here. **Observed (oracle run)**:

- Given the same bits (xorshift32, GPC from state 0, `last` positive and
  the running sum zero at the first frame), its octets and those of our
  `x2map_frame` are identical at all 15 rate indices in pad modes 0, 1 and
  2, for 20000 frames from each of three seeds: the digit order, the list
  of each position, the sign positions, the differential signs, the free
  sign and the octets sent behind each pad.
- `test/test_x2map.c` keeps its output for indices 1, 8, 12 and 14 in each
  mode (frames 0, 1 and 3999 and an FNV-1a of 4000 frames) as data, so
  that our mapper is held to it independently of our own unframe.
- The I-modem's own octets (Ie030002 in courier-emu's I-modem pair, the
  run of "Harness", x2 8b) agree as well: its 106 570 data frames at
  index 14 each decode with our index 14 lists (`x2map_unframe`), and,
  descrambled, give HDLC frames with good FCS (its LAPM I frames). Its
  free sign in the data levels differs from ours, as "Free sign" says.

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
     current rate its largest level, e.g. `9e`/`9f` at 52000). Each
     position is at the largest level of its own list: at 52000 position
     0's list begins at `9e`, the others' at `9f` ("Banks"; **derived**,
     and D-Modem's client looks for "each position at its largest level");
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
- The client's TRN in a rate change is 512 symbols on 4 points; its MP
  follows, then (after our first record) MP' and E, and its upstream goes
  on at 28800. **RE (D-Modem)**, x2-details.md "Our renegotiation"
  (D-Modem's client does it as the Courier).
- The rate change's MP is on **4 points**, not 16 as the start-up's (the
  start-up's 16 points follow from our J words, "Records"). The emulated
  Courier, 2026-11-20 ("Harness"): 16-point decisions after its S-bar found
  no MP in 6 s; 4-point decisions found `0378 1FFE 0000 0500` twice, the
  first some 80 ms after its S was detected. Its MP' and E are on 4 points
  as well, and B1 follows E, then data, as at the start-up: the Courier
  runs of x2 8 ("Harness"; `modem/x2d` decoded its MP' and E on 4 points and
  checked its B1).
- Behind a pad (**derived**, our choice): from the client's MP on,
  everything we send is for the pad **that MP** reports. The marker is
  each position's largest level at the rate in use, as sent for that pad
  (so that the client receives it as that level: index 1 `a5` sent as `9d`
  behind 3 dB, `95` behind 6 dB); the records go in index 1's levels for
  it (levels B as received); the new rate's levels are for it. At the
  Courier the rate change's MP reports the start-up's pad; if one reported
  another, the latest report wins (`test/test_x2tx.c`: a rate change from
  index 14 with no pad to index 8 with an MP reporting 6 dB: marker `8d`
  / `8e`, index 14's largest levels sent for 6 dB). D-Modem's client looks
  for "each position at its largest level" and "the levels the start-up's"
  (x2-details.md "Our renegotiation"); the I-modem's octets behind a pad in
  a rate change are not published.
- Our record in the rate change carries our word 1 ("Records"), as at
  the start-up: `7FFF` since x2 8.
- The 43 records are sent whatever the client does; the server does not
  wait for its E before the start segment (the I-modem's fixed 182
  frames). `modem/x2d` ends the call when the client is not back in data
  mode 6 s after its S.

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
- Our check of the pad model over all of D-Modem's published mode 1 and
  2 pairs (**derived**; `test/test_x2map.c` does it again on our tables):
  every mode 2 entry is reachable by the model; in mode 1 two levels are
  not: received `e2` sent `dc`, received `f1` sent `ed`, in the banks of
  indices 5, 6, 11, 12, 14 and 15 (15 entries over the distinct lists, 49
  over the positions). The model delivers `dc` as `e1` and `ed` as `f0`
  (linear 492 x 0.708 = 348.3 and 164 x 0.708 = 116.1, each just past the
  decision boundary, 348 and 116, on the louder side); no octet arrives as
  `e2` or `f1`. The I-modem
  sends the Quad's octets behind 3 dB at indices 1 and 14 (**RE
  (D-Modem)**), so these are what servers sent; the network's real 3 dB pad
  presumably differs from the model at these two codes. We send them as
  the Quad does. The simulated client (`sim/x2call`, `x2call_level`) takes
  each octet for the level whose octet sent arrives as it, as a client that
  knows what arrives for each level. The smaller-magnitude rule for two
  candidates holds at index 1 but not in every bank (mode 1: 46 entries
  take the other candidate; mode 2: 25).

Levels per position in modes 1 and 2 (**RE (D-Modem)**, read from its
published banks; mode 2 receives mode 0's lists, position for position, and
sends other octets for them):

| n | mode 1 | mode 2 |
| --- | --- | --- |
| 1 | 9 each | 9 each |
| 2 | 12, then 13 | 12, then 13 |
| 3 | 18 each | 18 each |
| 4 | 28, then 19 | 21, then 20 |
| 5 | 19, then 26 | 27, then 22 |
| 6 | 23, then 26 | 28, then 25 |
| 7 | 27, then 29 | 28, then 29 |
| 8 | 38, then 31 | 32 each |
| 9 | 32, then 40 | 41, then 35 |
| 10 | 42, then 40 | 42, then 40 |
| 11 | 47, then 45 | 47, then 45 |
| 12 | 55, then 50 | 50, then 51 |
| 13 | 58, then 57 | 52, then 62 |
| 14 | 70, then 63 | 64 each |
| 15 | 72 each | 82, then 70 |

In every bank positions 1 to 5 share one list; position 0 may have its
own (in mode 0 and 2, index 7's first position has index 6's). Mode 2
index 8 has one received list for all six positions but sends another
octet for one of its levels at position 0 than at the others.

Where our banks come from (x2 8, 2026-11-20):

- **No rule found.** The design asks for a rule from the mu-law levels,
  the pad and the power limit that reproduces the published mode 0 banks
  for 1, 8, 11, 12, 13 and 14. In the upper segments the lists use every
  code from the top down, at the bottom a minimum spacing; but where they
  change between the two the lists skip codes irregularly (index 9's later
  positions, index 14's first position, which has one level fewer in the
  middle and one more at the top than the others). A greedy ladder (from
  the smallest level, each next level the first code at least d above the
  last, one d per list) reproduces 17 of the 30 mode 0 lists, among them
  both lists of 1, 8, 11 and 13 and the later positions' of 12 and 14, but
  not the first position's of 12 or 14; from the top down, 5. Index 12's
  and 14's first lists are I-modem line lists, so that is not enough.
  **derived** (a scratch script, not kept).
- **Not stored in the firmware.** The Quad's constructor (QF060003; its
  callers C862, C9BF, C761, C92A as courier-emu's
  `docs/x2-rate-and-data-handoff.md` and its mapper notes name them)
  builds the banks at run time from the record and a nine-entry
  constructor table (levels A, or the louder table that `d241` also holds
  in the I-modem); our scan of QF060003 found none of the lists of indices
  8, 11, 12 or 14 as bytes or as words. So they cannot be read from the
  image without running the constructor, which is what D-Modem did.
- **So the banks are included as data**: `modem/x2bank.c`, generated by
  `tools/gen_x2_banks.py` from D-Modem's published values
  (`dsp/x2_tables.cpp` at the commit of "Tags and sources", outside the
  repository; its numbers only, in our own layout), with the checks of this
  section (B, MD, mode 0's counts, index 1 = levels B, sorted lists, enough
  digits). `docs/limits.md` "Included tables (x2)" lists them. Index 1's
  octets sent behind the pads equal the pad rule's (`tools/x2_pad.c`;
  `test/test_x2map.c`).

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
- A rate change to an index we have no bank for (since x2 8 only index 0
  is left: D-Modem saw a Courier ask for "rate 0" once), with MD other than
  5 or a pad mode above 2: we clear down (V90M_END_X2_FAILED), as at the
  start-up, never answer with another rate.
- A-law: no client is known to have done it; not offered.
- Robbed bits: no MP field for them is known; not handled.

## Harness

An emulated U.S. Robotics Courier calls our answerer: `make test-x2-courier
COURIER_EMU=<courier-emu checkout> [PAD=0|3|6]` (`tools/x2-peer/`; courier-emu
and its ROM images stay outside the repository). The Courier is courier-emu
186d653f5624 running `artifacts/courier-board-21210-capture-403/courier-board.rom`
(supervisor 7.4.16, DSP 3.1.2) with its `idsdl403` NVRAM, `--with-dsp`, its
line on a socket (`--line-link`, `COURIER_LINE_DIGITAL=1`, 20 ms frames of
8 kHz linear). `tools/x2-peer/bridge.c` is the exchange: the Courier's codec
samples to mu-law at -5.86 dB, our octets (through the pad) back at
-5.54 dB, and our answerer (public API) with a V.42 endpoint on its bit
pump. The Courier types `ATX1S32=66S54=0S58=48&A3&B1Q0DT5551234`, and after
`CONNECT`, `+++` and `ATI11`. The call lasts until the Courier's budget
(400M instructions) runs out, at 116 s of line; that takes some 33 s of wall
time.

The run, x2 not offered (2026-11-20, library 0.3.0, PAD=0):

```
[   9.54] line: seized (Courier off hook)
[  18.82] line: ringing (Courier off hook)
[  18.82] line: we answer
[  28.52] ours: CONNECT 31200/28800 V.34
[  28.98] ours: V.42 LAPM (compression 1)
[ 116.08] ours: data mode reached, V.34 31200/28800, V.8, V.42 up
courier: CONNECT 31200/ARQ/V34/LAPM/V42BIS
courier: libv90modem x2-peer bridge here
courier: USRobotics Courier V.Everything Link Diagnostics...
courier: Modulation               V.34+
courier: Carrier Freq    ( Hz )   1920/1829
courier: Symbol Rate              3200/3200
courier: Trellis Code             64S-4D/16S-4D
courier: Nonlinear Encoding       ON/OFF
courier: Precoding                OFF/OFF
courier: Shaping                  ON/OFF
courier: Preemphasis Index        0/0
courier: Recv/Xmit Level (-dBm)   17.6/3.5
courier: SNR             ( dB )   38.6
courier: Near Echo Loss  ( dB )   61.7
courier: Far Echo Loss   ( dB )   88.1
courier: Roundtrip Delay (msec)   61
courier: Timing Offset   ( ppm)   -112
courier: Carrier Offset  ( ppm)   159
courier: RX Upshifts              0
courier: RX Downshifts            0
courier: TX Speedshifts           0
courier: V.90 Status              0005
x2-peer: PASS (both sides in data mode)
```

With `PAD=6`: `CONNECT 28800/ARQ/V34/LAPM/V42BIS`, received level 26.6 dBm,
SNR 35.6 dB. The 9 dB drop at the Courier (against 6 dB of pad) is ours: the bridge measured our data-mode
signal at -17.4 dBm0 before the pad and -23.4 dBm0 after it (PAD=0: -14.4
both), so the pad is 6 dB and our V.34 transmitter itself sent 3 dB less in
that call. Open: why (presumably the power the Courier asked for in the V.34
start-up at the lower level; not traced).
The Courier's timing offset (-112 ppm) and carrier offset (159 ppm) did not
change when the bridge's instruction stamp was corrected (it ran 920 ppm
slow): the stamp does not clock the Courier, and the offsets are the
emulator's (or the Courier's estimate), not the line's. Open as well.

x2 offered (`MODS=x2`), 2026-11-20, this library at x2 6 (`modem/x2d`,
32000 down only): at each pad the Courier takes x2, and the call reaches
data mode both ways with V.42 LAPM up.

| | PAD=0 | PAD=3 | PAD=6 |
| --- | --- | --- | --- |
| ours | CONNECT 32000/24000 x2 at 30.40 s | the same | the same |
| the Courier's CONNECT | `48000/ARQ/x2/LAPM/V42BIS` | `50666/ARQ/x2/LAPM/V42BIS` | `48000/ARQ/x2/LAPM/V42BIS` |
| ATI11 Modulation | `x2/V.34+` | `x2/V.34+` | `x2/V.34+` |
| ATI11 Symbol Rate, Trellis, Nonlinear, Shaping | `8000/3200`, `NONE/64S-4D`, `NONE/ON`, `OFF/ON` | the same | the same |
| ATI11 Recv Level, SNR | 21.4 dBm, 56.3 dB | 20.7, 56.2 | 21.2, 56.0 |
| ATI11 x2 Status | `0000; 0000-0000-0000-0000-0000-0000; 01,00 4539;01` | `0001; 1000-1000-1000-1000-1000-1000; ...` | `0010; 1000-...; ...` |
| our level in data mode, before / after the pad | -16.5 / -16.5 dBm0 | -13.6 / -16.6 | -10.4 / -16.5 |

- The Courier's upstream is as "Upstream" says: 3200 baud, 64 states,
  non-linear encoding, shaping; at 24000 (its mask `03FE`).
- Its CONNECT figure is not our downstream rate (index 1: 32000 payload,
  33333 as USR prints it): 48000 and 50666 here, against the same
  downstream; they are the rates of index 9 and index 11 ("Banks").
  Presumably what its own measurement of the line allows (courier-emu: its
  MP keeps a measured ceiling and asks for index 1 at start-up). These
  runs sent our record's word 1 as the I-modem's `7FFF`; with `0001` the
  figures are the same ("Records", our record's word 1). Not settled.
- The first field of x2 Status follows the pad (`0000`, `0001`, `0010`):
  presumably the pad it measured, as its MP reports it.
- 12.1 s after our CONNECT (12.6 s after the start segment's data levels
  began; D-Modem: 12.66 s) the Courier stopped its upstream data and sent
  S: its rate change ("Rate change"). This version does not answer it;
  our call ended 6 s later, and the Courier hung up 36 s after that.

The Courier's rate change, x2d answering it (2026-11-20, this library at
x2 7; the same bridge, courier-emu and ROM as above; `X2D_TRACE` builds for
the MP words, which print each MP x2d decodes):

| | PAD=0 | PAD=3 | PAD=6 |
| --- | --- | --- | --- |
| its S (x2d's clock, from the marker) | 19.72 s | 19.72 s | 29.47 s |
| its MP, 4 points, twice | `0378 1FFE 0000 0500` | `0B70 1FFE 8888 0588` | `1378 1FFE 8888 0588` |
| index asked for (word 0 bits 2-5) | 14 (54666) | 12 (52000) | 14 (54666) |
| pad reported, MD | 0, 5 | 1, 5 | 2, 5 |
| x2d | X2D_ERR_RATE 94 ms after its S: we clear down, V90M_END_X2_FAILED ("ours: ended: x2 failed") | the same | the same |
| the Courier | on hook 47.1 s after our clear down; nothing more on its terminal (it is in command mode after `ATI11`) | the same | the same |

(On the bridge's line clock our CONNECT is at 30.40 s in every run and
the data stop at 42.50 s, 42.50 s and 52.24 s: the Courier's rate change
12.1 s into data at PAD=0 and 3, 21.8 s at PAD=6; not looked into.)

- The rate change's MP is on 4 points ("Rate change"). With 16-point
  decisions (the start-up's) x2d found no MP and ended the call 6 s later
  (X2D_ERR_RENEG); that was the first run of x2 7.
- The Courier asks for index 14 (12 behind 3 dB), above our record's word
  1 (`0001`, index 1 only). That is the rate its own measurement allows
  (its CONNECT figure, 48000 / 50666, is not it either): word 1 of our
  record does not bound its request. Until the banks of later work, x2d
  clears down there, as the design says; the call does not go on.
- Not seen yet against the Courier at x2 7: our answer (marker, records,
  start), its MP' and E on 4 points, B1 after E, its upstream at 28800.
  The x2 8 runs below show all of them.

The Courier's rate change followed (2026-11-20, this library at x2 8:
banks for indices 1 to 15 in each pad mode, our record's word 1 `7FFF`;
the same bridge, courier-emu and ROM; a normal and an `X2D_TRACE` bridge at
each pad, all six runs alike):

| | PAD=0 | PAD=3 | PAD=6 |
| --- | --- | --- | --- |
| ours, start | CONNECT 32000/24000 x2 at 30.40 s, V.42 LAPM at 32.88 s | the same | the same |
| the Courier's CONNECT | `48000/ARQ/x2/LAPM/V42BIS` | `50666/...` | `48000/...` |
| its rate change (data stopped) | 42.50 s | 42.50 s | 42.50 s |
| its MP, then MP' (4 points) | `0378`, `8378` `1FFE 0000 0500` | `0B70`, `8B70` `1FFE 8888 0588` | `1378`, `9378` `1FFE 8888 0588` |
| E, B1 (x2d) | 4 points; B1 0 of 1152 bits wrong | 48 wrong | 48 wrong |
| ours after it | **54666/28800** at 43.24 s | **52000/28800** at 43.24 s | **54666/28800** at 43.24 s |
| its second rate change | none by the budget's end | 111.56 s: `0B74` (index 13), **53333/28800** at 112.32 s (B1 57 wrong) | none |
| at 116.08 s (the budget) | x2 54666/28800, V.42 up | x2 53333/28800, V.42 up | x2 54666/28800, V.42 up |
| our level, before / after the pad | -12.2 / -12.2 dBm0 | -8.8 / -11.8 | -6.1 / -12.2 |

- The Courier's rate changes are followed to the rate each asks for: 14 at
  PAD=0 and 6, 12 and then 13 at PAD=3. Each takes 0.74 s from the stop of
  its upstream to data again both ways.
- Its MP' and E in a rate change are on 4 points, and B1 follows E: the
  assumptions of "Rate change" hold at the Courier.
- After the change the call stays up to the budget's end (73 s at the new
  rate), V.42 up, the Courier's terminal quiet (it is in command mode after
  `ATI11`, typed before the change). A Courier given the wrong levels
  renegotiates within 3 s and hangs up (**RE (D-Modem)**); this one did
  not, and at PAD=3 its next rate change, 68 s later, asked for a higher
  index. That was taken as evidence that it decodes our new rate; its own
  counts were not read. **x2 9 (below) shows it does not**: LAPM stays up
  on the Courier's answers to our polls, but no I frame of ours gets
  through after the change.
- At PAD=6 its rate change now came 12.1 s into data as at the others (x2
  7: 21.8 s); not looked into.

Without a V.42 endpoint on our side (the first try) the
Courier printed only `NO CARRIER`: it cleared down 6.4 s after V.34 data
mode (presumably its stored settings want error control; not checked).

The Courier's data after its rate change (2026-11-20, this library at x2 8,
6396873; the bridge with `-k 4`, as `make test-x2-courier MODS=x2` now runs
it; the same courier-emu and ROM). A control burst of 4096 incompressible
octets once LAPM is up, at 32000, and another after the rate change once
data moves again; the Courier is in command mode (after `ATI11`), where its
LAPM still acknowledges:

| | PAD=0 | PAD=3 | PAD=6 |
| --- | --- | --- | --- |
| control, 32.88 s, 32000 | 33 I frames, all acknowledged in 1.22 s (N(R) advanced in 23 line frames); 0 sent again, 0 REJ, 0 T401 | the same (26) | the same (25) |
| rate change, data again | 54666/28800 at 43.24 s | 52000/28800 | 54666/28800 |
| burst after it, at 116.08 s (the budget) | **0 of 615** I frames acknowledged; 600 sent again, 68 T401, 0 REJ | **1 of 658**; 642 sent again, 68 T401, **2 REJ** | **0 of 600**; 585 sent again, 68 T401, 0 REJ |
| frames received from the Courier since the burst began | 59 good (its RR F=1 to our polls) | 55 | 55 |
| `x2-peer` | FAIL | FAIL | FAIL |

Our receiver counts some 40 to 47 bad frames a second (FCS, runts,
aborts) before and after the change alike, in the control too; they are
the upstream's, not the Courier's decoding of ours. Each pad gave the same
figures in two runs.

- At 32000 (index 1) the Courier decodes our downstream end to end: the
  control is acknowledged at once, and its first 910 octets appear on the
  Courier's terminal, byte for byte, before the `+++` takes effect
  (PAD=0).
- After the change the Courier answers each poll (RR P=1 after T401) with
  an RR F=1 that acknowledges nothing: a 3-octet supervisory frame of ours
  gets through, an I frame of some 128 octets (almost) never does. At PAD=3 it
  decoded at least one I frame out of sequence (2 REJ).
- One-off diagnosis, without error control (a scratch copy of the bridge
  and of run.sh, not kept: the Courier with `&M0` and no `+++`/`ATI11`,
  numbered 64-character text lines written continuously, our endpoint in
  start-stop; PAD=0): the Courier's terminal shows the text as sent up to
  the rate change, apart from lines its DTE side dropped (its serial port
  is slower than the line). After the change (54666) the text is mostly
  corrupt: 20 % of the 7948 octets it printed lie in correct alphabet runs
  of 4 or more (median run 12 octets, about 3 frames of 41 bits, longest
  40), between stretches of octets that look like the text bit-shifted
  (start-stop resynchronising after an error). So the errors are frequent
  but not total: most of a frame's worth of bits get through between them.
- Not settled at x2 9 (settled by x2 8b, below: the emulated Courier,
  not our octets): whether our banks or mapping differ from what the Courier
  decodes at these indices, or the emulated line or DSP cannot carry
  them. Our simulated client decodes with our own mapper and banks
  (`x2call_level`, `x2map_unframe`), so its bit-exact runs cannot show a
  mismatch with a real client. Index 1 (32000) is the only rate shown to
  carry data to the Courier.

The same Courier against two other servers (x2 8b, 2026-11-20): to tell our
downstream from the emulator, the Courier of these runs (the same
courier-emu, ROM, NVRAM, AT settings, 20 ms frames and BearerLineLink
levels) was given D-Modem's x2 server (`dsp/test/courier_peer` at
`c06a380d`, built outside the repository) through a scratch copy of
`run.sh`'s exchange, and the I-modem itself (Ie030002, courier-emu's
`tools/probe_imodem_analog_pair.py` with its working settings,
`X1S27=1S54=0S58=48&A3&B1Q0&U26&N39` / `S54=0S58=48&A3&B1Q0`, sending a
numbered line about every second). **Observed**:

- The line octets: ours equal D-Modem's from the preamble through the
  alignment, the sync frames and the records (0 of 28 719 + 9 960 + 678
  octets differ), and from the marker of the rate change to the first data
  frame (863 frames) except the free sign of 102 frames (D-Modem does not
  reset its running sum at the marker). Against the I-modem's: the same
  from the preamble to the sync frames, and the 863 frames of its rate
  change differ only in the free sign of 219. Marker, records and start
  are at the same frames in all three (the marker, then 182 frames to the
  new levels, 681 frames of ones).
- The Courier decodes none of the three at the rate it asks for:

| Server | PAD | after the change | what the Courier decoded after it |
| --- | --- | --- | --- |
| ours, V.42 | 0 | 54666 | 0 of 615 I frames (128 octets) acknowledged |
| ours, no error control | 0 | 54666 | 0 of some 73 text lines intact (11 of 11 before) |
| D-Modem, V.42 | 0 | 54666 | the 2 lines sent in its first second; none in the 72 s after |
| D-Modem, no error control | 0 | 54666 | 0 of some 72 lines intact (11 of 11 before) |
| D-Modem, no error control | 3 | 52000 | 10 of some 71 lines intact (11 of 11 before) |
| D-Modem, no error control | 6 | 54666 | 0 of some 68 lines intact (11 of 11 before) |
| the I-modem, V.42 | 0 | 54666 | its 34 I frames after the change (3 to 64 octets, 429 in all) were each sent 1 to 19 times; its I frame 50 (10 octets) was first acknowledged 15.8 s after its data began; its own terminal says NO CARRIER |

  The text that does arrive after the change is mostly corrupt in the
  same way for D-Modem as for ours (runs of correct text between
  bit-shifted stretches). With the same octets from three servers, the
  original among them, failing alike, the emulated Courier of this
  harness does not decode x2 above index 1 reliably; the fault is not in
  our mapper, banks or rate change. Not looked into: why (the codec's
  8000-to-9600 Hz reconstruction, the levels, the emulator's timing).
  courier-emu's own x2 results (`docs/imodem-analog-x2-20261003.md`,
  "53333/x2") are the Courier's CONNECT figure and one message at the
  start-up's rate, before any rate change, as ours are.

Looking for a setup in which the emulated Courier decodes x2 above index 1
(x2 8b, follow-up, 2026-11-20). **Observed**, unless marked otherwise:

- D-Modem's documented setup (its `docs/x2-upgrade-details.md` 6.4,
  `docs/dsp/x2-details.md`): courier-emu's
  `tools/probe_v90modem_closed_loop.py analog` with `courier_peer`, `--fast`,
  `COURIER_AT='ATS30=1X1S54=0S58=48&A3&B1Q0&U26&N39DT5551234'`, lines "6 to
  14 dB down", and an option `COURIER_INT0_TOUT` (the frame interrupt
  following the DSP's timer). `COURIER_AT`, `COURIER_NVRAM` and
  `COURIER_INT0_TOUT` are not in courier-emu as published (`main` is
  186d653, ours; its one other branch is merged into it), so D-Modem ran a
  modified copy that we cannot reproduce. Otherwise the probe is our
  harness: the same ROM, NVRAM fixture, `--tick-ms 5`, 20 ms frames and
  BearerLineLink's AC01 levels (-5.86 / -5.54 dB, ours). D-Modem's own
  Courier emulator (`half_courier`, IDSDL302) does not finish V.34
  training between two Couriers (its `docs/interop-details.md`), so it was
  not tried for x2.
- The clock: the line is sample-locked. The bridge answers each frame
  with as many samples as it received; courier-emu stores the frame's
  instruction stamp (`peer_instructions`) and clocks nothing by it, so
  the stamp (x2 3's 920 ppm) cannot offset the line. The Courier's codec
  runs at 9600.000 Hz in data mode (MCLK 3.456 MHz, 4200 DSP cycles at
  40.32 MHz a sample), and both of courier-emu's conversions (codec to
  line, and the 128-tap reconstruction from line to codec) use that
  rate: exactly 6 codec samples to 5 line samples. ATI11's -112 / +159
  ppm are the Courier's estimates, not an offset of the line. courier-emu's
  only offset setting, `COURIER_LINE_SKEW_PPM`, is 0 by default; +112 or
  -112 (Python frame path) ends our calls in the start-up (x2 failed at
  33.7 s); 0 on that path reproduces 0 of 615.
- Other settings, our server, PAD=0, `-k 4` (a loss in the scratch
  bridge, after the codec gain):

| Setting | rate(s) asked | after the change |
| --- | --- | --- |
| line 3 dB down | 54666 | 0 of 615 I frames acknowledged |
| line 6 dB down | 52000, then 53333 | 15 of 265; ended (x2 failed) at 68.4 s, V.42 down |
| line 8 dB down | 52000, 53333, 54666 | 0 of 716, 1 REJ |
| line 10 dB down | (its rate change 6.5 s into data) | we end the call (x2 failed); not traced |
| INT0 at 2700 Hz (`--frame-hz`, measured 2401) | 54666 | 1 of 721, 1 REJ |
| INT0 at 2900 Hz | 54666 | the upstream lost after the change (V.42 down at 54.9 s) |
| INT0 at 3200 or 4800 Hz | | the Courier stops before it dials |

  D-Modem's server without error control: line 6 dB down, 52000 then
  53333 then 54666, 1 of some 71 lines intact after the change; 10 dB
  down, 52000, 3 of some 72. The AT settings of courier-emu's probe
  (`ATX1S27=1S54=0S58=48&A3&B1Q0&U26&N39`) end both servers' calls in the
  start-up.
- No setup found carries data above index 1 to the emulated Courier, from
  any server. The bridge is not at fault (its line is exact), so it is
  unchanged.


The harness since then (x2 9, the release ruling): `make test-x2-courier
MODS=x2` passes when the Courier acknowledges our V.42 data at the
start-up rate (32000, the control burst) and prints what happens after
its rate change as information; `STRICT_RENEG=1` (bridge `-r`) requires
that data to be acknowledged too, which no server does on the published
courier-emu (`tools/x2-peer/README.md`). At 0.4.0 (2026-11-20), the
default run at PAD=0, 3 and 6: PASS each (control 33 of 33 I frames
acknowledged in 1.22 s; after the change 0 of 615, 1 of 658 with 2 REJ,
0 of 600); `STRICT_RENEG=1` at PAD=0: FAIL, as expected.
