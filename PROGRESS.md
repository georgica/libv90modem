# libv90modem: progress

What was done and measured, newest last, with the test output. Earlier
history (the answering modem) is in v90d's PROGRESS.md.

## Phase 2, the calling side: what is shared and what is new (2026-11-17)

Before any code. The library starts at "the far end has answered"
(dialling, DTMF and call progress stay outside).

### The test peers in sim/: what they skip

They test the answerer and are a reference for behaviour, not code to ship.

- `v8call`: 2100 Hz energy only (ANSam and ANS look the same), floating
  point, no time-outs, no CI, Te fixed at 1 s.
- `callpeer`: the library's own `v22m` in the caller role, so not
  independent of the answerer; V.21 without carrier detection.
- `p2c` (V.34 Phase 2, caller): integer, built from the library's blocks;
  but "the test decides what INFO1c says", the rate is a rough SNR formula,
  it never starts a retrain, INFO0a's capabilities are ignored.
- `v34peer`: the library's own `v34dx` in the call role; at most 3 blind
  retrains.
- `v32call`: independent of `v32/` and the most complete (echo canceller,
  timing and carrier recovery, equaliser, Viterbi), but double precision,
  no pre-start (sends AA at once), canceller frozen after TRN, AGC set once,
  no rate renegotiation, rough retrain trigger, one 12 s time-out.
- `p2a` (V.90 Phase 2, analogue): integer; INFO1a fixed (down SR 4, rate 12),
  the probe result unused, retrain assumes the first INFO0d.
- `p34a`, `v92a`, `v92call` (V.90 / V.92 analogue): downstream they read the
  digital modem's octets as sent (`p34a.h:6-9`, `v92a.h:11-12`), with 12 / 24
  octets of look-ahead; no receiver at all (no timing recovery, equaliser or
  canceller); DIL, CP and CPu fixed patterns; upstream in double.
- `v90_dec`: exact octets in, -1 on any Ucode outside the constellation.
- Channels: `channel` and `v92chan` have matched clocks and a known echo;
  `v92chan` does not model the downstream at all; `v32line` has real
  separate clocks but exports the exact delays to the tests.

### Shared with the answering side

| stage | reused as is | reused with a role parameter |
|---|---|---|
| C1 V.8 | `v8_encode/decode/frame/selected`, `v8rx`, `async` | `v21` (high-channel band-pass table needed, `v21_bpf.h` is low channel only) |
| C2 V.21, V.22, V.22bis | `v22m` with `V22_CALLER` (carrier swap, no guard tone, USB1 / 456 ms / S1, retrain), `v22tx/rx`, `scramble`, `v14` | V.21 data mode (channels swapped) |
| C3 V.32bis | `v32_code`, `v32_viterbi`, `v32_ec`, `v32_tabs`; `v32tx` already sends AA and CC | `v32_init` (passes `answer=1` today), `v32rx` (descrambler takes the role; AC/CA detection new) |
| C4 V.34 | `v34dx` with `answer=0` (Phases 3/4, MP, data, renegotiation, cleardown: already used by `v34ans_init_call`), `v34mp`, `v34mod`, `qamrx`, `ec`, `v34train`, `v34/`, `probe`, `dpsk`, `info0a_*` | `v34ans` (retrain loop for the caller), `info1d_build` used as INFO1c |
| C5 V.90 analogue | `info0a/1a_build`, `probe`, `dpsk` (Phase 2); `v34mod`, `v34/` (V.34 upstream); `v90_seq` (Jd, DIL, CP, MP formats), `v90_scr`, `v90_enc` (map from CP); `v90tx` as a reference generator for training | `v90_dec` promoted from the tests: fixed integer already, needs error tolerance |
| C6 V.92 analogue | `v92_enc` (the analogue transmitter, bit-exact, fixed point), `v92_mod`, `v92_map`, `v92_sig`, `v92_seq` (Ja, CPu, SUVu; CPd, SUVd, Jp parsing), `v92_info` | |

### New

- C1: a fixed-point answer-tone detector telling ANSam (15 Hz AM) from ANS;
  `v8orig` (optional CI, Te, CM until two JM, CJ, 75 ms, time-outs); the
  choice from JM; fallbacks without V.8 (V.22bis on USB1, V.32bis AA).
- C2: originate sequencing after V.8 and without it; V.21 carrier detection.
- C3: the V.32bis caller's start-up (AA, AC/CA reversal, CC, round trip,
  S / TRN / R1, R2, R3 / E), retrain and rate renegotiation from the caller's
  side, cleardown.
- C4: `p2call`, the caller's Phase 2 (from `sim/p2c`, with a real INFO1c
  decision from the probe, modelled on `p2ans`), the caller's retrain and
  recovery (V.34 11.5, 11.2.2.1.5-6), the `v34orig` sequencer.
- C5 (the bulk of Phase 2): the PCM downstream receiver (timing locked to the
  network's 8 kHz clock, AGC, long feedforward and feedback equaliser, an
  echo canceller for our own upstream, slicing to Ucodes); DIL design and
  DIL analysis (per-slot levels, robbed bits, digital pads, the law); the
  choice of constellations and rate for CP (and CPt, LD, a/b); training
  detection on noisy decisions; the V.34 upstream sequencer for V.90's
  Ja / CPt / CP (not `v34dx`'s MP); upstream timing from the receive clock.
- C6: the V.92 upstream sequencer (Ru, TRN1u, Ja, Su with the Jp slip,
  TRN1u, CPt, E1u, TRN2u, SUVu, CPu, E2u, B1u); a fixed-point fractional
  delay transmit filter for the Jp slip; CPu design (the analogue side's).
- Tests: a downstream channel model for C5 / C6 (codec reconstruction
  filter, loop, clock offset, robbed bits, pads), which no peer has today;
  calling-side versions of the spandsp interop tests (V.8, V.22bis).
- API: `V90M_ROLE_ORIGINATE` accepted stage by stage (the modulations not
  yet written refused with `V90M_ENOTSUP`); end reasons for the caller (no
  answer tone, no JM, no common mode); the V.42 endpoint's role from the
  modem's; rate ceilings defined as transmit / receive for both roles.

## C1: V.8 on the calling side (2026-11-17)

- `modem/anstone`: the calling modem's answer tone detector, fixed point.
  2100 Hz brought to baseband in blocks of 5 ms; a block is tone when its
  2100 Hz component holds half of its power and is above -43 dBm0 (three
  quarters of it, for ANSam's troughs); the 15 Hz part of the envelope over
  windows of 200 ms (three periods): above 0.12 of the mean ANSam (V.8 7.2:
  0.2), below 0.06 ANS (V.25); two windows that agree decide. A block where
  the phase reversed is bridged with the one before it.
- `modem/v8orig`: V.8 8.1 for the call DCE: optional CI (ON 3 sequences,
  OFF 1 s; the tone is listened for in the OFF periods), ANS -> no CM (7.2),
  ANSam -> Te (1 s by default, 0.5 s at least) -> CM on V.21(L) until two
  identical JM on V.21(H) -> CJ at the next octet boundary -> 75 ms.
  Results: OK, ANS, no tone (10 s), no JM (5 s of CM), no common mode.
- Each test was seen failing first against a stub (anstone: 35 of 45
  checks failed; v8orig: 40 of 53).

Test output (`test/test_anstone.c`, `test/test_v8orig.c`, `test/interop_v8orig_spandsp.c`):

```
ANSam, the library's own transmitter (modem/ansam)
  peak 5602, reversals 0: ANSam at 0.400 s
  peak 5602, reversals 1: ANSam at 0.400 s
  peak  223, reversals 0: ANSam at 0.400 s
  peak  223, reversals 1: ANSam at 0.400 s
ANSam and ANS, written here from V.8 7.2 and V.25
  ANSam -12 dBm0, reversals                                  ANSam at 0.400 s
  ANSam -12 dBm0, no reversals                               ANSam at 0.400 s
  ANSam -42 dBm0                                             ANSam at 0.400 s
  ANSam 0 dBm0                                               ANSam at 0.400 s
  ANSam 10 Hz high (a carrier shift on the line)             ANSam at 0.400 s
  ANSam 10 Hz low                                            ANSam at 0.400 s
  ANSam -30 dBm0 in noise 12 dB below                        ANSam at 0.400 s
  ANSam at the narrowest envelope V.8 allows (0.81..1.19)    ANSam at 0.400 s
  ANSam starting off the block grid                          ANSam at 0.398 s
  ANS -12 dBm0, reversals (V.25)                             ANS at 0.400 s
  ANS -12 dBm0, no reversals                                 ANS at 0.400 s
  ANS -40 dBm0                                               ANS at 0.400 s
  ANS -30 dBm0 in noise 12 dB below                          ANS at 0.400 s
  ANS with a level step and a phase jump (V.8 7.2 NOTE 1)    ANS at 0.800 s
  ANSam with the same transient                              ANSam at 0.400 s
ANS is never taken for ANSam, wherever its reversals fall
  24 starting offsets: 0 not ANS
not an answer tone
  ANSam at -50 dBm0, below the floor                         nothing at -1.000 s
  1800 Hz with the same modulation                           nothing at -1.000 s
  2250 Hz (a V.22bis answerer's unscrambled ones)            nothing at -1.000 s
  white noise at -20 dBm0                                    nothing at -1.000 s
  dial tone 350 + 440 Hz: nothing; 500 + 1300 + some 2100 Hz: nothing
anstone: 45 checks, 0 failures
V.8 calling side against the library's answering side
  clean line                                   result 1, tone ANSam at 0.910 s, CM from 1.910 s, CJ at 3.177 s, done 3.277 s; selected 3; answerer: result 1, selected 3
  10 dB loss, echoes, noise, 2 Hz, 100 ppm     result 1, tone ANSam at 0.930 s, CM from 1.930 s, CJ at 3.230 s, done 3.330 s; selected 3; answerer: result 1, selected 3
  the same with CI, the tone 2.5 s late        result 1, tone ANSam at 3.130 s, CM from 4.130 s, CJ at 5.430 s, done 5.530 s; selected 3; answerer: result 1, selected 3
PCM: a V.90 / V.92 analogue caller to a digital answerer
  result 1, selected 100 (V.90 / V.92 is 100), answerer selected 100
nothing in common: JM with no modulation mode (8.1.2)
  result 5, answerer result 1
a pre-V.8 answerer: ANS (V.25), so no CM (V.8 7.2)
  result 2, tone ANS at 0.730 s, our transmit before and at it 0
ANSam but never a JM
  result 4 after 6.730 s; CM from 1.730 s
no answer tone at all
  result 3 after 10.000 s
v8orig: 53 checks, 0 failures
  V.34 V.32 V.22 V.21 both ways: result 1, selected 1, JM modes 0x101a; spandsp: ok 1, our CM modes 0xa06, protocol 1
  V.34 V.32 V.22 V.21 both ways, with CI: result 1, selected 1, JM modes 0x101a; spandsp: ok 1, our CM modes 0xa06, protocol 1
  V.22bis answerer, noise and echo: result 1, selected 4, JM modes 0x0010; spandsp ok 1
  V.90 digital answerer: result 1, selected 100, JM pcm 1 0x02; spandsp ok 1, our pcm 1
  ANS answerer: result 2, tone 1, CM at 0
interop_v8orig_spandsp: 26 checks, 0 failures
```

The answering side of spandsp repeats in JM the modes of the CM it received;
its application is to keep only what it has itself when CM is reported
(V8_STATUS_V8_OFFERED). `test/peer_spandsp.c` does that now; before, a
V.22bis-only spandsp answered our V.34 offer with V.34.

Not tried yet: a real modem answering (that needs a call to go on after V.8:
C2).
