# HST and MNP on the answering side: the facts

2026-11-24. The single source of values for the HST and MNP work
(`docs/superpowers/specs/2026-11-24-hst-mnp-design.md`, plan
`docs/superpowers/plans/2026-11-24-hst-mnp.md`). Later tasks take their
values from here, by section name. The `sim/` peers (`mnppeer`, `hstcall`)
are written from this file and V.42 Annex A, never from library code.
No code or prose of BinModem (GPL-3.0), D-Modem or courier-emu is in this
repository; what follows are facts in our words, each with where it came
from.

## Tags and sources

Every value below carries exactly one tag, in square brackets at the end of
its bullet or table row:

- `[FW]`: from Courier firmware (a V.Everything flash build of 06/09/95), as
  BinModem's author reports it. Strong but second-hand: BinModem has never
  talked to a Courier.
- `[PUB]`: from a public document: a USR manual, a patent, or an ITU
  Recommendation (V.42 Annex A, V.17).
- `[BM]`: BinModem's own choice. Not evidence about HST; we may match it to
  talk to BinModem, but nothing else rests on it.
- `[OURS]`: our guess or decision, with its reason. A recording or a test
  settles it. When a recording settles one, the tag becomes `[OURS ->
  observed]` and the recording's hash is named (section `Recordings`).

Citations:

- **BF §n**: `docs/hst/binmodem-facts.md`, section n (our digest of
  BinModem at `2a1bfcc`; its `file:line` tags are in that file).
- **PS §n**: `docs/hst/public-sources.md`, section n (manuals, patents,
  V.42 Annex A).
- **A.x**: clause x of ITU-T V.42 (03/93) Annex A, text in
  `build/hst-sources/itu/v42-93.txt` (Annex A at lines 3100 to 4420).
- **V.17 §x**: ITU-T V.17 (02/91), text in `build/hst-sources/itu/v17-91.txt`
  (made with `pdftotext -layout` from `V17-199102.pdf`, fetched from itu.int,
  sha256 `2198a1457f4c7b5c4ce12c01e7c3a9bf19c89cae5fc90069870cd62290446cc9`;
  the V.42 (03/93) PDF is
  `dbf193fb78dff78324ee74a1b37422addea0d495a05a1d12eb5bd727474f7bda`). The
  figures were read from rendered page images.
- **P1, P2, P3**: US 5,008,901, US 4,890,316, US 5,579,305. The local text
  files (`build/hst-sources/patents/*.txt`) carry no column numbers, so a
  patent is cited by its text-file line: `P1:416` is `US5008901.txt` line 416.
- **SPEC Dn**: decision n of the spec's "Decided how" table.

`build/` is untracked: the sources are not committed, only named here.

## Call start

What the answerer does, and what we do:

| value | source | tag |
|---|---|---|
| Plain 2100 Hz answer tone (V.25 style), no phase reversals, no V.8. | BF §1 | [FW] |
| The Courier's DSP entry for the answering side is 0xC29D: answer tone, then the forward transmitter and the back receiver start together. | BF §1 | [FW] |
| The answerer holds the fast (forward) channel at the start; the caller has the back channel. | BF §1, §4; PS §1.4 item 3 | [FW] |
| With `B1` the Courier answers with a Bell 2225 Hz tone and no V.32 tones; with `B0` with the V.25 2100 Hz sequence plus V.32 tones ("takes slightly longer"). | PS §1.4, §2.3 | [PUB] |
| The modems "begin handshaking by training at 2400 bps", negotiate error control, then "shift up and train at 9600", then up to 12K, 14.4K, 16.8K, or fall back to 7200 and 4800. | PS §1.4 items 1-2 | [PUB] |
| A Dual Standard Courier at `B0` goes HST only if the other end is HST; otherwise V.32bis. | PS §1.4 | [PUB] |
| HST modems "drop to 2400 bps" if error control cannot be set up; with `&N0` and no error control a Courier connects only at 2400 or lower. | PS §1.4 items 4-5 | [PUB] |
| The caller starts its back transmitter and forward receiver together once the tone has stopped (DSP entry 0xC573). | BF §1 | [FW] |
| BinModem's answerer timeline: 2000 ms silence, 2100 Hz tone 3300 ms (peak 0.3 of the data level), 75 ms silence, then the session. | BF §1 | [BM] |
| BinModem's caller treats the tone as present after 400 ms and as over after 30 ms of absence; a shorter burst resets the count. | BF §1 | [BM] |
| BinModem's caller sends its LR on the back channel at 450 bit/s whatever back speed it wants in the end. | BF §1, §3 | [BM] |
| BinModem picks HST only on `AT+MS=HST` or `HST96`: no automode, no fallback to V.32, V.32bis, V.22bis or V.21. | BF §1 | [BM] |
| BinModem's result codes: `CONNECT <rate>/ARQ/HST` (BinModem says the format copies a Courier's); plain `CONNECT` at X0. | BF §1 | [BM] |
| BinModem ends the call if MNP is not up 60 s after it goes off hook (`NO ANSWER` if no tone was heard, else `NO CARRIER`). | BF §1 | [BM] |
| Front panel: AA goes out at the answering end while the channels are reversed; HS is off during link negotiation. | PS §1.4 item 7; BF §4 | [PUB] |
| We keep ANSam (2100 Hz with reversals) for every caller, after V.8 and the existing CM and AA listening; we send no plain 2100 Hz and no 2225 Hz tone. Reason: the box answers every caller on one number, and V.8 callers must be untouched. | SPEC D9 | [OURS] |
| We assume a Courier caller accepts ANSam as an answer tone (Couriers call V.34 answerers, which send ANSam). If wrong, an HST caller never starts its back channel and the call goes V.22bis as in 0.6.0: nothing is lost. | spec "Conflicts" row 1 | [OURS] |
| After ANSam ends with no CM and no AA, stay silent up to W = 400 ms (at most 700 ms, only if simulation shows misses) and run the back-channel detector; if it fires, enter `ANS_HST`, else V.22bis or V.21 as today. | SPEC D9, D25 | [OURS] |
| `ANS_HST` is the point of no return; a false detection costs a V.22bis call (no LR, so `NO_EC`). | spec "The answering sequence" | [OURS] |
| Backstop: no MNP link within 15 s of entering `ANS_HST` ends the call `V90M_END_HST_FAILED`. | spec "Retrain, watchdogs" | [OURS] |
| The forward channel starts straight after the tone, with no V.22bis stage first, unless R1 shows a V.22bis carrier. | spec "Conflicts" row 2 | [OURS] |
| Opening rate of the forward channel, before MNP agrees on rates: 9600 (`HST_OPEN_CODE` 4). Reason: the manual says the modems "shift up and train at 9600". | PS §1.4; spec "Forward training" | [OURS] |
| BinModem's opening rate is 7200; used by us only under `-DHST_PROFILE_BM`. | BF §1 | [BM] |

## Back channel

Differential 8-PSK, 3 bits a symbol. The table is from one place in the
firmware's DSP (BF §3).

| variant | NCO increment | polyphase step | tick rate | symbol rate | carrier | bit rate | source | tag |
|---|---|---|---|---|---|---|---|---|
| fast | 0x6AAA | 6 of 72 taps | 300/s | 150 baud | 375 Hz | 450 bit/s | BF §3 | [FW] |
| slow | 0x638E | 4 of 72 taps | 200/s | 100 baud | 350 Hz | 300 bit/s | BF §3 | [FW] |

- Phase step for each tribit, in eighths of a turn. The firmware keeps the
  eight steps packed in the 32-bit constant 0x43705261, one nibble each,
  lowest nibble first (BF §3). [FW]

  | tribit t | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | step | 1 | 6 | 2 | 5 | 0 | 7 | 3 | 4 |

- New phase = (phase + step) mod 8; the signal points sit at 22.5 degrees +
  45 degrees x k, k = 0..7 (BF §3). [FW]
- The DSP's tick rate is twice the symbol rate: alternate ticks are zeros, so
  a symbol takes two ticks (BF §3). [FW]
- Bits are sent least significant bit first everywhere on the Courier (BF §0,
  §3). [FW]
- Within a tribit the first bit sent is the tribit's least significant bit:
  inferred from "LSB first", not confirmed (BF §3). [BM]
- Scrambler 1 + x^-14 + x^-17, self-synchronising: output = input XOR
  out[n-14] XOR out[n-17]; the descrambler uses the received history (BF §3).
  [FW]
- The shaping filter has 72 taps and its start-up lasts 17 ticks; coefficients and
  contents are unknown (BF §3). [FW]
- Carriers by role: originate-mode transmitter 375 Hz (450 bit/s) or 350 Hz
  (300 bit/s), answer-mode transmitter 1800 Hz; the receiver table is the
  mirror (PS §1.2). [PUB]
- The carriers are tied to the role, so after a reversal they presumably swap
  with the direction of the fast channel; the manual does not say so (PS §1.2,
  flagged there as inference). [OURS]
- The NCO increments are the carrier in units of 7200/2^19 Hz: 375 gives
  27306 = 0x6AAA, 350 gives 25486 = 0x638E (arithmetic on BF §3's table).
  [OURS]
- Manual analogue facts: back-channel band-pass zeros at 550 and 750 Hz, a
  forward-channel notch under the back channel, AGC time constant 1.5 s on
  the back channel and 15 ms on the forward one (PS §1.2; P2 "Analog
  Architecture"). [PUB]
- Back-channel band: the 450 variant about 274 to 476 Hz, the 300 variant 350
  +- 68 Hz; the forward channel's lower edge is 600 Hz at -3 dB and 400 Hz at
  -36 dB (BF §3, "Spectral coexistence"). [FW]
- BinModem starts the transmitter's phase at 0 and its scrambler at zero at
  each start (BF §3). [BM]
- BinModem's end holding the forward channel listens for both speeds at once
  (two receivers, two HDLC decoders), so a speed change needs no coordination
  (BF §3). [BM]
- A 300 bit/s call is chosen if either end sets the 300 flag in 0xC0; the
  change waits until the confirming LA has been sent, then 250 ms (BF §3).
  [BM]
- BinModem's pulse is a 35 % RRC over +-4 symbols and its preamble 60 symbols
  of scrambled ones, 0.4 s at 450 and 0.6 s at 300 (BF §3). [BM]
- Our transmit pulse: root-raised-cosine, beta = 0.35, 274 to 476 Hz at 450
  and 283 to 417 Hz at 300; at 8000 Hz a symbol is 53 1/3 or 80 samples
  (spec "Back channel"). [OURS]
- Our preamble: 300 ms of scrambled ones, phase and scrambler starting at
  zero; our receivers never rely on it (spec "Back channel"). [OURS]
- Our scrambler: `modem/scramble.[ch]` reused if its V.22bis rule (invert
  after 64 ones) can be switched off; nothing says HST has the rule, so it
  is off (spec "Back channel"). [OURS]
- Our receiver: 250 to 500 Hz band-pass, AGC, 32-bit NCO to baseband,
  decimation to 1600 Hz, matched RRC, Gardner timing at 2 samples a symbol,
  differential decisions; carrier detect with hysteresis on symbol power;
  thresholds from simulation (spec "Back channel"). [OURS]
- Both receivers (450 and 300) run while we hold the forward channel; before
  agreement the locked one's bits are used, 450 preferred (spec "Back
  channel"). [OURS]
- The detector that starts `ANS_HST` demands a locked 8-DPSK demodulator, not
  band energy (spec "Detecting the caller's back channel"). [OURS]

## Forward channel

- Symbol rate 2400 baud, carrier 1800 Hz (BF §2). [FW]
- A secondary source agrees (2400 baud forward at 1800 Hz, 350 Hz and 300
  baud back); unverified, from a snippet of a Cloudflare-blocked page (PS
  §1.2). [PUB]
- HST.bis (a later, different modem) has a 16-point eye at 3200 baud for 9600
  bit/s; not used (PS §1.3; P3). [PUB]
- Rate code n = 2..7 carries n bits a symbol, rate 2400 x n (BF §2). [FW]

  | code | rate | coding |
  |---|---|---|
  | 2 | 4800 | uncoded, V.32's 4800 (4 points) |
  | 3 | 7200 | trellis coded as V.32bis |
  | 4 | 9600 | trellis coded as V.32bis |
  | 5 | 12000 | trellis coded as V.32bis |
  | 6 | 14400 | trellis coded as V.32bis |
  | 7 | 16800 | 256-point set (V.32terbo's, published); not implemented |

- Mapper: the V.32bis signal sets, 8-state trellis encoder and differential
  encoding, looked up by the same tables and indices as the firmware's V.32
  transmitter uses (BF §2). [FW]
- Trellis coding at 7200 and above, none at 4800 ("TCM" listed for 7200 and
  above; 4800 "QAM" alone) (PS §1.1; P2 "Receiver 103 routines"). [PUB]
- Scrambler 1 + x^-18 + x^-23 (V.32's calling-modem polynomial, "GPC"), the
  same polynomial whichever end sends, since the forward channel carries one
  transmitter at a time (BF §2). [FW]
- Bit order: least significant bit first (BF §2). [FW]
- Pulse: flat out to 1100 Hz from the carrier, -3 dB at 1200 Hz from the
  carrier, -36 dB at 1400 Hz from the carrier (BF §2). [FW]
- So the forward channel fills about 600 to 3000 Hz and leaves the band below
  about 450 Hz free for the back channel; the two channels are kept apart by
  frequency alone, with no echo canceller (BF §2, §0). [FW]
- Classic HST has no echo cancellation: "an echo cancellation mechanism is not
  necessary" (PS §1.3; P3 background). [PUB]
- The DSP sample rate is 7200 Hz (BF §0). [FW]
- Our pulse: root-raised-cosine with beta = 1/12. Flat to (1 - beta) x 1200 =
  1100 Hz, -3 dB at 1200 Hz, nominally zero beyond 1300 Hz; the channel spans
  500 to 3100 Hz. `gen_hst_tabs.py` makes the table 30 symbols long
  (Kaiser window, beta 1.75), for -40 dB at 400 Hz and below with the -3 dB
  points within 0.2 dB (at 24 symbols the window pulls them to about -3.5
  dB); a unit test checks the three firmware points (SPEC D11; spec
  "Forward channel: transmitter"; hst 14 in PROGRESS.md). [OURS]
- `v32tx` gets a pulse-table pointer and length defaulting to V.32's own 25 %
  table, so V.32's output stays byte-identical (spec "`v32tx` change"). [OURS]
- Guard filters: a high-pass before the forward receiver (stop below 480 Hz,
  pass above 560 Hz, at least 50 dB against the back band) and the back
  receiver's band-pass at least 50 dB down at 600 Hz and above (spec "No echo
  canceller"). [OURS]
- BinModem uses V.17's 25 % RRC pulse and a 10th-order Butterworth high-pass
  at 560 Hz on its forward transmitter output and in front of its forward
  receiver (BF §2). [BM]
- Rates we offer: forward 4800, 7200, 9600, 12000, 14400; 16800 deferred
  until a Courier recording shows its 256-point set; back 450 and 300 (SPEC
  D2, the owner's). [OURS]
- Our receiver, built on `qamrx` (2400 baud, carrier 3/4): guard high-pass
  and carrier detect in 600 to 3000 Hz; AB detection on both band-edge tones
  (600 and 3000 Hz) for 40 ms; decision-directed training on the 4 points A
  to D; data once more than 3 of 8 points fall off the 4-point grid; stored
  equaliser per direction (SPEC D10, D12; spec "Forward channel: receiver").
  [OURS]

## Training

- The Courier's forward start-up is 180 symbols, then 72, then either 200
  or 3096 (written 180 + 72 + 200 or 3096). The contents of the segments and the rule that picks the 200 or the 3096 form
  are not known (BF §2; BF "Open questions" 1). [FW]
- There is no in-band rate signal: the rates are agreed in the LR's 0xC0
  before the forward retrain, unlike V.32's R sequences (BF §2). [BM]
- In the firmware, a change of rate alters the bits sent per symbol with no
  new training (BF §2, §6). [FW]
- HST.bis (later, different) trains with `wink 64 Tf`, `AB 72 Tf`, `CD 8 Tf`,
  `TRN1 N1 Tf` (N1 = 1600 symbols, for a 16-point eye at 3200 baud), and has
  quick and full retrains and "quick turnaround" with stored echo canceller
  and equaliser (PS §1.3; P3 "Modem Training"). [PUB]
- We send, provisionally, V.32's segment generators with the firmware's
  lengths (spec "Forward training"). [OURS]

  | segment | symbols | contents |
  |---|---|---|
  | S1 | 180 | AB alternation (V.32's S) |
  | S2 | 72 | CD alternation (V.32's S-bar) |
  | S3 | 3096 (long) or 200 (short) | TRN: scrambled ones as V.32 TRN dibits on the 4 points |
  | then | | data at the rate in force (HDLC flags from the endpoint) |

- Totals: long 3348 symbols (1.395 s), short 452 symbols (188.3 ms), at 2400
  baud (180 + 72 + 3096; 180 + 72 + 200). [OURS]
- Long for the first training of each direction (the far receiver has nothing
  stored), short for later ones in the same direction (the post-agreement
  retrain, 0x2R, recovery, a reversal back). [OURS]
- V.17's long train is 3344 symbols, a hint that the long form is equaliser
  training, not proof (spec "Forward training"). [OURS]
- Constants, each commented provisional (Task 20 / R1): `HST_S1_SYMS` 180,
  `HST_S2_SYMS` 72, `HST_S3_LONG` 3096, `HST_S3_SHORT` 200, `HST_OPEN_CODE` 4
  (plan Task 15). [OURS]
- S3 maps its dibits by V.32's Table 4 from its first symbol, on all 4
  points: our receiver, once it has found AB, trains decision-directed on
  the 4 points A to D (spec "Forward channel: receiver"). V.32's own TRN
  begins with 256 symbols on A and C alone, for V.32's receiver. The scrambler (v32tx's, `answer` 0: GPC) starts
  from zero at each training and runs on into the data (hst 15 in
  PROGRESS.md). [OURS]
- BinModem sends V.17's long train (section `V.17 long train`) and V.17's
  turn-off (32 symbols of ones, 48 quiet): stand-ins, not HST (BF §2). [BM]
- The `-DHST_PROFILE_BM` test build: opening code 3 (7200), every training
  (long or short) V.17's long train, rate requests 0x3R with a retrain
  (hst 15 and its review fixes in PROGRESS.md). Release builds and golden
  files never use it (SPEC D16). [OURS]

## V.17 long train

From ITU-T V.17 (02/91), Table 3/V.17 and clause 5.1. These drive the
`-DHST_PROFILE_BM` training (Task 15).

| value | source | tag |
|---|---|---|
| The scrambler is 1 + x^-18 + x^-23 (the same as the HST forward channel's); the scrambled data divide by the polynomial at the transmitter. | V.17 §4 | [PUB] |
| Long train = four segments: 256 symbols of ABAB alternations, 2976 of equaliser conditioning, 64 of bridge, 48 of scrambled ONEs; 3344 symbols in all, about 1393 ms. | V.17 Table 3, §5.1 | [PUB] |
| Segment 1: alternations between states A and B, 256 symbols (A, B, A, B ...). | V.17 §5.1.1 | [PUB] |
| Segment 2: the states A, B, C, D sent from a pseudo-random 4800 bit/s pattern of the 1 + x^-18 + x^-23 scrambler with binary ONE at its input; differential encoding off; scrambled dibits encode as 00 = C, 01 = D, 11 = A, 10 = B. | V.17 §5.1.2, Table 4 | [PUB] |
| Segment 2's initial scrambler state is the one that outputs, with ONE at the input, the first 16 dibits `00 01 00 01 00 01 00 01 00 01 00 01 10 01 10 01` (C D C D C D C D C D C D B D B D), the first bit of a dibit first in time. | V.17 §5.1.2, Table 4 | [PUB] |
| That state, as the 23 bits sent before the first output (oldest first), is `01011101100110111010101`, which is 0x2ECDD5 with the most recent bit as the least significant bit. We found it by running out(n) = 1 + out(n-18) + out(n-23) backwards from the table's first 23 output bits; the table's other 9 output bits pass as a check. | derived from V.17 Table 4 | [PUB] |
| Segment 3 (bridge, long train only): a 16-bit sequence sent 8 times (128 bits, 64 symbols at 4800 bit/s), B0 the first bit into the scrambler; B7, B11 and B15 are 1 and the rest 0 (`0000 0001 0001 0001`). Bits 4-6, 8-10, 12-14 are for further study; a receiver ignores them. | V.17 §5.1.3, Table 5 | [PUB] |
| Segment 3 is scrambled and its dibits differentially encoded: 00 = +90 degrees, 01 = 0, 10 = 180, 11 = -90 (Table 6), starting from the last symbol of segment 2; the scrambler carries on from the state segment 2 left. | V.17 §5.1.3, Table 6; §5.1.4 | [PUB] |
| Segment 4: scrambled binary ONEs at the channel rate for 48 symbols; for the long train the differential encoder starts from the first symbol of segment 3; the convolutional encoder starts at state zero; the scrambler continues from the last symbol of the previous segment. | V.17 §5.1.4 | [PUB] |
| The points segments 1 to 3 use are the synchronising elements A, B, C, D of Figures 2 to 5/V.17, the same four at every rate: A = (-6, -2), B = (2, -6), C = (6, 2), D = (-2, 6), each the previous one turned +90 degrees. Read from the drawn markers on Figure 3 (12000 bit/s). | V.17 Figs 2-5 | [PUB] |
| Turn-off: 32 symbols of scrambled ONEs, then 48 symbols of no energy (80 symbols, 33 ms). | V.17 §5.2, Table 7 | [PUB] |
| Talker echo protection (optional, not used): unmodulated carrier 185 to 200 ms, then silence 20 to 25 ms. | V.17 §5.3 | [PUB] |
| BinModem sends these four segments: 3344 symbols, 1.39 s. | BF §2 | [BM] |

The resync row of Table 3 is damaged in the PDF's text layer (it prints 2938
and 3342 where 38 and 342 fit the 142 ms printed beside them); Task 15 uses
the long train only.

## MNP framing

- Annex A has two framings, chosen by the LR's framing mode parameter: mode 2,
  start-stop octet-oriented, used for the establishment phase, and mode 3,
  bit-oriented HDLC. An entity that supports mode 3 supports mode 2; the
  responder answers with the lesser mode (A.3, A.4, A.6.4.1.5; PS §3.1).
  [PUB]
- Mode 2: each octet carries a start and a stop bit (10 bits an octet). A
  frame is SYN DLE STX (`16 10 02`), the body (header and information), DLE
  ETX (`10 03`), then the 16-bit FCS (A.3, Figure A.1). [PUB]
- Mode 2 transparency: a DLE in the body is followed by a second DLE (the
  receiver discards the second); the DLEs of the start and end flags are not
  doubled (A.3.4). [PUB]
- Mode 2 FCS: generated by x^16 + x^15 + x^2 + 1; the body and the ETX octet
  are in the calculation, the start flag and the doubling DLEs are not
  (A.3.6). [PUB]
- Mode 2 FCS initial value and octet order are in no public text. We take
  CRC-16/ARC: initial 0, reflected, low octet first; check value 0xBB3D for
  "123456789". All four variants sit behind one constant (`MNP_FCS2`); R3
  settles it. [OURS]
- Mode 3: flag 0x7E, zero insertion after five ones, FCS-16 of V.42 8.1.1.6.1,
  flag, bits LSB first (A.4, A.4.1, A.4.4). [PUB]
- CRC-16/X.25 (CCITT, reflected): preset 0xFFFF, the ones' complement is sent,
  low octet first; "123456789" gives 0x906E, which goes out as `6E 90`; the
  good-frame residue (the register after a frame and its FCS) is 0xF0B8. The
  firmware routine is at 8FBB:1874 (BF §5); we checked the check value and
  the residue by computation. [FW]
- Idle: flags in mode 3, mark in mode 2, mark before the protocol is known
  (V.42 7.2.1.3, as today) (spec "Framings"). [PUB]
- HST runs MNP class 3, the synchronous class; MNP's own start-stop framing
  is not used on HST (BF §5). [FW]
- BinModem sends its LR in HDLC only (BF §5). [BM]
- An answerer leaves detection "on receipt of continuous flags, or of an LAPM
  or alternative procedure protocol frame" (V.42 7.2.1.3; spec "Detection of
  an MNP caller"). [PUB]
- The ODP is DC1 with even parity (0x88), one or two 0xFF, DC1 with odd parity
  (0x89), then as many 0xFF; the ADP is `E $FF [$FF] C $FF [$FF]` or `E $FF
  [$FF] NUL $FF [$FF]` sent at least ten times; then LR, LR, LA within T401
  (PS §4.2). [PUB]
- Our detection, with `mnp.enable` 1: an ODP leads to LAPM as today; a
  CRC-valid LR leads to MNP, in mode 2 (SYN DLE STX in the start-stop stream)
  or mode 3 (an HDLC frame whose header is `LI 01 02 ...` with LI the header
  length; an LAPM frame cannot pass, its third octet is never the constant 2);
  we reply in the framing the LR came in (spec "Detection of an MNP caller").
  [OURS]
- Off HST, nothing within T400 = 750 ms leads to start-stop, as today; R3 may
  change it. [OURS]

## MNP frames

- Types (Table A.1): LR 1, LD 2, LT 4, LA 5, LN 6, LNA 7; type 3 unused
  (A.6.2.2; PS §3.1). [PUB]
- The header is a length indication (LI: the number of header octets after it;
  255 means a 16-bit extended length follows), the type, fixed parameters,
  then variable parameters, each `type (1..254), length (1), value` (A.6.2).
  [PUB]
- The same type numbers on the Courier (BF §5). [FW]

Standard layouts, the whole header shown with its LI (A.6.4 to A.6.7; PS §3.1)
[PUB]:

| frame | layout | clause |
|---|---|---|
| LT | `04 04 01 01 N(S)` + information, 1..N401 octets, never empty | A.6.6.1, Table A.6a |
| LA | `07 05 01 01 N(R) 02 01 N(k)` | A.6.4.2, Table A.3a |
| LN | `07 06 01 01 N(SA) 02 01 type` | A.6.7.1, Table A.7 |
| LNA | `04 07 01 01 N(RA)` | A.6.7.2, Table A.8 |
| LD | `04 02 01 01 reason`, or LI 7 with `02 01 user-code` | A.6.5.1, Table A.4 |

- Annex A optimised layouts (LR parameter 8 bit 2, "fixed field LT and LA
  frames"): LT `02 04 N(S)` + information, LA `03 05 N(R) N(k)`; the LI is
  kept; LN, LNA and LD are unchanged (A.6.4.1.8, Tables A.3b and A.6b). [PUB]

Streamlined (HST) layouts: the LI is dropped from every frame, the first octet
is the type. From the firmware's receive path 8FBB:1892 and frame builders at
0x862B8, 0x861E1, 0x85F8C, 0x91DC2 (BF §5). [FW]

| frame | streamlined layout |
|---|---|
| LT | `04 N(S) data...` |
| LA | `05 N(R) N(k)` |
| LN | `06 N(SA) code` |
| LNA | `07 N(RA)` |
| LD | `02 reason` |

- The patent says the same: formats "streamlined to eliminate redundant
  bytes ... the process applies to all frame types, and the first Length
  indicator byte found in standard MNP has been removed" (P1:446; PS §3.2
  item 1). [PUB]
- An LR is always in the standard layout, even after optimisation; a decoder
  in streamlined mode recognises an LR by its second octet 0x01 (BF §5). [FW]
- BinModem's standard layouts of LA, LN, LNA and LD follow MNP's published
  parameter form, not the Courier (BF §5). [BM]
- We send streamlined layouts when HST is agreed and both LRs carry a non-zero
  parameter 8; otherwise standard, or Annex A's optimised ones if parameter 8
  says so. The rule is BinModem's (BF §5) and we follow it to talk to it.
  [OURS]
- We receive every layout: standard when octet 0 is a consistent LI and octet
  1 a known type; streamlined when octet 0 is a known type and the length
  fits; when both fit the agreed layout wins and the expected N(S) breaks a
  tie. Reason: the Courier's LR template has no parameter 8, yet the patent
  streamlines all frames, so which one a Courier sends is unknown (spec "HST
  profile"). [OURS]
- Before the caller's layout is known (no parameter 8 from both), it is
  learned from its first frame that settles it, an in-sequence LT
  included; a frame read two ways as two types, neither an LT in
  sequence, is not acted on (spec "HST profile", review of Task 8). [OURS]
- Frame lengths for T401 (Lf the frame overhead, Llt the LT header, Lla the LA
  header): octet-oriented Lf 7, bit-oriented Lf 4; non-optimised Llt 5 and Lla
  8; optimised Llt 3 and Lla 4 (A.7.5.1). [PUB]
- Streamlined: Llt 2 (type and N(S)) and Lla 3 (type, N(R), N(k)). Annex A
  has no streamlined case; these are the layouts above counted (plan Task 1
  Step 3). [OURS]
- LD reasons (Table A.5): 1 "LR expected but not received", 2 "LR constant
  parameter 1 contains an unexpected value", 3 "LR received with incompatible
  or unknown parameter value", 4 to 254 reserved, 255 user-initiated
  (A.6.5.1.3; PS §3.1). [PUB]
- Who may send code 3: the Note under Table A.5 says "Code 3 is only used
  during the protocol establishment phase by the establishment initiator";
  codes 1, 2 and 3 are the establishment failures (A.5, Table A.5 and its
  Note, in A.6.5.1.3). A responder that cannot take an LR's parameters does
  not send 3. [PUB]
- The Courier's S17 (disconnect reason) list: 1 "Received data different than
  requested", 2 "Incompatible MNP protocol", 3 "Connection request parameters
  unknown", 4 "Remote modem retry timeout", 5 "Inactivity timeout", 6
  "Destination user not found", 254 "Peer protocol error", 255 "User
  initiated disconnect" (PS §2.1, [S6]). [PUB]
- We send reason 4 when retries run out ("remote modem retry timeout"; spec
  "Status, end reasons"). [OURS]
- USR frame types we do not implement: NAK (a double ACK: the last good LT,
  later ones out of sequence), LTA (a data frame and an acknowledgement
  combined), and on the back channel only LT1, LT2, LT3 (fixed frames of 1, 2
  or 3 data octets; the low 4 bits of the sequence number in the upper nibble
  of the frame-id octet), LTR and LTDR ("repeat" and "double repeat" the last
  data frame, only if it was under 5 octets, with "a phantom 0FFH shifted
  into the CRC"). A receiver checks the first octet for one of these and
  checks the CRC without waiting for the closing flag; a retransmission of
  one "is reformatted into a standard LT frame". Frame ids and layouts (FIG.
  5 of the patent, an image) are not public (P1:446-460; PS §3.2). [PUB]
- A CRC-valid frame with an unknown first octet is counted (`rx_unknown`,
  `rx_unknown_id`, the first octet only) and dropped, unacknowledged (spec
  "HST profile"). [OURS]
- BinModem leaves out four things: USR's acknowledgement-variant frame, a
  data frame that also acknowledges, the short data frames, and MNP 5 (BF §5).
  [BM]
- Back-channel data frames are at most 64 octets (P1:416). [PUB]

## LR and 0xC0

The Courier's link request template, byte for byte, from firmware address
0x856E7 (BF §5). [FW]

```
14  01  02  01 06 01 00 00 00 00 FF  02 01 03  03 01 08  04 02 40 00
LI  LR  c1  p1: len 6 "1,0,0,0,0,255"  p2: 3  p3: k = 8  p4: N401 = 0x0040 = 64
```

- LI 0x14 = 20 is the frame length (21 octets) minus 1; the octet after the
  type (0x02) is Annex A's constant parameter 1, which must be 2 or
  negotiation fails with LD reason 2 (A.6.4.1.3, A.7.1.5.1; BF §5). [FW]
- Constant parameter 2 is `01 06 01 00 00 00 00 FF` ("1,6,1,0,0,0,0,255"); any
  received value is accepted and the constant is always produced (A.6.4.1.4,
  A.7.1.5.2). [PUB]
- Parameter 2, the framing mode, is 3 in the template (BF §5). [FW]
- Parameter 3, k, is 8 in the template (BF §5). [FW]
- Annex A: "Applications should support k = 8" and N401 = 64; k never exceeds
  255 (A.7.5.6, A.7.5.7). [PUB]
- Parameter 4, N401, is two octets, little-endian in the template (`40 00` =
  64); Annex A gives no order (BF §5). [FW]
- The template has no parameter 8. BinModem appends parameter 8 = 3 when it
  wants optimised headers, and the USR parameter 0xC0 (BF §5). [BM]
- Parameter 8 (A.6.4.1.8) is a bit map: bit 1 = 1 means a maximum information
  field of 256 octets; bit 2 = 1 means fixed field LT and LA frames; bits 3 to
  8 reserved, zero on transmission, ignored on receipt. [PUB]
- Negotiation: framing mode the lower, k the lower, N401 the smaller, unknown
  parameters ignored and left out of the reply (A.7.1.5). [PUB]
- We offer k = 8; N401 = 64 (256 only if the host sets `n401` 256 and `opt`
  bit 1); parameter 8 = bit 2 only; framing mode 3 (SPEC D23; spec
  "Establishment"). [OURS]
- The USR parameter is `C0 02 flags rates`. The firmware routine that
  assembles it is at 0x85A82 and the one that reads it at 0x85422 (BF §5).
  [FW]
- flags: bit 0 (0x01) is HST; bit 3 (0x08) is the 300 bit/s back channel; any
  other bits are unknown (BF §5). [FW]
- rates: a bit mask, bit n set when rate code n (2400 x n bit/s) is acceptable,
  n = 2..7. Examples: 7200 to 14400 is 0x78; 9600 only is 0x10; all six rates
  0xFC (BF §5). [FW]
- The answering end's reply carries the intersection of the two rate masks
  (BF §5). [FW]
- Merge details: HST is agreed only if both ends set bit 0; the agreed flags
  are the AND plus the OR of the 300 bit/s bits (if either end wants 300 the
  call gets 300); the agreed rates are the AND of both masks, then AND 0xFC;
  the best rate is the highest bit set, 7 down to 2 (BF §5). [BM]
- Our merge: rates = ours AND theirs AND 0x7C, so 16800 is never agreed (spec
  "HST profile"). [OURS]
- Our offer: flags 0x01; rates = the codes n = 2..6 with 2400 x n <=
  `max_down_bps` (0x7C at the default), because the mask governs our fast
  channel at connect; `max_up_bps` does not trim the mask and is enforced at
  reversal (SPEC D20). [OURS]
- No 0xC0, or an empty agreed mask: plain MNP over the HST channels, the pump
  staying at the opening rate (spec "HST profile"). [OURS]
- BinModem's HST96 (the 1986 unit as it models it) is rates 0x10 with the
  0x08 back-300 flag set on top of HST's 0x01 (flags 0x09), so an HST96 end
  meets a later HST at 9600/300; whether the 1986 unit
  shares the LR, 0xC0, framing or training is not established (BF §7). [BM]
- The Courier's setting for "disable 450 bps back channel" is S15 bit 2 (value
  4) (PS §2.1). [PUB]

## Establishment

As responder (we answer), from Annex A.7.1 (PS §3.1):

- The responder is ready to respond immediately after the physical connection
  and starts T401; on an LR it negotiates; on success it sends its LR and
  starts T401; on the confirming LA it enters the data phase (A.7.1.1,
  A.7.1.3). [PUB]
- It sends its LR again if T401 expires waiting for the LA, if a frame
  arrives with a bad FCS, or if another LR arrives; if it expires or fails a
  second time it rejects and enters the disconnect phase (A.7.1.3, A.7.1.4).
  [PUB]
- The initiator sends the LR, starts T401, negotiates on the reply LR, sends
  the confirming LA and enters the data phase; in that LA N(R) is 0 and N(k)
  is the credit from the response LR (A.7.1.2, A.6.4.2.3, A.6.4.2.4). [PUB]
- LD reason 2 when constant parameter 1 is not 2, reason 3 for parameters that
  fail negotiation, reason 1 when no LR arrives (A.6.5.1.3, A.7.1.4). [PUB]
- Our reply LR carries the lower framing mode, the minimum k and N401,
  parameter 8 (the AND) only if the caller sent it, and 0xC0 only if the
  caller sent it and the HST profile is active (spec "Establishment"). [OURS]
- The originator, on the responder's LR, counts as connected and sends an LA
  with N(R) 0 and credit = the window as its last standard-layout frame (BF
  §5, "Link establishment"). [BM]
- The responder counts as connected on that LA or on an LT arriving first (a
  lost confirmation); then it drops that LT and asks for a resend with an LA
  (BF §5). [BM]
- A repeated LR to the originator is confirmed again in the standard layout
  (BF §5). [BM]
- On an HST-linked call the endpoint hunts for an LR from the first sample,
  not from `CONNECTED`; T400 and the start-stop fallback do not apply; the
  window is our training's end plus 5 s; with no LR the endpoint ends with
  `V90M_EC_R_NO_EC` (SPEC D18, D19). [OURS]
- The post-agreement retrain starts when the caller's LA arrives, at the
  highest agreed rate with the short training, and stops data at the API
  (`retraining` 1) (SPEC D21; spec "The answering sequence" 7). [OURS]
- The firmware's connect step has the same shape: idle, set the agreed rate,
  start the forward transmitter (8FBB:1C70; BF §2). [FW]
- Establishment T401 is 2 s (Annex A allows 0.5 to 9 s; A.7.5.1). [OURS]
- Our LR unconfirmed after its one resend ends with LD reason 1, not 4:
  Table A.5 leaves the responder only code 1 (3 is the initiator's, 4 to
  254 reserved) and A.7.2.2 wants an LD once protocol messages came; the
  Courier's S17 code 4 names a retry timeout, which we send in the data
  phase (spec decision 26). [OURS]

## Data and attention

- LT: V(S) starts at 1 and counts modulo 256; N(S) = V(S) when the frame is
  designated for transmission; the information field is 1 to N401 octets
  (A.6.6.1.3, A.6.6.1). [PUB]
- LA acknowledges all LT frames up to and including N(R); an LA is sent for an
  invalid frame, an out-of-sequence frame, no credit, or T404; an LA whose
  N(R) equals the previous LA's means "send again from the first
  unacknowledged LT"; "the first reception of an LT frame with N(S) = V(R) -
  1 is ignored and does not cause transmission of an LA" (A.6.6.2, A.7; PS
  §3.1). [PUB]
- Credit N(k) is the number of maximum-length LT frames the receiver can
  accept now; zero halts the sender until an LA with a non-zero credit
  (A.6.6.2.2). [PUB]
- The receiver accepts only the next expected N(S), discards out-of-order
  frames and acknowledges the last in-order one: go-back-N (BF §5). [BM]
- LN has its own sequence N(SA) with V(SA) starting at 1; one LN outstanding;
  sent again every T401 until its LNA, each delivered once, up to N400 tries;
  types 1 destructive and expedited break, 2 non-destructive expedited, 3
  non-destructive non-expedited (A.6.7, A.7.4; PS §3.1). [PUB]
- Outside HST, LN types 1 to 3 map to `v90m_ec_send_break` and
  `v90m_ec_get_break`; on HST the octet carries the attention codes (spec
  "Frames, data transfer"). [OURS]
- Attention codes on HST: 0x1R asks for the forward channel at rate code R,
  0x3R asks the forward holder to change to rate R; the low nibble is the rate
  code (BF §4, §5, §6). [FW]
- The patent's three examples of LN type octets: `atn_reverse_9600 equ 14h`
  (reversal at 9600), `atn_retrain_7200 equ 23h` (fall back to 7200 with a
  retrain), `atn_upshift_9600 equ 34h` (speed increase to 9600). The low
  nibble is the rate code in all three (4 is 9600, 3 is 7200) and the high
  nibble the action; the full code space is not published (P2:547; PS §1.5).
  [PUB]
- 0x2R, "retrain at rate R", is read from `23h`; the patent does not say the
  whole class 2 means that (spec "HST profile"). [OURS]
- BinModem sends 0x1R with R = the current forward rate for a reversal; on
  0x1R the holder takes R as the rate of the new direction; BinModem sends
  0x3R for every rate change and retrains (BF §4, §6). [BM]
- Reversal is "at the current speed" (P1:466), so we send 0x1R with R = the
  current forward rate. [PUB]
- We send 0x3R for upshifts and 0x2R for downshifts and recovery (SPEC D15).
  [OURS]
- As holder we accept 0x1R (reversal), 0x2R (retrain at R) and 0x3R (switch to
  R); any other code gets an LNA, a count and no action (spec "HST profile").
  [OURS]
- While a reversal or a rate change is pending, only control frames (LA, LN,
  LNA, LD) go out, never an LT, until the pump reports the new state (spec
  "HST profile"). [OURS]
- BinModem sends no LT frame while a handover or rate change is waiting to
  finish (BF §4). [BM]
- HST session defaults: window 8, max data 64, class 3 (BF §5). [BM]
- On HST a repeated N(R) sends us back once per N(R) value; a loss of the
  LT sent again is left to T401. Read literally, A.7.3.5 a sends us back
  at every LA the loss draws, and on the slow back channel they come one
  by one (spec "HST profile"). [OURS]

## Timers

- T401 for LT and LN frames (A.7.5.1) is at least
  `2 x ((k/2) x Lb x (Lf + Llt + N401) + Lb x (Lf + Lla)) / bps + Trt`, with
  Lb = 10 (mode 2) or 8 (mode 3), Lf, Llt and Lla from `MNP frames`, and Trt
  the round trip including remote processing and queuing. [PUB]
- Table A.9, N401 = 64: 6 s at 1200 bit/s and 4 s at 2400 bit/s; N401 = 256:
  16 s at 1200 and 19 s at 2400 (A.7.5.1). [PUB]
- The 256 column does not follow the formula (18.1 s plus Trt at 1200, more
  than the table's 16; 9.1 s plus Trt at 2400, less than 19). We use the
  formula and treat N401 = 256 as optional. [OURS]
- `MNP_TRT_MS` = 1000. Annex A leaves Trt to the implementer. With k = 8, mode
  2 and N401 = 64 the formula's first term is 5.32 s at 1200 and 2.66 s at
  2400, so Table A.9's 6 s and 4 s imply a Trt of about 0.7 to 1.3 s; 1000
  sits inside both. [OURS]
- Where the formula has one rate, the LT term uses our transmit rate
  (`tx_bps`) and the LA term the far end's (`rx_bps`), since over HST they
  differ 30-fold; recomputed at every rate change or reversal, never below
  1500 ms (`t401_min_ms`), rounded up to the ms (spec "MNP timers"). [OURS]
- The values the plan's `mnp_t401_ms` test pins (framing mode, layout, k = 8,
  N401 = 64, `tx_bps`, `rx_bps`, floor 1500, Trt 1000): mode 2 standard at
  2400/2400 gives 3659 ms; mode 2 standard at 1200/1200 gives 6317 ms; mode 3
  streamlined at 14400/450 gives 1560 ms; mode 3 optimised at 14400/450
  gives 1600 ms; a fast link returns the floor, 1500 ms. Worked by hand from
  the formula. [OURS]
- On HST, T401 also counts the far end's one LT ahead of its LA (an LT
  at `rx_bps`, Trt's "queuing delay") and, when we receive the back
  channel, its receiver's hold-back: 127 ms at 450 bit/s, 180 ms at 300
  (the back receiver of track B, Task 12). 14400/450 streamlined: 1560 +
  1245 + 127 = 2932 ms (spec "MNP timers"). [OURS]
- T402, the LA timer, is 0.5 x T401 (A.7.5.2). [PUB]
- T403, the inactivity timer, is optional with a period of at least 59 s
  (A.7.5.3). [PUB]
- Ours is off (spec "MNP timers"). [OURS]
- T404, the flow-control timer (Table A.10): 7 s at 1200 bit/s, 3 s at 2400 or
  faster (A.7.5.4). [PUB]
- We take T404 from the rate at which we receive LT frames: at 2400 and above
  3 s, below it 7 s. Reason: Annex A's "physical connection speed" has no one
  value on an asymmetric link, and T404 times our acknowledgement of what the
  far end sends (plan Task 1 Step 3). [OURS]
- N400 = 12 attempts (A.7.5.5). [PUB]
- When N400 is exhausted: LD reason 4 and `V90M_EC_R_N400` (spec "MNP timers").
  [OURS]
- T401 in establishment may be 0.5 to 9 s (A.7.5.1). [PUB]
- During a retrain the host calls `v90m_ec_line_stopped` and `line_restored`;
  MNP holds T401, T402 and T404 between them and resumes from where they
  stood (SPEC D21). [OURS]
- BinModem's HST session retransmits after 3000 ms with no progress, up to 15
  tries (its MNP defaults 2000 ms and 12); on a timeout every unacknowledged
  frame is marked for resend and an LA tells the far end where this end has
  got to (BF §5). [BM]
- T400 off HST is 750 ms; R3 may change it (spec "Detection of an MNP
  caller"). [OURS]
- The HST window in place of T400 and `fallback`: from the first sample until
  our training's end plus 5 s (SPEC D19). [OURS]

## Reversal

- At the start the answerer holds the forward channel and the caller the back
  channel (BF §4). [FW]
- The back-channel end asks for the forward channel with an LN, attention
  0x1R; the holder answers with an LNA and then stops sending the forward
  carrier and takes up the back channel instead (firmware event 0x73); the
  requester, once the LNA is in, stops its back channel and starts forward
  training (firmware event 0x76) (BF §4). [FW]
- Both ends reset their HDLC encoder and decoder at the switch; the new
  forward holder restarts its back-channel receivers (BF §4). [BM]
- The patent's rule: the direction reverses "whenever the modem currently
  transmitting over the backchannel accumulates more than a predetermined
  maximum backlog of untransmitted data": more than 64 bytes (the largest
  back-channel frame), provided the far modem has had the fast channel for at
  least a predetermined minimum (for example 2 seconds). The LN is "a type 4
  LN" sent by the back channel; "a LNA is sent at the current speed, and then
  the reversal takes place" (P1:416, P1:466; PS §1.5). [PUB]
- Our policy: ask when the backlog (queued plus unacknowledged) is over 64
  octets, the far end has held the forward channel at least 2 s, and no other
  attention is outstanding (SPEC D14, from the patent). [OURS]
- BinModem's policy: backlog over 256 octets, at least 3000 ms since the last
  switch, link up, nothing pending (BF §4). [BM]
- Our steps: the holder, once its LNA and closing flag have left, plus 20 ms,
  stops the forward carrier and starts the back transmitter (preamble) and the
  forward receiver; the requester, on the LNA, stops the back transmitter at
  once, waits 200 ms, then sends the forward training at R, long or short
  (spec "Channel reversal"). [OURS]
- BinModem's: the holder waits until its encoder and queue are empty plus 120
  ms; the requester waits 400 ms (BF §4). [BM]
- Bound: if the new forward signal is not detected within 3 s of the switch,
  the watchdogs take over (spec "Channel reversal"). [OURS]
- At the API: `moving` stays 1; `bitrate` and `rx_bitrate` swap, so
  `V90M_EV_RATES` fires with `tx_bps` and `rx_bps` exchanged;
  `v90m_stats.reversals` counts it; it is neither a renegotiation nor a
  retrain (SPEC D13). [OURS]
- Watchdogs: holding forward, no back carrier for 5 s ends the call
  `V90M_END_LOST`; holding back with no forward carrier, after 1 s ask 0x2R at
  the current rate, after 5 s take the forward channel back; no call stays
  connected without a far signal for more than 12 s (spec "Retrain,
  watchdogs"). [OURS]
- BinModem's watchdog: no far carrier for 5000 ms with nothing pending resets
  the roles to the defaults, and the call ends after 12 000 ms without a far
  carrier once linked (BF §4, §6). [BM]
- The fast channel's transmit buffer signals the DTE to stop (CTS low or XOFF)
  above 90 % full and to resume below half (P1:414; PS §1.5). [PUB]
- The Courier's `ATI6` counts "Line Reversals" (PS §2.2). [PUB]

## Rate changes

- Upshift: decided at 10 s intervals; if signal quality is good for 90 % of
  the interval a speed-up is requested; a speed-up request is inhibited for an
  interval after a speed-down request (P2:534; PS §1.5). [PUB]
- Downshift: whenever 15 frame errors occur within a 10 s interval that begins
  at the first error, and on "three failed attempts" to retrain within ten
  seconds (P2:536-538; PS §1.5). [PUB]
- Frame errors counted: CRC errors on MNP frames plus 50 ms blocks of fill
  characters, which count as frames when no MNP frame is being received
  (P2:538). [PUB]
- Signalling: an LN on the back channel whose type is the action (section
  `Data and attention`; P2:547). [PUB]
- Executing a rate-change request on the forward channel: at the end of the
  frame being sent, a 15 ms sustained mark at the current speed, then a 15 ms
  constant mark at the newly requested speed; marks are not otherwise sent,
  fill characters of a unique value fill the gaps (P2:561). [PUB]
- In the firmware a rate change needs no new training: only the bits per
  symbol change (BF §2, §6). [FW]
- Our 0x3R as holder: finish the frame; 15 ms (36 symbols) of ones at the old
  rate; switch the encoder's rate index (scrambler, differential and trellis
  states continue); 15 ms of ones at the new rate; resume (spec "Rate
  changes"). [OURS]
- Our receipt: arm a detector for at least 10 ms of ones at the old rate; then
  decode both rates in parallel for up to 48 symbols and switch when the new
  rate gives 24 ones in a row; with no switch within 2 s, or a training
  instead, the generic acquisition takes over. [OURS]
- 0x2R as holder: the short training at R (spec "Rate changes"). [OURS]
- Recovery: holding the forward channel, with the back channel showing the far
  end alive but nothing acknowledged for 2 x T401, send the short training at
  the current rate. [OURS]
- Who asks: the forward channel's receiver. "Good" is our MSE under the next
  rate's threshold: the SNR for a bit error ratio of 1e-5 at that rate plus 3
  dB, from simulation (SPEC D14). [OURS]
- When the caller holds the fast channel we never ask for a rate above
  `max_up_bps`; we never grant a 0x3R above it either (an LNA, a count, and
  our own 0x2R at the cap) (SPEC D20). [OURS]
- BinModem retrains on every rate change; its asker's working SNRs are 14400:
  27 dB, 12000: 24 dB, 9600: 21 dB, 7200: 18 dB ("3 dB above the 1e-5
  point"); down if SNR is under working(current) - 2 dB, up if over
  working(next) + 1 dB, only after 1000 ms in data since the last training;
  the holder waits for its LNA to drain plus 120 ms; the asker retrains anyway
  after 2500 ms without an LNA (BF §6). [BM]
- The Courier's `S15` bit 1 (value 2) disables online fallback (PS §2.1).
  [PUB]
- 3 failed retrains within 10 s at the lowest agreed rate end the call
  `V90M_END_RETRAIN_FAILED` (spec "end codes"). [OURS]

## Cleardown

- The LD layout is the Courier's, `04 02 01 01 reason` (standard) or `02
  reason` (streamlined) (BF §5). [FW]
- `ATH` sends an LD with reason 0xFF (255) and holds the line 1000 ms for it
  to get out (BF §6). [BM]
- Ours: `v90m_ec_release()` sends LD 255 and waits until `tx_busy` is 0, at
  most 1 s, then the host calls `v90m_cleardown()` (spec "Retrain, watchdogs,
  cleardown"). [OURS]
- Annex A advises not to send an LD on user disconnect "to promote proper
  interworking with the installed base" (A.6.5.1; PS §3.1). [PUB]
- We send it anyway: a Courier expects an LD (spec "Frames, data transfer").
  [OURS]
- Theirs: the endpoint gets an LD and writes it to the mailbox; the modem ends
  when the far carrier drops or after 2 s, whichever is first
  (`V90M_END_CLEARDOWN_FAR`) (spec "Retrain, watchdogs, cleardown"). [OURS]
- MNP retries exhausted: LD with reason 4 (BF §6). [BM]
- No LR answer within the retry limit ends the call with reason 1 and no LD is
  sent (BF §6). [BM]
- No LR on HST: the endpoint's `V90M_EC_R_NO_EC`, then the host clears down
  (`V90M_END_CLEARDOWN_OURS`) (SPEC D19). [OURS]
- End codes: `V90M_END_HST_FAILED` (backstop, no link in 15 s),
  `V90M_END_LOST` (far carrier gone), `V90M_END_RETRAIN_FAILED`,
  `V90M_END_CLEARDOWN_FAR`, `V90M_END_CLEARDOWN_OURS` (spec "Retrain,
  watchdogs, cleardown"). [OURS]
- The Courier's S20 holds the reason for NO CARRIER, values 0 to 7 (PS §2.1).
  [PUB]

## Evidence plan

Three recordings settle the provisional parts. R1 needs none of our code
(SPEC D17), so it goes to the owner now, alongside milestone 1; the owner's
plain-language version of the R1 procedure is made from this section. R3 is
made after Task 10; R2 at milestone 7. Everything in this section is our
plan. [OURS]

Where recordings go: `HSTRECS=/home/georgica/sync-build/hst-recs` (outside the
repository; created by the owner). File names: `r1-<b1|b0|...>-<nn>.pcap` and
`.wav`, `r2-<nn>.*`, `r3-<modem>-<mod>-<nn>.*`. Only `sha256sum` lines enter
the repository, in `test/golden/hst-recordings.sha256`. A recording that shows
a fault becomes a fixture (CONTRIBUTING.md), truncated before the first LT.

Privacy: R1 uses test text only, between the owner's Courier and BinModem on
the owner's own lines; R2 uses test text only; nothing is recorded past
error control up on the ISP: R2 and R3 are cut at MNP link-up (the LA that
completes establishment) at record time.

### R1: a Courier answering BinModem, tapped at the ATA

1. Equipment: the owner's Courier on an ATA port (the Cisco VG202XM
   preferred: RFC 2198, about 40 ms jitter buffer; the HT802v2 works with its
   known limits); a computer running BinModem's `modem-sip`, with a SIP route
   to the ATA port's number; a capture point that sees the ATA's RTP in both
   directions (tcpdump on its VLAN, or a mirrored port).
2. ATA: G.711 only, no VAD or comfort noise, no echo cancellation, a fixed
   jitter buffer, and the modem or fax pass-through settings the owner uses
   for modem calls.
3. Courier, run 1: `AT&F`, then `ATB1 &N0 &M4 &B1 &A3 S0=1` (HST handshake,
   negotiate the highest speed, error control, fixed DTE rate, extended
   result codes, auto-answer). Run 2: the same with `ATB0`. Note the model and
   `ATI7`, and `ATI4`, for each.
4. BinModem: `modem-sip --accounts sip.txt --account test --type "AT+MS=HST"
   --type "ATD sip:<port>@<ata>" --send "<test text>" --hold 20 --seconds 120
   --record r1-<b>-<nn>.wav` (BF §9, with its build workaround if rustc is
   1.93; BinModem's `--drive` sets the line level, default 0.1).
5. Capture: tcpdump of the RTP at the ATA, whole packets (`-s 0`), both
   directions, from before the call to after it ends (`HSTRECS`); BinModem's
   `--record` wav as the second copy (16 kHz).
6. Runs: (a) `B1`, short text, three calls; (b) `B0`, the same; (c) `B1` with a
   test text of 4 KB or more, so that BinModem's back-channel backlog passes
   256 octets and it asks for a reversal; (d) optional: `AT+MS=HST96`, and a
   Courier with `S15=4` (no 450 bit/s back channel).
7. After each call, on the Courier's terminal: the result-code line and
   `ATI6`, copied as text.
8. Cut: the original stays whole in `HSTRECS` (test text only); a fixture made
   from it is cut before the first LT.
9. Send back: the `HSTRECS` listing with `sha256sum`, the Courier's model,
   `ATI4`, `ATI6`, `ATI7` text, the terminal screen text, the ATA's model and
   firmware, and a line of notes per call (did it answer, what the terminal
   said, whether it connected).

Even if BinModem's stand-in back channel brings up no link, the tone and the
training are recorded: they precede any frame.

### R2: the Courier calling our answerer (Task 25, milestone 7)

1. Equipment: the owner's Courier on the box's line; the local nsdiald test
   build (SPEC D22); our recorder on the box.
2. Courier: `AT&F`, `ATB1 &N0 &M4 &B1 &A3`; one run with `S15=4` (300 bit/s
   back channel); note `ATI7`.
3. Dial the test number; test text only; a long file for a reversal and a
   forced rate change.
4. Capture on the box: audio cut at MNP link-up; after it, status only
   (`v90m_ec_status` fields and the stats timestamps).
5. Send back: the Courier's result line, `ATI6`, `ATI7`.

### R3: real MNP-only callers over V.32bis and V.22bis (Task 10, milestone 1)

1. Equipment: the owner's modems forced to MNP: the USR with `&M4 S27=32` (V.42
   detection and LAPM off); the Rockwell and Conexant parts with the setting
   their manuals give (Task 10 fills the commands in); the local nsdiald test
   build.
2. Force V.32bis, then V.22bis, with each modem's own command (Task 10 fills
   these in).
3. Each calls the test number; the box records up to link-up only.
4. Capture: audio from the first sample to the end of establishment (the LA),
   cut there at record time; note the time at which the first LR arrives.
5. Send back: nothing past the cut; the modem's model and settings.

### What each settles

| item | spec Open question | R1 | R2 | R3 |
|---|---|---|---|---|
| a Courier caller accepts ANSam, then sends its back channel | 1 | | yes | |
| the answerer's tone (2100 or 2225 Hz; `B0` against `B1`), the gap, any V.22bis stage, the opening rate, the forward pulse and level | 2 | yes | | |
| the training's contents and the long/short rule | 3 | first training; after a reversal (c) | after a reversal | |
| the back channel's pulse, preamble, start and tribit order | 4 | after a reversal (c) | yes | |
| the other 0xC0 bits; parameter 8; which layout a Courier sends | 5 | the Courier's reply LR, if BinModem's back channel gets through | yes | |
| the IDs and layouts of NAK, LTA, LT1-3, LTR, LTDR | 6 | | `rx_unknown_id`; FIG. 5 read by a person | |
| how a Courier takes a reply 0xC0 with an empty rate mask (`C0 02 01 00`, its offer 16800 only) | 5 | | a run with the Courier's rates set to 16800 only, if it allows | |
| the rate change on the line (marks, trellis and scrambler continuity); reversal timing | 7 | after a reversal (c) | a long call with a forced shift, from status timestamps | |
| the mode 2 FCS's initial value and octet order; how soon MNP callers send their LR (is T400 = 750 ms enough?) | 8 | | | yes |
| when a Courier moves to the 300 back channel | 9 | (d) | `S15` bit 2 | |
| low-band loss through the ATAs (a high-pass near 300 Hz hurts the back channel) | 10 | | the back receiver's SNR status on the HT802v2 and the VG202XM; recorded in `limits.md` | |
| 16800's set and trellis; whether the 1986 HST shares the framing | 11 | `&N9`, or a 1986 unit if one turns up; out of scope for 0.7.0 | | |

## Recordings

None made yet. A row is added for each, with its hash; each item a recording
settles is re-tagged `[OURS -> observed]` in its section above.

| id | date | setup | file in HSTRECS | sha256 | cut | settles |
|---|---|---|---|---|---|---|
| R1 | not yet made | | | | | |
| R2 | not yet made | | | | | |
| R3 | not yet made | | | | | |

## Unsettled

Every provisional value above and what settles it. Q is the spec's open
question number (last table of `Evidence plan`).

| item | provisional value | settled by |
|---|---|---|
| ANSam accepted by a Courier caller (Q1) | assumed | R2 |
| the answerer's tone, gap, V.22bis stage, opening rate (Q2) | ANSam; 400 ms window; none; 9600 | R1 |
| forward training contents and long/short rule (Q3) | V.32 S, S-bar and TRN, 180, 72, 3096 or 200 | R1, R2 |
| forward pulse and level | RRC beta = 1/12 from three points | R1 |
| back pulse, preamble, start, tribit order (Q4) | 35 % RRC; 300 ms scrambled ones; tribit LSB first | R2; R1 after a reversal |
| 0xC0 bits other than 0 and 3; parameter 8; the layout a Courier sends (Q5) | ignored; both offered, every layout received | R1, R2 |
| NAK, LTA, LT1-3, LTR, LTDR ids (Q6) | counted and dropped | FIG. 5; R2 |
| rate change without retrain: marks, state continuity (Q7) | 0x3R with 15 ms marks, 0x2R retrain, recovery | R2 |
| reversal gap and thresholds (Q7) | 20 ms, 200 ms; 64 octets and 2 s | R1, R2 |
| mode 2 FCS initial value and octet order; T400 (Q8) | CRC-16/ARC, low octet first; 750 ms | R3 |
| the 300 bit/s back-channel switch (Q9) | by LR flag, after the confirming LA plus 250 ms | R2 |
| low-band loss through ATAs (Q10) | none assumed | R2 |
| 16800 set; the 1986 HST (Q11) | not implemented; assumed the same | out of scope |
| symbol rate at 9600 | 2400 baud (HST.bis's 3200 is a later modem) | R1 |
| answer tone, 2100 or 2225 Hz | ANSam | R1, R2 |
| the Table A.9 N401 = 256 column and `MNP_TRT_MS` | formula; 1000 ms | R3 (time to the first LA) |
| the Courier's 0x2R | read from `23h` | R2 |
| MNP 5 and V.42bis over HST; HST without error control | not offered | follow-up |
