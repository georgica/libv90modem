# libv90modem: design

How the modem is built and why. The API is in `include/v90modem.h`; what was
measured and when is in v90d's `PROGRESS.md`, where this code grew.

## 1. Rules

- Portable C99. Fixed-point only in the signal path; the simulator (`sim/`)
  may use double, it is a test peer and never installed.
- No allocation after `v90m_create()` (or none at all with `v90m_init()` on
  memory of the caller's), no global state: any number of modems in one
  process (two calls at once run on the HT802, one modem per port).
- Every block builds and passes its tests on x86 and on an ARM926 (cross-built,
  run under qemu-arm).
- Written from the ITU-T recommendations. Reference implementations (spandsp)
  are test peers only: linked into host-only interop tests, never shipped,
  never copied.
- One sample in, one sample out underneath; `v90m_process_*` runs blocks of
  any length through it, and the result does not depend on how the samples
  are cut into blocks (`test/api/test_api.c` checks it).

## 2. Layers

| | what |
|---|---|
| `api/` | the public API over everything below: configuration, the block calls, events found by reading the modem's state after each sample, stats; start-stop (V.14) and V.42 objects |
| `modem/` | answering modem: V.8, V.21 and Bell 103, V.22bis/V.22 and Bell 212A, V.14, V.90 Phase 2 (`p2d`), Phases 3/4 and data (`p34d`), upstream receiver (`qamrx`, `ec`, `v34train`); V.34 answering (`p2ans`, `v34mp`, `v34mod`, `v34dx`, `v34ans`); the sequencer `answer`. Calling modem: V.8 (`v8orig`), V.34 (`p2call`, `v34orig`), the V.90 / V.92 analogue modem (`v90a` with `v90arx`, `v90dil`, `v90adec`; for V.92 `pcmtx`, `v92aseq` and `v92/v92_enc`); Bell 212A and 103 (`belltone`, the 2225 Hz answer tone); the sequencer `orig` |
| `v32/` | V.32bis / V.32 (4800 to 14400 bit/s), answer side |
| `v34/` | V.34 coding: training point sequences, data encoder and decoder (trellis, shell mapping, non-linear encoder) |
| `v90/` | V.90 digital transmitter from Phase 3 on (PCM signals, Jd, DIL, MP, data encoder), CP codec |
| `v92/` | V.92 PCM upstream, digital side: start-up sequences, the receiver for the caller's training signals, the CPd design, the data decoder, Phases 3/4 and data (`v92_p34d`) |
| `v42/` | V.42 LAPM, V.42bis, V.44, detection phase, start-stop fallback |
| `sim/`, `test/` | channel simulator and calling peers; the test suite (never installed) |
| `tools/`, `rig/` | the replay tool and its trace printers, recorders and benches, table generators |

## 3. The API layer

`api/v90modem.c` adds no signal processing. It maps `v90m_cfg` onto the
answering modem's configuration, clears the modem's memory before every
start (the code underneath expects zeroed memory), runs `answer_tick_pcm`,
`answer_tick_lin` or `answer_tick` once per sample, and after each sample
compares the modem's state with the one before to report `V90M_EV_*`
(connect, rates, data stopped / moving, end) and to count renegotiations.
The bit callbacks are called by the modem itself, inside that sample, at
the moment a bit is needed or decided: a V.42 endpoint (or a framer) on the
other side sees exactly the timing it would see sample by sample.

Everything but the `v90m_*` functions has hidden visibility: the shared
library exports nothing else, and the static library is linked into one
object whose internal symbols are made local (`ld -r`, `objcopy
--localize-hidden`), so a program's own symbols cannot clash with them.

## 4. The modem

Moved here from v90d's DESIGN.md (section 4 there) with the code.

### 4.1 Start-up
`modem/answer` runs the call: 200 ms silence, ANSam with phase reversals, V.8
(`v8rx`, `v8msg`, `v8ans`): receive CM, answer JM, wait CJ. The menu decides:

- V.90 offered by the caller (PCM analogue modem): Phase 2 (`p2d`), then
  Phases 3 and 4 and V.90 data.
- With V.92 allowed (`v92 = on`): the same Phase 2 says so (INFO0d bit 27);
  a caller that is V.92 too and selects PCM upstream in its INFO1a gets
  V.92 Phases 3 and 4 and data (`v92/v92_p34d`); every other caller goes
  the V.90 or V.34 way as before. V.8 is the same either way.
- otherwise V.22bis / V.22 (`v22tx`, `v22rx`, `v22bis`) or V.21 (`v21`).
- no V.8 at all: the V.22bis answer sequence after ANSam, V.21 after 8 s.
- Bell 103 (300 bit/s, the V.21 modulator and demodulator on that system's
  tones: we send 2225 / 2025 Hz and receive 1270 / 1070 Hz) for a caller
  without V.8 that sends a steady 1270 Hz while we wait for it. Such a
  modem takes the answer tone or the unscrambled ones of the V.22bis answer
  sequence (2250 Hz is in them) for its carrier and replies with its mark.
  The tone watch (`tonedet`) sits behind a receive band-pass
  (`bell103_bpf.h`) that keeps our own signal out, and wants 200 ms of the
  tone in the V.22bis wait, 60 ms in the V.21 wait (before that receiver's
  100 ms would take the tone for a V.21 carrier). On with
  `V90M_MOD_BELL103` (`ANS_MODE_BELL103`). No error control, as in V.21.
- Bell 212A (1200 bit/s; modulation and scrambler are V.22's, the answer
  tone is not). A caller without V.8 that has not answered 4 s of the
  V.22bis answer sequence gets 2225 Hz, the Bell answer tone, in place of
  the unscrambled ones for the next 4 s, while the V.22bis modem goes on
  listening: single-chip modems of that kind detect 2225 Hz within 10 Hz
  and take neither 2100 Hz nor 2250 Hz for it. What the caller then sends
  is what a V.22 caller sends, and the start-up goes on as V.22's. A call
  that began during the tone is reported as Bell 212A. The same tone serves
  a Bell 103 caller that is as strict. On with `V90M_MOD_BELL212A`
  (`ANS_MODE_BELL212A`) and V.22. Simulated only: no modem that is known to
  call as 212A and nothing else has been on the line.

### 4.2 V.90, digital side
Downstream (us to caller) is PCM at 8000 symbols/s; upstream is V.34 at 3200 or
3000 symbols/s.

Phase 2 (`info`, `dpsk`, `probe`, `p2d`): INFO sequences as 600 bit/s DPSK and
tones A/B from one shaped transmitter; phase reversals timed to the sample for
the round-trip delay; L1/L2 analysed with a 160-sample block DFT (the probing
signal repeats every 53.33 samples, so three periods are exactly one block and
every probing tone is orthogonal to the tones A/B and to the four gaps).
INFO1d offers only the upstream symbol rates our receiver implements.

Phases 3 and 4 (`p34d`): the caller trains us first (S, S-bar, PP, TRN, then the DIL
descriptor in Ja) while we are silent, then we send Sd, TRN1d, Jd and the DIL
it asked for while it is silent, then both run (Ri, TRN2d, MP against CPt, CP,
E), then B1 / B1d and data.

Receive chain for the upstream signal:

```
 8 kHz samples -> echo canceller (ec) -> S detector (v34train) ......... timing reference
                        |
                        v
   qamrx: interpolate to 3 samples/symbol (Kaiser sinc bank, linear between branches)
          -> AGC -> T/3 equaliser, 72 complex taps on the real pass-band signal
          -> carrier derotation -> point per symbol
                        |
                        v
   v34 decoder: 4-point decisions for Ja / CP / E, then Viterbi + inverse
   shell mapping + descrambler for data
```

- No Hilbert filter: the equaliser's complex taps on real input do the image
  rejection, which a separate filter could only do with a very long response
  (the signal runs from about 300 Hz to 3700 Hz).
- Carrier loop: decision-directed, second order. Timing loop: decision-directed
  MMSE gradient (equaliser output a third of a symbol early and late, each
  derotated for its own instant), second order, plus a slow pull that keeps
  the equaliser taps centred, because the equaliser can absorb any timing
  phase and would otherwise wander.
- The echo canceller learns the hybrid during TRN1d, when the far end is
  silent, at full NLMS step; afterwards it is frozen. It spans 12 ms, placed
  where the echo is: before training, the distance to the echo is found by
  correlating a stretch of received TRN1d with what was sent (a block-based
  line driver puts tens of milliseconds between the two). The stretch is
  taken as the change from sample to sample: a modem's line transformer
  can return our low frequencies as a tail of tens of milliseconds, larger
  in sum than the start of the echo, and that tail moved the peak (a
  U.S. Robotics 5686; PROGRESS.md, 2026-11-16).
- Adaptive filters round their updates. Truncation is a steady pull on every
  tap that takes a minute or an hour to show.
- TRN2d is sent for 1200 data frames (0.9 s) before MP, and Tone A is
  listened for through Phase 4 as in data mode: a U.S. Robotics 56K of 1998
  gave the start-up up with 0.3 s of TRN2d and asked for a retrain that
  went unheard (PROGRESS.md, 2026-11-16).
- The caller falls silent twice (during TRN1d / Jd and, with a DIL, again
  until it has seen enough of it) and comes back with S. Its clock has
  drifted and its carrier phase is anybody's guess, but the line is the
  same: the equaliser is kept, the carrier phase comes from the mean of S
  (its carrier line), the timing phase from the known alternation with a
  fast loop, the symbol count from the S / S-bar boundary (`sres`).
- The transmit side is integer logic only (`v90/`): scrambler, modulus
  encoder, constellation mapping from the caller's CP, spectral shaping, one
  G.711 octet per sample.

We dictate the caller's upstream transmitter through INFO1d and MP: symbol
rate, carrier, 16-state trellis code, no non-linear encoding, no precoding
(zero coefficients), minimum shaping. That keeps the receiver small without
giving up anything the line would not also cost.

**In data mode** (`p34d`, V.90 9.5 to 9.7). Three watches run on the echo
cancelled signal: loss of the upstream signal, the caller's S (the Phase 3
tone-pair detector), and Tone A (the share of each 10 ms block's power at
2400 Hz; the data signal leaves a few per cent there, Tone A with its guard
tone 80 %).

Two pieces are shared by the three engines that have a line echo to deal
with (`p34d`, `v92/v92_p34d`, `v34dx`), each in one place:
`modem/ecdly` keeps what was sent for the canceller's delayed reference and
measures how late the echo comes back (on plain samples for V.34, on the
change from sample to sample for V.90 and V.92, see above on the 5686), and
`modem/tonedet` is the watch for the retrain tones (Tone A at 2400 Hz, Tone
B at 1200 Hz). Each engine once had its own copy, and a correction had to
be made three times. What the engines do with the result stays with
them: the length of TRN2d, for one, differs on purpose (1200 data frames in
V.90 for the 1998 U.S. Robotics, 400 in V.92, which that modem does not
speak).

- S starts a rate renegotiation: `sres` follows S to its S-bar with the
  carrier already locked (`sres_resume`), the 4-point sequence receiver takes
  the CP, the transmitter sends Rd (384 T from a data frame boundary), Rd-bar,
  TRN2d, MP, MP', Ed, B1d. The V.90 transmitter never loses data frame
  alignment, and queued user bits are sent before Rd begins. With the
  request for silence (CPs): Ed, silence, and Rt, Rt-bar, MP once a CP
  without the request arrives.
- drn = 0 in a CP or in our MP is the cleardown.
- Tone A, or a renegotiation stalled for 6 s, is a retrain: p34d stops in
  `P34_RETRAIN`, `answer` sends 70 ms of silence, runs Phase 2 again from
  Tone B (`p2d_init_retrain`) and then a fresh p34d. `answer.retraining`
  tells the layer above; `dce` holds V.42 and PPP, timers included, until
  data mode is back (about 4.3 s in simulation).
- The upstream rate is ours to set (MP). When the receiver's error stays
  above what the rate needs for a second, we renegotiate with a lower
  ceiling; when nothing lower is left, or nothing is received, we retrain.
  `answer` lets a call go at the fourth retrain in a row.
- `dce` ends every orderly call with a cleardown after the LAPM release, so
  the caller's modem reports the end at once instead of trying to retrain
  into silence.

### 4.2a V.92, digital side
Downstream is V.90's. Upstream the caller sends PCM-coded symbols at 8000/s
through its own precoder, at 24000 to 48000 bit/s; we measure the channel
from its training signals, design its constellations and filters (CPd) and
decode. `v92/README.md` has the detail and every reading of the text.
- `answer` holds `v92d_t` in a union with the V.32bis modem; a call is never
  both.
- A retrain runs the V.92 Phase 2 again and the caller selects again. When a
  V.92 start-up never reached data mode, or the receiver gave up in data
  mode, the retrain's INFO1d withdraws PCM upstream and the call goes on as
  V.90; `dce` follows the modulation and the rates.
- Two V.92 modems that both named LAPM in V.8 skip the ODP / ADP of V.42
  (V.92 9.3.1); `dce` then gives the detection phase 5 s for the caller's
  first frame. The server offers V.92 only with V.42 on.
- A carrier watch ends the call when the caller is gone (V.92 has no
  carrier detector of its own in data mode).
- What a real modem (CX93001) needed is in `v92/README.md`, stage 4: a model
  of the slow echo tail behind the canceller, the CPd design spread over
  time (one step every 4 samples on the unit then, 2 now), z2(64) kept at zero, the
  first data frame after E2u judged before B1u is counted, no assumed
  first state in the trellis decoder.
- What a second one (U.S. Robotics 5686) needed, PROGRESS.md of 2026-11-16:
  it waits 3.8 s for SUVd and 3.5 s more for CPd, so the design takes one
  step every 2 samples and SUVd goes out 2.5 s into Phase 4 at the latest;
  the first CPd never carries the acknowledge bit (one that did was
  answered with silence); in data mode the receiver follows the baseline
  under the caller's symbols with each decision's error, because what is
  left of our own low frequencies wanders there.

### 4.2c V.92, analogue (calling) side
`modem/v90a` with `v92` on (`ORIG_MODE_V92`, `V90M_MOD_V92` calling): Phase
2 says V.92 in INFO0a and selects PCM upstream in INFO1a (Table 18: all four
filter sections, the longest filters, no MD) when INFO0d and INFO1d allow
it; otherwise the call is V.90. Downstream is V.90's receiver (`v90arx`,
DIL, CP) with V.92's Jd / Jp / Jp' and SUVd / CPd / Ed (`modem/v92aseq`,
word by word: CPd may be 20000 bits). Upstream: `v92/v92_enc` with what
CPd dictates, at LU = the configured level.
- The symbol clock is the network's (6.2/V.92). The transmitter
  (`modem/pcmtx`) runs the downstream receiver's interpolator accumulator
  with its increments, so it follows the receiver's timing loop sample for
  sample, and carries the symbols on to our D/A clock through a 64-tap
  bank that is also a low-pass at 3.7 kHz: the digital modem measures the
  channel once and does not follow it, so what reaches its codec must be
  one fixed response of our symbols however the two clocks stand, and near
  4 kHz no interpolator of that length is right.
- The clock is known before the first symbol: from the digital modem's L2
  in Phase 2 (each tone turns from block to block by its frequency times
  the offset; within 0.1 ppm on the test lines), and Sd's own estimate
  (off by up to 1.6 ppm) only overrules it beyond 5 ppm. A clock found at
  Sd and corrected afterwards moved our symbols against the network while
  the digital modem trained on them (failed starts at 100 ppm, a bit error
  every few seconds elsewhere).
- The echo canceller learns on the first TRN1u (the digital modem is
  silent), sent through the receiver's full band bank: the hybrid's echo is
  strongest at the band edge, where the low-pass has little left to learn
  on. Our own Ru has Sd's period, so Sd is not looked for until Ja.
- Rate renegotiation begun by the digital modem is followed (Ru on our data
  frame boundary after its Rd-bar); cleardown either way (CPu / CPd with
  drn = 0). No retrain, no renegotiation of our own, no quick connect, no
  modem-on-hold. Tried in simulation and against this library's own
  answerer only.

### 4.2d Bell 103 and Bell 212A, calling side
V.8 cannot offer them (`ORIG_MODE_BELL103` / `ORIG_MODE_BELL212A`,
`V90M_MOD_BELL103` / `V90M_MOD_BELL212A` calling; off by default), so they
are what a caller does when no V.8 comes about. The Bell answer tone is
2225 Hz within some 10 Hz (`modem/belltone`: 40 ms blocks, in which 2250 Hz,
a V.22 answerer's unscrambled ones, turns once and sums to nothing; 2100 Hz
is far off). The order, `modem/orig.h` in full:
1. ANSam and a V.8 mode on offer: V.8, as ever (the answerer chooses).
2. 2225 Hz before any 2100 Hz tone is told (a pre-V.8 Bell answerer), or
   after ANS while the legacy answerer is awaited: Bell 212A if offered,
   else Bell 103.
3. Only Bell on offer: no CM after ANSam (and no CI); silent, listening:
   2225 Hz -> 212A (else 103); with 103 alone, 150 ms of a carrier in the
   Bell 103 answer channel that is not 2100 Hz (a V.22 answerer's
   unscrambled ones are one, as for the plain modems of the time) -> 103;
   with 212A as well, 6 s of it without 2225 Hz -> 103 (this library's
   answerer plays 2225 Hz after 4 s of its unscrambled ones when 212A is
   on).
4. A 212A start answered by no 1200 bit/s within 3 s -> 103, if offered.

Bell 212A is the V.22 caller at 1200 bit/s, its scrambled ones from the
moment the tone is told (there are no unscrambled ones to time V.22's 456 ms
from), data 765 ms after the answerer's scrambled ones; no 2400, no
retrain. Bell 103 is the V.21 path on 1270 / 1070 Hz out and 2225 / 2025
Hz in, behind `bell103h_bpf.h` (`tools/gen_v21_bpf.py bell103h`: our own
tones 57 dB down). The Bell 212A handshake document was not to hand: the
timing follows V.22 and what this library's answerer (and the 5686 that
called it) accepts. Tried in simulation and against this library's own
answerer only; a V.21-only answerer with Bell 103 on is not reached (its
1650 Hz mark is 57 dB down in our Bell receiver).

### 4.2b Not done, maybe later: K56flex and x2
The two 56k schemes from before V.90 are not spoken; a modem that has
nothing else gets V.34. Neither has a published specification.
- K56flex (Rockwell / Lucent) opens with V.8bis, which is an ITU
  recommendation, and carries its capability in a manufacturer's block
  there and again in INFO1; after that it is a different downstream from
  V.90 (fixed training levels and constellation tables, frames of 8
  symbols with shell mapping, rates in steps of 2000 bit/s), a new engine
  and not a variant of `v90/`.
- x2 (U.S. Robotics) seems to be agreed inside the V.34 Phase 2; nothing
  more is known from published text.
- What there is beyond that are third parties' descriptions made from
  closed modem software. Using them would be an exception to "written from
  the recommendations" that has to be decided and stated, and it needs a
  modem that speaks nothing newer to test against. Neither is at hand
  (2026-11-16).

### 4.3 Fixed-point conventions
- Samples are 16-bit linear; 0 dBm0 is an RMS of 15770.
- Phases are 32-bit accumulators; `sintab` has 1024 entries, Q15.
- Equaliser input is normalised to an RMS of 2048; taps are Q20 in 32 bits;
  a 4-point training symbol is (+-4096, +-4096) at the equaliser output.
- Filter tables are generated by `tools/gen_*.py` into headers; nothing is
  computed with floating point at run time.

## 5. Testing

- Every block has a host test (`make check`); the same tests run cross-built
  under qemu (`make check-arm`).
- Peers written here (`sim/`): channel with loss, delay, echo, noise, clock and
  carrier offset; calling modems for V.8 / V.22bis, V.32bis, V.34, V.90 Phase 2
  and Phases 3/4 (analogue side), V.92; QAM modulator; `dschan` (the PCM
  downstream as an analogue modem's A/D has it) and `test/v90link.h` (a whole
  line between the library's analogue and digital modems). They may use knowledge a
  real modem does not have; they are a test reference, not code to ship.
- Independent peers (`make test-interop`, host only): spandsp for V.8,
  V.22bis, V.42 and V.42bis.
- Lessons that shaped the tests: a wrong-sign timing loop once passed every
  bit-error test because the equaliser hid it, and a delay line that moved
  under the equaliser update only showed for a third of the sub-sample
  alignments. So the tests check loop and equaliser state (tap centre, loop
  frequency, convergence across alignments), not just error counts.
- Real modems: recordings of real calls replayed with `tools/v90m_replay.c`;
  a fault found that way comes with its recording as a fixture.
