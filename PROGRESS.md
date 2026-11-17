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
