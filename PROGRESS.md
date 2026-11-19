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

## C2: V.21, V.22 and V.22bis on the calling side (2026-11-17)

- `modem/orig`: the calling modem's sequencer, the mirror of `modem/answer`:
  V.8 (`v8orig`) -> JM item 4: V.22bis / V.22 (`v22m` as caller, as it
  was), item 12: V.21 (channel 1 out, channel 2 in through a new band-pass,
  `modem/v21h_bpf.h`: 980 Hz 56 dB down). After ANS (no V.8): the V.22bis
  caller waits for the answerer's unscrambled ones; a steady channel 2 mark
  for 300 ms instead is a V.21 answerer. Ends: no answer tone, no JM, no
  common mode, start-up failed, carrier lost.
- The V.22bis caller's carrier flag rises on a V.21 channel 2 mark; what
  tells V.22bis is its caller leaving V22S_WAIT (155 ms of unscrambled
  ones). Found with the V.21 test.
- The public API takes `V90M_ROLE_ORIGINATE` with V.21 and V.22 / V.22bis;
  any other modulation gives V90M_ENOTSUP. New ends: no answer tone, no
  JM, no common mode; `cfg.send_ci`. The answering path is unchanged:
  `make bench-v90` gives 8e3136e9 six ways.
- Seen failing first: test_orig 39 of 40 checks against a stub.

```
V.8 then V.22bis, against the library's answerer
  clean line: data at 4.74 s, 2400 bit/s, modulation 3, V.8 1
    2400 bit/s: caller got 63 of 63 octets, answerer got 61 of 61
    after a retrain started by the answerer: ours 1, theirs 1; moving 1
    after the retrain: caller got 63 of 63 octets, answerer got 61 of 61
    the answerer silent: phase 4, end 5
  10 dB loss, echoes, noise, 1 Hz, 50 ppm: data at 4.64 s, 2400 bit/s
    2400 bit/s: caller got 63 of 63 octets, answerer got 61 of 61
V.8 then V.21
  data at 3.03 s, 300 bit/s, modulation 1
    300 bit/s: caller got 63 of 63 octets, answerer got 61 of 61
pre-V.8 answerers: ANS, then
  V.22bis: data at 5.11 s, 2400 bit/s, modulation 3, V.8 0
    2400 bit/s: caller got 63 of 63 octets, answerer got 61 of 61
  V.22 (1200 bit/s only): data at 5.56 s, 1200 bit/s, modulation 2
    1200 bit/s: caller got 63 of 63 octets, answerer got 61 of 61
  V.21: data at 3.74 s, modulation 1
    caller got 63 of 63 octets, answerer 61 of 61
no call
  no answer tone: phase 4, end 1
  nothing in common: phase 4, end 3
orig: 40 checks, 0 failures
  spandsp at 2400 bit/s, noise -50 dBFS, echo 0.20: up at 5.11 s, ours 2400 bit/s (modulation 3, V.8 0), spandsp 2400; errors spandsp->us 0 of 8000, us->spandsp 0 of 8000
  spandsp at 1200 bit/s, noise -50 dBFS, echo 0.20: up at 5.45 s, ours 1200 bit/s (modulation 2, V.8 0), spandsp 1200; errors spandsp->us 0 of 7780, us->spandsp 0 of 7784
  spandsp at 2400 bit/s, noise -44 dBFS, echo 0.35: up at 5.11 s, ours 2400 bit/s (modulation 3, V.8 0), spandsp 2400; errors spandsp->us 0 of 8000, us->spandsp 0 of 8000
  finding, not checked:
  spandsp at 2400 bit/s, noise -38 dBFS, echo 0.35: up at 5.53 s, ours 1200 bit/s (modulation 2, V.8 0), spandsp 1200; errors spandsp->us 0 of 7780, us->spandsp 3122 of 6303
interop_orig_v22bis_spandsp: 21 checks, 0 failures
```

Finding, not explained: with noise 20 dB below the signal (-38 dBFS),
spandsp's V.22bis answerer does not take our S1, falls back to 1200 bit/s and
then decodes about half of our bits wrong, while we decode it without error.
In the same channel the library's own V.22bis answerer trains on our caller at
2400 bit/s down to -32 dBFS (S1 SNR 41 dB at -38 dBFS), and spandsp's caller
trains with our answerer at -38 dBFS. Down to -44 dBFS spandsp's answerer and
our caller agree at 2400 bit/s without an error. Not settled whether it is
spandsp's answering receiver or something in our caller that only spandsp
minds; a second spandsp instance as the caller would tell.

Not tried yet: a real modem answering. v90d has a bench mode for it now
(`v90d -o`: a modem told ATA on the port is called at once).

## C3: V.32bis on the calling side (2026-11-17)

- `v32/v32_call.c`: the calling modem of V.32bis 6.1, chosen with
  `v32_init_call` (`v32_init` stays the answerer, its code moved, not
  changed): AA; the answerer's AC and its reversal, found on the part of the
  received signal that changes sign every symbol (our own AA / CC echo has
  none of it); CC 64 T after that reversal at the line (63.3 T measured);
  the second reversal stops the round-trip count (NT within 0.3 T of the
  line's); the answerer's S / S-bar / TRN and R1; S for NT, S 256, S-bar,
  TRN (the canceller's training, extended while it gains); R2 (our rates
  within R1, cut to what the receiver's error on R1 allows; cleardown when
  none); R3 -> E, B1, data. Retrain from either end (7.1); the answering
  side of rate renegotiation (8.2, tested against a test-only initiator:
  the library's answerer has no clause 8); carrier loss also when the
  signal falls 12 dB below its level at the start of data.
- `modem/orig.c`: JM item 3 -> V.32bis; after ANS, Annex A: AA once ANS has
  been heard about 1 s; AC -> V.32bis, the V.22bis answerer's tone -> V.22bis,
  a V.21 mark -> V.21; the 3.1 s wait of A.2.1.2.
- The API takes V90M_MOD_V32BIS for the calling role.
- Written by a subagent from the Recommendation, with `sim/v32call.c` as a
  behaviour reference; reviewed here; the counts below rerun here.

Seen failing first against stubs: test_acdet 44 of 75, test_caller 30 of 45
(and 24 renegotiation checks), test_orig_v32 53 of 90, test_api 1. Now (my
run, fresh build): orig_v32 95 checks, test_acdet 75, test_caller 247, api
124 (98 before: the V.32bis calling block added), 0 failures; every other
line of `make check` identical to C2; bench-v90 8e3136e9 six times;
test_orig_v32 on qemu-arm byte-identical to the host.

Results: clean line 14400, error-free, SNR 43.2 dB; retrains from either end
error-free after about 5.9 s; the impaired line of the C2 tests (about 20 dB
SNR) connects at 9600 (14400 needs about 25 dB; with -60 dBm0 noise and no
carrier shift it connects at 14400); rate ceilings 9600 and 4800 held;
Annex A cases (V.32bis, short ANS, V.22bis, V.21, the 3.1 s wait, an
automode answerer) pass; renegotiation about 213 ms then error-free at 9600.

No independent V.32bis answerer exists to test against (spandsp has no
V.32bis): the caller is checked against the library's answerer only, which
was itself checked against the independent `sim/v32call`.

Found in the answerer, not changed (each would change what it sends):
- its TRN1 can run 8230 symbols, over the 8192 of 5.2.3 (the extension is
  checked every 256 symbols from a minimum that is no multiple of 256);
- with no echo on the line its TRN1 always runs to the maximum (about 3.4 s):
  "less than 1 dB gained" never holds when there is nothing to cancel;
- with a carrier frequency shift neither end's frozen canceller removes the
  shifted far echo (about 17 dB of echo reduction on the impaired line).

Shortcuts: after the 3.1 s wait the V.22bis caller keeps its 456 ms of
silence before S1; AA starts 600 ms after ANS was recognised; R2 is judged
from the receiver's error on R1; the canceller freezes after TRN, as in the
answerer.

## C4: V.34 on the calling side (2026-11-17)

- `modem/p2call`: V.34 Phase 2 for the call modem (11.2.1.1) with the
  recovery rules of 11.2.2.1 (it begins a retrain itself when Tone A does
  not come under its L2; when INFO1a does not come it answers the
  answerer's Tone A as a retrain, or begins one 2 s later), and a real INFO1c
  decision from the probe and INFO0a: a symbol rate only where both ends
  allow it; the carrier by the band SNR per carrier (p2ans's 36 dB floor);
  the pre-emphasis filter that best evens the measured tone levels (if it
  beats none by 0.2 dB); the maximum rate by p2ans's formula, capped by
  Table 8, by 12 without 1664-point support and by our ceiling; power
  reduction from the empty probe bins' distortion. INFO1c (Table 15) has a
  builder and parser of its own in `modem/info.c`.
- `modem/v34orig`: the mirror of `v34ans` (left untouched): p2call, then
  `v34dx` in the call role, data; retrains from either side (70 ms, Phase 2
  without INFO0; at most 4 in a row without data), rate renegotiation,
  cleardown both ways, carrier loss.
- `modem/orig.c`: JM item 1 -> V.34; separate receive rate; rate ceilings,
  echo delay and law in its configuration. The API takes V90M_MOD_V34 for
  the calling role; v90m_cleardown works for a V.34 caller.
- Written by a subagent from the Recommendation with `sim/p2c` and
  `sim/v34peer` as behaviour references; reviewed here; the counts below
  rerun here.

Seen failing first against stubs: p2call 156 of 247, orig_v34 22 of 27,
api 3 of 128. Now (my run, fresh build): p2call 247 checks, orig_v34 366,
api 162 (124 before: V.34 calling cases added; two old checks that expected
V90M_ENOTSUP for V.34 now expect a call), 0 failures; every other line of
`make check` identical to C3; bench-v90 8e3136e9 six times; on qemu-arm the
output of test_p2call, test_orig_v34 and test_api is byte-identical to the
host's.

| line (caller against the library's answerer) | we send | we receive |
|---|---|---|
| good (no codec, 20 ms each way, echoes, noise 50 dB down, +30 ppm) | 31200 | 28800 |
| realistic (mu-law, -100 ppm, +-1 Hz, mild loop) | 24000 | 24000 |
| noise 30 dB down | 21600 | 19200 |
| noise 22 dB down | 12000 | 12000 |
| long loop (our pre-emphasis request helps what we receive) | 12000 | 24000 |
| ceilings 14400 out / 9600 in | 14400 | 9600 |

Retrain from either side: data again after 5.9 s, error-free. Rate
renegotiation from each side (16800, then 14400), error-free. Cleardown about
260 ms from either side. The answerer gone: "lost" after 550 ms in data mode,
"no carrier" after 16 s (one retrain) in training. The round trip measured
by the caller equals the answerer's on the good line.

No independent V.34 answerer exists to test against (spandsp has no V.34).

Findings, not explained or not changed:
- No 33600 anywhere: 31200 is the engine's best in every test; what the
  library's answerer sends tops out at 28800 because its V.34 always sends
  G.711 codec values, even on a linear line.
- With 3 % far echo plus noise and clock offset the answerer receives only
  16800 from our caller; `sim/v34peer` (the answerer's own test caller) gets
  the same, so it is the answerer's receiver.
- INFOMARKSa is not detected; a missing INFO1a falls back to a retrain.
- The INFO1c decision runs in one sample tick: about 1.5 M ARM instructions
  (counted under qemu), once per Phase 2. On the box that is one late block.
- The API counts the caller's V.34 renegotiations; it still does not count
  the answering side's own (as in Phase 1).

## C5-1: the PCM downstream receiver of the analogue modem (2026-11-17)

- `sim/dschan`: the downstream as the analogue modem's A/D sees it: octets
  pulled from the digital modem on the network clock -> optional pad
  (decode, scale, encode) -> optional robbed bit (LSB forced to 1 in one
  sample of 6, at a phase of the test's choosing) -> G.711 levels -> one
  pulse built in the frequency domain (two 8th order Chebyshev codec
  filters at 3550 Hz, 150 Hz transformer, 50 Hz DAA coupling, 0..6 km of
  0.4 mm cable, normalised at 1 kHz) -> noise in dBm0 -> sampled at
  8000 (1 + ppm) Hz from any phase -> plus the hybrid echo of our own
  transmit samples -> 16-bit A/D. Pulled per A/D sample, like the other
  channel models.
- `modem/v90arx`: the receiver, integer only. Echo canceller (`ec_t`),
  interpolator on to the network clock (`v34_interp`), AGC from Sd, DFE
  48 forward / 128 feedback taps, slicer per data frame interval with
  caller-settable levels. Clock offset measured over Sd (its 1333 Hz line
  turns by -2 pi offset per period); channel estimate (128 taps, NLMS) on
  TRN1d; the MMSE DFE computed from it by fixed-point Cholesky (LMS alone
  needed about 3 s to reach -45 dB); then decision directed: the estimate
  goes on, the DFE is computed again every 2048 / 1024 / 512 symbols
  (offset up to 30 / 61 / above ppm), timing loop on the estimate's phase,
  adaptation gated on 4 sigma and on slots 9 dB worse than the best.
- `test/test_v90arx.c` (488 checks), `tools/rec_v90a.c` + `rig/bench_v90a.c`
  (`make bench-v90a`, `make cost-v90a`).

Seen failing first against a stub: 287 checks, 80 failures (the channel's
own checks passed). Now 488 checks, 0 failures, host and qemu-arm output
byte-identical. Every other `make check` line identical; bench-v90
8e3136e9 six times.

Results (2 s of data per cell, 48000 bit/s = 64-point slots, Sr = 0,
points spaced as widely as -12 dBm0 allows):

```
data: Ucode decision error rate (against what the digital modem sent), 16000 symbols each
  channel \ bit/s                   28000    32000    36000    40000    44000    48000    52000    56000
    dmin, mu-law (units)             1280      896      544      384      256      176      128       64
  2 km, -75 dBm0,    0 ppm              0        0        0        0        0        0        0        0
  0 km, -75 dBm0,  +80 ppm              0        0        0        0        0        0  2.6e-01  8.7e-01
  6 km, -75 dBm0,  -80 ppm              0        0        0        0        0        0        0  7.8e-01
  2 km, -65 dBm0,  +30 ppm              0        0        0        0        0        0  9.1e-01  9.5e-01
  4 km, -60 dBm0,  -30 ppm              0        0        0        0  1.6e-03  7.9e-01  9.6e-01  9.6e-01
  2 km, -75 dBm0,  +30 ppm              0        0        0        0        0        0        0  9.4e-01
    as row 6, 3 dB pad                  0        0  2.5e-04  8.9e-01  9.3e-01  9.5e-01  9.5e-01  9.6e-01
    ... levels given (DIL)              0        0        0        0        0  3.2e-02  1.6e-01  9.4e-01
    as row 6, 6 dB pad                  0        0        0        0  8.8e-01  9.3e-01  9.5e-01  9.7e-01
    ... levels given (DIL)              0        0        0        0        0        0        0  9.3e-01
    as row 6, RBS                       0        0        0  7.2e-01  7.9e-01  9.2e-01  5.4e-02  2.9e-01
    ... levels given (DIL)              0        0        0        0  2.3e-02  3.1e-02  5.6e-02  9.5e-01
    as row 6, A-law                     0        0        0        0        0        0  2.6e-03  8.5e-01
  mse (dB re UINFO^2) and rms error (units) at 28000 / 40000 bit/s; rms of |y| - |sent| at 36000
  2 km, -75 dBm0,    0 ppm        -59.7 /  -60.5      6.88 /    6.29       5.88
  0 km, -75 dBm0,  +80 ppm        -52.3 /  -50.8     16.10 /   19.25      14.59
  6 km, -75 dBm0,  -80 ppm        -55.8 /  -55.2     10.73 /   11.52      10.40
  2 km, -65 dBm0,  +30 ppm        -51.1 /  -52.0     18.46 /   16.62      17.54
  4 km, -60 dBm0,  -30 ppm        -46.8 /  -47.1     30.27 /   29.44      29.82
  2 km, -75 dBm0,  +30 ppm        -55.7 /  -56.4     10.85 /   10.01      11.18
    as row 6, 3 dB pad            -40.7 /  -29.0     61.13 /  236.66      86.80
    ... levels given (DIL)        -54.6 /  -54.5     12.36 /   12.56     109.84
    as row 6, 6 dB pad            -44.6 /  -42.8     39.02 /   47.91      51.07
    ... levels given (DIL)        -53.4 /  -53.5     14.29 /   14.10      39.92
    as row 6, RBS                 -39.7 /  -25.9     68.92 /  336.12      60.06
    ... levels given (DIL)        -55.4 /  -55.1     11.25 /   11.73      46.67
    as row 6, A-law               -54.1 /  -56.6     13.32 /   10.02      10.45
    as row 6, RBS: robbed slot 0; per slot at 36000: errors, mean and rms of |y| - |sent| (units)
    [0] 0 -59.6 115.7 [1] 0 +1.6 50.0 [2] 0 +1.4 52.4 [3] 0 +1.1 24.9 [4] 0 +1.7 42.0 [5] 0 +1.0 25.2
    ... levels given (DIL): robbed slot 0; per slot at 36000: errors, mean and rms of |y| - |sent| (units)
    [0] 0 -60.7 111.5 [1] 0 -0.5 11.2 [2] 0 -0.3 11.2 [3] 0 -0.3 11.0 [4] 0 -0.4 11.3 [5] 0 -0.8 11.2
```

Clock offset found within 0.05 ppm from -100 to +100 ppm and any phase;
MSE after TRN1d (4800 T) -60 dB at 0 ppm, -47..-54 dB at 100 ppm; the
canceller (trained 0.5 s on wideband noise, then frozen) gives 69..80 dB
ERLE with our V.34 upstream 8 and 18 dB above the downstream.

Findings:
- The A/D at 8 kHz, not locked to the network: what the line passes above
  about 3.8 kHz aliases with a phase that turns with the offset, a slowly
  changing channel at the band edge. -61 dB at 0 ppm, about -48 dB at 10 to
  100 ppm with LMS tracking alone (an ideal 512-tap interpolator does no
  better); recomputing the DFE often brings 100 ppm back to -51..-58 dB.
  A codec clock locked to the recovered timing would remove it.
- NLMS on a V.34 upstream trains the canceller slowly (34 dB in 0.75 s,
  48 dB in 30 s): the band edges of the reference are weak. Wideband
  training (Phase 2 probing) is assumed.
- TRN1d's signs start far from white (lag-1 correlation 0.55 over the first
  300 symbols): the channel estimate needs about 1000 symbols.
- An odd UINFO is changed by a robbed bit in TRN1d and Sd; the tests use
  UINFO 90 (even codewords already have LSB 1).
- Without the levels, a robbed bit or pad propagates through the feedback
  into the other slots; with the levels given (as the DIL will find them)
  the other slots are clean and the limit is points moved to within dmin/2
  of a neighbour (a CP for the slot avoids those).

Cost (`make cost-v90a`, qemu instruction counts, 2 km, 48000 bit/s, our
V.34 echo 8 dB over the downstream): data mode 68.3 M ARM instructions per
second of line at 0 ppm, 78.5 M at 100 ppm (the difference, 11.7 more
computations of the DFE a second, puts one at about 0.87 M); Sd and TRN1d
(0.7 s) 38.6 M / 42.8 M. The digital modem's V.90 data mode is 39.5 M
(V34-REPORT.md), 32 % of the box's CPU, so this receiver alone would take
about 55 to 64 %, before the V.34 upstream transmitter and the rest of a
call. It fits one call; it does not fit beside other work. Not yet tried:
updating the taps every other symbol, a 16x16 multiply path, computing the
DFE less often with an A/D clock locked to the network.

C5-1 verified independently (fresh build, not the agent's):
- make check: 92 count lines, all 0 failures; against C4's run the only
  difference is the new line "v90arx: 488 checks, 0 failures".
- make bench-v90: transmit hash 8e3136e9, six of six.
- make bench-v90a: host and qemu-arm agree, hash a3bf6729, -56.4 dB, +100.00 ppm.
- modem/v90arx: no float, no allocation, the only statics are const tables.
- sizeof(v90arx_t) 12744 octets; orig_t is 68280 today, so it still fits
  V90M_MEM_SIZE (102400) with the receiver inside it.
- Open: cost (68-79 M ARM instructions per second of line, about twice the
  digital modem's data mode); the equaliser solve (0.87 M) runs within one
  tick; a band-edge loss at nonzero ppm.

## C5-2: the V.90 analogue modem, V.8 to data mode (2026-11-17)

- `modem/p2call`: a V.90 mode (`p2call_init_v90`, 9.2.2). The analogue
  modem's Phase 2 is V.34's *answer* procedure with V.90's INFO sequences
  (Tone A out, INFO0a, our L1 / L2 first, INFO1d in, INFO1a of Table 10
  out), so it is a second set of states inside p2call sharing its helpers
  (probe analysis, tone under probe, INFO0 builder); the V.34 call path is
  untouched (its only change: `default:` in the switch). INFO1a: UINFO 90
  (even: a robbed bit leaves its codeword alone), MD 22 x 35 ms, the
  upstream symbol rate INFO1d projects most for (3000 / 3200), its carrier,
  filter and power reduction for our transmitter. Recovery 9.2.2.2.1 to .3;
  no INFO1d (9.2.2.2.4) ends Phase 2 instead of retraining.
- `modem/v90dil`: the DIL we ask for (120 segments, Ucodes 0..119, 18
  symbols each: UINFO + + + then 15 training symbols, SP + + + + - - + - +
  + - - - + - + - +), its walk, the per-interval / per-Ucode accumulation,
  the analysis and the CPt / CP design (header comment has the details).
  The analysis snaps every mean to g x a Table 1 level (a pad or robbed bit
  still leaves the far codec on some codeword): g from the codeword UINFO
  arrives as (searched over a 15 dB range, least squares refined), only
  where the codec's step is >= 32 units. Robbed intervals: those departing
  from the median of the six at >= 16 of Ucodes 64..127. CP: Sr = 0, ld = 0,
  one constellation per interval, points from the bottom at d = z sigma,
  moduli where they cost least power, z raised to the power limit (Table 15
  less 0.1 dB, 8.5.2's weighted power rounded up), the highest K whose
  largest z reaches 12 (CPt: 20, K <= 16).
- `modem/v90adec`: the data mode decoder (5.4 backwards: labels, R0,
  signs for every Sr without the shaper's filter, see its header).
- `modem/v90a`: the sequencer (Phases 3, 4, data; renegotiation begun by
  the digital modem; cleardown both ways; time-outs of 9.3.2 / 9.4.2).
  Upstream: the V.34 blocks of C4 (`v34mod`, `v34_tmod`, `v34_tx`).
  Downstream: `v90arx` with the DIL as known symbols (their levels as
  measured so far), Ri recognised at the 4th symbol of a DIL segment.
- `modem/orig`: V.8 offers PCM (analogue) with `ORIG_MODE_V90`, JM's
  V.90 selection starts `v90a` (phase `ORIG_V90`, modulation
  `ORIG_MOD_V90` = `ANS_MOD_V90`), `bitrate` = our upstream, `rx_bitrate`
  = the downstream, `orig_snr_db10` (downstream), `orig_rtd` (RTDEa),
  `orig_cleardown`; `orig_retrain` gives -1. `v90a_t` is a member of its
  own: `orig_t` is 100744 octets (68280 before), within `V90M_MEM_SIZE`
  (102400; the API's compile-time check). A union with V.32bis / V.34 would
  have renamed `orig_t.v32` / `.v34`, which existing tests read.
- API: the calling role takes `V90M_MOD_V90` (V.92 still `V90M_ENOTSUP`).
- `v90arx` (C5-1): an optional known-level callback; `v90arx_hold` zeroes
  the timing loop's phase slew (it went on being applied each output while
  the loop was held: the DIL's output ran away). Neither changes C5-1's
  results (test_v90arx 488 checks as before, bench-v90a a3bf6729).
- Tests: test_v90arx_robust, test_v90adec, test_v90dil, test_p2v90,
  test_v90a (the sequencer against p2d + p34d, no V.8; time-outs),
  test_orig_v90 (end to end), test_api (V.90 calling case); the harness
  `test/v90link.h`. `make bench-v90c`, `make cost-v90c`
  (tools/rec_v90c.c, rig/bench_v90c.c).

v90arx against hostile line content (C5-1 code; the far end is not
trusted): (1) the estimator divided by the decided levels' power, which a
far end sending Ucode 0 alone decays to 0: floored at (L / 8)^2 (and L
itself at 1); (2) the clock measured on Sd: a tone 20 Hz off 1333 Hz makes
the tangent huge, the product overflowed int64 and the step came out wild
or wrapped: the tangent is clamped to 1 and the step to 1000 ppm; (3) the
timing loop integrated the step without bound: clamped to 1000 ppm, the
slew to 500 ppm (step + slew > 0 always), and the interpolator makes at
most 3 outputs per input sample. The other divisors (Cholesky pivots,
bias, the DFT phase, AGC) were already bounded. Valid signals: test_v90arx
488 checks as before, bench-v90a a3bf6729 unchanged. The changed lines no
longer left-shift signed values (multiplied instead).

Choices (where V.90 leaves them open): the echo canceller learns on MD,
white noise at the transmit level for 770 ms while the digital modem is
silent (L1 / L2 is a comb of 25 lines and would leave the canceller wrong
between them); its ERLE at the end of MD is 62..73 dB. DIL as above, four
passes (>= 8 measurements per interval and Ucode, about 1.1 s); the
receiver is held through the first pass and learns on the measured levels
after. CP's margin: d >= 12 sigma (2 Q(6) = 2e-9 for Gaussian noise), sigma^2
taken as twice what TRN2d shows (data mode shows 2 to 4 dB more error than
TRN2d with CPt's sparser points: with the TRN2d figure alone the A-law line
ran at 48000 with about 7 bit errors a second). TRN 1024 T; SCR after
Ri-bar for 4000 TRN2d symbols while the noise is measured; S, S-bar and CP
for our cleardown; Rd recognised as two frames of each interval's largest
point + + + - - - (or the inverse), delivery running two frames late so
that Rd is never handed on as data.

Seen failing first (against stubs, or the committed code):
- test_v90arx_robust: part 1 died (SIGFPE, exit 136), part 2 11 of 18
  (clock -30658 ppm; at 1351 Hz the overflow wrapped it to +21592), part 3
  1 of 1 (followed a 1500 ppm clock to -1536 ppm). Now 21 checks, 0.
- test_v90adec 204 of 398; test_v90dil 69 of 210 (391 checks now: a biased
  measurement case added after the snapping); test_p2v90 47 of 69;
  test_v90a 38 of 49 (50 now); test_orig_v90 34 of 104 with orig.c as
  committed (it fell back to V.34); test_api 8 of 167 with the committed
  api/v90modem.c (its V.90 block skipped), 187 checks now.

`make check` (fresh build): against the baseline taken from HEAD before any
change, the only differences are the six new lines and the API's:

```
> v90arx_robust: 21 checks, 0 failures
> v90adec: 398 checks, 0 failures
> v90dil: 391 checks, 0 failures
> p2v90: 69 checks, 0 failures
> v90a: 50 checks, 0 failures
> orig_v90: 104 checks, 0 failures
< api: 162 checks, 0 failures
> api: 187 checks, 0 failures
```

bench-v90: transmit hash 8e3136e9, six of six. bench-v90a: a3bf6729, host
and qemu-arm. bench-v90c: 5a5e5ca7, host and qemu-arm (data mode at the
recorded sample).

End to end (test_orig_v90; the library's caller with V.8 against the
library's answerer; downstream through sim/dschan, hybrid echo -16 dB,
upstream 6 dB loss and -70 dBm0 noise through the network codec; rows
"Table 1 levels" take Table 1's levels instead of the DIL's, a test
setting):

```
  line                                   data ms   down     up SNR dB   rtd  bits down  fail    bits up  fail  robbed
  2 km,    0 ppm, -75 dBm0                 10225  52000  28800  56.4    46     519987     0     288000     0  000000
  0 km, +100 ppm, -75 dBm0                 10527  33333  28800  38.5    47     333300     0     288000     0  000000
  6 km, -100 ppm, -75 dBm0                 10524  46666  28800  51.3    46     466690     0     288000     0  000000
  4 km,  +50 ppm, -65 dBm0                 10526  40000  28800  45.9    47     399960     0     288000     0  000000
  2 km,  +30 ppm, 3 dB pad                 10226  44000  28800  51.5    47     439989     0     288000     0  000000
    ... Table 1 levels                         0      0      0   0.0     0          0     0          0     0  000000
  2 km,  -30 ppm, 6 dB pad                 10525  40000  28800  49.0    46     400020     0     288000     0  000000
    ... Table 1 levels                     10225  30666  28800  37.2    46     306682     0     288000     0  000000
  2 km,  +30 ppm, robbed bit               10526  46666  28800  49.6    47     466655     0     288000     0  000010
    ... Table 1 levels                     10226  30666  28800  37.1    47     306659     0     288000     0  000001
  2 km,  +30 ppm, 6 dB pad, robbed bit     10226  42666  28800  48.8    46     426624     0     288000     0  010000
  2 km,  -50 ppm, A-law                    10224  45333  28800  50.6    46     453356     0     288000     0  000000
ending a call
  ours: caller ended 4 after 1223 ms (end 7), answerer phase 3, cleared 1
  the answerer's: caller ended after 99 ms, end 6
```

"fail" counts failures of the self-synchronising check over 10 s (each
wrong bit fails it three times): none anywhere. Without the DIL's levels
the pads and the robbed bit cost 9..16 kbit/s (the noise TRN2d shows grows
with the wrong levels and CP follows it), and the 3 dB pad does not
connect at all; the robbed interval is found in every robbed row (with
Table 1 levels the analysis still finds it, but the design ignores it).
"data ms" counts from the start of the call (V.8 about 2 s, Phase 2 about
2 s, Phase 3 3.8 s, Phase 4 1.4 s). The sequencer test (test_v90a, no V.8,
2 km, +30 ppm, echo -10 dB): 45333 / 28800, 4 s without an error, the
time-outs at 1500 ms from Ja (no Sd), 4500 ms from its end (no Jd), 15 s
from INFO1a (no MP), loss in data mode after 593 ms; renegotiation by the
digital modem followed (one break of the data stream: 27 check failures,
none in the 2 s after), cleardown either way.

ARM: every new test's output under qemu-arm is byte-identical to the
host's (test_v90arx_robust, test_v90adec, test_v90dil, test_p2v90,
test_v90a, test_orig_v90), and test_api passes under qemu.
Cost (qemu instruction counts, bench_v90c replaying a recorded call into
`orig_t` alone; 2 km, our V.34 upstream at 28800, echo -16 dB): data mode
77.4 M ARM instructions per second of line at 0 ppm (52000 down), 79.8 M at
100 ppm (46666 down): the C5-1 receiver's 68..79 M plus the V.34 upstream,
which is small beside it. That is about 65 % of the box's CPU (V34-REPORT:
39.5 M is 32 %); one call fits, nothing beside it. The longest ticks (the
four slowest on the host at 100 ppm, each counted alone as the recording
cut after it less the recording cut before it):

| tick | what | ARM instructions |
|---|---|---|
| 8.825 s | end of the DIL: analysis (gain search, snapping) and CPt design | 9.03 M (11.92 M before Table 1 was kept in a local array) |
| 9.552 s | CP design | 0.87 M (1.21 M before) |
| 14.151 s | the receiver's equaliser computed again, data mode | 0.87 M |
| 7.046 s | the receiver's first equaliser, end of TRN1d | 0.84 M |

The DIL tick is 9 M instructions in one sample (about 70 ms of the box's
CPU): mostly the gain search's 64-bit divisions (41 candidates x 576
measurements), which ARMv5 does in software. To be spread over ticks (or
the divisions turned into one reciprocal per candidate) before the box.

Findings:
- V.8 (C1, unchanged): with the hybrid echo at -10 dB, our own CM sits next
  to JM, and v8orig demodulates channel 2 without a band-pass (the V.21
  data path has one, `v21h_bpf.h`): on a 6 km loop and behind a 6 dB pad,
  whether two identical JMs are decoded depends on how the round trip
  aligns the echo with JM's bits (4 or more samples of extra upstream delay
  flipped the 6 km line from pass to "no JM"). At -14 dB and below both
  pass; the end-to-end test runs at -16 dB. A follow-up for v8orig.
- The receiver's 16-bit input (v90arx's AGC puts Sd at 4096 RMS) overloads
  on runs of Ucodes 122..127 (Ucode 127 arrived 9 % low): the DIL leaves
  out 120..127. A data constellation at -12 dBm0 never wanted them here.
- The DIL's repetitive patterns bias its means by up to 0.8 % (the
  equaliser's residual interference with a constant neighbour), which in
  data mode was 2 to 6 times the noise (rms error 26..69 units with the
  raw means, 10 with Table 1 levels on a clean line): hence the snapping.
- Holding the receiver through the whole DIL (1.1 s) lost 0 km / 100 ppm
  lines (the band edge's alias turns): it learns again after one pass.
- 0 km at 100 ppm stays C5-1's weak case: 33333, SNR 38 dB.
- At 50 ppm the receiver's error cycles by about 5 dB over 5 s (the band
  edge again); CP's 3 dB allowance covers it on the lines tried.
- Once, with CP designed on TRN2d's noise alone, an A-law call that already
  had a trickle of bit errors collapsed 14 s into data mode (the clock
  estimate ran from -50 to -24 ppm); with the allowance it did not happen
  again on that line, and it was not explained.
- The test harness's own upstream FIFO (our clock to the network clock)
  underflowed when it started empty: a dropped sample in our second S made
  the digital modem miss it. It fills 8 samples before it is read now.

Shortcuts and deviations:
- No retrains (9.5): `v90a_retrain` refuses; a digital modem's retrain
  (silence, Tone B) is taken as the downstream lost and ends the call.
  Phase 2 recovery 9.2.2.2.4 (no INFO1d) ends the call. No INFOMARKSa.
- No rate renegotiation of our own (9.6.2.1), nor its request for silence
  (CPs); the digital modem's is followed with the CP of the start-up.
- 4-point CP / E / SCR only (a Jd asking for 16 points is not honoured).
- No spectral shaping asked for (Sr = 0, ld = 0); the decoder does all Sr.
- The DIL is fixed (not adapted to what Phase 2 saw); the CP rms ratio
  field is sent as 1.0; INFO0a's capabilities are V.34's of C4.
- Bit 38 of INFO0d (power at the codec or the terminals) is not looked at:
  8.5.2's power is always computed on Table 1 levels.
- Echo delay: a fixed reference delay (`echo_delay`), not measured.
- Tested only in simulation against this library's own answerer.

Follow-ups: retrains (Phase 2 again from Tone A / Tone B); a band-pass in
v8orig's JM receiver; spreading the receiver's DFE solve (one tick) and the
DIL analysis / CP design (one tick each) over several ticks; renegotiation
from our side; 16-point CP; shaping; a real modem.

C5-2 verified independently (fresh build under /home, not the agent's):
- make check: all 0 failures; against C4 the only differences are the new
  lines (orig_v90 104, p2v90 69, v90a 50, v90adec 398, v90arx 488,
  v90arx_robust 21, v90dil 391) and api 162 -> 187.
- bench-v90: transmit hash 8e3136e9, six of six. bench-v90a (receiver,
  after the robustness fixes): a3bf6729 as at C5-1.
- New modem code: no float, no allocation, no mutable statics.
- Open: worst single tick 9.03 M ARM instructions (end of the DIL, ~70 ms
  of the box's CPU) must be spread before a real call; data mode ~65 % of
  the box; no retrains; V.8 JM lost at -10 dB hybrid echo (C1 v8orig has no
  channel-2 band-pass).

## Sync with v90d 0.4.1 (2026-11-17)

v90d main's modem changes since the split (819d38e..4799c4c: one copy of
the echo delay measurement, `modem/ecdly`, and of the retrain tone watch,
`modem/tonedet`, out of `p34d`, `v92/v92_p34d` and `v34dx`; `test_ecdly`;
`test_p34`'s mid-call events timed from data mode), applied as they are:
every file it touches is now octet for octet what main has. Here as well:
both modules in `MODEM_SRC`, `test_ecdly` in `TESTS`; the echo delay is
read from `ecd.delay` by the API's status, the replay tool's trace
(`tools/trace.c`) and `test_orig_v34` (the calling code itself used none of
what changed: `v34orig` and `orig` fill `v34dx_cfg.echo_delay`, which
stays). `V90M_MEM_SIZE` 100 KiB -> 112 KiB: the history in `p34d` and
`v92d` is 4096 samples now (2048), and `answer_t` is 106496 octets on the
host (98288), 106208 on ARM; `orig_t` 100752.

Checked against `calling` (0367bee), same PC:

| | calling | this |
|---|---|---|
| `make check` | 98 programs, ALL HOST CHECKS PASS | the same 98 with the same counts, plus `ecdly: 1019 checks, 0 failures`; ALL HOST CHECKS PASS |
| `make check-arm` | 98 programs, ALL ARM CHECKS PASS | the same, plus ecdly; ALL ARM CHECKS PASS |
| `make bench-v90` | | transmit hash 8e3136e9 (internals and API; host, shared, qemu-arm) |

The output of the calling side's tests (anstone, v8orig, orig, orig_v32,
orig_v34, orig_v90, p2call, p2v90, v90a, v90adec, v90arx, v90arx_robust,
v90dil, api) is identical line for line; the only lines of the whole run
that differ are `test_p34`'s (main's change to the test: its events now
start later in the call, so fewer bits, the same 372 checks) and timings.

In v90d (its branch `libv90modem-split` with main merged, built against
this): the 108 programs of main's `make check` with the same counts, the
bench hash 8e3136e9 as main gives it, 106 recordings x 8 configurations
replayed (1613 files) identical octet for octet; see v90d's PROGRESS.md.

Not done: `modem/v90a` (the calling side's V.90) keeps its own history of
what was sent for its fixed-delay canceller (`ecdl`, `V90A_ECDL`); it could
use `ecdly_put/ref/next`, a change of its own to be proved on its own.

## Sync with v90d 0.5.0: Bell 103 and Bell 212A answering (2026-11-17)

v90d main 4799c4c..9484fb7 (Bell 103 and Bell 212A callers answered),
modem side applied as it is: `modem/answer.c`, `answer.h`, `v21.h`, the new
`modem/bell103_bpf.h` and `test/test_answer.c` are octet for octet main's.
`tools/gen_v21_bpf.py` is merged (the library's `--high` and main's
`bell103`); it regenerates `v21_bpf.h`, `v21h_bpf.h` and `bell103_bpf.h`
octet for octet. No new source file, so no Makefile list changed.
`answer_t` 106496 -> 106560 octets on the host, 106208 -> 106264 on ARM
(`tonedet_t` and the tap pointer); `V90M_MEM_SIZE` stays 112 KiB.

API 0.2.0: `V90M_MOD_BELL103` (0x0040) and `V90M_MOD_BELL212A` (0x0080),
answering only and not in `v90m_cfg_default` (they change what a caller
without V.8 hears after 4 s); originating with either gives
`V90M_ENOTSUP`. `V90M_CONN_BELL103` = 8 and `V90M_CONN_BELL212A` = 9,
appended, checked equal to `ANS_MOD_*` at compile time. State, stats and
events need nothing new: the connection's modulation says it (SNR -1 in
Bell 103, as in V.21). `test_api` gains the cfg checks and five calls
(Bell 103 with and without V.22, Bell 212A, each not offered). Against the
unchanged API (the new names supplied by a scratch header): 203 checks, 10
failures; now 237 checks, 0 failures. Calling with Bell: planned after C6.

Checked against sync-0.4.1 (dc4e119), same PC, both run afresh:

| | sync-0.4.1 | this |
|---|---|---|
| `make check` | 99 programs, ALL HOST CHECKS PASS | the same 99, the same counts but `answer` 33 -> 54 and `api` 187 -> 237; ALL HOST CHECKS PASS |
| `make check-arm` | the same 99, ALL ARM CHECKS PASS | the same two changes; ALL ARM CHECKS PASS |
| `make bench-v90` | transmit hash 8e3136e9 | 8e3136e9 (internals and API; host, shared, qemu-arm) |

The output of both runs, host and ARM, is identical line for line but for
the new Bell lines of `answer` and `api`, their counts, the order `-j8`
gives and timings: the calling side's tests (anstone, v8orig, orig,
orig_v32, orig_v34, orig_v90, p2call, p2v90, v90a, v90adec, v90arx,
v90arx_robust, v90dil and the calling cases of api) print the same.

In v90d (`libv90modem-split` with main 9484fb7 merged, built against this):
main's 108 host and 107 ARM programs with the same counts (but `tel`, the
branch's own), bench hash 8e3136e9 as main, 106 recordings x 8
configurations replayed (1613 files) identical octet for octet, release
image built and `test-release` passes; see v90d's PROGRESS.md.

## C6-1: the PCM upstream transmitter's pieces (2026-11-17)

The analogue modem's V.92 upstream (6.2/V.92: 8000 symbol/s "derived from
the digital network") on our own D/A clock, and the receiver for what the
digital modem sends in Phase 4. Branch `calling-v92` from sync-0.5.0.

- `modem/pcmtx`: the transmit clock and interpolator. The symbols are made
  on the network clock as the downstream receiver (`v90arx`) knows it: the
  same accumulator as its interpolator, with the same increments (`step +
  slew`), so one symbol goes out for every network sample the receiver
  makes and every correction of its timing loop moves the transmitter too;
  the round trip between them is a constant. The symbols reach our clock
  through modem/v34_tabs.h's bank (the receiver's, used the other way
  round), 33 samples late. `pcmtx_slip()` delays the symbols that follow by
  a fraction of a symbol (the half symbol of the first Su-bar, Jp's
  fraction). Before the receiver has measured the clock (Ru, the first
  TRN1u, Ja) the step is our own sample.
- `modem/v92aseq`: SUVd, CPd and Ed out of the TRN2d descrambler, a data
  frame at a time. CPd may be about 20000 bits; every field after its frame
  sync is a start bit and 16 bits, so it is taken word by word into the
  `v92_cpd_t` with the CRC running along and no bit kept (v92_cpd_parse()
  would want a 20 kbyte array, more than `V90M_MEM_SIZE` has left). Ed is the
  first data frame of zeros straight after a sequence's fill (a fill that is
  a whole frame, when the CRC ends one, is passed over).

Seen failing first, against stubs: test_pcmtx 27 checks, 18 failures;
test_v92aseq 3020 checks, 1677 failures. Now:

```
same clock: 0 of 3967 samples differ from the symbols 33 samples earlier
    +0 ppm: 80000 symbols in 80000 samples (80000.0 expected), at most 1 in one sample
  +100 ppm: 79992 symbols in 80000 samples (79992.0 expected), at most 1 in one sample
  -100 ppm: 80008 symbols in 80000 samples (80008.0 expected), at most 2 in one sample
 +1000 ppm: 79920 symbols in 80000 samples (79920.1 expected), at most 1 in one sample
 -1000 ppm: 80080 symbols in 80000 samples (80080.1 expected), at most 2 in one sample
round trip through the test line's resampler, 3.4 kHz band limit:
     +0 ppm: fitted on 0.1..2.5 s, error over 2.5..7.5 s -283.3 dB, group delay -1.000
    +30 ppm: fitted on 0.1..2.5 s, error over 2.5..7.5 s -71.5 dB, group delay -1.000
   +100 ppm: fitted on 0.1..2.5 s, error over 2.5..7.5 s -71.7 dB, group delay -1.000
   -100 ppm: fitted on 0.1..2.5 s, error over 2.5..7.5 s -71.7 dB, group delay -1.000
   +300 ppm: fitted on 0.1..2.5 s, error over 2.5..7.5 s -70.8 dB, group delay -1.000
  slip 0.5000 at   +0 ppm: delay +0.5000 symbols, errors -298.4 / -70.3 dB
  slip 0.2500 at +100 ppm: delay +0.2500 symbols, errors -71.5 / -70.2 dB
  slip 0.7500 at -100 ppm: delay +0.7501 symbols, errors -71.6 / -70.4 dB
  slip 0.0044 at   +0 ppm: delay +0.0045 symbols, errors -298.4 / -80.2 dB
constant 32767: last output 32765, 0 outputs off
constant -32768: last output -32766, 0 outputs off
pcmtx: 27 checks, 0 failures
335 streams (D = 6..72, CPd of 2 to 1170 words), 15 with a whole frame of fill after the CRC
a bit wrong in the first of two CPd: 1 taken, 1 bad
ones, zeros, random: events 0x0
v92aseq: 3021 checks, 0 failures
```

"One fixed response": what the network codec gets is the symbols through a
response that does not change as the two clocks turn against each other
(fitted on 2.5 s, it predicts the next 5 s, two turns at 100 ppm, to -70 dB).
That needs the codec's band limit: without it, the band edge, where both
interpolators are poor, changes with the phase between the clocks. A real
line has that limit (G.712); the test line of C5 has none upstream, so C6-2
gives it one.

Both tests' output is byte-identical under qemu-arm. No answering-side
file is touched (two new modules, two new tests, the Makefile lists).

## C6-2: the V.92 analogue modem, V.8 to data mode with PCM upstream (2026-11-17)

- `modem/p2call` (V.90 mode): `cfg.v92` sets INFO0a bit 26; when INFO0d
  bit 27 and INFO1d bit 70 allow it, INFO1a is Table 18 (PCM upstream: all
  four filter sections, Ltot 384, Lmax 320, UINFO, no MD) and `pcm_up` is 1.
  `clk_ppm_q8`: our clock against the network's from the digital modem's
  L2 (each tone turns from 20 ms block to block by 2 pi 3 m e; weighted
  least squares over the tones with 20 dB SNR). Within 0.1 ppm on every
  test line; Sd's estimate, which V.90 uses, is off by up to 1.6 ppm.
- `modem/v90a` with `cfg.v92`: when INFO1a selected PCM upstream, V.92's
  Phases 3 and 4 and data (9.5.2, 9.6.2; the header of the V.92 block in
  v90a.c has the order): silence, Ru, Ru-bar, TRN1u 6000 T (the canceller
  learns on it), Ja with the DIL descriptor and our rates until Sd-bar (to
  a 12 bit boundary); Jd / Jp / Jp' parsed (Table 21, 22); Su 144 T, Su-bar
  24.5 T, Su until Jp, Su-bar (24 + Jp's fraction) T; TRN1u while the DIL is
  measured, CPt (Table 23 type 0, V.90's CPt design) once it is, until Ri;
  E1u; TRN2u (4 or 8 points as Jp says) until SUVd or 12000 T; SUVu, one CPu
  after SUVd (V.90's CP design), the acknowledge bit after CPd, E2u, B1u and
  data through `v92/v92_enc` with CPd's moduli, constellations, precoder,
  prefilter and gain at LU = the configured level. Downstream as C5, with
  SUVd / CPd / Ed from `v92aseq`. Rate renegotiation by the digital modem
  (Ru on our data frame boundary after its Rd-bar, TRN2u 2400 T, as Phase 4)
  and cleardown either way (CPu / CPd with drn = 0). Time limits: 9.5.2.2.1,
  9.5.2.2.2, 9.6.2.2.1 (20 s), Jp within 3 s of Su and Jp' of Su-bar (ours),
  12 s for a renegotiation (the digital modem designs CPd again).
- The transmitter (`pcmtx`, C6-1) runs on the receiver's clock from the
  first symbol: `v90arx_clock()` sets the receiver's step from L2 before
  Phase 3, and Sd overrules it only beyond 5 ppm. `v90arx_deaf()`: Sd is not
  looked for before Ja (our own Ru has its period: its echo was taken for
  Sd before the canceller had learnt). Neither changes V.90 (not called
  there; the Sd step computation is now relative to the step Sd came
  through, which is one sample in V.90, the same expression).
- `pcmtx` changed after C6-1: its bank is `modem/pcmtx_tab.h`
  (`tools/gen_pcmtx_tab.py`): 64 taps, a low-pass at 3.7 kHz (-1.7 dB at
  3.6 kHz, -15 dB at 3.8, -31 at 3.9), every branch within -78 dB of the
  others over the band; `pcmtx_wide()` uses the receiver's full band bank
  (v34_tabs.h), for the canceller's training on TRN1u. Why, below.
- `modem/orig`: `ORIG_MODE_V92` (= `ANS_MODE_V92`) passes `v92` to v90a;
  modulation `ORIG_MOD_V92` (= `ANS_MOD_V92`) when the upstream is PCM.
- API: the calling role takes `V90M_MOD_V92` with `V90M_MOD_V90`
  (`V90M_ENOTSUP` without it); header and README say "tried in simulation
  and against this library's own answerer only". `V90M_MEM_SIZE` stays
  112 KiB: `orig_t` 100752 -> 109504 octets on the host (109232 on ARM),
  so `struct v90m`, a union of the two roles, is now sized by the caller
  (about 109.6 KiB) where `answer_t` (106560) sized it before.
- Test line (`test/v90link.h`): the digital modem's V.92 parts (p2d with
  V.92 offered, v92d as answer.c starts it) or the whole answerer with
  `ANS_MODE_V92`; `up_line`: the upstream through a line and codec response
  on the network clock (1: sim/v92chan's pulse, the answerer's own V.92 test
  line, short to 2 km, long beyond; 2: dschan's downstream loop response),
  and our clock carried on to the network's by a 320-tap resampler
  (`sim/resamp`'s new `lresamp`). Off, everything is as in C5.
- Tests: test_p2v92a, test_v92a (the sequencer against p2d + v92d, no V.8),
  test_orig_v92 (end to end), test_api (V.92 calling case). Bench:
  `rec_v90c ... v92` / `bench_v90c` replay a V.92 recording (header
  "V92C0001"); `make bench-v92c`, `make cost-v92c`.

Seen failing first: test_p2v92a 46 checks, 9 failures (fields added, no
behaviour); test_v92a 65 checks, 18 failures (v90a not yet V.92: the call
went V.90), and its L2 clock check 66 / 1 before p2call measured it;
test_orig_v92 109 checks, 24 failures (orig not passing V.92 on: V.90
everywhere); test_api 242 checks, 6 failures (V.92 calling refused). Now
46 / 66 / 109 / 262 checks, 0 failures.

What it took (each found in simulation, in this order):
1. The answerer's MD wait: v90a defaults MD to 22 units, which Table 18
   reads as 22 x 276 symbols; the digital modem waited it out and missed
   our Ru. PCM INFO1a carries MD = 0.
2. Our Ru's echo taken for Sd (deafness until Ja, above).
3. C5's upstream (48-tap resampler, no band limit): PCM upstream needs
   the far codec's band limit and a resampler that is right to 3.9 kHz;
   without them B1u came with 21 units RMS of error at 30 ppm (9.6 at 0
   ppm) and decoded wrong (588 of 2688 bits).
4. The band edge. The digital modem's prefilter puts a third of our power
   above 3.5 kHz (it makes up for the codec filters); the receiver's 64-tap
   bank is -74 dB to 3.7 kHz but -9 dB at 3.9 kHz, and what it gets wrong
   there turns with the clocks: errors came and went with a 4 s period at
   30 ppm. A 128-tap bank cut at 3850 Hz was time-invariant but rang 64
   taps ahead of the cursor, outside the digital modem's 24-tap look-ahead
   (-39 dB of its energy): unbudgeted interference, errors at 0 ppm. The
   64-tap cut at 3700 Hz has -64 dB beyond 24 taps and is time-invariant to
   -78 dB.
5. The canceller trained on the low-passed TRN1u reached 33 dB (73 before):
   the hybrid's echo peaks at the band edge (+23 dB at 3.8 kHz against 1
   kHz in sim/dschan), where the low-pass leaves little to learn on. TRN1u
   (and Ru, Ja) go out through the full band bank (73 dB).
6. The clock. Found at Sd and corrected afterwards, it moved our symbols
   against the network by up to a hundredth of a sample during and after
   the digital modem's measurements (at 100 ppm its training receiver lost
   the first TRN1u altogether: no Ja); from L2 (0.1 ppm) the first TRN1u is
   already on the network clock and the receiver's loop has little to do.
7. Echo -10 dB: the PCM upstream's band edge echo clipped our 16-bit A/D
   (peaks of 30000) before the low-pass of 4; the end-to-end lines use
   -16 dB as C5's did; test_v92a's -10 dB call is clean now.

End to end (test_orig_v92, seeds as in the suite; 10 s of data per line;
"fail" counts failures of the self-synchronising check, three per wrong
bit; rtd includes the 320-tap resampler's 20 ms):

```
  line                                    mod data ms   down     up SNR dB   rtd  bits down  fail    bits up  fail
  2 km,    0 ppm, -75 dBm0               V.92   12251  52000  34666  56.7   184     520026     0     346632     0
  0 km, +100 ppm, -75 dBm0               V.92   12994  38666  34666  50.4   184     386628     0     346632     0
  6 km, -100 ppm, -75 dBm0               V.92   12707  44000  30666  51.6   183     440055     0     306682     0
  4 km,  +50 ppm, -65 dBm0               V.92   13822  40000  30666  45.8   185     399960     0     306636     0
  2 km,  +30 ppm, 3 dB pad               V.92   12246  48000  36000  45.3   184     479952     0     359964     0
  2 km,  -30 ppm, 6 dB pad               V.92   12702  42666  34666  50.1   184     426688     0     346632     0
  2 km,  +30 ppm, robbed bit             V.92   13155  44000  34666  50.3   184     439989     0     346684     0
  2 km,  +30 ppm, 6 dB pad, robbed bit   V.92   12018  40000  34666  49.5   184     399960     0     346632     0
  2 km,  -50 ppm, A-law                  V.92   11506  46666  37333  51.8   183     466690     0     373352     0
V.90 when V.92 is not on both sides
  the answerer without V.92              V.90   10697  52000  28800  56.5   184     520026     0     288000     0
  ours with V.92 off                     V.90   10394  52000  28800  56.9   184     520026     0     288000     0
reported only: the upstream through the downstream's loop and codec response
  2 km,    0 ppm, dschan's response up   V.92   13791  52000  32000  56.5   195     520026     0     319968     0
ending a call
```

Every line reaches V.92 with PCM upstream, 30666 to 37333 bit/s up and
38666 to 52000 down, with no check failure either way in 10 s; the V.90
fallback both ways (answerer without V.92, our V.92 off); cleardown from
either end. test_v92a (sequencer, 2 km, +30 ppm, echo -10 dB): 45333 /
32000, 4 s clean; the digital modem's renegotiation followed (one break of
the stream, 24 check failures, then 2 s clean); cleardown both ways; the
time-outs (no Sd, no Jd, no Jp, no DIL, no CPd by 20 s, carrier in data).

Not in the suite, the same table with six more seeds per line (54 calls),
the final code: 48 clean. The rest, honestly:
- 0 km, +100 ppm, seeds 100 and 104: the downstream receiver collapses in
  data mode and takes our transmitter's timing with it. Not V.92's: C5's
  V.90 caller on its own C5 line with the same seeds collapses on 3 of 6
  (and A-law -50 ppm had downstream errors on 2 of 6, which V.92's runs did
  not). C5's single-seed table hid it; it is C5-1's band edge at 100 ppm.
- 2 km, +30 ppm, robbed bit, seed 104: 1428 downstream check failures.
- 2 km, +30 ppm, 6 dB pad and robbed bit: seed 101 never reached data
  mode on both sides (the answerer gave B1u up, ERR_B1, and retrained; we
  do not retrain); seeds 103 and 104 have upstream errors (1836, 389). The
  cause found: our receiver's timing loop walks by about a hundredth of a
  sample between TRN1d and the end of the DIL (decisions on unmeasured
  pad / robbed bit levels), comes back during TRN2u, and our symbols move
  with it while the digital modem measures the channel for CPd, which it
  does not follow afterwards. Tried and not kept (no net gain over the 54
  calls): holding the loop from TRN1d to TRN2d, waiting 1 s after the DIL
  before CPt, the loop at 1/4 and 1/8 of its gains (48, 46, 44, 49 clean).

ARM: check-arm passes, the same programs and counts as the sync-0.5.0
baseline taken here first (99 programs) but for the new ones and api;
host and qemu-arm output of test_pcmtx, test_v92aseq, test_p2v92a,
test_v92a, test_orig_v92 identical. Benches: bench-v90 8e3136e9 (internals
and API, host, shared, qemu-arm); bench-v90c 5a5e5ca7 as at C5;
bench-v92c (2 km, +100 ppm) cd66ac82 on the host and under qemu-arm.

Cost (`make cost-v92c`, qemu instruction counts, the caller alone on its
recording): data mode 102.9 M ARM instructions per second of line at 0 ppm
(52000 / 34666 here), 112.5 M at 100 ppm (40000 / 34666), against C5's
V.90 77.4 / 79.8 M: the PCM upstream (encoder with LZ2 128, LP1 64; the 64
by 2 tap bank) costs some 25 to 33 M more than the V.34 upstream. That is
about 90 % of the box's CPU (V34-REPORT: 39.5 M is 32 %): one call, barely.
The six slowest ticks at 100 ppm:

| tick | what | ARM instructions |
|---|---|---|
| 8.286 s | end of the DIL: analysis and CPt design (C5's) | 9.03 M |
| 7.012, 9.125, 9.189, 13.861, 16.421 s | the receiver's equaliser computed again (C5's) | 0.84 to 0.86 M |

No V.92 step is among them: the worst tick is C5's DIL analysis, as
before, still to be spread over ticks.

Answering side, by diff against the sync-0.5.0 baseline (host and ARM):
the only differences are the new programs (pcmtx, v92aseq, p2v92a, v92a,
orig_v92) and api 237 -> 262 (its calling cases). No answering-side file
changed (modem/answer.c, p2d, p34d, v92/, v90/ untouched); `answer_t` is as it was.

Open:
- The multi-seed failures above; most of all our timing walking during
  Phase 3 on lines with pads and robbed bits (a transmitter clock that does
  not follow the receiver's loop excursions, but still the network, is the
  next thing to try).
- Cost: ~103 to 112 M instructions per second of data mode.
- No retrain (a digital modem's retrain or its B1u failure ends our call),
  no renegotiation of our own, no quick connect, no modem-on-hold, no
  fast parameter exchange. A CPd that leaves out a part (has_mod / has_filt
  / has_const = 0) is refused (V90A_ERR_CPD) rather than merged with the
  last one.
- SUVu says "no level measured"; 8-point TRN2u is coded but our answerer
  never asks for it, so it is untested; MD is never sent.
- The upstream lines are the answerer's own V.92 model (sim/v92chan);
  through dschan's full loop response (up_line 2) the 2 km row is clean on
  its seed, but the loop's low frequency tail outlasts the digital modem's
  12 ms channel estimate by -46 dB, which its design does not budget for.
- Tried in simulation and against this library's own answerer only: a
  reading of V.92 common to both sides passes every test.

## C7: Bell 103 and Bell 212A on the calling side (2026-11-18)

Branch `calling-bell` from `calling-v92` (c20c4ce). The calling modem
(`modem/orig`) calls in Bell 103 and Bell 212A when asked to
(`ORIG_MODE_BELL103` / `ORIG_MODE_BELL212A`, the numbers of the answering
side's `ANS_MODE_BELL*`; API: `V90M_MOD_BELL103` / `V90M_MOD_BELL212A` in
the calling role, still not in the defaults). Reported as
`ORIG_MOD_BELL103` / `ORIG_MOD_BELL212A` = `V90M_CONN_BELL103` /
`V90M_CONN_BELL212A`, as the answerer does.

- `modem/belltone`: the Bell answer tone, 2225 Hz within some 11 Hz, fixed
  point. 40 ms blocks brought down from 2225 Hz; a block is tone when that
  sum holds more than half its power and it is above -43 dBm0; 4 blocks in
  a row decide (160 ms). 2250 Hz (a V.22 answerer's unscrambled ones) turns
  once in a block and sums to nothing, 2100 Hz is far off.
- `modem/bell103h_bpf.h` (`tools/gen_v21_bpf.py bell103h`): the Bell 103
  answer channel for the caller, 2025 / 2225 / 2250 Hz flat, our 1070 / 1270
  Hz 57 dB down (and 1650 Hz too). The other three tables regenerate octet
  for octet.
- `modem/orig`: the order (orig.h has it in full, DESIGN.md 4.2d):
  1. ANSam and a V.8 mode on offer: V.8 as before (Bell is not in CM).
  2. 2225 Hz before any 2100 Hz tone is told (a pre-V.8 Bell answerer), or
     after ANS while a legacy answerer is awaited: 212A if offered, else 103.
  3. Only Bell on offer: no CI, no CM after ANSam / ANS; silent and
     listening (`ORIG_BELL`, new): 2225 Hz -> 212A (else 103); 103 alone:
     150 ms of a carrier in the Bell 103 answer channel that is not 2100 Hz
     -> 103 (a V.22 answerer's unscrambled ones are such a carrier, as for
     the plain modems the answering side was made for); 212A and 103: 6 s of
     that carrier without 2225 Hz -> 103 (our answerer plays 2225 Hz after 4
     s of unscrambled ones when 212A is on, so 212A wins where it is there).
  4. A 212A start without a 1200 bit/s answer in 3 s -> 103, if offered.
  Bell 212A: `v22m` as the caller at 1200 bit/s, scrambled ones from the
  moment the tone is told, data 765 ms after the answerer's (V.22's
  6.3.1.2.1 c, as the 212A caller in test_answer); no 2400, no retrain.
  Bell 103: the V.21 path on 1270 / 1070 Hz out, 2225 / 2025 Hz in. Off,
  nothing changes: the Bell code runs only with a Bell bit in the modes.
- Spec: the Bell 212A handshake document was not to hand. What was checked:
  2225 Hz answer tone for 212A and 103 and 1200 Hz originate carrier (TDK
  73K222BL and Conexant data sheets), V.22-compatible DPSK and scrambler,
  and the library's own answerer (V22_BELL_MS, its 1270 Hz watch) and its
  212A test caller. The 3 s and 6 s fallbacks are ours.
- `orig_t` 109504 -> 109616 octets on the host; `V90M_MEM_SIZE` stays
  112 KiB. `answer_t` and every answering-side file unchanged.

Seen failing first: test_belltone 27 checks, 8 failures (stub detector);
test_orig_bell 43 checks, 25 failures (the constants only: no Bell call,
only the V.8 fallback and the no-call cases passed); test_api 273 checks,
11 failures (Bell calling refused with V90M_ENOTSUP). Now 27 / 211 / 303
checks, 0 failures.

End to end (`test/test_orig_bell.c`: our caller, sim/v32line, pseudo-random
octets both ways through V.14 for 10.5 s, every data bit compared; "bits"
are data bits, 8 per octet; peers: the library's answerer with V.22, V.21,
Bell 103 and Bell 212A, or as said; two pre-V.8 answerers written in the
test that play nothing but 2225 Hz until answered, a 212A one that takes
103 callers too and a 103 one):

```
Bell calls, 10.5 s of data each way (bits: data bits compared)
  Bell 103, to our answerer                    clean                                          up  5.76 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  Bell 103, to our answerer                    10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  5.80 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2504 bits 0 errors
  Bell 103, to our answerer                    20 dB, -52 dBm0 noise, -3 Hz, -100 ppm         up  5.83 s, mod 8 / 8,  300 bit/s; up 2504 bits 0 errors, down 2504 bits 0 errors
  Bell 103, to our answerer                    6 dB, loud echoes (6 dB), 7 Hz, 100 ppm        up  5.78 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  Bell 212A (and 103), to our answerer         clean                                          up 10.82 s, mod 9 / 9, 1200 bit/s; up 10056 bits 0 errors, down 10056 bits 0 errors
  Bell 212A (and 103), to our answerer         10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up 10.91 s, mod 9 / 9, 1200 bit/s; up 10032 bits 0 errors, down 10040 bits 0 errors
  Bell 212A (and 103), to our answerer         20 dB, -52 dBm0 noise, -3 Hz, -100 ppm         up 10.93 s, mod 9 / 9, 1200 bit/s; up 10016 bits 0 errors, down 10016 bits 0 errors
  Bell 212A (and 103), to our answerer         6 dB, loud echoes (6 dB), 7 Hz, 100 ppm        up 11.07 s, mod 9 / 9, 1200 bit/s; up 10048 bits 0 errors, down 10048 bits 0 errors
  Bell 212A only, to our answerer              clean                                          up 10.82 s, mod 9 / 9, 1200 bit/s; up 10056 bits 0 errors, down 10056 bits 0 errors
  Bell 212A only, to our answerer              10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up 10.91 s, mod 9 / 9, 1200 bit/s; up 10032 bits 0 errors, down 10040 bits 0 errors
  Bell 212A only, to our answerer              20 dB, -52 dBm0 noise, -3 Hz, -100 ppm         up 10.93 s, mod 9 / 9, 1200 bit/s; up 10016 bits 0 errors, down 10016 bits 0 errors
  Bell 212A only, to our answerer              6 dB, loud echoes (6 dB), 7 Hz, 100 ppm        up 11.07 s, mod 9 / 9, 1200 bit/s; up 10048 bits 0 errors, down 10048 bits 0 errors
fallbacks
  212A and 103, answerer 103 only              clean                                          up 11.61 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  212A and 103, answerer 103 only              10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up 11.65 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  V.22, V.21 and Bell, answerer with V.8       clean                                          up  4.74 s, mod 3 / 3, 2400 bit/s; up 20088 bits 0 errors, down 20080 bits 0 errors
  V.22, V.21 and Bell, answerer with V.8       10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  4.64 s, mod 3 / 3, 2400 bit/s, V.8 1
  212A and 103, 2225 Hz-only 212A answerer     clean                                          up  1.54 s, mod 9 / 9, 1200 bit/s; up 10056 bits 0 errors, down 10056 bits 0 errors
  212A and 103, 2225 Hz-only 212A answerer     10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  1.63 s, mod 9 / 9, 1200 bit/s; up 10040 bits 0 errors, down 10040 bits 0 errors
  V.22 and Bell, 2225 Hz-only 212A answerer    clean                                          up  1.54 s, mod 9 / 9, 1200 bit/s; up 10056 bits 0 errors, down 10056 bits 0 errors
  V.22 and Bell, 2225 Hz-only 212A answerer    10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  1.63 s, mod 9 / 9, 1200 bit/s; up 10040 bits 0 errors, down 10040 bits 0 errors
  103 only, 2225 Hz-only 212A answerer         clean                                          up  0.47 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  103 only, 2225 Hz-only 212A answerer         10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  0.53 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2504 bits 0 errors
  212A and 103, 2225 Hz-only 103 answerer      clean                                          up  3.47 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  212A and 103, 2225 Hz-only 103 answerer      10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  3.53 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2504 bits 0 errors
  V.22 and Bell, 2225 Hz-only 103 answerer     clean                                          up  3.47 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2512 bits 0 errors
  V.22 and Bell, 2225 Hz-only 103 answerer     10 dB, echoes, -45 dBm0 noise, 1 Hz, 50 ppm    up  3.53 s, mod 8 / 8,  300 bit/s; up 2512 bits 0 errors, down 2504 bits 0 errors
no call
  212A only, answerer 103 only: phase 4, end 4; answerer phase 3, modulation 0
  no Bell offered, 2225 Hz-only answerer: phase 4, end 1
  Bell offered, nobody answers: phase 4, end 1
orig_bell: 211 checks, 0 failures
```

Every call above reached its modulation, with no bit error either way in
10.5 s of data: Bell 103 and Bell 212A on four lines each (clean; 10 dB,
echoes, -45 dBm0 noise, 1 Hz, 50 ppm; 20 dB, -52 dBm0, -3 Hz, -100 ppm; 6 dB
with echoes 6 to 8 dB down, 7 Hz, 100 ppm), the fallbacks on two. Bell 212A
against our answerer takes 10.8 to 11.1 s (its 5 s of ANSam, then 4 s of
unscrambled ones before the 2225 Hz); 212A and 103 offered to an answerer
with only 103 takes 11.6 s (the 6 s wait). test_api adds an API caller to
an API answerer (echo of a fifth each way): Bell 103, Bell 212A (both
`moving`, text both ways, retrain and cleardown refused), and V.22 + Bell
offered going V.8 / V.22bis.

Checked against calling-v92 (c20c4ce), same PC, both run afresh (the host
baseline equals ~/c52v/c6v/check.log count for count):

| | calling-v92 | this |
|---|---|---|
| `make check` | 104 programs, ALL HOST CHECKS PASS | the same 104 with the same counts but `api` 262 -> 303, plus `belltone` 27 and `orig_bell` 211; ALL HOST CHECKS PASS |
| `make check-arm` | 104 programs, ALL ARM CHECKS PASS | the same changes; ALL ARM CHECKS PASS |
| `make bench-v90` | 8e3136e9 | 8e3136e9 (internals and API; host, shared, qemu-arm) |
| `make bench-v92c` | cd66ac82 | cd66ac82 (host and qemu-arm) |

Sorted output of each pair, compile lines and timings left out, by diff: the
only baseline line that is not in the new run is `api: 262 checks`; every
other difference is a new line (belltone, orig_bell, the API's Bell calls).
test_orig_bell under qemu-arm prints what the host prints but for one
connect time (5.84 s against 5.83 s on the 20 dB line: the line simulator
is in double; the modem code is fixed point, bench-bell's hashes agree).

ARM cost (`make cost-bell`, one qemu run per call, the trace split where it
enters `bench_bell_mark`; the caller alone replaying `rec_bell`'s recording
of a call against our answerer on the 10 dB line; `make bench-bell`: hashes
48f22641 (103) and fc125ef2 (212A), host and qemu-arm the same):

| call | to data mode | first second of data mode | the second after |
|---|---|---|---|
| Bell 103 (data at 5.56 s) | 52.05 M | 9.85 M | 9.86 M |
| Bell 212A (data at 10.91 s) | 98.67 M | 4.43 M | 4.44 M |

So about 9 to 10 M ARM instructions per second of line while waiting and in
Bell 103 (the 81-tap band-pass runs every sample, as in V.21), 4.4 M in
Bell 212A: some 8 % of the box (V34-REPORT: 39.5 M is 32 %).

Open:
- Tried in simulation and against this library's own answerer (and two
  answerers written in the test) only; no real modem has been called.
- The Bell 212A handshake document was not to hand: no wait between the
  tone and our scrambled ones, and the 3 s / 6 s fallbacks, are our choices,
  checked against our own answerer only. A strict 212A answerer might want
  some silence first.
- An answerer with V.21 and Bell 103 but no V.22 plays 1650 Hz after ANSam;
  our Bell receiver has it 57 dB down, so a Bell-only caller waits there
  until it gives up (30 s). Not tested.
- JM with nothing in common does not fall back to Bell (it cannot: the
  answerer has gone V.8); ANSam with a V.8 mode on offer never tries Bell.
- A 212A start that fails without 103 on offer waits the V.22 30 s.

## ports-2: the port matrix on calling-bell (2026-11-18)

Branch `ports-2`, from calling-bell (632a820), with the parked `ports` work
(b26345f: the 16-target matrix, `tools/winerun`, the static library's
internal symbols renamed `v90m__i_*` by `tools/symmap.sh`, `check-symbols`,
CROSS / NM / SUB_BUILD in the Makefile, two clang warnings) cherry-picked
on top: no conflict.

### Why the earlier matrix runs failed on every target

They wrote their logs, and the compilers their temporary files, to /tmp,
which had run out of quota; nothing of them is left. The same script with
everything under /home builds and passes every target (below): it was the
environment. `tools/check-ports.sh` now refuses an OUT under /tmp and sets
TMPDIR to OUT/tmp.

### The matrix script

- Each test program's output is kept apart (`tools/portrun`, given to make
  as QEMU) and compared with host gcc's, program by program. Only lines
  that measure the host are left out: CPU and wall time, and the two lines
  that print the size of structures in memory (`sizeof(v90tx_t)` and
  `v34_rx_t`, which follow the pointer width and alignment).
- PAR targets at once (`xargs -P`), JOBS compilers each; QUICK="..." skips
  the slow programs of `tools/ports-slow.txt` on the targets named;
  COMPARE_ONLY=1 compares what a run left.
- `-DTEST_FULL` on every target: the ten tests that ran a shorter version
  under `__arm__` (v32 call, caller, startup, v8; v34 precoder, bench,
  resync, ber; v92 e2e, design) now say `TEST_SHORT`, which test/test.h
  sets on 32-bit ARM unless TEST_FULL is given. So `make check-arm` on the
  box's ARM is what it was, and the matrix runs the whole tests everywhere.
- 32-bit x86 (i686, win32) builds with `-msse2 -mfpmath=sse`; see below.
- `tools/printf-probe.c` is run on each target; see below.

### What was wrong

1. Unspecified order of evaluation (clang against gcc on the same host):
   test_viterbi drew its two noise samples as two arguments of one call,
   `v32_vit_step(&v, f(rng_gauss()), g(rng_gauss()), ...)`; gcc evaluates
   them right to left, clang left to right, so the two compilers fed the
   decoder different noise (clang's run: 8 bit errors at 12000 bit/s where
   gcc's has 0). test_constel did the same with `tu_rand()` twice in
   `v34_map_point`'s arguments. Both now draw in gcc's order in separate
   statements: gcc's output is unchanged, clang's is now the same.
2. Undefined behaviour in the signal path, found with `make check-ubsan`
   (new: the host suite built with clang `-fsanitize=undefined
   -fno-sanitize-recover`; it failed before, 36 reports, the tests aborted):
   - left shifts of negative numbers, on 23 lines: qamrx (2), dpsk, ec (2),
     v22rx, v90arx (10, and 3 more of the same kind beside them), v92_p34d
     (2), v32_rx (2), v32_ec, v34_nl (2). Each
     is now the multiplication it stood for (`x << 16` -> `x * 65536`,
     `x << -rs` -> `x * ((int64_t)1 << -rs)`): the same value wherever the
     shift was defined, and what every compiler made of it before.
   - signed overflow in 32-bit products: v22rx's carrier derotation
     (`zr * c + zi * s` while the gain has not settled: 168199 * -26905 in
     test_v22bis) and v90arx's forward-filter update (`es * x[i] + half`
     with a wild error, 286331153 * -8 in test_v90a, and in test_v92a). They
     wrapped on every target; they still wrap, now defined (unsigned
     arithmetic, converted back). Making them 64-bit would change what
     the modem does in those moments (and could move hashes): left as an
     open question, below.
   After: `make check-ubsan` passes, no report. The real-modem fixtures
   through v90m-replay, and the recordings of bench-v90, bench-v90c,
   bench-v92c and bench-bell replayed by sanitized builds: no report, and
   the same hashes (8e3136e9, 5a5e5ca7, cd66ac82, 48f22641, fc125ef2).
3. test.h's CHECK_EQ_I compared in `long` (32 bits on every 32-bit target
   and on Windows): now `long long`.

### Floating point: what differs and why

- uClibc-ng's printf (armv5 and m68k toolchains, uClibc-ng 1.0.43) rounds
  up where the correct rounding is down: 0.25 -> "0.3", 5.835 (the double
  5.83499999999999996...) -> "5.84", where glibc, musl and mingw print
  0.2 and 5.83. That is the 5.84 s against 5.83 s seen on ARM before
  (test_orig_bell): the same double, printed differently, not a different
  computation. `tools/printf-probe.c` finds such a printf on the target
  itself, and only there `tools/ports-cmp.awk` lets a number be one unit
  larger in its last printed digit (same sign and decimals); those lines
  are counted and reported; any other difference fails.
- The x87 (i686 and win32 by default) computes the simulator's doubles in
  80 bits: the simulated line differs, so the modem is fed other samples
  (test_v34dx on i686 without SSE: SNR at MP 40.3 / 43.3 dB against
  39.7 / 45.1, one renegotiation to 19200 against 16800 bit/s; the 32
  programs before it were identical). The library itself has no floating
  point: built for i686, its 78 objects contain no x87 instruction. So
  32-bit x86 builds the suite with SSE2 doubles, which round as binary64
  does everywhere else.

### Found after 4f4059d (the matrix and MemorySanitizer)

4. test_p2call compared INFO1d and INFO1c parsed from the same bits with
   `memcmp`, the padding octet before `freq_off` included, which neither
   parser writes: the check depended on stack garbage (clang
   `-fsanitize=memory` over the host suite: this was its only report).
   Both structs are cleared first now.
5. win32: test_v90arx_robust's watchdog used SIGALRM and alarm(), which
   Windows does not have, so the root tests did not even build there. The
   watchdog is there only `#ifdef SIGALRM` now.
6. v34 test_bench asserts at most 5000 ns of wall time per symbol; under
   qemu it failed on riscv64, mipsel and ppc (and was left out on ARM
   only). With `TEST_EMULATED=1`, which check-ports.sh gives its qemu
   targets, the check still counts but cannot fail.
7. test_v44 prints the encoder's and decoder's sizes: one more line that
   measures the host's ABI, left out of the comparison like `sizeof`.
8. Not a fix, a consequence of 1.: gcc for ARM evaluates arguments left to
   right, as clang does on x86, so calling-bell's `make check-arm` printed
   clang's numbers for test_viterbi (4 lines: "7200 bit/s ... plain
   decisions 613 symbol errors ..."). With the draws in a fixed order, ARM
   prints what the host prints. Same counts.

### m68k: qemu-m68k cannot be trusted with this code

On m68k-uclibc (68040, qemu-m68k 10.1.5) twelve programs fail: every one
built on the V.34 receiver core, `modem/qamrx` (qamrx itself: training mse
+0.6 dB, 6607 symbol errors at 3000 symbols/s). The emulator, not the code:

- the same binary, run with `-one-insn-per-tb`, fails differently (3200
  symbols/s, which passes in a normal run, fails too) - a deterministic
  program whose result depends on how qemu cuts it into blocks;
- qamrx.c alone at -O1, the rest at -O2: passes; the whole suite at -O1:
  fails at 3200 as well; -mcpu=68060 run as m68060: passes;
- clang's UBSan and MSan over the host suite report nothing in qamrx, and
  the other 15 targets (five big-endian, seven 32-bit, the other uClibc)
  print what the host prints.
No m68k hardware here to settle it; m68k ran QUICK (11 slow programs left
out), 83 of the 95 that ran identical to the host (5 lines with uClibc's
rounding), the 12 qamrx users failing as above.

### The matrix (commit 4f4059d, the three programs changed since rerun on
### every target from this commit; win32 run whole from this commit)

`PAR=7 JOBS=1 tools/check-ports.sh` over ~/ports2-m2, then
`COMPARE_ONLY=1`. Every target built with -Werror. "Identical": the
program's output byte for byte, but for the timing / size lines.

| target | tests run | identical to host gcc | notes |
|---|---|---|---|
| host gcc | all 106 | (reference) | |
| host clang | all 106 | 106 | |
| x86_64 musl | all 106 | 106 | |
| i686 musl | all 106 | 106 | SSE2 doubles |
| armv5 uClibc | all 106 (TEST_FULL) | 106 | 6 lines with uClibc's rounding |
| armv7hf musl | all 106 (TEST_FULL) | 106 | |
| aarch64 musl | all 106 | 106 | |
| aarch64be musl | all 106 | 106 | big-endian |
| mips musl | all 106 | 106 | big-endian |
| mipsel musl | all 106 | 106 | v34 bench's wall-time check failed first (6.) |
| ppc musl | all 106 | 106 | big-endian; as mipsel |
| ppc64 musl | all 106 | 106 | big-endian |
| riscv64 musl | all 106 | 106 | as mipsel |
| s390x glibc | all 106 | 106 | big-endian |
| m68k uClibc | 95 (QUICK) | 83 | 12 fail: qemu-m68k, above |
| win32 mingw | all 106 (wine) | 106 | SSE2 doubles |

106 programs: the 60 tests of the top level, the API test against the
static library (check-symbols on every target: only `v90m_*` defined), and
the 45 of v34, v90, v92, v42, v32.

### Host and ARMv5 unchanged

| | calling-bell (632a820) | ports-2 |
|---|---|---|
| `make check` (host) | 106 count lines = ~/c52v/bellv/check.log | the same 106 lines, by diff; every other line the same (sorted, compile lines out) |
| `make check-arm` (armv5, qemu) | 106 count lines | the same 106 lines, by diff; output the same but test_viterbi's 4 lines (8.) |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 |
| `make bench-v92c` | cd66ac82 (host, qemu-arm) | cd66ac82 (host, qemu-arm) |
| `make bench-bell` | 48f22641 (103), fc125ef2 (212A), host and qemu-arm | the same |

### v90d re-proved on top of it

v90d-split d707463 built against this tree (`LIBV90M=~/ports2-wt`
at 4f4059d, the way libv90m.mk takes it; script
~/sync-build/proof/afterports.sh, after050.sh with that one
change), against the 0.5.0 sync's proof (libv90modem 629ae89):

| | 0.5.0 sync | ports-2 |
|---|---|---|
| `make check` | 126 programs, 2,839,306 checks | 133: the same 126 lines but `api` 237 -> 303, plus pcmtx, v92aseq, p2v92a, v92a, orig_v92, belltone, orig_bell (the calling side's C6, C7): 2,842,879 checks, 0 failures |
| `make check-arm` | 125 programs | 132, the same differences |
| `make bench-v90` | 8e3136e9 | 8e3136e9 x6 |
| replay, 106 recordings x 8 configs | 1613 files | manifest identical, all 1613 |
| `make -C fw release`, `test-release` | pass | pass, the same lines but timings |

So bringing in C6, C7 and the port changes changes nothing for v90d's
answering use; v90d-split needed no change.

### Open

- m68k: needs real hardware (or another emulator) to say more.
- The two 32-bit products that wrap (v22rx derotation before the gain has
  settled, v90arx's forward update with a wild error) are now defined but
  still wrap: 64 bits would be the right arithmetic, but would change the
  modem's behaviour in those moments, maybe hashes. The owner's call.
- UBSan and MSan saw what the host suite, the fixtures and the benches
  run; code no test reaches is not covered.
- The benches' recordings (`rec_v90a`, `rec_v34`, v32's test_bench record
  mode) are written in host byte order; replaying one recorded on x86 on
  a big-endian target would read it wrongly. Not used across byte orders
  today.
- On 32-bit x86 the suite runs with SSE2 doubles; an x87-only build of the
  test harness prints other numbers (its line simulation), as described.

## realcheck: the calling side against recordings of real callers (2026-11-18)

Until now the caller (C1 to C7) had met only the library's own answerer, so
a misreading of a Recommendation shared by both sides would pass every
test. The only outside reference without a phone line: v90d's 106
recordings of real modems calling the box (sync-build/live-050, a copy of
v90d's live/*.raw; not in this repository). What real callers send was
read out of them with the answering side's own decoders, our caller's
calls were recorded the same way and read with the same tool, and the two
compared field by field.

### What the recordings hold

- Format (tools/README.md): per 10 ms, 80 samples the box's codec received
  and 80 codewords it sent. 75 recordings receive in 16-bit linear
  (`V90DLIN1`), 31 companded (mu-law). Both directions at the box's 2-wire
  port: what the caller sent (with the echo of the box's own signal under
  it) and what the box sent, from off-hook; 20 to 82 s, half under 36.
- No metadata in the files. The caller is told by what it sends (CM and
  INFO0a differ between the makers) and by v90d's PROGRESS.md (which modem
  was on the port that day): Conexant CX93001 (USB) 56 calls (16 V.90,
  33 with V.92 PCM upstream offered, 6 V.34 forced, 1 V.21 forced);
  U.S. Robotics 5686 (flash 5.4.62) 44, all V.92; U.S. Robotics 56K of
  1998 (flash 5.1.4, no V.92) 6, V.90. None of them sent CI. No V.32bis,
  V.22bis or Bell caller was recorded.
- The box's software changed between the calls (0.2.x to 0.4.x): today's
  answerer, set up as the recording box was (the options below), sends
  what was recorded until 15 to 25 s into most calls, data mode or late
  Phase 4; the 12 `pw_*` calls diverge after 8 to 10 s (Phase 3). Of every
  call the caller's side up to there is read with the answerer following.

### Tools

- `tools/v90m_fields.c` (`build/v90m-fields`, not installed): the answering
  modem run over a recording as `v90m-replay` runs it (same options), and
  after each sample what it decoded: CM (its octets), INFO0a / INFO0c,
  INFO1a / INFO1c, the probe's per-tone levels, Ja (V.90 and V.92), CPt,
  CP, CPu, SUVu, MP, the states of each engine with their times. Next to
  it, from the first sample of the recording, the library's V.21(L)
  start-stop receiver (CI, CM, CJ, V.21 data) and the Phase 2 DPSK
  receivers on both carriers (every CRC-clean INFO frame, every reversal
  of the caller's tone), and the caller's level per state of the answerer
  (L1 and L2 apart, the 1800 Hz guard tone against 2400 Hz). Every
  sequence is also rebuilt from what the parser read with the builder the
  caller sends it with (info0a_build, info1a_build, info1c_build,
  v90_dil_build, v90_cp_build, v92_ja_build, v92_cpu_build,
  v92_suvu_build, v34_mp_build, v8_encode) and compared bit by bit.
- `tools/rec_orig.c` (`build/rec_orig`, host): a call of our caller
  (V.92 + V.90 + V.34 + V.32bis + V.22bis + V.21; V.34 and below; V.21
  only) against the answerer through the public API set up exactly as
  `v90m-replay` sets it up, recorded at the answerer in the format above
  (test/v90link.h: 2 km, +100 ppm, the V.92 upstream line; test/line34.h's
  "real" line for V.34 and V.21). `v90m-replay` follows these recordings
  with 0 transmit differences up to data mode (15 to 59 in data mode,
  whose data are not the replay's), so `v90m-fields` reads them exactly as
  it reads the real ones (step 4: cross-decode).
- test/v90link.h: a hook for the digital end (`dig`), off in every test.

Options per recording: of `-9 -u 33600`, `-9`, `-9 -u 32000`,
`-9 -u 33333` and none, the one with which the replay's transmit stays
equal to the recording's longest (the seven extracts below name theirs).

### Real callers and ours, field by field

| field | Conexant CX93001 | USR 5686 | USR 1998 | ours | verdict |
|---|---|---|---|---|---|
| CI | none | none | none | none (send_ci off) | (a) V.8 7.1: optional |
| Te (ANSam start to first CM) | 1.24-1.26 s (1.56 in 10) | 1.50-1.52 s | 1.41-1.42 s | 1.49-1.52 s | (a) 8.1.1: >= 0.5 s, >= 1 s for V.25 echo control |
| CM octets | c1 65 13 94 2a 0d 27 (V.92/V.90), c1 45 13 94 2a (V.34) | c1 65 13 10 27 0d | c1 65 13 10 27 0d | c1 65 13 90 2a 27 (V.92/V.90), c1 45 13 90 2a | **(b)** 6.3 and 7.3: with the PCM category the PSTN access category "shall also be present"; every real PCM CM has 0d. The rest (V.23, LAPM, category order) (a): clause 6 fixes no order and the two makers differ |
| CM repeated | back to back, 10 ONEs between | same | same | same | = |
| JM to CJ (end) | 2 JM + the CM octet: 0.673 s (5-octet JM), 0.807 s (7) | 2 JM + the rest of the CM | same | 0.806 s on the V.90 line; one JM more (0.94 s) on line34's 20 ms delay | **(b)** 8.1.2 "after a minimum of 2": legal, but the first JM after ANSam is lost (below) |
| CJ | after callf (c1) | after the whole CM | after the whole CM | mid-CM, at the next octet | (a) 8.1.2: "complete the current octet" |
| INFO0a | all symbol rates, 3429 allowed, power reduction, 1664, bit 26 (V.92) 1, ack 0; sent once | the same | the same, bit 26 0 | 3000/3200 only, no 3429, power reduction, 1664, bit 26 1 with V.92; once | (a) capabilities; bits 26/27 as Table 16/V.92 |
| INFO0c (V.34) | all rates, bits 26:27 (clock) 00 | - | - | 3000/3200, 00 | (a) |
| Tone A / guard | carrier 1800/2400 Hz -6.1..-6.7 dB | -6.2 dB | -6.3 dB | -5.5 dB (the simulated loop's slope) | = 8.2.3.1/V.90: guard 6 dB under the carrier |
| reversals | 2nd = RTD + 80.0 ms; L2 521.6 ms (RTD 33 ms) | RTD + 80.0; L2 544 ms (RTD 34) | the same | RTD + 80.0; L2 528 ms (RTD 23) | = 9.2.2.1.5/.6: 40 + 40 ms; L2 <= 550 ms + RTD |
| L1 / L2 level | L1 = L2 + 6.0 dB | + 6.0 | + 6.0 | + 6.0 | = 10.1.2.4/V.34 |
| INFO1a, Table 18/V.92 | 12:17 0, MD 0, UINFO 79 (69..85 with the box's power), 32:33 0, 34:36 6, 37:39 6, **40:49 0x3fd** | 12:17 0x04 (Ltot 256), UINFO 78, 40:49 0x3ff | - | 12:17 0x3f (z1, p2; 384, 320), UINFO 90, 40:49 0x3ff | (a) ours as the table; the Conexant puts its frequency offset in 40:49, "not interpreted" by the digital modem, and ours does not |
| INFO1a, Table 10/V.90 | 12:17 0, MD 0, UINFO 79, up_sr 4, freq -3 | (in its retrain) MD 0, UINFO 78, 4, freq 0 | MD 0, UINFO 78, 4, freq 0 | MD 22 (770 ms), UINFO 90, 4, freq -5 (the line's 100 ppm) | (a) |
| INFO1c (V.34) | power red. 3-4, 3000/3200 pre-emph. 1, rates 11/13, freq -3 | - | - | power red. 0 + 3 more, pre-emph. 0, 11/12, freq -46 (line34's 1 Hz shift: sign and size right) | (a); freq. offset sign per Table 15 |
| Ja (V.90 DIL) | N 119, Lsp 124, Ltp 126, H 22..8, REF 79 | N 197, Lsp 66, H 10, REF 0 | the same as the 5686 | N 120, Lsp 18, H 2, REF 90 | (a) designs; layout the same (rebuilt bit for bit) |
| Ja (V.92) | as V.90 + rates 0x7ffff, filled to 12 | + 0x7ffff, **2092 bits: no fill to 12** | - | + 0x7ffff, filled | (a) ours per Table 20 / 8.5.4; the 5686 leaves the fill out, ours accepts it |
| CPt (V.90) | drn 9, Sr 1, ld 1, rates 0x1fff, a1 1.0, 1 constellation, **1 fill bit** | drn 16, Sr 1, ld 3, 0x07ff, a1 1.0 | drn 16, Sr 1, ld 3, 6 constellations, codec differs | drn 14, Sr 0, ld 0, 0x0fff, a1 0, 6 constellations | (a) Sr 0 (no shaping) and a1 0 are allowed (5.4.5); every real one asks for the DC null T(z) = 1 - z^-1. Conexant: Table 14 has 3 fill bits |
| CP (V.90) | (none recorded: V.92 taken) | drn 21, ack 1, 64-65 points | drn 21, ack 1, 65 points | drn 14, 25 points | (a) |
| CPt / CPu (V.92) | drn 9 / 18, Sr 1, ld 1, a1 1.0; CPt **289 bits** between repeats (no 12-fill) | drn 16 / 21, ld 3 | - | drn 14 / 10, Sr 0, ld 0, a1 0 | (a); Table 23 fills to 12 symbols, the Conexant does not between CPts, ours does |
| SUVu | no level, no wait, no silence | the same | - | the same | = |
| MP (V.34) | type 1 (precoder), max 12-13, nonlinear, rates 0x3fff, asym | - | type 1, trellis 64, shaping, 0x0ffe | type 0, 14/10, linear, 0x1ffe | (a) |
| Phase 3/4 durations (V.92) | TRN1u+Ja 2.4 s, Jd to Su 3.8 s, CPt after 2.0 s | 2.2, 2.1, 3.0 s | - | 0.92, 0.07, 1.25 s | (a) all inside 9.5.2: TRN1u >= 2040T, Su within 5 s of Jd, CPt within 5 s of Su |
| upstream level, Phase 3 (V.90, V.34 TRN) | 2.2 dB under Tone A | 6.0 under | 5.1 under | 0.2 under | (a) ours as INFO1d asks (no reduction); the real ones go lower |
| upstream level, V.92 TRN1u | 4.4 under | 2.0 under | - | 0.7 under | (a) LU is the modem's choice (3.8/V.92) |
| V.21 forced | no V.8 at all: silent through ANSam and V.22's unscrambled ones; V.21 after the answerer's mark | - | - | V.8 with CM "V.21 only", then V.21 | (a) |

Rebuilt bit for bit from the parsed fields with the caller's builders:
every CM of the 5686 and the 1998 modem and every V.34 CM; INFO0a/c 107
of 107; INFO1a 102 of 135 (the other 33: the Conexant's 40:49 of Table
18, nothing else); INFO1c 9/9; V.90 Ja 42/42, V.92 Ja 34 + 44 (the 5686's
fill); CPt, CP, CPt (V.92), CPu, SUVu, MP all equal but the fill lengths
named above. No field was read differently from what the tables say and
the values make sense for three modems of two makers (e.g. the training
Ucodes 1..118 or 116, 115, 0, 110 ...; the constellations' Ucodes
rising; the DIL's sign patterns), so the parsers' layout is confirmed from
outside, and the caller's builders give the real modems' bits.

Answering side (c): nothing the real modems would have had to tolerate.
Our answerer takes the shorter fills of the Conexant (V.90 CP: 1 bit; V.92
CPt: none) and of the 5686 (V.92 Ja) and the Conexant's 40:49; it
should.

### Found

1. **CM without the PSTN access category** (V.8 6.3, 7.3): the caller
   offers PCM and leaves access0 out. Fix (modem/orig.c, access 0: an
   analogue connection, as the real ones) and its test: branch
   `realcheck-v8`, see below.
2. **The first JM after ANSam is lost** on some line delays: the V.21(H)
   receiver reads ANSam's 2100 Hz as characters, and one in progress when
   JM begins swallows JM's ten ONEs, so its sync does not start a sequence
   and a third JM is needed (0.23 s more). test_v8orig over the clean
   line with delays 0 to 30 ms: 21 of 31 waited for a third JM. Fix
   (modem/v8orig.c: while 2100 Hz is heard the JM receiver reads idle
   mark; anstone's 5 ms block test, held two blocks): 0 of 31, CJ at most
   0.533 s after JM arrives. Branch `realcheck-v8`.
3. Both fixes move V.8's end by tens of milliseconds, and so every
   simulated call after it; with that, test_orig_v90 (0 km +100 ppm: 1626
   or 27362 downstream bit errors), test_orig_v92 (a row under its floor)
   and test_orig_v32 (the realistic row at 7200, floor 9600, at 21.1 dB)
   fail. The fault is not in the fixes: on ports-2 unchanged, moving only
   Te by 4 to 130 ms, the V.90 caller's data mode collapses (10^5 bit
   errors, the receiver's SNR from 45-50 dB to 25-30 dB within 10 s) in 10
   of 22 calls on the 0 km +100 ppm line, 1 of 13 on 2 km +30 ppm, 0 of 13
   on 6 km -100 ppm. In the bad calls there are errors from the start of
   data mode (about 1e-4), and after a few seconds the timing loop of
   modem/v90arx (fed by the decision-directed channel estimate) swings
   (delta -400 .. +2700 per block) and the errors run away. The tests
   passed because ports-2's V.8 timing happens to land in good calls. Not
   fixed here (C5's receiver; it needs its own work); the V.8 fixes stay
   on `realcheck-v8` until it is.

### New test

`test/test_realcheck.c` with `test/data/realcheck.txt` (20 kB of text: 32
sequences from 7 named recordings, as v90m-fields printed them, and where
each comes from). For each: the parser reads the fields named in the test
for that recording; the caller's builder given them gives the same bits,
CRC included (and the documented departures: the Conexant's 40:49 and
fill, the 5686's Ja fill, where ours follows the table); our INFO0a as
p2call sends it. CM, INFO0a, INFO0c, INFO1a (Tables 10 and 18), INFO1c,
Ja (V.90, V.92), CPt, CP, CPt and CPu of V.92, SUVu, MP. Portable: plain
text read with sscanf, no byte order. 652 checks, 0 failures.

### Unchanged

| | ports-2 (0f67c01) | realcheck |
|---|---|---|
| `make check` (host) | 106 count lines = ~/c52v/portsv/check.log | the same 106 lines, by diff, plus `realcheck: 652 checks, 0 failures`; every other line the same (sorted, compile lines out) but the ns timings of v34's bench |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 |
| `make bench-v92c` | cd66ac82 (host, qemu-arm) | cd66ac82 (host, qemu-arm) |
| `make bench-bell` | 48f22641, fc125ef2 | the same |

No file of the library changed (tools, test/, the Makefile).

### What the recordings cannot say

- The downstream as a client receives it: the recordings are taken at the
  box, so how the PCM downstream, JM, INFO0d / INFO1d, Jd, Jp, CPd, SUVd,
  MP and Ed of a real digital modem look, and how our caller's receivers
  take them, is not in them; the server side is always ours.
- A real answerer's ANSam, JM and Phase 2 timing (ours in all of them).
- Absolute levels: the box's port and the loop are not calibrated; only
  levels relative to the same call's Tone A compare.
- V.32bis, V.22 / V.22bis, Bell 103 / 212A and CI: no caller sent them.
  Short Phase 2, quick connect and modem-on-hold of V.92: none used them.
- Rate renegotiations and retrains begun by the caller are in a few calls
  only (v34a, x2r4, usr7's V.90 retrain); not compared here.

## realcheck-v8: the two V.8 fixes, held back (2026-11-18)

On branch `realcheck-v8`, over `realcheck`. Not to be merged as it is:
it turns three of the caller's simulation tests red, by the receiver fault
of "realcheck", Found 3 (below).

### The fixes, each test first

- CM: `modem/orig.c` adds the PSTN access category (access0 = 0, 0x0d)
  whenever the PCM category is sent (V.8 6.3, 7.3). Test: test_realcheck's
  "ours CM" against the 5686's CM: before, `m.has_access == 0, want 1`
  and no 0x0d (2 failures); after, `c1 65 13 90 2a 27 0d`, realcheck: 666
  checks, 0 failures.
- JM after ANSam: `modem/v8orig.c` holds the JM receiver in idle mark while
  2100 Hz is heard (5 ms blocks, the anstone test, two blocks of hold).
  Test: test_v8orig "CJ after the second JM, whatever the delay": the
  clean line at one-way delays 0 to 30 ms in 1 ms steps. Before: 21 of 31
  waited for a third JM, CJ up to 0.766 s after JM reached the caller;
  after: 0 of 31, at most 0.533 s. v8orig: 54 checks, 0 failures.
- Against spandsp's V.8 (an implementation not ours): `make
  test-interop` all pass with both fixes (interop_v8orig_spandsp 26, the
  V.90 digital answerer case with our CM's access octet included).
- Our caller's recorded calls read again with v90m-fields: CM `c1 65 13
  90 2a 27 0d`, every sequence rebuilt the same; the CJ now ends 0.707 s
  after the start of a 5-octet JM on line34 (two JMs; 20 ms each way
  included; was 0.94) and 0.807 s after a 7-octet one, the Conexant's
  0.673 and 0.807 s.

### What turns red, and why

Both fixes end V.8 at another moment (the CM is 33 ms longer, so the
answerer's JM starts 67 ms later; the JM fix saves a JM, 233 ms, on some
delays), and every simulated call after V.8 then meets other noise and
clock phases:

| program | ports-2 | realcheck-v8 |
|---|---|---|
| orig_v90 | 104, 0 failures | 2 failures: 0 km +100 ppm 41333 bit/s with 1626 bit errors in 10 s; "Table 1 levels" (2 km +30 ppm, robbed bit) at 33333, SNR 17.4 dB, 161670 errors |
| orig_v92 | 109, 0 | 1: "2 km +30 ppm, 3 dB pad" under its floor |
| orig_v32 | 95, 0 | 1: the realistic row (10 dB loss, 20 ms, 100 ppm) at 7200 bit/s, SNR 21.1 dB (ports-2: 9600 at 19.2 dB); floor 9600 |

The V.90 failures are the fault described in "realcheck", Found 3: on
ports-2 unchanged a shift of Te alone gives them (10 of 22 calls bad on 0 km
+100 ppm, 1 of 13 on 2 km +30 ppm). The V.32bis row sits at the edge of
9600 at that SNR; a Te shift alone on ports-2 did not move it (95, 0) at
the one shift tried. These want their own work (the V.90 receiver first);
then these fixes can go in with the tests green.

### Unchanged

| | ports-2 | realcheck-v8 |
|---|---|---|
| `make check` (host), answering side | 106 count lines | the same lines but v8orig 53 -> 54, orig_v32 / orig_v90 / orig_v92 as above, realcheck 666 added; the 45 subdirectory lines identical (run apart: `check` stops at `test`); fixtures all pass; api 303; symbols 551 internal |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 |
| `make bench-bell` | 48f22641, fc125ef2 | the same |
| `make bench-v92c` (the caller) | cd66ac82 | 66686c34, host and qemu-arm agree: the caller's own V.8 changed |
| `check-arm` (armv5, qemu; run once, on this tree) | the ARM count lines of sync-build/proof/afterports/check-arm.log | the same lines but v8orig 54, realcheck 666, and orig_v32 / orig_v90 / orig_v92 with the host's failures; v34, v90, v92, v42, v32 all pass, identical |
| v90d-split's live/replay, 106 recordings x 8 configs | 1613 files (sync-build/proof/afterports) | manifest identical, all 1613 (a copy of v90d-split d707463 built against this tree, uncommitted fixes in; rc-scripts/replayproof.sh) |

## robust 2a: the V.90 caller's data-mode collapse, and V.32bis's rate (2026-11-18)

Branch `robust`, from `realcheck-v8` (848f47a). Harnesses (not in the
repository): ~/rb-tmp/t (`ts.c` a Te sweep of one line, `tr.c`
the receiver's decisions against the octets the answerer sent, `tr4.c` /
`tr5.c` a least-squares channel fitted on the true symbols, `dil.c` the
DIL's levels, `sweep.sh` / `sweep92.sh` every line of test_orig_v90 /
test_orig_v92 with its seed, Te 1000 to 1130 ms).

### Test first

- `test/test_orig_v90_te.c` (new): test_orig_v90's 0 km +100 ppm line (seed
  21), 2 km +30 ppm (seed 24) and 2 km -50 ppm A-law (seed 31), each with
  Te = 1000, 1006 ... 1126 ms (22 calls); every call must reach data mode,
  meet the line's floor and carry 10 s both ways without a check failure.
  On realcheck-v8: 3 checks, 2 failures (15 of 22 calls bad on 0 km, 0 of 22
  on 2 km +30, 14 of 22 on A-law). ~16 s on the host; TEST_SHORT 4 per line.
- `test/test_orig_v32.c`: the realistic line again with Te = 1000 ... 1130
  ms (14 calls), each at 9600 or more and error free. On realcheck-v8: 96
  checks, 2 failures (the row at 7200; 5 of 14 calls at 7200 or 4800).

### What was wrong: three faults

1. **The DIL's small Ucodes** (modem/v90dil). The receiver's decisions in
   the bad calls against what the answerer sent: every early error (the
   1e-4) was one point, the smallest of one interval (e.g. slot 4, Ucode 1:
   the receiver's level 158 units, its true level 8; sign errors). The
   DIL's means of Ucodes 0 to 6 were off by up to 160 units on 0 km (Ucode
   0 at 105..131, Ucode 1 at 120..167, falling to normal by Ucode 7), not
   snapped (where the codec's step is under 32 units the mean stands), and
   CP took such a Ucode as its smallest point whenever its false level
   cleared d/2. The cause: the segment of Ucode 0 followed that of Ucode
   119; what the equaliser leaves of fifteen symbols at the largest level
   lies on the next 126 symbols (segments 0..6) and, the DIL repeating
   every pass, does not average out. The Te shift only decided whether a
   false level was picked. Fix: the DIL in the order 0 2 4 ... 118 119 117
   ... 3 1 (no segment after a much larger one). With it the levels of
   Ucodes 0..11 on 0 km are 1..98, rising, as Table 1 has them.
2. **The band edge** (modem/v90arx). With (1) alone 16 of 308 calls (0 km,
   +100 ppm, 7 seeds x 44 Te) still collapsed, four of five at the same
   moment of the clock drift (1.507 s into data mode). The equaliser's input
   fitted against the true symbols: its residual grows from 5 to 50 units
   at 4 kHz within 30 ms, and sliding fits show the channel at 4 kHz going
   from -20 to -70 dB and back every 1.25 s (3.9 kHz: -17 to -22 dB): the
   image of the band above 4 kHz folding on to the band below it with a
   phase that turns with the clocks (C5-1's finding). The MMSE equaliser,
   computed from an estimate 64 ms old, amplified that band as if it stood
   still (+12 dB over mid-band at Nyquist): errors on alternate symbols,
   then the decision directed estimate and the timing loop followed them
   (delta to -2500, total loss; the receiver never came back). Fix:
   `v90arx_edge()`: the image taken into the design as interference of
   unknown phase, a quarter of the power the estimate has at 3828, 3875,
   3922 and 3969 Hz, each over its 46.875 Hz (R += sum w P_k cos(w_k d)).
   v90a turns it on at Ri-bar (TRN2d, which CP is designed on, included);
   the DIL is measured with the plain equaliser (with it during the DIL the
   pad lines' levels came out worse: 6 dB pad's gain search picked the
   wrong codeword). Tried and dropped: a fixed triangle at Nyquist (costs
   rate on every line, breaks the pad lines).
3. **The estimate's regressor while held** (modem/v90arx): `sb[]`, the
   symbols the channel estimate is computed against, was written only when
   not held, so after every hold (Jd' to two DIL passes; Ri for 128
   symbols) the estimate resumed with up to 128 stale symbols in its span:
   its residual jumped (63 -> 1455) and the timing loop swung (delta -900
   during TRN2d) while TRN2d's noise, which CP is designed on, was being
   measured. Now written always.

V.32bis (orig_v32, realistic line, 20-22 dB): of 14 Te, 3 took 7200 and 2
took 4800; ports-2 the same line: 3 at 7200 and 2 that never reached data.
Not a row on its floor: a caller weakness, two of them.
4. Our receiver took the end of the answerer's AC for S (sim: in S mode for
   130 symbols, then the 16 T of silence before the real S, whose start
   passed for S-bar): it trained on S, read R1 with an error of 25000 and
   offered 4800 only in R2. Fix (v32_rx, calling role only: `!r->answer`):
   four symbols with next to nothing on time in S mode and it was not S;
   look for S again.
5. Our TRN ended at 1280 T + the round trip (the echo canceller had
   converged), but it also trains the answerer's receiver, which picks R3 by
   its error at the end of it: R3 at 7200 where its error in data mode
   allows 9600. Fix (v32_call): the caller's TRN at least 2560 T (6.1
   allows 8192); calls reach data 0.1 to 0.5 s later.

### After

`test_orig_v90_te`: 3 checks, 0 failures (0 of 22 on each line; 0 km at
45333 .. 46666, was 40000 .. 45333). `test_orig_v32`: 96, 0 (9600 on all 14).

Te sweeps, every line of the suites with its own seed, Te 1000 .. 1130 ms in
5 ms steps (27 calls a line; bad = any check failure or no data mode):

| line | V.90 before | V.90 after | V.92 before | V.92 after |
|---|---|---|---|---|
| 2 km, 0 ppm | 0, 52000 | 0, 50666..52000 | 0, 52000 | 0, 50666..52000 |
| 0 km, +100 ppm | **23**, 40000..45333 | 0, 45333..46666 | 2, 37333 | 0, 38666..48000 |
| 6 km, -100 ppm | 0, 46666..48000 | 0, 46666..48000 | 0, 42666..45333 | 0, 46666 |
| 4 km, +50 ppm, -65 dBm0 | 0, 40000..41333 | 0, 38666..40000 | 0, 38666..40000 | 0, 38666..40000 |
| 2 km, 3 dB pad | 0, 44000..48000 | 0, 44000..48000 | 0, 42666..48000 | 2 (up), 41333..48000 |
| 2 km, 6 dB pad | 0, 30666..40000 | 0, 40000..45333 | 0, 41333..45333 | 0, 44000..45333 |
| 2 km, robbed bit | 1, 45333..48000 | 0, 46666..49333 | 0, 44000..45333 | 0, 41333..46666 |
| 2 km, 6 dB pad, robbed bit | 1, 30666..42666 | 0, 29333..44000 | 10, 30666..44000 | 10 (up), 29333..45333 |
| 2 km, -50 ppm, A-law | **17**, 45333..46666 | 0, 46666..49333 | 0, 46666 | 0, 46666..48000 |

V.90: 42 of 243 calls bad, now 0. V.92: 12 of 243, still 12, all upstream
errors on the pad lines (2b). 0 km +100 ppm, 7 seeds x 44 Te: 0 of 308.

Floors moved, on that evidence (a row is one draw of its line's spread):
test_orig_v90's "6 dB pad, robbed bit" 40000 -> 28000 (29333 .. 44000
with Te alone, 30666 .. 42666 before these changes), test_orig_v92's "3 dB
pad" 45333 -> 40000 (41333 .. 48000; before 42666 .. 48000: five of 27
now at 41333, the DIL's levels under a pad, see 2b). The pad lines' spread
is the DIL's accuracy with pads and robbed bits (level biases of 45 units,
a codeword, on some points; the timing loop walking after the holds): 2b.

realcheck-v8's failures: orig_v90 104 checks 0 failures (0 km +100 ppm at
46666, SNR 52.8), orig_v92 109, 0, orig_v32 96, 0.

### Unchanged

| | realcheck (~/c52v/rcv/check.log) | robust 2a |
|---|---|---|
| `make check` (host), 107 count lines | | the same lines, by diff, but orig_v32 95 -> 96, v8orig 53 -> 54 and realcheck 652 -> 666 (realcheck-v8), orig_v90_te added; every answering-side line identical; the subdirectories (v34, v90, v92, v42, v32) identical |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 |
| `make bench-bell` | 48f22641, fc125ef2 | the same, host and qemu-arm |
| `make bench-v92c` (the caller) | 66686c34 (realcheck-v8) | ff7367a8, host and qemu-arm |
| `make bench-v90c` (the caller) | | 695b27e5, host and qemu-arm |
| `check-arm` (armv5, qemu; once, on this commit's tree) | sync-build/proof/afterports/check-arm.log's library lines | all pass; the same lines but orig_v32 96, v8orig 54, realcheck 666 (realcheck-v8), orig_v90_te 3 (TEST_SHORT: 4 Te per line), by diff |

Answering side: no file of it changed (v32_rx's change is under `!r->answer`;
v32_call, v90arx, v90dil, v90a are the calling side's). `v32rx_t` has one
int more (`s_lost`), which `answer_t` holds through its union.

## robust 2b: the rest of the simulation failures (2026-11-18)

### Test first

`test/test_orig_seeds.c` (new): every line of test_orig_v90 / test_orig_v92
with seeds 100 .. 105 (C6's extra seeds), V.90 and V.92: 108 calls, 10 s of
data each way (~50 s on the host; TEST_SHORT one seed). Every call: data
mode on both sides and kept, >= 28000 down (V.92: PCM upstream, >= 24000
up), the downstream without a check failure, the upstream likewise but for
one documented exception (below). On robust 2a: 3 checks, 2 failures:

- V.90 1 of 54: 3 dB pad, seed 103: the receiver lost (217000 failures);
- V.92 4 of 54, all on 6 dB pad + robbed bit: seeds 100, 103, 105 the
  answerer gave B1u up and retrained (we stayed in "data" until the carrier
  check ended the call 10 s later: 2c), seed 104 2389 upstream failures.

(On ports-2, C6 counted 6 of 54 for V.92; 2a fixed the two 0 km ones.)

### What was wrong

1. **The DIL's snapping under a pad** (modem/v90dil). The receiver's
   decisions against the octets sent: the 3 dB pad call lost after
   repeated errors on one point (slot 2, Ucode 69: level 2628, true 2743,
   one codeword off); one wrong decision there started an avalanche through
   the feedback (dense constellation, a feedback tail of norm 1.1) from
   which a decision directed receiver does not come back. The DIL's raw
   means against the true levels (data mode, 2 km, 3 dB pad): a gain of
   their own per interval, -0.4 .. -0.8 % in intervals 0..2, +0.3 .. +0.9 %
   in 3..5 (the segment positions next to the REF symbols or not), and one
   common gain was used to pick each mean's codeword: at a pad's spacing
   that picked a neighbour now and then. Fix: the codeword is picked with
   the interval's own gain (least squares over its Ucodes 64..127, twice,
   within 3 % of the common gain), the level written is the common gain's
   (data mode has no such bias). Snapped levels within 12 units of the true
   ones on that line (were off by a codeword on some points).
2. **TRN1d with UINFO robbed** (modem/v90arx). UINFO 90 is even so that a
   robbed bit leaves it alone, but behind a 6 dB pad it is codeword 75,
   which a robbed bit changes: in one interval in six TRN1d's symbols are
   not +-L. The channel estimate, fitted against +-L everywhere, carried
   that into every tap (TRN1d's error 42 dB where the other lines show
   21..24), the timing loop followed the estimate 0.03 sample away and back
   during TRN2d, and V.92's transmitter, on that clock, with it: the digital
   modem lost B1u (it retrained) or had upstream errors. Fix (`ls_set`):
   over TRN1d's estimator steps 700..1300 the residual is projected on each
   interval's part of the prediction, each interval's UINFO level set from
   it (within 25 % of L, left at L within 1 %), the reference and the
   symbols in the estimate's span take it; the estimate has 400 more steps
   to settle. TRN1d's error on that line 42 -> 33 dB, the timing walk
   during TRN1d gone.
3. **The timing loop during the DIL** (modem/v90a, V.92 only). Even so the
   loop walked 0.01 sample during the DIL on the pad / robbed bit lines
   (the DIL's repetitive patterns on levels still being measured pull the
   estimate) and came back during TRN2d, moving our PCM upstream while the
   digital modem measured it for CPd. V.92's clock comes from L2 (within
   0.1 ppm, C6-2): `v90arx_timing_hold()` holds the loop (step kept, no
   phase correction) from the DIL to Ri-bar. V.90 is left as it was: its
   clock comes from Sd (up to 1.6 ppm off) and nothing follows the loop
   (holding it there cost V.90 rates when tried).
4. **The v90arx input and Ucodes 122..127**: measured, not changed. Runs of
   Ucodes 116..127 in the DIL's sign pattern sent on 0 km, +100 ppm (test
   line, the A/D at the line's -6 dB): the A/D peaks 22716 (116) .. 32599
   (121) and clips from 122; the receiver's own 16-bit input (after the AGC,
   Sd at 4096 RMS, gain 1.28 there) clips from 125. Neither is reachable:
   the DIL stops at 119, CP uses measured Ucodes only, Rd is CP's largest
   point, every other downstream sequence is UINFO; data mode peaks at
   9200 .. 13500 (7.7 dB of headroom at the A/D). Taking the AGC below unit
   gain would move every fixed-point scale of the receiver for no reachable
   case; left, documented here.

### After

test_orig_seeds: 3 checks, 0 failures; V.90 0 of 54 bad, V.92 0 of 54 bad,
2 of 54 with an upstream burst (pad6rb seed 102 at 37333 up: 147 failures;
robbed bit seed 105 at 37333: 88). The C6 extra-seed table (54 calls, V.92):
6 bad on ports-2, 5 on 2a, 0 now (+ 2 bursts).

The Te sweeps again (27 calls a line, as in 2a):

| line | V.90 2a | V.90 2b | V.92 2a | V.92 2b |
|---|---|---|---|---|
| 2 km, 0 ppm | 0, 50666..52000 | 0, 50666..52000 | 0, 50666..52000 | 0, 49333..52000 |
| 0 km, +100 ppm | 0, 45333..46666 | 0, 45333..46666 | 0, 38666..48000 | 0, 45333..48000 |
| 6 km, -100 ppm | 0, 46666..48000 | 0, 46666..48000 | 0, 46666 | 0, 46666 |
| 4 km, +50 ppm, -65 dBm0 | 0, 38666..40000 | 0, 38666..40000 | 0, 38666..40000 | 0, 38666..40000 |
| 2 km, 3 dB pad | 0, 44000..48000 | 0, 44000..48000 | 2 (up), 41333..48000 | 0, 44000..48000 |
| 2 km, 6 dB pad | 0, 40000..45333 | 0, 40000..45333 | 0, 44000..45333 | 1 (up 81, at 37333), 44000..45333 |
| 2 km, robbed bit | 0, 46666..49333 | 0, 46666..49333 | 0, 41333..46666 | 0, 44000..46666 |
| 2 km, 6 dB pad, robbed bit | 0, 29333..44000 | 0, **38666..45333** | 10, 29333..45333 | 1 (up 32, at 34666), 40000..45333 |
| 2 km, -50 ppm, A-law | 0, 46666..49333 | 0, 46666..49333 | 0, 46666..48000 | 0, 46666..48000 |

V.92: 12 of 243 -> 2 of 243 (both one upstream burst). With the pad
lines' spread narrowed, 2a's floors go back up: test_orig_v90's "6 dB
pad, robbed bit" 28000 -> 37333 (38666 .. 45333), test_orig_v92's "3 dB
pad" 40000 -> 42666 (44000 .. 48000).

### Left open: V.92 upstream bursts

At the rates the digital modem's CPd picks at the top of a line (34666 to
37333 here) its PCM upstream receiver sometimes takes one symbol wrong: a
burst of 30 to 50 check failures (about 10 to 17 bits), recurring about
every 4.5 s on such a call (17.54 s and 22.01 s on robbed bit seed 105),
4 calls of 243 + 108. Traced: our transmit timing against the network is
steady to 0.0005 sample through the burst; the harness's upstream FIFO
neither under- nor overflows; freezing our timing in data mode makes it
far worse (drift). The answerer's receiver does not adapt in data mode
(v92_p34d: v92_trn_store) and picks the rate: answering-side code, not
touched here. test_orig_seeds counts such calls and lets them pass while
they stay bursts (under 200 failures in 10 s).

### Unchanged, and the end-of-step checks (on this commit's tree)

| | reference | robust 2b |
|---|---|---|
| `make check` (host) | ~/c52v/rcv/check.log | the same count lines, by diff, but orig_v32 95 -> 96, v8orig 53 -> 54, realcheck 652 -> 666 (2a, realcheck-v8), orig_v90_te 3 and orig_seeds 3 added; every answering-side line and the subdirectories identical |
| `check-arm` (armv5, qemu, once) | 2a's | all pass; the same lines plus orig_seeds 3 (TEST_SHORT: seed 100, 18 calls, 0 bad, 0 bursts), by diff |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 |
| `make bench-bell` | 48f22641, fc125ef2 | the same, host and qemu-arm |
| `make bench-v92c` (the caller) | ff7367a8 (2a) | 506f2bcc, host and qemu-arm agree |
| `make bench-v90c` (the caller) | 695b27e5 (2a) | 695b27e5, host and qemu-arm |
| v90d's replay, 106 recordings x 8 configs (rc-scripts/replayproof.sh, v90d-split d707463's copy built against this tree) | sync-build/proof/afterports/manifest.sha256 | MANIFEST IDENTICAL, 1613 of 1613 |
| `make check-ubsan` | | ALL UBSAN CHECKS PASS, no runtime error |
| `tools/check-ports.sh` i686-musl, aarch64be-musl, mips-musl, win32-mingw, host-clang (PAR=5, outputs under ~/rb-tmp/ports) | host gcc | ALL PORTS PASS: 109 of 109 programs identical on each |

No answering-side file changed in 2b (v90arx, v90dil, v90a are the
calling side's). The ubsan, port and replay runs were made on a copy of
this tree (~/rb-2b, `diff -r` identical but PROGRESS.md) while
check-arm ran in the worktree.

## robust 2c: retrains on the calling side (the design, as written before the step)

Left for its own step (2a and 2b took the branch's time). What is there:
V.32bis (v32_call: 7.1 / 7.2 both ways, test_orig_v32 checks both) and
V.34 (v34orig: 11.5.1.1 / 11.5.1.2 both ways, test_orig_v34) already
retrain from either end; V.90 and V.92 do not: `v90a_retrain()` and
`orig_retrain()` refuse in V.90 / V.92 (`v90m_retrain` gives V90M_ESTATE in
the calling role), an answerer's retrain (70 ms of silence, then Tone B,
answer.c ANS_RETRAIN) is taken for the downstream lost and ends the call
(V90A_ERR_CARRIER, ~0.5 s of input 12 dB under, or 10 s after a B1u the
answerer gave up), and the receiver does not ask for one when it degrades.
The digital modem's renegotiation is followed (9.6.2.2 / 9.8.2.2).

Design:
- Detecting the answerer's retrain in data mode or Phase 3/4: the PCM
  downstream silent (the 70 ms; v90arx's input power, as the loss check
  but over ~40 ms) followed by Tone B (2400 Hz, p2call's tone detector):
  go to Phase 2 of a retrain (V.90 9.5.1.2 / V.92 9.5: p2call's V.90 mode
  started at "after Tone B": Tone A from us, INFO0 not exchanged, INFO1d /
  INFO1a, V.92's Table 17/18 forms when both indicated V.92 at the start),
  then v90a's Phases 3 and 4 again from Ja with a fresh v90arx (the DIL
  measured again) and, for V.92, a fresh pcmtx / echo canceller training on
  TRN1u.
- Ours (9.5.1.1): 70 ms of silence, Tone A, the same Phase 2. Triggers:
  `orig_retrain()` (API), and the receiver: v90arx's error over 1 s more
  than 6 dB above what CP was designed for, or the self-check of the
  descrambled data (Rd never ambiguous), or decision errors in Rd / B1d.
  A limit (2 in a minute, as answer.c's rt_recent) before giving up.
- Our rate renegotiation (9.6.2.1 V.90 / 9.8.2.1 V.92): Rd-equivalent from
  the analogue side is CPs (CP with the request for silence) on V.90, Ru on
  V.92; then CP / CPu designed on the current noise. A first step that
  would also give the receiver a way down without a retrain.
- orig_t has room: the V.90 retrain reuses p2call and v90a in place.
- Tests, failing first: test_orig_v90 / v92: `answer_retrain()` mid-call
  (today: the call ends), a line change mid-call through test/v90link.h
  (level drop 10 dB, noise +20 dB: today the receiver collapses and the
  call ends 0.5 .. 10 s later) that must lead to our retrain and data again,
  renegotiation both ways; test_api: `v90m_retrain` in the calling role
  V90M_OK (today V90M_ESTATE, which test_api asserts); then README /
  include/v90modem.h on what the calling role does.

## robust 2c: retrains and rate renegotiation, V.90 / V.92 calling side (2026-11-18)

Branch `robust`, from robust 2b (8fb789c). Harnesses (not in the
repository): ~/rb-tmp/t2 (`dbg.c` one case with the caller's
and the answerer's states, the receiver's error against the design noise
every 250 ms; `ed.c` that ratio over 10 s of data on every test line).

### Test first

- `test/test_orig_retrain.c` (new): the library's caller against the
  library's answerer, 2 km, V.90 and V.92; each case to data mode, 2 s of
  data, the event, the time until data moves both ways again, then 5 s that
  must pass the check without a failure, V.92 staying V.92: the answerer
  retrains (answer_retrain), we retrain (orig_retrain), noise +15 dB (down,
  then back up when it goes), the level -10 dB, a 6 dB pad put in, our
  renegotiation with a ceiling of 40000 and back up without it, the
  answerer's renegotiation (p34d / v92d), the receiver wiped (channel
  estimate and equaliser zeroed: a total collapse), a 300 ms noise burst at
  -10 dBm0. Then the limits: three retrains in a minute, the fourth refused,
  the receiver lost after them a cleardown; a retrain never answered ends
  the call. On robust 2b: **194 checks, 91 failures** (every case but the
  answerer's renegotiation: the call ended on the answerer's retrain or a
  V.92 line change, the receiver stayed lost on the others, orig_retrain /
  orig_renegotiate gave -1; the later checks, V.92 staying V.92, who began
  the retrain, the cleardown past the limit, use fields 2b has not).
  ~13 s on the host; TEST_SHORT: V.90 and its limits.
- `test/api/test_api.c`: the V.90 and V.92 caller blocks ask for a retrain
  (V90M_OK, a second at once V90M_ESTATE), wait for data again (retrains 1,
  DATA_STOPPED, rates, no renegotiation counted), then clear down. On robust
  2b: **302 checks, 10 failures**. Then a block (it needs the new end code,
  so not run on 2b): a retrain the answerer never answers ends the call with
  V90M_END_RETRAIN_FAILED and V90M_EV_ENDED.
- test_orig_v90 / test_orig_v92 / test_v90a / test_v92a asserted the
  refusal (-1); those checks now assert no retrain on a clean start-up (the
  counts stay: 104, 109, ...).

### What was built (modem/v90a, modem/p2call, modem/orig, api)

1. **Phase 2 of a retrain** (`p2call_init_v90_retrain`): Tone A at once,
   no INFO0 (the answerer's p2d_init_retrain sends none, V.92 too, though
   9.7 says "the full Phase 2"; not ours to change), the first INFO0d kept;
   9.2.2.1.3 on as at the start, V.92's INFO1d / INFO1a forms as then.
2. **Retrains** (9.5.2 / 9.7.2): the answerer's Tone B (modem/tonedet on
   what our echo canceller leaves, from Phase 3 on: data mode, Phases 3 and
   4, renegotiations) or ours: 70 ms of silence, Phase 2 as above, then
   Phases 3 and 4 with everything fresh (v90a re-initialised in place:
   receiver, DIL, canceller, V.92's pcmtx) but the call's counts, clock and
   first INFO0d. `v90a_retrain()` / `orig_retrain()` from data mode or a
   renegotiation. A renegotiation without Ed (9.6.2), and a Phase 3 / 4 that
   fails once the call has carried data, are retrains too; a retrain's
   Phase 2 that fails ends the call.
3. **Our renegotiation** (9.6.2.1: S, S-bar, SCR until the digital modem's
   TRN2d has shown 4000 symbols of noise, then CP designed on it; 9.8.2.1:
   Ru, Ru-bar, TRN2u, SUVu, a CPu designed the same way), `v90a_renegotiate`
   / `orig_renegotiate(o, ceiling)`, the ceiling never above the
   configured one. Found on the way: V.92's SUVd / CPd / acknowledge / Ed
   flags of Phase 4 were still set when our Ru went out, so we sent E2u at
   once and the answerer failed B1u (v92d ERR_B1, a retrain, V.90 after it):
   cleared at the start of ours. Not CPs (the request for silence): our
   canceller needs no new training for a renegotiation.
4. **The receiver's watch** (once a data frame): the sum over the six
   intervals of v90arx's error against the noise CP was designed for (twice
   TRN2d's). Measured (`ed.c`, 72 calls on the test lines, 4 seeds): 0.20 to
   0.97 of it in data mode, max over 50 ms. The design said 6 dB over: by
   then the decision feedback is already going (noise +15 dB on 2 km: the
   ratio sits at 4, 0 errors for 2.5 s, then an avalanche, the error 10^5
   over). So: 3 dB over for 1 s -> a renegotiation down when only the noise
   rose, a retrain when the equaliser's input power moved 3 dB (level, pad:
   the DIL no longer holds) or a renegotiation for the line ended within 5 s;
   12 dB over for 250 ms (the receiver lost) -> a retrain, V.92's timing loop
   held meanwhile (our PCM upstream runs on its clock: unheld, the wiped
   receiver dragged it until the answerer's design failed and it retrained
   with PCM upstream off), and Rd not believed meanwhile (a lost receiver
   took garbage for Rd). 6 dB under for 5 s after the watch took the rate
   down -> a renegotiation back up (once). On those 72 calls: 0 retrains, 0
   renegotiations.
5. **Limits** (as answer.c's): 3 retrains without a minute of data mode
   between them (one forgotten per minute). Past it: orig_retrain gives -1;
   the answerer's Tone B, or a retrain that fails, ends the call; one our
   receiver needs is a cleardown (CP / CPu with drn = 0). Either way
   V90A_ERR_RETRAINS -> ORIG_END_RETRAIN (8) -> V90M_END_RETRAIN_FAILED
   (appended to v90m_end) with V90M_EV_ENDED.
6. orig: orig_retraining / orig_retrains for V.90 / V.92; include/v90modem.h
   and README say what the calling side does now.

### After

test_orig_retrain: 222 checks, 0 failures. Time from the event to data both
ways again, downstream / upstream before -> after:

| case | V.90 | V.92 |
|---|---|---|
| the answerer retrains | 7.4 s, 52000/28800 -> 48000/28800 | 10.6 s, 49333/34666 -> 52000/32000 |
| we retrain | 7.4 s, 50666 -> 50666 | 8.3 s, 50666/36000 -> 46666/36000 |
| noise +15 dB | 2.0 s, renegotiation, 46666 -> 36000; noise gone: 6.1 s, back up to 49333 | 10.2 s, renegotiation then retrain, 48000 -> 36000; back up to 45333 in 8.3 s |
| level -10 dB | 7.6 s, retrain, 48000 -> 36000 (later up to 41333) | 10.1 s, retrain, 46666 -> 40000 |
| 6 dB pad put in | 7.6 s, retrain, 52000 -> 45333 | 9.4 s, retrain, 46666 -> 45333 |
| our renegotiation, ceiling 40000 | 1.0 s, 52000 -> 40000; no ceiling: 1.0 s, 52000 | 4.0 s, 52000 -> 40000; 4.8 s, 52000 |
| the answerer renegotiates | 0.4 s (as before) | 4.8 s (as before) |
| receiver wiped | 7.6 s, retrain, 50666 -> 50666 | 10.6 s, 48000 -> 50666, PCM upstream kept |
| noise burst 300 ms | 7.6 s, retrain, 48000 -> 52000 | 9.3 s, 46666 -> 48000 |

Retrain limits: three in a row 7.4 s each (V.90), 9.8 to 10.7 s (V.92);
the fourth refused; lost after them: cleared down in 0.9 s, the answerer
told (cleared 1); never answered: ended 8.1 s after the request, silent.
test_api: 326 checks, 0 failures (V.90 retrain by API 7.3 s, V.92 8.9 s).

Per-tick work added: the Tone B watch, one tonedet_tick per sample from
Phase 3 on (two multiplies, three accumulates; every 80 samples a block
decision); the watch once a data frame (two six-term sums). A retrain
re-initialises v90a_t in place (one memset of it). Nothing heavy; costs are
step 3's.

### Unchanged (by diff)

| | reference | robust 2c |
|---|---|---|
| `make check` (host) | ~/c52v/robv/check.log | the same count lines, by diff, but orig_retrain 222 added and api 303 -> 326; every answering-side line and the subdirectories identical; orig_v90 104, orig_v92 109, orig_v90_te 3, orig_seeds 3 as they were |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 |
| `make bench-bell` | 48f22641, fc125ef2 | the same, host and qemu-arm |
| `make bench-v92c` (the caller) | 506f2bcc (2b) | 506f2bcc, host and qemu-arm (unchanged: nothing in these calls retrains) |
| `make bench-v90c` (the caller) | 695b27e5 | 695b27e5, host and qemu-arm |
| v90d's replay, 106 recordings x 8 configs (rc-scripts/replayproof.sh on a copy of this tree, ~/rb-2c) | sync-build/proof/afterports/manifest.sha256 | MANIFEST IDENTICAL, 1613 of 1613 |
| `make check-ubsan` (copy ~/rb-2c-u) | | ALL UBSAN CHECKS PASS, no runtime error; it found one on the way (below) |
| `tools/check-ports.sh` (copy ~/rb-2c-p, PAR=5, outputs ~/rb-tmp/ports2c) | host gcc, 110 programs | aarch64be-musl, mips-musl, win32-mingw, host-clang: 110 of 110 identical; i686-musl: the full-length run stopped by the coordinator an hour into test_orig_seeds, run instead with -DTEST_SHORT=1 against a host reference built the same way (~/rb-tmp/short), checked by the coordinator |
| `check-arm` (armv5, qemu, TEST_SHORT, once) | 2b's | all pass; the same lines plus orig_retrain 111 (TEST_SHORT: V.90 and its limits) and api 326, by diff |

ubsan's find: `v90arx`'s LMS step on the forward taps (`f[i] -= ...`) and
the feedback taps overflowed int32 once the receiver was wiped (the taps of
a lost receiver run to the end of the range). Both now wrap through
uint32 (defined; the same values whenever nothing overflows, so every
output above is unchanged); the retrain follows anyway.

No answering-side file changed: api/v90modem.c's change is the
originating end code, include/v90modem.h's an appended enum value and
comments; modem/p2call, orig, v90a, v90arx are the calling side's.

### Open

- The answerer drops PCM upstream for the rest of the call when it
  retrains on its own (answer.c: `retrain_ours && !rt_asked`); when its
  check of our upstream fires before our receiver's watch does, a V.92
  call goes on as V.90 after the retrain. The watch's 250 ms collapse rule
  and the held timing loop make that rare in the tests (the wiped and
  burst cases stay V.92); answering side, not touched.
- 9.7/V.92 asks for "the full Phase 2" in a retrain; the answerer sends no
  INFO0 (p2d_init_retrain), and so neither do we. Against a real V.92
  digital modem that sends INFO0d in a retrain, P2L_A_INFO0 would take it
  (got0d is set, a frame is parsed) but this is untried.
- No public `v90m_renegotiate`: our renegotiation is internal
  (orig_renegotiate) and automatic.
- The watch's thresholds (3 dB over for 1 s, 12 dB for 250 ms, 6 dB under
  for 5 s) come from the simulator's lines only.
- V.92 upstream bursts (2b) unchanged.

## answer-fix 0: check-arm runs its programs in parallel (2026-11-19)

Branch `answer-fix`, from `robust` (6d8e259). The user's standing rule:
verification is to be quick. `make check-arm` (and the subdirectories'
`test-arm`) ran their qemu programs one after the other; now
`tools/par-run.sh` runs them `PAR` at a time (default 8), each one's
output (stdout and stderr) to `$(BUILD)/arm_log/<name>.log`, and prints
the logs in the order of `TESTS`, each after its `[arm] name` line, as
before; the exit status says whether any failed. The builds were already
parallel under `make -j`. Host `make check` is unchanged (its programs
print straight to the terminal).

Run once on this tree (`make -j16 check-arm`, armv5, qemu-arm-static):
all pass, and the count lines are those of robust 2c's
(~/rb-tmp/check-arm-2c.log), by diff (test-api-arm's line
comes first now: it runs beside test-arm under -j). Wall time 47 min, on a
host busy with other runs: the last programs to finish were orig_seeds (33
min), orig_bell (36) and orig_v92 (41), then the subdirectories.

## answer-fix 1: the V.92 upstream bursts (answering side) (2026-11-19)

robust 2b left open: at the rates CPd picks at the top of a line (34666 to
37333) the digital modem's PCM upstream receiver had bursts of 30 to 150
check failures, 2 calls of test_orig_seeds' 54 V.92 calls and 2 of the Te
sweep's 243. The user allowed the answering side to change for it.
Harnesses (not in the repository): ~/afix-tmp/h (`er*.c` the
receiver's input against what our transmitter sent, symbol by symbol;
`lsq.c` a least-squares fit of the error on v(n); `ex.c` / `ups.c` one call,
the upstream failures after 11 s; `ts.c` / `sweep92.sh` the Te sweep), and
an instrumented copy of the tree (~/afix-tmp/inst: our
transmitter's symbols recorded, the baseline rule switchable).

### Test first

`test/test_orig_seeds.c`: the exception for upstream bursts (under 200
failures in 10 s passed as "open") is gone; every call must be clean both
ways. On robust 2c: 3 checks, 1 failure (V.92 2 of 54 bad: robbed bit seed
105, 45 upstream failures at 37333; 6 dB pad + robbed bit seed 102, 147 at
37333).

### What was wrong

- **Not the clock.** The ~4.5 s period of the robbed bit seed 105 call is
  near the 4.17 s in which a 30 ppm line slips one sample, and its three
  bursts (17.54, 22.01, 25.89 s) did fall within 0.05 of a sample of the
  slip. But on pad6rb seed 102 (also +30 ppm) the five bursts sit at 0.46,
  0.79, 0.76, 0.33, 0.28 of it, and over 14 s of data the receiver's
  error, its symbol error rate and its large errors are flat against the
  clocks' phase (20 bins). Holding our transmit step at the exact clock
  ratio changes nothing (SER 2.0e-3 -> 2.3e-3). A coincidence of three.
- **The raw error.** Our transmitter's symbols against the receiver's
  slicer: 2.0e-3 symbol errors before the trellis (one every 60 ms), all
  one point off; error RMS 9.7 at the small points (Gaussian, kurtosis
  2.9) where the design assumed 7.6 (kappa 7 at 53 units' spacing). Of the
  excess, a linear fit on v(n) over +-240 symbols takes 9.7 -> 6.6: the
  residual interference of a channel estimate that is not quite the
  channel, mostly at lags 90 .. 130 from the symbol. The trellis
  carries that: what turned errors into bursts was the baseline.
- **The baseline follower** (v92_p34d, reading 63: an eighth of each
  decision's error, for the wander a real U.S. Robotics 5686 showed). Each
  burst began where the baseline had moved: at 17.52 s an error of 127 at
  the point 4220 (+-128 units off there in about half the cases: the
  codec's step, nothing to do with the baseline) moved it by 16 units at
  once, a third of the smallest spacing (53); at 21.99 and 25.88 s errors
  near the decision boundary moved it by 30 to 40 units over a few ms;
  with the baseline that far off, decisions at the small points went wrong
  in runs (each wrong decision moves it further the wrong way), and the
  trellis gave up. With the
  follower off the same calls have 3.6 times fewer symbol errors (2.25e-3
  -> 6.2e-4) and no failures, but the follower is needed for the wander
  (test_v92call's 25d: 25 Hz, 100 units: 47981 bad bits without it).

Variants tried (two burst calls over 40 s, and test_v92call 25d): clipping
the error at 16 / 32 / 64 units (73 .. 197 failures left); ignoring points
beyond 1000 (0 failures on the burst calls, 38328 bad bits on the wander);
weighting by noise / (noise + q) (the wander fails: the large points no
longer follow it); scaling by 2 noise / q only where q > 2 noise (95
failures left on seed 102 at 40 s); that and the taper below (0, 0, and the
wander clean). Only the combination held all three.

### The fix (v92/v92_p34d.c, `base_step()`)

The baseline's step for a decision is still an eighth of its error, less
where the error says little about the baseline:
- where the codec's quantising variance q at the decided point is more than
  twice the line's noise (TRN1d's), scaled by 2 noise / q;
- where the error is beyond a quarter of the spacing on its side (the
  decision may be wrong), falling linearly to nothing at half the spacing.
With a linear codec (`rx_linear`) the step is the plain eighth, as before:
no quantiser there, and on v90d's 106 real recordings (all of them linear
where V.92 data mode is reached) the taper changed the modulus decoder's
flagged frames both ways (8 recording x configuration pairs one more, 15
one fewer; no decoded bit changed) and raised the receiver's error by 0.03
dB: not taken where it was not shown to help. Cost: two binary searches a
symbol (the point's index, the codec step), with a G.711 codec only.

The rate choice was left alone: the design's noise model is 2 dB short of
what the residual interference makes at the top rates, but with the
follower fixed no call of the suites has an upstream error, and a margin
would lower the rate of every V.92 call, real ones included.

### After

| | robust 2c | answer-fix 1 |
|---|---|---|
| test_orig_seeds, V.92 | 3 checks, 1 failure: 2 of 54 calls bad (45, 147 upstream failures) | 3 checks, 0 failures: 0 of 54 |
| V.92 Te sweep, 9 lines x 27 Te (1000 .. 1130 ms) | 2 of 243 bad (6 dB pad seed 25, pad6rb seed 27) | 0 of 243; every rate, down and up, the same |
| the two burst calls, 40 s (failures after 11 s) | 403, 334 | 0, 0 |
| test_v92call | 1054, 0 | 1054, 0; case 19 (the caller's Tone A: what the decoder made of it after the caller left data mode) 1039 -> 1007 "bad" bits; 25d still clean |
| test_answer_v92 | 231, 0 | 231, 0; the decoder's SNR 0.1 to 0.3 dB higher on each line |
| test_answer_lin, test_v92real, test_p2_v92 | | identical output |
| v90d's replay, 106 recordings x 8 configs | | MANIFEST IDENTICAL, 1613 of 1613 |
| v92 subdirectory (host and armv5) | | all pass, the same count lines |
| armv5 (qemu, TEST_SHORT), orig_seeds, v92call, answer_v92, v92real, p2_v92 | | all pass; output equal to the host's; v92call and answer_v92 changed as on the host, v92real and p2_v92 identical to before |

The replay and the Te sweep were run on a copy of this tree
(~/afix-tmp/dev, the same v92_p34d.c) built with the fixes of
answer-fix 2 and 3 as well; neither touches the data mode receiver.

## answer-fix 2: PCM upstream after the answerer's own retrain (2026-11-19)

robust 2c left open: after a retrain the digital modem began of its own
accord (v92d's checks: a rate renegotiation that stalls, the upstream not
holding even the lowest rate), answer.c offered no PCM upstream in the
retrain's Phase 2 (`retrain_ours && !rt_asked`): the call went on as V.90.
Only answer_retrain() (rt_asked) kept it. 9.3/V.92 has every retrain of a
call in which both indicated V.92 use V.92's Phase 2, and that Phase 2,
with Phases 3 and 4 after it, measures the line again.

### Test first

`test/test_orig_retrain.c`: a case "the answerer's own retrain": v92d_retrain()
(p34d_retrain() for V.90) called on the engine, not through answer_retrain(),
as v92d's own checks do. Checked as the other retrains are: data again
within 12 s, 5 s clean both ways, one retrain counted by each side, not
ours, and V.92 still V.92. On robust 2c: 248 checks, 1 failure (V.92: 46666
/ 34666 -> 50666 / 28800 V.90).

### The fix (modem/answer.c, v92/v92_p34d)

PCM upstream is offered again in the Phase 2 of every retrain of a call
that reached V.92 data mode, whoever began it, except when
- that start-up never reached data mode (as before),
- v92d failed (as before: the design found no rate, B1u wrong, ...), or
- v92d retrains because the upstream did not hold even the lowest PCM
  rate in data mode (its rate drops reached 24000): new, `v92d_t.up_floor`.
  There the line has shown it does not carry PCM upstream, whatever a new
  design says, and a V.90 call goes on where V.92 would retrain again.
`answer_t.rt_asked` is gone (nothing else read it).

### After

test_orig_retrain: 248 checks, 0 failures. V.92 "the answerer's own
retrain": 9.9 s, 46666 / 34666 -> 48000 / 34666, still V.92; V.90 7.4 s
(V.90 has no PCM upstream to lose). The other cases print as in robust 2c
(same seeds; the new case is appended). test_answer_v92 (whose retrains
are answer_retrain() and the caller's, and the start-up failures that end
in V.90), test_v92call, test_answer_lin, test_v92real: output identical to
answer-fix 1's; test_answer, test_answer_v90, test_answer_v34 identical to
before. armv5 (qemu, TEST_SHORT: V.90's cases): orig_retrain, answer_v92,
answer pass. v90d's replay: MANIFEST IDENTICAL, 1613 of 1613 (no
recorded call has a retrain of v92d's own from data mode; the retrains in
them are the callers' Tone A, or a start-up that failed).

## answer-fix 3: INFO0 in a retrain's Phase 2 (2026-11-19)

robust 2c's open point: 9.7/V.92 says the modems "proceed in accordance
with the full Phase 2 start-up procedure" in a retrain, and our answerer
(p2d_init_retrain) sends no INFO0d then, so our caller sends no INFO0a.

### The text

V.92 (11/2000), 9.7.1.1 (the digital modem begins): silence 70 ms, Tone B,
detect Tone A, then "condition its receiver to detect a Tone A phase
reversal and proceed in accordance with the full Phase 2 start-up
procedure"; 9.7.1.2, 9.7.2.1, 9.7.2.2 the same, from the tones. "Full Phase
2" is 9.3's title ("Full Phase 2 - Probing/ranging"), against 9.4's "Short
Phase 2 - Ranging": the retrain is a Phase 2 with probing, not the short
one, and it is entered at the tones and their reversals, as V.90's 9.5
enters it at 9.2.1.1.3 (after INFO0). Nothing in 9.7 sends INFO0.

### The recordings

`v90m-fields -9` over all 106 recordings of live-050 (every CRC-clean INFO
frame the caller sent, every reversal of its tone): 35 of them have a
second Phase 2 (two INFO1a: a retrain), from U.S. Robotics 5686s (usr*,
pd_*, pw_*, f*), the 1998 U.S. Robotics (x2b), Conexant (cxfail, exp_*),
and others (p*a, v92a..e, fin_48000). In none did the caller send INFO0a
in the retrain: each answered our INFO0-less Tone B with Tone A and its
reversals (e.g. usr7: retrain at 16.53 s, Tone A reversals at 16.68 and
17.44 s, INFO1a at 18.20 s; exp_1 waited 2.8 s, then the same). Real
callers do what our answerer expects. What real digital modems send cannot
be seen in these recordings (every one is of our answerer).

So the answerer is not wrong and is not changed. The caller must take
either form, for a server that sends INFO0d all the same.

### Test (a check, not a fix)

`test/test_orig_retrain.c`: a case "answerer retrains with INFO0d": after
answer_retrain() the answerer's Phase 2 is restarted as at the start of the
call (p2d_init with INFO0d, V.92 offered as before). Our caller's
P2L_A_INFO0 takes an INFO0d without the acknowledge bit for one that has not
heard ours and answers with INFO0a, and the new INFO0d holds. Checked: the
answerer got INFO0a, data again, 5 s clean both ways, V.92 still V.92. It
passes as the caller was (V.90 7.9 s, 52000 -> 46666; V.92 9.7 s, 48000 /
34666 -> 52000 / 34666; on robust 2c's caller the same): only the comments
of p2call say so now. test_orig_retrain: 276 checks, 0 failures; armv5
(qemu, TEST_SHORT, V.90's cases with this one): passes. test_p2call,
test_p2v90, test_p2v92a: pass, unchanged.

### Open (answer-fix)

- The V.92 design's noise model is about 2 dB short at the top upstream
  rates on the simulator's lines (error RMS 9.7 at the small points, 7.6
  assumed): residual interference of the channel estimate, at lags 90 ..
  130. The trellis carries it now that the baseline no longer turns single
  errors into runs; a margin would lower every V.92 call's rate. Measured
  on the simulator only.
- base_step()'s taper is not used with a linear codec: on v90d's real
  (linear) recordings it changed nothing that was decoded and moved flagged
  frames both ways. Bursts like the simulator's have not been seen there.
- What a real digital modem sends in a retrain's Phase 2 is unknown (every
  recording is of our answerer); our caller takes either form, tried only
  against our own answerer made to send INFO0d.
- v92d's own retrain for an upstream that does not hold 24000 still drops
  PCM upstream for the rest of the call (up_floor), by design.

## answer-fix: the end-of-branch checks (2026-11-19)

On the branch's last commit (3f271f4; ubsan and the port matrix on a copy
of its tree, ~/afix-tmp/final, identical but PROGRESS.md).

| | reference | answer-fix |
|---|---|---|
| `make check` (host, inside v90d-split's `make check`) | robust 2c's count lines (~/rb-tmp/check-2c.log) | the same lines, by diff, but orig_retrain 222 -> 276 (answer-fix 2, 3); every answering-side line identical (orig_seeds 3, its exception gone); subdirectories identical; fixtures all pass; realcheck 666; api 326; symbols 555 internal |
| `make bench-v90` | 8e3136e9 x6 | 8e3136e9 x6 (a V.90 call: nothing here acts on it) |
| v90d's replay, 106 recordings x 8 configs (v90d-split d707463 built against this tree; replay_all050.sh's files, run 12 at a time) | sync-build/proof/afterports/manifest.sha256 | MANIFEST IDENTICAL, 1613 of 1613 |
| `make check-ubsan` | | ALL UBSAN CHECKS PASS, no runtime error |
| `tools/check-ports.sh` (PAR=5, JOBS=2, outputs ~/afix-tmp/ports) | host gcc, 110 programs | aarch64be-musl, mips-musl, win32-mingw, host-clang: 110 of 110 identical; i686-musl with QUICK: 99 of 110 identical, the 11 of tools/ports-slow.txt skipped (orig_seeds, orig_retrain, orig_v90_te and realcheck whole: the full i686 run would have passed the hour); ALL PORTS PASS |
| v90d-split (d707463, unchanged, nothing committed) built against this tree (LIBV90M=the worktree, as sync-build/proof/afterports.sh; ~/afix-tmp/v90dproof.sh, outputs ~/afix-tmp/proof) | sync-build/proof/afterports | `make check`: ALL HOST CHECKS PASS, v90d's own count lines identical, the library's as above; `make check-arm` (the library's now in parallel): ALL ARM CHECKS PASS, v90d's own lines identical, the library's those of robust 2c's check-arm but orig_retrain 111 -> 138; bench-v90 8e3136e9 x6; replay MANIFEST IDENTICAL 1613; `fw release` and `fw test-release` exit 0, test-release's count lines identical |

No recording's output changed, so there is no recording / before / after
table to give: the answering-side changes act only where no recorded call
goes (a G.711 codec in V.92 data mode; v92d's own retrain from V.92 data
mode; an INFO0d in a retrain).

For v90d main (separate repository, its own copies of the answering
modem): answer-fix 1 and 2 are patches to its modem/answer.c, answer.h,
v92/v92_p34d.c and v92_p34d.h, which are the library's but for the
portability fix of ports-2 in v92_p34d.c (et_g's update); answer-fix 3
changes nothing there. Its HT802 records linear (`rx_linear`), so fix 1's
step is the old eighth on the box unless the codec runs G.711; fix 2 acts
on any V.92 call v92d retrains of its own accord.

## cpu 1: the calling side's ARM cost, bit-exact part (2026-11-19)

The box's budget (v90d-split HARDWARE.md, PROGRESS.md): DVF9918, ARM926EJ-S
at 400 MHz, no divide, no FPU. The answering modem's V.90 data mode is 39.5 M
ARM instructions per second of line under qemu and 32 % of the CPU on the
box, so the box is about 123 M qemu-counted instructions a second (1 % =
1.23 M): 15.4 k per sample, 1.23 M per 10 ms block. v90d serves the line in
blocks of 80 samples (10 ms), driver latency 240 samples (30 ms): what
matters for a long tick is the block it lands in. The answering modem's own
heaviest tick (`bench_v90`'s 40 s V.90 call, counted with tools/qprof): 173 k,
then 158 k, 144 k, 110 k; its V.34 receiver's periodic ticks 87 k.

How it is measured now: `make prof-v90c` / `prof-v92c` (PPM=0 or 100):
rec_v90c records a call with 4 s of data mode, bench_v90c `-t` replays it
under qemu (`-d in_asm,exec,nochain`, no single-instruction blocks) calling
a mark before every tick, tools/qprof turns the log into the instructions of
every tick and, over seconds 2 to 3 of data mode, per function. Checked
against `-one-insn-per-tb` on 2000 samples: 640601 against 640775.
One run of a whole call takes a minute or two (cost-v90c's per-tick runs
took longer each). The bench's own loop (fread, the hash) is in the counts
as it was in cost-v90c's, about 1 M a second.

Profile at the start (answer-fix, seconds 2 to 3 of data mode, M ARM
instructions a second):

| | V.90 0 ppm | V.90 100 ppm | V.92 0 ppm | V.92 100 ppm |
|---|---|---|---|---|
| total | 78.8 | 89.4 | 104.1 | 114.0 |
| v90arx_tick (equaliser 48 + 128 taps, its LMS, interpolator 2 x 64, Sd) | 28.6 | 28.6 | 28.6 | 28.6 |
| estimate (channel estimate, 128 taps, NLMS, two 64-bit divisions a symbol) | 27.5 | 27.5 | 27.5 | 27.5 |
| v92_enc_symbol (precoder and prefilter, LZ1 / LP1 / LZ2 / LP2) | | | 19.0 | 19.0 |
| dfe_design (the equaliser computed again, every 2048 / 512 symbols) and its 64-bit divisions (__udivmoddi4) | 4 | 17 | 7 | 16 |
| pcmtx_out (the upstream interpolator, 2 x 64 taps a sample) | | | 5.6 | 5.6 |
| ec_tick (96 taps) | 4.4 | 4.4 | 4.4 | 4.4 |

The heaviest ticks: 9.30 M at the end of the DIL (v90dil_analyse: 41 gain
candidates x 576 measurements, each a 64-bit division and a binary search);
0.90 M each time the equaliser is computed (0.55 M of it 64-bit division);
0.59 to 0.90 M the CP design.

What changed (none of it changes a result; every bench hash and the test
output of every caller program tried stays as it was):
- `estimate`: the two loops over the symbol ring in two runs that do not
  wrap, four taps a pass; the update without a 64-bit product per tap (es =
  256 q + rr: (es s) >> 8 = q s + ((rr s) >> 8), low 32 bits); the division
  by L by L's reciprocal (`l_rcp`, set once).
- `equalise`, `produce`, `ec_tick`, `pcmtx_out`, `v92_enc_symbol` (hdot16 /
  hdot32): four taps a pass, rings read in two runs instead of an index mask
  per tap, products accumulated one by one (gcc's SMLAL into one sum).
- The equaliser's design (dsn_start / dsn_step, run whole by dsn_now here):
  R built along its diagonals in 32 bits, the Cholesky factor, z and f
  multiplied 32 by 32 bits (bounded under 2^29 / 2^30; checked, and a value
  that does not fit is a failed design: none can), every division by
  G's diagonal and by the bias through a reciprocal (`sdiv_rcp`: the high
  word of |n| rcp is the quotient or one less), so 50 64-bit divisions
  instead of 1400. The band edge's image as one value per diagonal.
- The DIL analysis: `nearest_g` finds nearest(t1, (l << 16) / g) without
  the division (t1[i] 2^4 <= floor(L / g) is t1[i] 2^4 g <= L, and the same
  for the rounding), walking from the last answer; checked against nearest()
  on 4e7 random (l, g) pairs, both laws: no difference. A gain candidate's
  cost stops being summed once it can no longer win (its terms are not
  negative).
- `V90M_MEM_SIZE` 112 -> 120 KiB: `v90arx_t` holds the design's working
  state (`v90arx_dsn_t`, 6216 octets: R / G packed, the estimate taken,
  solutions) so that the next commit can spread it over symbols; `orig_t`
  109920 -> 116160 octets on x86-64 (115880 on ARM); `answer_t` unchanged.
- `rig/bench_v90c` -t / -n, tools/qprof.c, `make prof-v90c` / `prof-v92c`.

After (same recordings, M a second, % of the box at 123 M):

| | V.90 0 ppm | V.90 100 ppm | V.92 0 ppm | V.92 100 ppm |
|---|---|---|---|---|
| data mode, before | 78.8 (64 %) | 89.4 (72 %) | 104.1 (84 %) | 114.0 (92 %) |
| data mode, after | 54.5 (44 %) | 59.0 (48 %) | 69.5 (56 %) | 73.7 (60 %) |
| heaviest tick, before | 9.30 M (DIL) | 9.27 M | 9.30 M | 9.30 M |
| heaviest tick, after | 1.14 M (DIL) | 1.17 M | 1.14 M | 1.15 M |
| then | 0.62 M (CP design), 0.39 M (equaliser) | 0.90 M, 0.40 M | 0.72 M, 0.40 M | 0.59 M, 0.40 M |

Unchanged: bench-v90c 695b27e5 and bench-v92c 506f2bcc (host and qemu-arm);
the replays at 0 ppm f3111e17 / ca4ae751; test_v90arx, v90arx_robust,
v90dil, v90a, pcmtx, v92a, orig_v90, orig_v92 print byte for byte what
answer-fix's build prints; bench-v90 8e3136e9 x6 (ec.c is the answerer's
too: the same sums).

## cpu 2: Phase 2 and V.34 set-up ticks, shared code, bit-exact (2026-11-19)

With the DIL's tick spread (cpu 3), the heaviest ticks left were shared
code, the answering modem's as much as ours (its own heaviest two, 173 k
and 158 k, are these):

| tick | what | before | after |
|---|---|---|---|
| start of Phase 2 | probetx_init (the L1 / L2 table: a modulo per tone per sample, cos160's 64-bit divisions), dpsktx_init (240 64-bit divisions) | 189 k | 98 k |
| end of probing | the block DFT of the last 20 ms (25 bins x 160 samples at once), then the analysis (powers() four times a bin, log2 by a 64-step loop) | 223 k | 82 k |
| V.90 data mode begins | v34_params_init's shell tables (loops over every k with tests inside) | 167 k | 79 k |

What changed (the same results: bench-v90c 695b27e5, bench-v92c 506f2bcc
host and qemu-arm; bench-v90 8e3136e9 x6; test_p2call, p2v90, p2v92a,
orig, orig_v34, v8orig print what answer-fix's build prints):
- modem/probe.c: cos(2 pi k / 160) as a constant table (generated by the
  code it replaces); the generator by tone, the analyser's DFT summed
  sample by sample as the samples come (the same sums in the same order;
  proberx_t holds the 26 running bins instead of the block and the cosine
  table: 224 octets smaller, so `answer_t` 106552 -> 106104 on x86-64);
  proberx_bin() gives snr, level and power from one powers(); log2q8 by
  halving.
- modem/p2call.c: analyse() through proberx_bin().
- modem/dpsk.c: dpsktx_init divides in 32 bits where the product fits.
- v34/v34_params.c: the convolution loops over the k that pass the tests.

## cpu 3: the heavy computations spread over ticks (2026-11-19)

Target for the heaviest tick: under 100 k ARM instructions, i.e. under 8 %
of the 1.23 M a 10 ms block of the box has, below the answering modem's own
heaviest (173 k before cpu 2), and some 6 average samples of V.92 data mode
(a block of V.92 data mode, 0.6 M after cpu 1, has room for it twice over).

What is spread (these change when things happen, so they change numerics):
- The equaliser's design (`v90arx`): a job of steps, one a symbol, from
  the estimate taken when it starts: start (R's first column, diagonal,
  band edge, load, scale, the first 14 of R's 48 diagonals), fill (the
  other 34), Cholesky (columns by a budget of 9000 multiplies a step, 4
  steps), solve (z, f, the bias, the forward taps), feedback taps (then
  both sets go in): 8 symbols. The first equaliser, at the end of TRN1d's
  estimate, likewise: the estimate goes on and the equaliser starts on the
  symbol its design ends on. A tick in which the receiver took a step
  (`v90arx_t.dsn_ran`) takes no step of v90a's jobs.
- The DIL's analysis, CPt's design and CP's design (`v90dil_analyse_begin
  / _step`, `v90dil_design_begin / _step`; the whole-call functions run
  them to the end): at the end of the DIL, S goes out at once as before and
  the analysis (begin, one gain candidate a step, the common gain, one
  interval a step twice, the robbed bits) and CPt's design (one trial
  constellation set a step) follow, some 60 ticks (8 ms); CP's design
  from TRN2d's noise the same way. Where a result is wanted before its
  job is done (our S-bar before CPt, Ri-bar, our SCR's 2 s) the rest runs
  then (none of the tests needed it).
- `v90a_t` holds the job (a union of the analysis and the design: 2.8
  kbytes), `v90arx_dsn_t` grows by R's first column and diagonal values
  (768 octets): `orig_t` 116160 -> 118704 octets on x86-64 (118416 on
  ARM), within the 120 KiB of cpu 1.

Found on the way (test_orig_retrain's "lost past the limit", V.92, failed
with the new timing: the answerer never saw our cleardown): with the
receiver wiped, line_watch held the timing loop at the collapse, let it go
at the first data frame whose error fell under 16 times the design's,
and the half-recovered receiver (its estimate regrown from nothing) moved
the clock -25 ppm in 150 ms; the cleardown's CPus then went out where the
digital modem did not listen. answer-fix's run had moved it -2.4 ppm (on
8 seeds of that scenario: answer-fix 8 of 8, this commit without the fix 7
of 8, with it 8 of 8). Now the loop stays held after a collapse until the
error is back within twice the design's (`col_hold`).

Heaviest ticks (prof-v90c / prof-v92c, the whole call):

| | before (answer-fix) | after cpu 1 | after (this) |
|---|---|---|---|
| V.90 0 ppm | 9.30 M (DIL), 0.62 M (CP design), 0.39 M (equaliser) | 1.14 M, 0.62 M, 0.39 M | 98 k (Phase 2's start), 82 k (end of probing), 81 k (a step of the equaliser's design) |
| V.90 100 ppm | 9.27 M, 0.91 M, 0.91 M | 1.17 M, 0.90 M, 0.40 M | 98 k, 82 k, 80 k |
| V.92 0 ppm | 9.30 M, 0.72 M, 0.91 M | 1.14 M, 0.72 M, 0.40 M | 98 k, 82 k, 80 k |
| V.92 100 ppm | 9.30 M, 0.59 M, 0.91 M | 1.15 M, 0.59 M, 0.40 M | 98 k, 82 k, 81 k |

Data mode is as cpu 1 left it (the designs cost the same, spread):
54.6 / 58.8 M a second (V.90, 0 / 100 ppm), 69.4 / 73.9 M (V.92).

Rates (the same tests, answer-fix's build against this one; each call
is one seed, so single calls move by a step either way):
- test_orig_seeds (9 lines x 6 seeds each, with the mean added in a copy
  of the test): V.90 down 46073.8 -> 46123.1, up 28800.0 -> 28888.9; V.92
  down 45728.0 -> 45777.4, up 34765.2 -> 34765.1; 0 of 108 bad either way.
- test_orig_v92's table (11 calls): down 47111 -> 47444, up 33244 -> 33911.
- test_orig_retrain's tables (44 rate pairs): down 47666 -> 47818, up
  31642 -> 31703.
- test_orig_v90's table, its 9 lines: down 46370 -> 46222 (the 6 dB pad
  45333 -> 44000, robbed bit 49333 -> 48000, pad and robbed bit 40000 ->
  41333), the A-law line's upstream 31200 -> 28800.
- bench-v90c fa7486c7 (46666 / 28800 as before), bench-v92c 22bf87ff
  (46666 / 34666; it was 48000 / 36000): host and qemu-arm the same.

Tests (short set): v90arx, v90arx_robust, v90dil, v90a, orig_v90, v92a,
orig_v92, p2call, p2v90, p2v92a, orig_retrain, orig_v90_te, realcheck,
orig, orig_v34, api, orig_bell, orig_seeds: all 0 failures. bench-v90
8e3136e9 x6.
