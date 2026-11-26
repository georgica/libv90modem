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

(Corrected in the 0.6.0 audit, Task 5b: this was wrong. It was not qemu
but GCC 12.3's m68k back end; see "m68k: a GCC wrong-code bug, not qemu"
below. What follows is what was believed at ports-2.)

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

- m68k: settled in the 0.6.0 audit (Task 5b): a gcc bug, "m68k" below.
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

## cpu 4: retrain and renegotiation ticks measured (2026-11-19)

`tools/rec_v90c ... rt` / `rn`: 2 s into data mode the recorded caller
retrains (orig_retrain) or renegotiates (orig_renegotiate, no ceiling);
the magic ends in 2 and the header says which and before which tick;
`rig/bench_v90c` asks for the same at the same tick, so the replay stays
the call (V90C0001 recordings as before). Counted with prof (every tick of
the whole call, 2 km, +100 ppm, 12 s after the event):

| call | event | heaviest tick from the event on | data mode at the end |
|---|---|---|---|
| V.90 | our retrain | 97.7 k (the retrain itself: v90a_init's clearing of 50 kbytes), 95.2 k (+70 ms, Phase 2's start), then 82 k | 59.2 M a second |
| V.90 | our renegotiation | 81.8 k (steps of CP's design and of the equaliser's) | 59.3 M |
| V.92 | our retrain | 97.7 k, 95.2 k, then 82 k | 74.8 M |
| V.92 | our renegotiation | 77.7 k | 75.2 M |

Nothing new is heavy: the retrain's ticks are those of a call's start.
Tried and dropped (each measured on the tests): the channel estimate on
every other data symbol (-6.4 M a second; test_v90arx's MSE 1.5 to 2.5 dB
worse at 30 to 80 ppm, one check failed, test_orig_v92 one failure); the
equaliser's LMS on every other symbol (-4 M; orig_seeds' mean downstream
46123 -> 45185 and one bad call of 54); the interpolators (receiver and
PCM upstream) two taps a pass (gcc's code for two 64-bit sums per tap got
worse: +1.3 M).

## cpu: the end-of-branch checks (2026-11-19)

The calling side's cost on the box (qemu-counted ARM instructions; the box
is about 123 M a second, 1 % = 1.23 M; seconds 2 to 3 of data mode, 2 km):

| | answer-fix | cpu | % of the box, before -> after |
|---|---|---|---|
| V.90 data mode, 0 ppm | 78.8 M/s | 54.6 M/s | 64 % -> 44 % |
| V.90 data mode, 100 ppm | 89.4 M/s | 58.8 M/s | 72 % -> 48 % |
| V.92 data mode, 0 ppm | 104.1 M/s | 69.4 M/s | 84 % -> 56 % |
| V.92 data mode, 100 ppm | 114.0 M/s | 73.9 M/s | 92 % -> 60 % |
| Bell 103 / 212A data mode (cost-bell) | 9.85 / 4.43 M/s | 9.84 / 4.43 M/s | 8.0 / 3.6 % |
| heaviest tick, V.90 and V.92 calls | 9.30 M (DIL end) | 98 k (Phase 2's start) | 755 % -> 8 % of a 10 ms block |
| heaviest tick of our retrain / renegotiation (cpu 4) | (DIL 9.3 M again; CP design 0.6-0.9 M) | 98 k / 82 k | 8 % / 7 % of a block |
| Bell heaviest tick | | 2.3 k / 2.8 k | |
| the answering modem's heaviest tick (bench_v90, shared code of cpu 2) | 173 k | 87 k | its output unchanged |
| the answering modem, 40 s V.90 call | 1391 M | 1365 M | |

Where the 100 ppm data mode goes now (M/s, V.92): equaliser 48 + 128 taps
and its LMS, interpolator, Sd watch 24.1; channel estimate 13.1; V.92
precoder / prefilter 10.1; upstream interpolator 4.2; canceller 3.6; the
equaliser's designs (16 a second at 100 ppm, 4 at 0 ppm, some 350 k each)
5.5; the rest under 3 each. What is left is the per-tap work of the
algorithm (3.5 to 7 instructions a tap); halving the estimate's or the
equaliser's adaptation was tried and costs SNR or rate (cpu 4).

Checks on the branch's last commit:

| | result |
|---|---|
| `make check` (host) | ALL HOST CHECKS PASS; its 110 count lines are answer-fix's (~/c52v/afix/check.log) line for line, every answering-side line among them; fixtures pass |
| `make check-arm` (parallel, BUILD=build/armchk) | ALL ARM CHECKS PASS; the counts are the host's but for the programs that run shorter on ARM, as before (orig_retrain 138, v32's and v34's) |
| `make check-ubsan` | ALL UBSAN CHECKS PASS, no runtime error |
| `make bench-v90` | 8e3136e9 x6 |
| `make bench-bell` | 48f22641 / fc125ef2, host and qemu-arm |
| bench-v90c / bench-v92c | fa7486c7 / 22bf87ff (cpu 3 changed them; host and qemu-arm the same) |
| v90d's real-recording replay (afix-tmp/replaypar.sh against this tree, 106 recordings x 8 configs) | MANIFEST IDENTICAL 1613 (to sync-build/proof/afterports/manifest.sha256, sorted) |
| tools/check-ports.sh (PAR=3 JOBS=2, QUICK=i686-musl, SKIP=arm_test_orig_seeds) | host-clang, aarch64be-musl, mips-musl, win32-mingw: 109 of 110 identical, orig_seeds skipped; i686-musl: 98 of 110 identical, 12 skipped; ALL PORTS PASS |

`tools/check-ports.sh` takes SKIP="prog ..." now: programs every target
but the reference skips (orig_seeds here: covered on the host and on ARM).

Memory: `V90M_MEM_SIZE` 112 -> 120 KiB; `orig_t` 109920 -> 118704 octets
on x86-64 (109632 -> 118416 on ARM): the equaliser design's state (7 k),
the DIL / CP jobs' (2.8 k); `answer_t` 106552 -> 106104 (proberx_t smaller).

Open:
- V.92 at 100 ppm is at 60 % of the box (56 % at 0 ppm): one call fits
  with room, two V.92 calls at once do not. What is left is per-tap work;
  further cuts would be numeric (a shorter estimate or feedback filter, a
  slower design rate at high clock offsets), each to be weighed on rates.
- The estimate's per-symbol 64-bit division (some 1.4 M a second) and the
  timing loop's DFT (0.9 M) could still be made cheaper exactly.
- All counts are qemu's instruction counts; cycles on the ARM926 (cache,
  multiply latency) have not been measured on the box.

## release-0.3.0: version, changelog, documents and header reviewed (2026-11-19)

Branch `release-0.3.0` from cpu (e6e03c1). No signal-path code changed.

- Version 0.3.0: header, `v90m_version()`, Makefile (so the .pc file and
  the shared library's name). test_api checks the string, the header's
  numbers and that the two agree: 2 failures against 0.2.0, then 0.
- `CHANGELOG.md` (new): 0.1.0, 0.2.0, 0.3.0 for users of the library,
  from this file and the history.
- `README.md` rewritten as one document: the standards table with both
  roles; what was tried on real modems (answering) and what only in
  simulation (all of the calling side); building, cross builds, the port
  matrix; memory (120 KiB bound, 118816 octets on x86-64, 118504 on ARMv5)
  and the ARMv5 instruction counts of the cpu section; the limitations;
  examples. Gone: "everything inside [the static library] is local" (it
  is renamed `v90m__i_*` since ports-2) and the scattered stage notes.
- `include/v90modem.h`: per field and function, which role it applies to
  and every error return, checked against api/v90modem.c by a new
  test_errors in test_api (94 checks). Found:
  - `v90m_reset(m, NULL)` crashed (create, init and `v90m_ec_reset` give
    `V90M_EINVAL`): seen failing (SIGSEGV in v90m_reset), now
    `V90M_EINVAL`. The only code change outside the tests.
  - Comments that did not match the code, corrected: `v90m_stats.echo_delay`
    is answering V.90 / V.92 only (always -1 calling); `renegotiations`
    counts calling V.32bis too and not answering V.34; `odp_bypass` is
    answering only; `codec_linear_rx` and `max_tx_code` are answering
    only; the rate ceilings mean send / receive in V.34, V.90 and V.92 in
    either role (V.92's PCM upstream under max_down when calling); the
    header named a `v90m_process()` that does not exist and said
    `v90m_ec_*` make callbacks (they make none); the first line still said
    Bell was answering only. `v90m_ec_send_break` / `_get_break` return
    0 / -1 and 1 / 0, not V90M_* codes: documented as they are.
- `DESIGN.md`: 4.2b moved before 4.2c (the order was 4.2a, 4.2c, 4.2d,
  4.2b); stale: the symbol hiding (renamed, not localized), the API layer
  (also `orig`), `v32/` (both roles), 4.2c "no retrain, no renegotiation of
  our own" (robust 2c), 4.2a's PCM upstream after retrains (answer-fix 2)
  and the baseline follower (answer-fix 1), where measurements are kept,
  the `v92 = on` of v90d's config; testing: port matrix, ubsan,
  realcheck.
- `examples/call_loop.c` (new): a caller and an answerer back to back
  through the public API, `-m` for what the caller offers (v92 by
  default; v90, v34, v32, v22, bell212, bell103 all connect, text both
  ways, cleardown where there is one). `make` builds the examples now
  (`all: lib examples`); `make check` runs call_loop (test-examples).
- `libv90modem.pc.in`: the description names Bell and both roles.

Hygiene: no TODO / FIXME / XXX / `#if 0` in the library. The diagnostic
prints in modem/p34d.c, modem/v34dx.c and v42/v42_lapm.c are all behind
`P34D_TRACE` / `V42_XID_TRACE` (p34d's own under the V.42 macro: odd, left
as it is, signal-path file); the `#ifndef` knobs (`V22_BELL_MS`,
`V90_MAX_TX_CODE`, `V92_DESIGN_PACE`, `V92_DSN_ECHO_DIV_LIN`, `LOSS_MSE`,
`V92D_PROBE`) are the documented bench / EXTRA_CPPFLAGS ones. `getenv`
only in tools and tests. `make clean` leaves nothing (top level and the
five subdirectories). `make install DESTDIR=... PREFIX=/usr` installs the
header, `.a`, `.so.0.3.0` with its two links, the .pc file and
v90m-replay; call_loop built against it with pkg-config, shared and
`--static`, runs; `make uninstall` removes every file. gcc and clang
(-std=c99 -O2 -Wall -Wextra -Werror): library, examples, v90m-replay,
v90m-fields, every rec_* / bench_*, qprof, ber_v90, printf-probe: no
warning; the header alone also under -pedantic and as C++98.

Checks:

| | result |
|---|---|
| `make check` (host, from clean) | ALL HOST CHECKS PASS; 110 count lines, those of cpu (~/c52v/cpuv/check.log) but `api` 326 -> 421 (version +1, test_errors +94); fixtures pass; call_loop V.92 |
| `make check-arm` (BUILD=build/armchk) | ALL ARM CHECKS PASS; the host's counts but the programs shortened on ARM, as before (orig_retrain 138, test_bench, test_ber, test_call, test_caller, test_startup) |
| `make check-ubsan` | ALL UBSAN CHECKS PASS, no runtime error |
| bench-v90 | 8e3136e9 x6 |
| bench-bell | 48f22641 / fc125ef2, host and qemu-arm |
| bench-v90c / bench-v92c | fa7486c7 / 22bf87ff, host and qemu-arm |
| port matrix | not run to the end: stopped by the session for low system memory (swap full) while building; to be run again |

Open, for the owner before 0.3.0:
- `v90m_ec_send_break` returns -1, the value of `V90M_EINVAL`, where
  `V90M_ESTATE` is meant; changing it changes the API's values.
- The answering role takes `modulations` = 0, and `V90M_MOD_V92` without
  `V90M_MOD_V90`, without complaint; the calling role refuses both.
- In check_cfg, the calling role's `V90M_ENOTSUP` for "nothing it can do"
  and for unknown bits can no longer happen (every bit is callable);
  harmless dead code.
- DESIGN.md has no section on the V.90 analogue (calling) side, nor on
  V.21 / V.22bis / V.32bis / V.34 calling: 4.2c leans on it.

## x2 4: the API, and negotiation with V.34 fallback (2026-11-20)

x2 offered on the answering side, up to the client's marker; no PCM
training yet (ANS_X2 ends the call with V90M_END_X2_FAILED until x2 6).
The facts are docs/history/x2-facts.md "Phase 1" and "Phase 2 marker".

- API: `V90M_MOD_X2` (0x0100, not in the defaults; without V.34 or in the
  calling role `V90M_ENOTSUP`), `V90M_CONN_X2` and `V90M_END_X2_FAILED`
  appended. `ANS_MODE_X2`, `ANS_MOD_X2`, `ANS_X2`; `answer_t.x2_offered`
  (V.34 too, and a mu-law line: answer_v90_cfg recomputes it) and
  `x2_failed`.
- V.8 (v8ans): with x2 offered, a CM with V.34 and no PCM, and V.34 the JM's
  choice: the carrier is inverted after each complete JM; a CM with the
  PSTN access octet gets it back with the digital-network bit (0d -> 8d).
  `v8ans_offer_x2`, `v8ans_x2`.
- Phase 2 (p2ans, through v34ans_cfg.x2): INFO0a max_sr_diff = 4, cme = 1.
  From the start of our L1 the DPSK receiver hunts a 7-bit INFO frame
  (`p2ans_x2_info_frame` builds one: lead-in 11, fill, sync, body, CRC,
  fill; body 4d -> CRC 24dd as the facts say); one that checks out with 6
  in bits 1..3 by 960 ms after L1 began: P2N_X2, silent, `x2_marker`;
  v34ans V34A_X2 -> answer ANS_X2. L2 goes on meanwhile exactly as
  without x2; the window also closes when the tones restart (L2 unanswered:
  760 ms + round trip after L1 began) or the caller's L1 / L2 have been
  analysed.
- The JM flip broke our own V.8 receivers (sim/v8call, and so modem/v8orig
  would have): across the 180 degree step the V.21 correlators read space
  for more than half a bit, and the receiver took a character FF out of the
  ten ONEs, which left the next JM without its preamble (21 JMs sent, no
  CJ). `v8rx_t.jm_flip` (set by v8call and v8orig, not by v8ans) counts an
  FF character as the mark it is; no V.8 octet is FF. The answerer's V.8
  receiver is unchanged.
- tools: v90m-replay `-x`; trace / v90m-fields know the phase X2.

Tests (seen failing first, then passing): test_api (refusals, enum
values; the "unknown bit" check moved from 0x100 to 0x200), test_v8ans (JM
negated after each complete JM, octets unchanged; the access octet 8d; A-law
and a PCM CM sample for sample as without x2), test_p2ans (the frame's bits
and CRC; INFO0a bits 21..24 = 0 0 1 1 and the rest of Phase 2 the same
samples from L1 on; marker 4d at 170 and 260 ms and 0d accepted; a bad CRC,
a marker 900 ms after the end of L1, 49 and 41 refused, each with our
samples from L1 on identical to a run without x2), test_answer_x2 (new:
sim/v34peer with x2 offered connects V.34 28800 / 28800 at 3200 as without,
0 bad bits; with the access octet too; A-law: all 87752 samples as without
x2; a V.90 caller: all 64000 as without; a marker caller: ANS_X2 244 ms after
L1 began, the call ends, x2_failed). `make check`: ALL HOST CHECKS PASS.

Replay proof, every recording of ~/sync-build/live-050 (106), main
(19ba2c6, a worktree) against this branch, each tool in its own directory
(bits.txt kept), 8 at a time:

    ls live-050/*.raw | xargs -P 8 ... v90m-replay [FLAGS] REC > NAME.txt; bits.txt -> NAME.bits
    FLAGS: main "" and "-9"; branch "", "-9", "-x", "-x -9"

| comparison | outputs (106 .txt, 21 / 40 .bits) |
|---|---|
| main vs branch, no flags | identical, byte for byte |
| main -9 vs branch -9 | identical, byte for byte |
| main vs branch -x | 100 identical; 6 differ (ab_cc_v34, ab_cl_v34, v34a, v34b, v34c, x1: the V.34-only callers) |
| main -9 vs branch -x -9 | the same 6 |

In those 6 only the count of transmit samples that differ from the
recording changes, by 2094 each (x1: 110394 -> 112488): the second JM
negated (1867 samples; JM 233 ms, the first difference at its start, e.g.
x1 8.234 s, JM began 8.000 s) and INFO0a from bit 21 on (227). Every
decision, phase, rate and bit received is the same; .bits identical. None
of the recordings is an x2 caller, so no marker was taken.

`make check-arm` (armv5 uclibc, qemu): ALL ARM CHECKS PASS. test_answer_x2
was run in full (46 checks). It now has a TEST_SHORT version for ARM that
leaves out the access-octet call and the A-law pair: 30 checks, 3.4 s.

## x2 5: records with their CRC, and the downstream mapper at 32000 (2026-11-20)

Written up at release (x2 9); from the Task 5 report, commits b4b95cf and
109c2d0. No change to what the modem sends yet: the pieces Task 6 runs.

- `modem/x2rec`: `x2_crc16` (from FFFF, 0x8408 reflected, no final
  inversion), `x2rec_build` (17 ones, a zero, each word LSB first and a
  zero, the CRC of the word bits LSB first; `ack` sets word 0 bit 15) and
  `x2rec_parse` (from the sync; `X2REC_SHORT`, `X2REC_BAD`), with the MP
  field accessors.
- `modem/x2tab`: levels A (`a5 a7 ad af b7 bd c5 cf e5`) and levels B,
  and the levels received / octets sent behind each pad (the octets sent
  derived by `tools/x2_pad.c`'s rule, equal to the Quad's).
- `modem/x2map`: index 1 (24 bits a frame): six positions, signs
  differential, the free sign by the levels A rule (negative when the
  other five sum to more than 805 in 14-bit units) or, in the data levels,
  the running sum. `x2map_unframe` takes the octets as received.

Tests (each seen failing first: `test_x2rec: 29 checks, 20 failures`,
`test_x2map: 40 checks, 35 failures`):

    test_x2rec: 29 checks, 0 failures   (the facts' four CRCs 14AD 92B0 73F3 F9A6; 2000 random records round trip, every single flipped bit BAD)
    x2map: data levels, running sum within +-35840 from 500 frames after the switch
    test_x2map: 45 checks, 0 failures   (10000 random frames each in levels B, A, B behind 3 and 6 dB, A behind 6 dB, against a model of the facts)

Mutation checks: the free sign's tie rule `<=` to `<` gives 17 failures,
its threshold +2 or -5 six each. clang UBSan clean; ARM prints the host's
lines (`make check-arm`: ALL ARM CHECKS PASS).

## x2 6: training, records and data at 32000; a simulated client; the emulated Courier (2026-11-20)

The client's marker now leads to the x2 start-up and data mode at rate
index 1 (32000 down), V.34 up. The facts are docs/history/x2-facts.md
"Phase 3 script", "Records", "Mapping", "Free sign", "Pads", "Upstream".

- `modem/x2tx`: the server's script, octet by octet: 20 x 7f, the
  preamble (from its rule: Barker-11 signs, the 0712 magnitudes; checked
  against the facts' 22 octets), TRN (GPC from 0, b1 / 31), the J words
  (09B0 x 99, 099F), the ones (c1 / 41), the alignment, sync frames of
  ones in levels A (1667 at least, until the client's MP), our record
  F33C 7FFF 0000 padded to 4 frames (whole records, until its E), the
  start (1 + 680 frames of ones), data. GPC runs on through every mapped
  frame (not the alignment).
- `modem/x2d`, modelled on p34d, one sample in, one octet out: the
  client's V.34 Phase 3 (sdet, PP and 512 TRN symbols known, 4-point
  decisions, J of either kind twice) -> our script; the echo canceller
  learns on our TRN while the client is quiet (it stops its J 150 ms into
  our preamble: a fact added to "Phase 3 script"), the echo delay measured
  first when not given; sres on its Phase 4 S; 16-point decisions (the
  equaliser learns only from decisions near a point, so TRN's 4-point
  start does not pull it); its MP (an x2 record of 4 words) taken when two
  in a row agree: the index must be in X2D_RATE_MASK (0x0002), the pad 0..2,
  the upstream the highest of its word 1 at most 28800 that v34/ takes
  with GPA, expanded shaping, 64 states and non-linear encoding; E (20
  ones) -> B1 and data through qamrx + v34/. Each wait has a limit; after
  the marker every failure ends the call. The client's S in data mode is
  X2D_RENEG, not answered yet: the call ends 6 s later (x2 7).
- `modem/answer`: ANS_X2 runs x2d in `u` (with V.32bis and V.92: 20 KB of
  45; answer_t unchanged, V90M_MEM_SIZE unchanged); data mode is
  ANS_MOD_X2 with x2d's rates; an end before data mode is x2_failed
  (V90M_END_X2_FAILED); answer_cleardown in x2 data mode goes silent
  (cleared = 2, V90M_END_CLEARDOWN_OURS); SNR, rtd, echo delay, moving.
- `sim/x2call`: the client, from the facts: V.8 and Phase 2 (v8call with
  jm_flip, p2c) to our L1, the marker 4d 170 ms after it began, Phase 3
  (TRN 6400, J on 16 points until our preamble has run 150 ms), Phase 4 at
  our J words (TRN 512 on 4 points then 16, 5120 in all), its MP until our
  record decodes, one MP', E, B1, data. Downstream it passes our octets
  through the pad model and checks every segment.
- The Task 4 review's minors (commit c85b990): the marker in the first
  Phase 2 only; its 800 ms counted to the frame's start (and through a
  return to Tone A after an unanswered L2); V8_SEL_V34; self_retrain
  through x2_stop.
- tools: `rec_x2` (a recording of a sim call), `bench_v90 -x`, `make
  bench-x2`, `make cost-x2`; the x2-peer bridge and run.sh take MODS=x2.

Tests (each seen failing first: test_x2tx against a stub x2tx, 2027 of
2034 checks failed; test_answer_x2's and test_api's x2 calls against an
x2d that ends at once, as Task 4's ANS_X2 did: 56 and 5 failures):

    test_x2tx:     50 checks, 0 failures (every segment, records, start, 48000 data bits)
    test_answer_x2: 172 checks, 0 failures
      x2 no pad: data 32000 / 24000 bit/s; down 118512 bits 0 bad, up 100020 bits 0 bad
         pad mode 0 in the MP and the mapper; echo delay 295; upstream SNR 36.6 dB
         client saw: preamble 0 bad, TRN 0 bad, J words 99 + 1, ones 0 bad, alignment 0 bad,
         sync 1667 frames (0 bad), records 14 (0 bad), start 680 (0 bad), data frames 4938 (0 lost)
      x2 3 dB pad, 6 dB pad: the same (pad mode 1, 2; echo delay given 0; SNR 34.9, 33.8 dB)
      x2 MP asks for index 5: ended 6.47 s after the marker, error 5, x2_failed 1, 4 MPs sent
      x2 MP reports pad mode 3: ended 6.47 s after the marker, error 6, x2_failed 1, 4 MPs sent
      x2 client silent after its marker: the call ended 5.93 s later, error 2, x2_failed 1
      x2 client silent after its J: the call ended 3.02 s later, error 3, x2_failed 1
      x2 client silent after its MP: the call ended 6.61 s later, error 8, x2_failed 1
      x2 client silent in data mode: the call ended 0.53 s later, error 10, x2_failed 0
    test_api: the x2 call through the public API: V90M_CONN_X2, 32000 / 24000,
      SNR 38.1 dB, events CONNECTED, DATA_MOVING ... ENDED, v90m_cleardown -> CLEARDOWN_OURS
    test_p2ans: the marker refused in a retrain, taken 15 ms before the
      deadline (sent 945 ms after L1 began), refused 5 ms after it

The echo canceller carries the upstream: frozen (a diagnostic build, not
kept), the no-pad call never got the client's MP (SNR -0.1 dB).

The emulated Courier (`make test-x2-courier MODS=x2`, courier-emu
186d653f5624, its 4.03 board ROM) at each pad: PASS. Ours `CONNECT
32000/24000 x2` at 30.40 s of line, V.42 LAPM at 32.88 s; the Courier
`CONNECT 48000/ARQ/x2/LAPM/V42BIS` (PAD=3: 50666), ATI11 `Modulation
x2/V.34+`, `Trellis Code NONE/64S-4D`, `Nonlinear Encoding NONE/ON`,
`Shaping OFF/ON`, SNR 56 dB, x2 Status `0000` / `0001` / `0010` by pad.
12.1 s into data mode the Courier began its rate change, which this
version does not answer: our end came 6 s later. Details and what is not
settled: x2-facts.md "Harness". No difference from the simulated client
showed.

CPU (`make bench-x2`, `make cost-x2`; ARMv5 gcc -O2 under qemu): the
recording of a sim call (tools/rec_x2, 40 s, data mode from 10.73 s)
replays on the host and on ARM with the same transmit hash (37c3e5f5),
713400 bits up, 0 bad. Instructions per second of line in data mode
(seconds 15 to 16 of each call, counted as cost-v34 does):

| | M instructions / s | of the box (123 M = 100 %) |
|---|---|---|
| V.90 data mode (rec_v90: 50666 / 28800) | 38.9 | 32 % |
| x2 data mode (rec_x2: 32000 / 24000) | 80.4 | 65 % |

x2's data mode costs 206 % of V.90's, against the plan's "no more than
V.90's". The cost is the upstream the client dictates (x2-facts.md
"Upstream", as the Courier's ATI11 confirms): 64-state trellis, non-linear
encoding, expanded shaping; V.90's upstream is 16 states, linear, minimum
shaping, which p34d asks for in its MP and x2 has no way to ask. A scratch
build of client and server with 16 states, linear and minimum shaping
(not kept) costs the same as V.90 on the host bench (0.2 % of the host
against 0.4 % with the Courier's options, 3 runs each). One x2 call fits
the box; two at once would not.

`make check`: ALL HOST CHECKS PASS. `PAR=16 make check-arm` (armv5
uclibc, qemu): ALL ARM CHECKS PASS; arm_test_x2tx 50 checks,
arm_test_answer_x2 156 (its x2 cases all run on ARM, about 8 s, and print
the host's lines exactly; only the older TEST_SHORT block is left out),
arm_test_p2ans 255, api 458. clang -fsanitize=undefined builds of
test_x2tx and test_answer_x2 print what gcc's do.

Replay proof (every live-050 recording, 106; v90m-replay at 109c2d0
against this branch; flags "", "-9", "-x", "-x -9"): all four
comparisons identical, .bits included.

Review fix (x2 6): our record's word 1 named every rate index (the
I-modem's 7FFF) against the design's "our record's rate mask names only
the indices that have banks". Against the emulated Courier at PAD=0: 0001
behaves as 7FFF (CONNECT 48000, V.42 up, the rate change at 12.1 s; PAD=3
CONNECT 50666 too); 0002 never gets a CONNECT from it and it hangs up 7.1
s into our data mode. Word 1 is now 0001, bit n - 1 for index n (the MP's
word 1 convention), and X2D_RATE_MASK is that same word, so the record
and the check of the client's MP agree (the 0x0002 / bit n above is
superseded). x2-facts.md "Records" has the runs; 48000 and 50666 are the
rates of index 9 and 11. test_x2tx pins the word; test_answer_x2's record
check in sim/x2call follows.

## x2 7: the client's rate change (2026-11-20)

x2d now answers a rate change of the client's (docs/history/x2-facts.md
"Rate change"). It no longer ends the call 6 s after the client's S.

- `modem/x2d`: when the client's S comes in data mode (X2D_RENEG), the
  data stops going up and goes on down. sres_resume follows its S-bar. Then
  come 4-point decisions (RX_RN) on its TRN and its MP, which is checked as
  at the start-up. After its MP (never before), `x2tx_data` and `x2tx_go`
  start our answer. Its E leads to B1 and data at the new upstream rate,
  and then back to X2D_DATA with the rates taken again. The limit is 6 s
  from its S (X2D_ERR_RENEG).
- `modem/x2tx`: `x2tx_go` in data mode starts a rate change after the
  frame being sent. It sends 8 frames +++--- and one ---+++ at the largest
  level of the data levels in use, as sent (a5 / 25 as received). Then
  GPC restarts from zero with the sign positive, and our record goes out
  in levels B, 10 times as 733C and 33 times as F33C. Then one frame of
  ones in levels B, 681 at the new rate, and data from the owner's next
  bit.
- The Task 6 review's minors: `x2d_cleardown` now works in X2D_RENEG too.
  The MP's MD (word 3, high byte) must be 5, in the first MP and in a rate
  change's alike; any other value gives X2D_ERR_RATE.
- `modem/answer`: an end after data mode other than carrier loss or our
  cleardown counts as x2_failed (V90M_END_X2_FAILED). That covers a rate
  change we cannot serve or that does not complete.
- `sim/x2call`: `reneg_ms` gives the rate change: S, S-bar, TRN of 512
  symbols, its MP (reneg_index, reneg_mask, reneg_md) and MP' and E, all
  on 4 points, then B1 and data. Downstream, the sim checks our marker,
  records, ones and data frame by frame, and counts any marker frame
  before its MP. `md` sets the first MP's MD.
- The emulated Courier's rate-change MP is on 4 points, not 16. Found
  with an `X2D_TRACE` build (an fprintf trace, off by default, as p34d's
  V42_XID_TRACE). It asks for index 14 (12 behind 3 dB), above our
  mask, so we clear down, as the design says until there are more banks.
  Recorded in x2-facts.md "Rate change" and "Harness".

Tests (each seen failing first):

    test_x2tx: 66 checks, 0 failures
      x2tx rate change: marker 0 bad; records 10 733C then 33 F33C (0 bad); ones 1 + 681 frames; data 24000 bits on, 0 wrong; 32000 bit/s
    test_answer_x2: 303 checks, 0 failures
      x2 MP has MD 6: ended 6.47 s after the marker, error 5, x2_failed 1, 4 MPs sent
      x2 rate change, no pad (and 3 dB): data 32000 / 28800 bit/s; x2d saw the client's S 0.020 s after it began, data again 0.92 s after it
         client saw: marker 8 + 1 (0 bad, 0 before its MP), records 10 733C then 33 F33C (0 bad), ones 1 + 681 (0 bad), data frames 19017 (0 lost)
         down 456408 bits 0 bad (50016 after the change); up 369156 bits 0 bad (53256 after), the client's S: 300 bits lost, 0 extra
      x2 rate change asks for index 5 / has MD 6: ended 0.27 s after the client's S, error 5, x2_failed 1
      x2 rate change and our cleardown in it: ended 0.02 s after the client's S, error 12, cleared 2

The upstream loses the 300 bits that are still in our receiver's
decision delay when the client's S begins. The test resumes its check
from the client's next bit, and V.42 recovers such a loss. On TEST_SHORT
(ARM under qemu) the rate change comes 2 s into data and runs behind no
pad only.

The Courier (`make test-x2-courier MODS=x2`, PAD=0 / 3 / 6): each run
passes the start-up as before (CONNECT 32000/24000 x2, V.42 LAPM). Its
rate change asks for index 14 / 12 / 14 with mask 1FFE. We clear down
94 ms after its S ("ended: x2 failed"), and the Courier goes on hook 47 s
later. The call does not go on past the Courier's rate change until the
banks for its index exist (the bank task).

## x2 8: rates up to 56000 (2026-11-20)

x2's downstream now runs at every rate index the client can ask for, 1 to
15 (32000 to 56000 bit/s), behind no pad, 3 dB or 6 dB, at the start-up
and in each of its rate changes. The emulated Courier's rate change is
followed to the rate it asks for, and the call goes on.

- **The banks are included, not derived** (x2-facts.md "Banks"). No rule
  we tried reproduces the published mode 0 banks for 1, 8, 11, 12, 13 and
  14 (a greedy minimum-spacing ladder fits 17 of the 30 lists, but not the
  first position's of 12 or 14), and the Quad firmware builds its banks at
  run time instead of storing them. So `modem/x2bank.c` holds the 45 banks
  (3 pad modes x 15 indices) as data, generated by `tools/gen_x2_banks.py`
  from D-Modem's published values, which it read back from the Quad's
  constructor (QF060003). Origin in the file's head and in `docs/limits.md`.
  A pool of octets, 80 lists of (received, sent, n), and per bank B and
  the lists of position 0 and of positions 1..5.
- `modem/x2map`: each position has its own list (`x2map_list`), B up to 37
  bits (the number in 64 bits), and the free sign and the MD signs follow
  the levels received, not the digits, as the facts say (where the lists
  differ the two disagree: index 12, all digits 0, is `9e` at position 0
  and `9f` at the others, so position 0 has the largest level).
  `x2map_unframe` refuses digits past B bits.
- `modem/x2tx`: the rate change's marker has each position at its largest
  level in the rate in use (9e / 9f at 52000, as the I-modem's). From the
  client's MP on, everything goes out for the pad that MP reports: marker,
  records and new rate (one rule, stated in x2-facts.md "Rate change"). The
  two minors of the Task 7 review.
- Our record's word 1 and `X2D_RATE_MASK` are `7FFF` (every index has a
  bank in every pad mode), the I-modem's. Index 0, MD other than 5 and pad
  modes above 2 still clear down.
- `sim/x2call`: a second rate change (`reneg_index2`, `reneg_ms` after the
  first's data), per-rate-change counts, the marker per position, and
  `x2call_level`: the client takes each octet for the level whose octet
  sent arrives as it behind the pad. Two levels of the Quad's 3 dB banks
  (`e2`, `f1`, sent as `dc`, `ed`) arrive one code off under the pad model;
  the I-modem sends the same octets, so we keep them (x2-facts.md "Banks").
- `tools/rec_x2`: an optional fourth argument, the index the client asks
  for (for the cost at 56000).

Tests (each seen failing first):

    test_x2map: 46 checks, 0 failures
      x2map: 45 banks, 0 entries not as the facts, 49 mode 1 levels no octet arrives as; 90000 frames, 0 not as the model or not back
    test_x2tx: 109 checks, 0 failures
      x2tx rate change to index 12, pad mode 0: marker 0 bad; records 10 733C then 33 F33C (0 bad); ones 1 + 681 frames; data 39000 bits on, 0 wrong; 52000 bit/s
      x2tx rate change to index 14, pad mode 0: marker 0 bad (9e / 9f) ... 54666 bit/s
      x2tx rate change to index 8, pad mode 2: marker 0 bad (8d / 8e: the MP's pad) ... 46666 bit/s
    test_answer_x2: 359 checks, 0 failures
      x2 rate changes to 8 and 14, no pad (and 3 dB, 6 dB): data 46666 then 54666 / 28800 bit/s; data again 0.92 s after the client's last S
         client saw: markers 16 + 1 (0 bad, 0 before its MP), records 20 733C then 66 F33C (0 bad), ones 2 + 1362 (0 bad), data frames 36160 (0 lost)
         down 1084683 bits 0 bad (76852 after the last change); up 747876 bits 0 bad (50040 after), the client's S: 960 bits lost, 0 extra
      x2 rate change asks for index 0 / has MD 6: ended 0.27 s after the client's S, error 5, x2_failed 1

On TEST_SHORT (ARM under qemu) the two rate changes come 2 s into data and
2 s apart, behind no pad only.

The Courier (`make test-x2-courier MODS=x2`, PAD=0 / 3 / 6; x2-facts.md
"Harness"): CONNECT 32000/24000 x2 at 30.40 s each time. Its rate change
at 42.50 s asks for index 14 / 12 / 14, and we follow it: 54666/28800,
52000/28800, 54666/28800 at 43.24 s. At PAD=3 a second one at 111.56 s
asks for 13: 53333/28800 at 112.32 s. Every call stays up with V.42 to
the budget's end (116 s). Its MP' and E in a rate change are on 4 points,
and B1 follows E.

Size (ARM, -Os): the tables go from 72 bytes (`x2tab.o`) to 9 + 4577
(`x2tab.o`, `x2bank.o`), +4.5 KB; `x2map.o` 928 -> 1304, `x2tx.o` 1776 ->
1800. CPU (`bench_v90 -x` on a 40 s `rec_x2` call, second 15 to 16):
81.9 M ARM instructions per second at 32000 (80.4 M at x2 6: the general
mapper, +1.9 %), 83.7 M at 56000 (+2.1 % over 32000).

## x2 9: the Courier's data after its rate change (2026-11-20; release held)

The release was to rest on data the emulated Courier decodes after its rate
change; until now the evidence was indirect (V.42 stayed up). The harness
now pushes data and counts the acknowledgements, and the Courier does
**not** decode our downstream after the change. 0.4.0 is not released.

- `tools/x2-peer/bridge.c`, `-k KB`: once LAPM is up a control burst of KB
  KiB of incompressible octets, and another after each x2 rate change once
  data moves again. Each is logged when the Courier has acknowledged all of
  it, with the line frames in which its N(R) advanced and the errors seen
  from our end (I frames sent again, REJ, violations, T401). With
  `MODS=x2`, `run.sh` passes `-k 4` (`KB`), and the run passes only when
  there was a rate change and every burst was acknowledged cleanly. A
  bridge exit of 1 no longer kills the Courier, so its report is kept.

`make test-x2-courier MODS=x2 PAD=0 / 3 / 6` (x2-facts.md "Harness"), the
same figures in two runs at each pad:

| | PAD=0 | PAD=3 | PAD=6 |
| --- | --- | --- | --- |
| control at 32000 (32.88 s) | 33 of 33 I frames acknowledged in 1.22 s, nothing sent again | the same | the same |
| after the rate change (43.24 s) | 54666: **0 of 615** acknowledged, 68 T401 | 52000: **1 of 658**, 2 REJ, 68 T401 | 54666: **0 of 600**, 68 T401 |
| `x2-peer` | FAIL | FAIL | FAIL |

The Courier answers every poll after the change (55 to 59 good frames from
it), acknowledging nothing: short frames get through and I frames of some
128 octets almost never do. Without error control (a one-off scratch run,
PAD=0) its terminal shows our text clean before the change and mostly
corrupt after it: 20 % of what it printed in correct runs, median 12
octets. Our simulated client decodes with our own banks and mapper, so its
bit-exact runs could not show this. Index 1 is the only rate shown to carry
data to the Courier. The x2 8 reading ("the call stays up ... the evidence
that it decodes our new rate") was wrong: LAPM stays up on polls alone.

Also (the Task 8 review's minors): `test_x2map` pins the 49 mode 1 levels
no octet arrives as, and checks that mode 2's received lists equal mode
0's at all 90 positions (47 checks, 0 failures; the check fails at 90 of 90
when pointed at mode 1); `test_answer_x2`'s comments give the second rate
change as `reneg_ms` (12.7 s) into the first one's data, not 3 s; x2-facts
"Records" puts our `0001` record in the past.

## x2 8b: the Courier's data after its rate change, debugged (2026-11-20; BLOCKED, release held)

x2 9 found that the emulated Courier decodes our downstream at index 1
(32000) and not after its rate change (54666 at PAD=0 and 6, 52000 at
PAD=3). x2 8b looked for the cause in our octets and found none: the same
Courier fails alike on D-Modem's server and on the I-modem's own firmware,
whose octets are ours. The fault is the emulated Courier (or the harness's
line model, which every server shares), not our mapper, banks or rate
change. Per the brief, no library code was changed to suit it.

The evidence (x2-facts.md "Oracle: D-Modem's mapper" and "Harness", the
last part; the runs are scratch, outside the repository):

- **Mapper and banks.** D-Modem's `X2Map`, built and run as a black box,
  and our `x2map_frame` give identical octets at all 15 rate indices in
  pad modes 0, 1 and 2 (20000 frames from each of three seeds): no first
  differing frame. The I-modem's 106 570 data frames at index 14, as it
  sent them to the Courier, decode with our lists and give LAPM frames
  with good FCS.
- **Rate change and start-up.** On the line, our octets equal D-Modem's
  and the I-modem's from the preamble through the sync frames and records,
  and through the marker, the 43 records and the 682 frames of ones of the
  rate change; only the free sign of data-level frames differs (it differs
  between the I-modem and D-Modem too, and the client does not check it).
- **The Courier.** With the same settings and line, after the change:
  ours 0 of 615 I frames acknowledged, and without error control 0 of some
  73 text lines intact; D-Modem 0 of 72 lines (PAD=0), 10 of 71 (PAD=3,
  52000), 0 of 68 (PAD=6), and with V.42 only the lines of its first
  second; the I-modem's 34 short I frames (429 octets) needed 1 to 19
  transmissions each, one 10-octet frame 15.8 s. Before the change all
  three deliver everything.

Test (independent vectors, no fix behind it): `test_x2map` holds our
mapper to D-Modem's output at indices 1, 8, 12 and 14 in each pad mode
(frames 0, 1 and 3999 and an FNV-1a of 4000 frames, from the oracle run,
kept as data); `x2map: D-Modem's mapper's octets (oracle run) at 12 of 12
rate/pad pairs`. It passes on the current code, since the mapper is right.
Its teeth: with the free sign's tie rule turned (`<` for `<=`) 12 of 12
pairs fail; with positions 0 and 1 sharing a list, the 8 pairs whose
lists differ by position fail. Index 1's octets are unchanged.

`make test-x2-courier MODS=x2` still fails at every pad (x2 9's figures,
reproduced at PAD=0: 0 of 615); it cannot pass on this emulator. What
would: a check against a Courier that decodes the I-modem after its rate
change (another emulator setting, or a real modem), or an acceptance that
rests on the octets being the I-modem's, which this branch now shows.

Follow-up (a bounded search for a setup that passes; x2-facts.md
"Harness", the last part). D-Modem's documented courier-emu setup used a
locally modified courier-emu (`COURIER_AT`, `COURIER_NVRAM`,
`COURIER_INT0_TOUT` are not in the published one) and lines 6 to 14 dB
down; the rest is our harness. Our bridge's clock is not off: the line is
sample-locked (equal sample counts each frame; the stamp clocks nothing;
the Courier's codec at exactly 9600 Hz, 6 to 5), so the Courier's -112 /
+159 ppm are its own estimates, and courier-emu's one offset setting is
already zero (+-112 ppm breaks the start-up). Lines 3, 6, 8 and 10 dB down,
and the Courier's INT0 at 2700 to 4800 Hz, pass no data after the rate
change, for us (best: 15 of 265 I frames at 52000, 6 dB down) or for
D-Modem (best: 3 of some 72 lines). Nothing passed in the time box; the
bridge is unchanged and the acceptance check still fails at every pad.


### x2 9, continued: release 0.4.0 (2026-11-20/21)

After x2 8b (the emulator, not our octets, fails above index 1) the
ruling: labels as below, the post-change data check opt-in, the free sign
listed as a known difference, then the release.

- **Labels** (CHANGELOG, README, docs/limits.md, docs/answering.md): at
  32000, "decoded by the emulated USR Courier, V.42 data acknowledged";
  above 32000, "octet-identical to the USR I-modem and D-Modem's server;
  the published courier-emu decodes no server above index 1; not yet
  confirmed on a real modem".
- **Harness** (d09ce5b): `make test-x2-courier MODS=x2` passes on the
  control burst at 32000 and prints the post-change counts as
  information; `STRICT_RENEG=1` requires them. PAD=0, 3, 6: PASS; PAD=0
  strict: FAIL (0 of 615), as for every server on this emulator.
- **Version 0.4.0**: Makefile, header, `v90m_version()` (test_api first:
  `api: 458 checks, 1 failures` on the string, then on the numbers, then
  0). `v90m_mem_size()` 118824 on x86-64, 118512 on ARMv5 (8 octets more
  than 0.3.0); `V90M_MEM_SIZE` unchanged.
- **Docs**: CHANGELOG 0.4.0; README (x2 in the list, the standards table,
  what was tried, the CPU table: x2 81.9 / 83.7 M at 32000 / 56000);
  DESIGN 4.2b (x2 done, the stated exception to "written from the
  recommendations"; K56flex not); docs/answering.md (an x2 section),
  api.md (`V90M_MOD_X2`, `V90M_CONN_X2`, `V90M_END_X2_FAILED`, retrain and
  cleardown in x2), limits.md (an x2 section: offered only, A-law, robbed
  bits, no way back, the free sign, 3 dB octets; CPU twice V.90's, one x2
  call per box), testing.md; the header's comments on retrain and
  cleardown. The "x2 5" section above was missing and is added.

Checks on the release tree:

| check | result |
| --- | --- |
| `make check` | ALL HOST CHECKS PASS (api 458, answer_x2 359, test_x2map 59 checks, 0 failures) |
| `make check-ubsan` | ALL UBSAN CHECKS PASS |
| `PAR=16 make check-arm ARM_CC=...armv5...` | ALL ARM CHECKS PASS (answer_x2 247 TEST_SHORT; x2map's lines as the host's) |
| `PAR=1 tools/check-ports.sh build/ports-t9 host-gcc host-clang i686-musl aarch64be-musl mips-musl win32-mingw` | host-clang, i686-musl, aarch64be-musl, mips-musl, win32-mingw: 115 of 115 programs identical to host-gcc each; ALL PORTS PASS (17:11 to 00:06) |
| replay proof, 106 live-050 recordings, 19ba2c6 (0.3.0) against this tree | `""` and `-9`: identical (106 .txt; 21 / 40 .bits). `-x` and `-x -9`: the same 6 differ as at x2 4 (ab_cc_v34, ab_cl_v34, v34a, v34b, v34c, x1), each only in +2094 transmit samples (x1 110394 -> 112488), no other line, .bits identical |
| `make test-x2-courier MODS=x2` PAD=0 / 3 / 6 | PASS each; `STRICT_RENEG=1` PAD=0 FAIL (by design) |

(The tag `v0.3.0` in /home/georgica/libv90modem points at a v90d commit,
90687bd; the base was 19ba2c6, origin/main at 0.3.0, as at x2 4.)


## k56 1: the facts, written down (2026-11-21)

`docs/history/k56-facts.md`, the K56flex counterpart of x2-facts.md: every
value later tasks use, by section, each tagged documented / RE (who, from
what) / our observation / derived. No product code.

Sources fetched outside the repository:

- D-Modem cloned to `~/modem-research/D-Modem` at `c06a380d` (`git log -1`:
  `c06a380d86105b2309c8cb8958e41120856bb84b`); its `docs/dsp/k56flex-details.md`
  is identical to the copy in `~/modem-research/sources/`. Code read for
  values only. Not built.
- ITU-T V.8 bis (11/2000) to `~/modem-research/itu/` (SHA-256 `1eddb79f...`);
  the CL, SM56's MS and ACK(1) checked against Tables 3 to 6 and 8.2.3, 8.5,
  octet by octet.
- Gough Lui's recording `k56flex-48000.wav` to `~/modem-research/goughlui-k56/`
  (SHA-256 `e45f73cc...`), decoded with throwaway Python there (V.21 FSK and
  HDLC, V.34 INFO DPSK, tone and level steps) and two small C programs that
  link the repository's `modem/info.c` and `modem/v8msg.c` read-only.

What the recording settled (a real Rockwell RCV56DPF client and a real
server):

- The server sent **CRe** (segment 1 about 290 ms), not MRe; CL `12 c9 80
  80 81 c2 09 b5 02 00 94 81 83 42 46 e0` (FCS `23 50` good): the 9-octet
  block of decision 3 exactly, but with an S field offering data and V.42
  (`81 c2`). The client's MS `11 c9 80 80 81 c2 09 b5 02 00 94 81 81 02 47
  85` (FCS `71 d8`), ACK(1) `14` (FCS `dd a6`). CM and JM `e0 c1 45 13 94 2a
  0d` (V.34, no PCM; no digital-access bit in the JM).
- INFO1c offset `0x009`, INFO1a offset `0x005`: an **A-law** K56flex call,
  confirming the law bits D-Modem inferred.
- Training lengths: TRN1A about 16650, TRN1B about 8180, TRN1C and Js about
  18470, TRN2A and MARK about 8380 (SM56-accepted: 16368, 8448, 8448 + 128,
  16336 + 32); data at 48000 from about 2.2 s after TRN2A ends.

Found while writing it down (in "Unsettled", for the controller):

- D-Modem's MS check skips four level 1 blocks and would reject the real
  client's MS (its S field has a data Par(2) block); the "MS test" section
  parses the V.8bis trees instead.
- D-Modem restarts the data scrambler, running sum, shaping counter and
  3-frame cycle at every data start (each MP), and SM56 re-initialises its
  decoder on each MP: they do not carry across a renegotiation.
- V.8bis 10.2.2 asks for 400 ms of silence before MRe/CRe (spec: 200 ms);
  9.8 asks for NAK(1) on an invalid frame (decision 2: never NAK).

`glibc-devel.i686` is not installed (`rpm -q`): the SM56 harnesses cannot
run yet ("Harness"). No build or test change in this task; `make check` run
before the commit as the plan asks.

## k56 2: the oracles and the SM56 harnesses, outside the repository (2026-11-21)

`glibc-devel.i686` (and `libxcrypt-devel.i686`) installed by the owner during
the task: the SM56 harnesses ran; the release label is not held at L0 by the
toolchain.

D-Modem at `c06a380d` in `~/modem-research/D-Modem`, built unmodified:

    cmake -B build -DCMAKE_C_FLAGS=-mstackrealign -DCMAKE_CXX_FLAGS=-mstackrealign
    cmake --build build -j16
    ctest --test-dir build -R k56flex -L sm56     # 3 of 3 pass

(without `-mstackrealign` GCC 15's `half_sm56` faults at once: an aligned SSE
store on the module's 4-byte-aligned stack). D-Modem's server against SM56
(`sm56_k56flex`): CONNECT 44000/31200; its timeline is in `k56-facts.md`
"Harness".

Oracles (ours, `~/modem-research/k56-oracle/`, `make`):

- `./enc > test/data/k56enc_oracle.h`: `K56Enc` at 13 rates x 3 pads x 64
  masks, 600 frames each, 0.3 s. `K56Enc` scrambles internally; the pipeline
  for our encoder (xorshift32 from 1, GPA from 0, then the encoder) is in
  "Oracle" and the header's comment.
- `./train > test/data/k56tx_oracle.h`: `K56Train` driven alone (no
  `open_k56flex` run needed), 12 cases of J (`00 40 80 04 12 98` x J after
  12000 / 13000 of silence 2, MP after 3000 of TRN2B) and one renegotiation
  (MP after 1500); per segment length, FNV-1a, first 8 octets, to the first
  data frame; our MP's octets included.
- `sm56dec/` (32-bit, `sm56.ko`'s `pstn_init_rx` / `putBIOS`): on `enc -d`'s
  dumps, 3 of 3 (32000/0/00, 44000/1/12, 56000/2/3f) decode to the data
  sent; `check.sh DIR` runs a directory (Task 7's `k56-dump`).
- GPC upstream settled from D-Modem's `k56_start` / `V34Rx::init` (RE).

The peer (`tools/k56-peer/`, `make test-k56-sm56`, not in `check`):

    make test-k56-sm56                                        # DMODEM not set, exit 0
    make test-k56-sm56 DMODEM=/x CC=false                     # no 32-bit toolchain (glibc-devel.i686), exit 0
    make test-k56-sm56 DMODEM=~/modem-research/D-Modem MODS=none
      ours: CONNECT 28800/28800 V.34 at 13.0 s of line; sm56: CONNECT 28800/LAPM
      to SM56: 4096 octets, identical; to us: 4096 octets, identical; PASS
    ... RBS=0x08                                              # PASS, the same
    ... MODS=k56                                              # bridge: no V90M_MOD_K56 yet (Task 5), FAIL

SM56 dials `AT+MS=K56FLEX,0` (the brief's): with nothing answering its
V.8bis it falls back to V.34 by itself, so Ruling F2's automode is not
needed for a fall-back. With automode (`AT+MS=K56FLEX,1`, `SM56_MS=`) its CM
offers V.90 too and **our V.90 answerer fails against SM56 in Phase 2** (no
INFO1a after our INFO1d; SM56 NO CARRIER), as with `AT+MS=V90`; D-Modem's
server passes that call. Recorded in "Harness", not investigated here.

Corrections to `k56-facts.md` from Task 1's review: the server-started
renegotiation sends R (not R2); a retrain repeats Phase 2 (INFO1), not
"INFO1a flagged again".

`make check`: ALL HOST CHECKS PASS (nothing new runs in it).

## k56 3: the rate tables (2026-11-21)

`modem/k56tab.{h,c}`: K, u, r and M per pad of the 13 rates, the 39 ring
lists (468 octets) and the 39 constellations (1382 octets), `k56_ring(rate,
pad)` and `k56_bios(rate, pad)`. The rule was tried first, in a scratch
script outside the repository (`k56-facts.md` "Tables"): rings by group
energy, 0 of 39; magnitudes as a minimum-distance ladder, 25 of 39 behind
the pad and only with each table's first point and spacing given. So the
rings and magnitudes are included, generated from D-Modem's published
values (`docs/limits.md`, "Included tables (K56flex)"):

    python3 tools/gen_k56_tables.py ~/modem-research/D-Modem/dsp/k56flex_tables.cpp > modem/k56tab.c
      gen_k56_tables: 468 ring octets, 1382 constellation octets
    make build/test_k56tab && build/test_k56tab
      test_k56tab: 3838 checks, 0 failures

The generator checks K, u, r and M against the facts, the frame identity,
M^8 >= 2^K, each ring list a permutation and each constellation M << (u -
1) increasing magnitudes. Not in the signal path yet (Task 7's encoder
uses it); no check-arm.

## k56 4: the V.8bis messages (2026-11-21)

`modem/v8bismsg.{h,c}`: the FCS (CRC-16/X.25, checked against "123456789"
= `0x906e` and the recorded `23 50`, `71 d8`, `dd a6`), our CL (default the
real server's 16 octets, ruling C2; `V8B_NS7` the 13-octet form;
`V8B_NS_DEFAULT` overridable), ACK(1) `14`, the framing (32 ones, three
flags, octets and FCS LSB first with zero insertion, one closing flag; V.8bis's
own, not V.42's HDLC), the receiver (`v8bis_rx_bit`: octets, 0, or -1 for
fewer than three octets, not whole octets, bad FCS, too long, abort), SM56's
CL block test, and `v8bis_ms_is_k56`, which walks the I and S field trees of
8.2.3 (ruling C3), so the real client's MS (`81 c2` Par(2) block) passes where
D-Modem's fixed skip reads it as an NS block of length `0xc2`.

    make build/test_v8bismsg      (first: no rule for modem/v8bismsg.c; then the
                                   first run failed 10 checks, receiver and trees)
    build/test_v8bismsg
      test_v8bismsg: 297 checks, 0 failures

Not in the signal path (Task 5/8 use it); no check-arm.

## k56 5: the API, and Phase 1 with fall-through (2026-11-21)

`V90M_MOD_K56` (0x0200: answering only, with V.34, not in the defaults),
`V90M_CONN_K56FLEX`, `V90M_END_K56_FAILED` (both appended); `v90m_mem_size()`
pinned by test_api at 118824 (x86-64) and 118512 (32-bit ARM). The answerer
(`ANS_MODE_K56`, mu-law with V.34 only) begins with `ANS_V8BIS`
(`modem/v8bis`, in `answer_t.u` while it runs): 400 ms of silence (ruling C6),
CRe (ruling C1: 1375 + 2002 Hz 290 ms, 400 Hz 100 ms, 14.6 dB below our
messages, as the real server), CRd's pair begun within 1000 ms of CRe's end
and followed to its 1900 Hz end, our CL (the real server's, ruling C2) on V.21
channel 1 120 ms later, the MS on channel 2 until 1500 ms after the CL,
ACK(1) 180 ms after an MS with the block, NAK(1) after an invalid frame;
then V.8 as before. An agreement turns x2 off for the call and is handed to
V.34 (`v34ans_cfg.k56`, used from Task 6). A-law, or K56flex not offered:
no new state is entered. `sim/k56call` (a client from the facts, then
`sim/v34peer`), `tools/k56_crd_scan.c` (`make crd-scan RECS=<dir>`),
`v90m-replay -k`.

    make build/test_v8bis              (first: no rule for modem/v8bis.c)
    build/test_v8bis
      CRe: 1375.0 + 2002.0 Hz, 400.0 Hz; 14.6 / 14.6 dB below the CL
      CL begins 125.0 ms after CRd ends; ACK(1) 180.0 ms after the MS ends
      test_v8bis: 48 checks, 0 failures
    build/test_answer_k56              (first: ANS_V8BIS undeclared)
      K56flex client, K56flex and x2 offered: V.34 28800 / 28800, 0 bad; agreed 1, x2 0, ACK 1
      V.8bis caller without K56flex: our ANSam 1700.0 ms after our CL ended
      our calling modem in V.90 / V.92, K56flex offered: 52000 / 28800, 52000 / 34666, 0 fail,
        the same as the reference (which starts as late as V.8; V.92's rates move a step
        with the line's noise), the same octets for 12 s from V.8 on
      answer_k56: 115 checks, 0 failures
    make check                         ALL HOST CHECKS PASS

Delays before our ANSam, measured in test_answer_k56 (for `docs/limits.md`,
F10): a caller without V.8bis 1.790 s later than without K56flex; a V.8bis
caller without K56flex (CRd, no MS) 3.807 s later (ANSam 1700 ms after our
CL); a K56flex client 3.413 s later (ANSam at 3.613 s after answering).

The CRd scan over the 106 recordings of `~/sync-build/live-050`:

    make crd-scan RECS=~/sync-build/live-050
      k56-crd-scan: 106 recordings, 0 CRd detections (0 with the 1900 Hz end)

The same detector on the client channel of Gough Lui's recording (outside
the repository) finds its one CRd: the pair at 2.040 s (it began at 1.885),
the end at 2.400 s (1900 Hz ended at 2.390).

Replay proof: the 0.4.0 + x2-gate tree (704b370, `~/k56-base`) against this
one, the 106 recordings, flags `""`, `-9`, `-x`, `-x -9`, each run in its own
directory: every `.txt` (424) and `.bits` (122) byte-identical. With `-k`
(and `-k -9`) every recording completes; V.8bis hears silence and gives up
(result 2, no CRd) in all 106; the final modulation and rates line equals
the run without `-k` in 106 of 106 (the transmit-difference count grows by
CRe, which the recording does not have).

SM56 (`make test-k56-sm56 DMODEM=~/modem-research/D-Modem MODS=k56`, SM56
dialled `AT+MS=K56FLEX,0`): PASS, V.34 28800 / 28800, 4096 octets each way
identical. Decoded from its `call.rec`: SM56's CRd, our CL with the
9-octet block, SM56's MS `11 c9 80 80 80 09 b5 02 00 94 81 83 02 46 e0` (the
block, our 9-octet form echoed with version `02`), our ACK(1) `14`. So the
9-octet form stays the default (`V8B_NS_DEFAULT` = `V8B_NS9`). INFO1a is not
flagged yet (Task 6).

## k56 6: the INFO1a flag, the point of no return (2026-11-21)

`p2ans_cfg.k56` (from `v34ans_cfg.k56`, in the first Phase 2 and in a
retrain's): when INFO1c's frequency offset is `0x001` and the symbol rate
chosen is 3200 (spec decision 6, the builder-check answer), INFO1a's bits
40:49 are `0x001` (mu-law, x = 0), `p2ans_t.k56` is set, and Phase 2 ends in
`P2N_K56` (silent) where it would end in `P2N_DONE`; `v34ans` goes to
`V34A_K56`, the answerer to `ANS_K56`, which (until the engine of Task 9)
ends the call on the next sample with `k56_failed` (`V90M_END_K56_FAILED`).
Any other agreed call is plain V.34, with a measured offset ending in binary
01 moved one step toward zero (`p2ans_k56_nudge`). L1, L2 and everything
without `cfg.k56` unchanged. Test peers: `p2c_cfg.k56` (INFO1c `0x001` or
`0x200`), `v34peer_cfg.k56` (stops in `VP_K56` after a flagged INFO1a),
`k56call_default`, `k56call_cfg.info1c_k56` (default 1), `up_sr_3000`.
test_p2ans shifts the caller's frequency (a Hilbert transformer) to make
the measured offsets the nudge works on.

    build/test_p2ans                   (first: P2N_K56 undeclared; then, with the
                                        interface alone, 21 failures)
      K56flex flag: INFO1a offset 1, P2N_K56 at 2.0293 s (P2N_DONE without: 2.0293 s), silent
      INFO1c 0x200, line offset +0.10 / -0.06 / +0.02 / -0.14 / +0.18 Hz:
        measured 5 / -3 / 1 / -7 / 9, INFO1a 4 / -2 / 0 / -6 / 8; 0, 4, -2 kept (same samples)
      INFO1c 0x001 at 3000 baud (client without 3200, or 3200 not allowed): unflagged, 1 -> 0
      p2ans: 402 checks, 0 failures
    build/test_answer_k56              (first, without the answer.c change: 3 failures)
      agreed client, INFO1c 0x001: INFO1a offset 1 at 3200 baud, ANS_K56 at 7.884 s,
        the client took the flag at 7.873 s, the call ended 11.0 ms after that
      agreed, INFO1c 0x200: V.34 28800 / 28800, as without K56flex
      agreed, 3000 baud up at most: V.34 28800 / 26400 at 3000, as without K56flex
      answer_k56: 156 checks, 0 failures
    build/api_test_api                 (first, without the answer.c change: 4 failures)
      API K56flex call: the client took our flagged INFO1a at 7.836 s, end 10 (K56_FAILED) 11.9 ms after
      api: 490 checks, 0 failures
    make check                         ALL HOST CHECKS PASS
    make -j8 check-arm ARM_CC=...armv7-eabihf musl...
      p2ans: 402 checks; answer_k56 (TEST_SHORT): 108 checks; api: 490 checks (the
      K56flex call 11.9 ms, mem size as pinned)   ALL ARM CHECKS PASS

Replay proof: `~/k56-base` (704b370) against this tree, the 106 recordings
of `~/sync-build/live-050`, flags `""`, `-9`, `-x`, `-x -9`: every `.txt`
(424) and `.bits` (122) byte-identical. `-k` and `-k -9`: 106 of 106
complete, the final modulation line as without `-k` in 106 of 106.

## k56 7: the data encoder (2026-11-21)

`modem/k56enc` (frames of 8 symbols, "Data" and "Robbed bits" of
`k56-facts.md`): the K' shell bits (shifted left by L - n on a robbed-bit
line; K' = K, no shift, at 3 robbed in 6) through V.34's shell mapper, then
per symbol a sign bit (none on every r-th symbol over the call, whose sign
is set against the running sum: positive at 0 or below) and q bits (bit 0
not sent on a robbed symbol), ring to group by `k56_ring`, point to
magnitude by `k56_bios`, mu-law. 5 robbed count as 6; no robbing at 32000
and 34000. `k56enc_set_rate` is a new encoder (Ruling C4: nothing carries
across an MP). `k56enc_unframe` inverts it for the tests and the coming sim
client. V.34's shell-table construction moved out of `v34_params_init`
into `v34_shell_tables(p, M)` (same arithmetic; v34's own tests and every
V.34 test unchanged) so that K56flex sets the tables for its own M; the
encoder holds a `v34_params_t` (1044 bytes on x86-64) for them.
`tools/k56enc_dump.c`, `make k56-dump`: our octets and data bits for SM56's
decoder harness.

    make build/test_k56enc             (first: k56enc.h: No such file or directory)
      frame / unframe: 78 of 78 rate x pad x mask, 2000 frames each
      shaping: 2416 symbols, 0 with the wrong sign, 0 frames whose shaping signs carried data
      oracle: 2496 of 2496 rate x pad x mask equal to D-Modem's; frames 0, 1, 599 at mask 0: 0 of 39 differ
      k56enc: 98 checks, 0 failures   (0.28 s; also clean under clang -fsanitize=undefined)
    mutations: shaping sign `< 0` for `<= 0`: 57 of 2496 differ; 5 robbed not
      folded to 6: 198 differ (the n6 = 3 entry of the loss table is not used)
    make k56-dump; ~/modem-research/k56-oracle/sm56dec/check.sh build/k56dump
      sm56dec: 2496 of 2496 decode to the data sent
    make check                         ALL HOST CHECKS PASS
    make -j8 check-arm ARM_CC=...armv7-eabihf musl...
      k56enc (TEST_SHORT, masks 00 24 3f 15): oracle 156 of 156, 98 checks, 0 failures
      ALL ARM CHECKS PASS

## k56 8: the training transmitter (2026-11-21)

`modem/k56tx` ("Training down", "Phase 4", "Rate renegotiation" of
`k56-facts.md`): silence 1, R1, TRN1A, TRN1B, TRN1C, Js, silence 2 (until
the client's J: max(12288, J) rounded up to 8), R2, TRN2A, MARK, TRN2B
(until our MP, at least 2048, and the grid of 24), MP, MP', E, then the
data through `k56enc` (B1 first, a new encoder and data scrambler at each
data start, Ruling C4), one mu-law octet a tick. One GPA scrambler, its
history and the running sum from R1 through E, carried across a
renegotiation (`k56tx_reneg`: R2 now, or at the end of the frame being
sent; TRN2A 2048, MP / MP' 1 bit a symbol, E 24). `k56tx_got_j` refuses
pad code 3 (silence 2 goes on; Ruling F3, k56d checks the J first). Our MP
and MP' from `v34_mp_build`, J from `v34_j16`, the scrambler `v90_scr`.

Two facts corrected (`k56-facts.md` "Training down", "Corrections (k56
8)"), found against the oracle and confirmed in D-Modem's `K56Train`:
TRN2B scrambles a one and a zero a symbol (not two ones); the even
symbols of TRN1C, Js, TRN2A and MARK scramble as many bits as the last
TRN2B / MP / E symbol did, so the first renegotiation's TRN2A and MARK
take 2 bits an even symbol (a later one's 1). The client still
descrambles TRN2A to ones (GPA's sequence decimated by 2 obeys the same
recurrence), checked for the call and two renegotiations.

    make build/test_k56tx              (first: undefined references to k56tx_*)
      our MP and MP' equal the oracle's: yes
      oracle: 13 of 13 cases equal to D-Modem's, segment by segment
        (before the corrections: 0 of 13; TRN2B on from the first TRN2B,
        then the renegotiation's TRN2A on)
      TRN1B: 0 of 8448 magnitudes not 40 41 4b 4c 50 51 50 51 by 3 scrambled bits
      Js: 4 words of 0x89b0, 0 bits wrong
      pad 1: 0 of 4432 TRN2B / MP / MP' / E symbols off 35 4d 58 60
      pad 1, renegotiation: 0 of 1432 MP / MP' / E symbols off 44 5d; E 24
      silence 2: max(12288, J) rounded up to 8 at J 0 .. 13007
      J with pad 3 refused: silence 2 still after 32000; a good J then ends it
      grid: MP at 2048 .. 2071: 0 with the data off the grid of 24
      renegotiation in mid-frame: R2 at the frame's end
      TRN2A's even signs descrambled, not ones: call 0, first renegotiation 0, second 0
      k56tx: 84 checks, 0 failures   (0.04 s; also clean under clang -fsanitize=undefined,address)
    make check                         ALL HOST CHECKS PASS
    make -j8 check-arm ARM_CC=...armv7-eabihf musl...
      k56tx: 84 checks, 0 failures (oracle 13 of 13 under qemu-arm)
      ALL ARM CHECKS PASS   (6 min 49 s)

## k56 9: training, MP and data at 32000 against a simulated client (2026-11-21)

`modem/k56d` (the engine from our flagged INFO1a on, as `x2d` for x2; `x2d`
untouched): `k56tx` down; up, the client as V.34's call modem (GPC): our
echo canceller (and the echo delay, when not given) on our TRN1A to TRN1C
while the client is silent; from our Js its S, PP, TRN (4 points), its J24
(two J words 24 bits apart, the octet between: pad and robbed symbols; pad
code 3 ends the call, Ruling F3), J' 24 bits after the last J word, TRN, MP,
MP' and E on 16 points (MP: two in a row that agree), then V.34 data (16
states, linear, minimum shaping: what our MP asks) with p34d's watchdog. Our
MP: type 0, rate_ac r = min(its rate_ac, max_down's code, 13), rate_ca the
highest rate of its mask at 3200 baud (at most its rate_ca and max_up),
mask 3FFF, asymmetric. Time limits: S 3 s from our Js, J24 3.2 s from its S
(Ruling F4: 2.14 s + 1 s), MP 4 s from our TRN2B, E 2 s from our MP', B1 1 s.
`answer`: `u.k56` in the union, `ANS_K56` runs it, `ANS_MOD_K56`,
cleardown (silence 0xff at once), no retrain, SNR, rtd, `k56_lost` (the API's
`V90M_END_LOST` in data mode; `fail()` clears `connected`, so the general
rule could not say it). API: `echo_delay` from `u.k56.ecd`.

`sim/k56call` from the flagged INFO1a: the pad and robbed-bit line model,
every training segment checked against the facts (`seg_bad[]`), Js, MP and
MP' decoded and checked, E, B1 dropped, data through `k56enc_unframe` and
GPA; up, V.34's call modem with the J24 (the truth of the line or
`j_octet`), its MP (`rate`, `up_mask`, `mp_bad`), `silent` at five points.

    make build/test_answer_k56 (engine a stub)   272 checks, 62 failures
    build/test_answer_k56                          273 checks, 0 failures (2.7 s)
      r = 1, pad 0, no robbed bits: 32000 / 31200 (client 32000 / 31200),
        102057 bits down, 100065 up, 0 bad; 0 bad training octets, Js, 16 MP,
        16 MP', E, B1 good; data 8.33 s after ANS_K56; silence 2 12288,
        TRN2A 16336, TRN2B 2052
      mask 0FFF, echo delay measured (0): 32000 / 28800 = sim/v34peer's 28800 up (F9)
      max_down 32000, client asking 13: r = 1, 32000
      our cleardown: octet FF at once, cleared 2
      silent before S / J24 / MP / E / in data: ended 3.00 / 1.87 / 6.04 /
        1.99 / 0.53 s later, K56flex failed (data: lost)
      J24 octet C0: K56D_ERR_J; MP rate_ac 0, mask 0, mask 2000: K56D_ERR_RATE
    make test-api      502 checks, 0 failures: V90M_CONN_K56FLEX 32000 / 31200,
                       SNR 38.1 dB, retrain V90M_ESTATE, cleardown
                       V90M_END_CLEARDOWN_OURS, silent in data V90M_END_LOST; pin holds
    sizes (x86-64): k56d_t 21424 (u's largest is v92d_t 45096), answer_t 106184
    make check         ALL HOST CHECKS PASS
    replay proof (106 recordings, against 0.4.0's v90m-replay):
      flags '', -9, -x, '-x -9': identical (.txt and .bits); -k, -k -9: 106/106 exit 0
    make test-k56-sm56 MODS=k56 DOWN=32000   FAIL (k56-facts.md "Unsettled"):
      SM56 sends INFO1c 0x001 to us, takes our flagged INFO1a, the training to
      Js; we receive its S, J24 (octet 00) and J' with GPC (GPC confirmed);
      it then stays in its front end state 19 during our TRN2A, sends no MP,
      cause 0x21; we end K56D_ERR_NO_MP 4 s into TRN2B. Our octets from R1 to
      TRN2B equal D-Modem's oracle with the same J time, byte for byte.
    make -j16 check-arm ARM_CC=...armv7-eabihf musl...
      answer_k56: 225 checks, 0 failures (TEST_SHORT: 20000 bits each way)
      api: 502 checks, 0 failures (pin 118512 holds)
      ALL ARM CHECKS PASS

## k56 9b: SM56's TRN2A, the bridge's line model (2026-11-21)

k56 9's SM56 gate failure (SM56 stays in front end state 19 during our
TRN2A, no MP, cause `0x21`) was the bridge, not the modem. The
cross-experiment (D-Modem run as a black box, nothing of it in the tree):
our answerer, wrapped as a half program (`half_ours`, outside the
repository), called by SM56 through D-Modem's `interop_call` and netsim
(`NET_LOOP=0.45`, `AT+MS=K56FLEX,0`): K56flex connects, 32000 / 31200,
SM56 `CONNECT 32000`. Through the same netsim with the loop off or at 0,
D-Modem's own server fails exactly as ours did; with `AT+MS=K56FLEX,0` it
fails at 0.45, 0.6 and 0.75 as well. SM56's TRN2A acceptance moves with the
line model and the delay toward it, for either server; what inside SM56
decides it is not known (k56-facts "Harness", "Unsettled").

`tools/k56-peer/bridge.c`: the loop model is now a one-sample delay
(`LOOP=0`; a fraction still through the Lagrange interpolator) and a
first-order 300 Hz high pass (bilinear), and robbed bits go through it too.
The old second-order high pass also spoiled SM56's data above 32000 (error
80 to 140 against 5 to 7). No library change.

    make test-k56-sm56 ... MODS=k56 DOWN=32000      before: FAIL (state 19, cause 0x21)
                                                    after:  PASS, 32000 / 31200, 4 KiB each way
      MODS=k56 (no cap)                             PASS, 52000 / 31200
      MODS=k56 DOWN=32000 RBS=0x08                  PASS (J24 octet 08)
      MODS=k56 RBS=0x24                             PASS, 52000 / 31200
      MODS=none, and RBS=0x08                       PASS, V.34 28800 / 28800
      SM56_MS=AT+MS=V90                             FAIL (Phase 2), as before
      SM56_MS=AT+MS=K56FLEX,1                       PASS, but V.90 29333 (we pick V.90)
      STRICT_RENEG=1                                FAIL (no renegotiation yet: Task 11)
    the default model, 12 repeats (fractions 0 and 0.1)    24 / 24 PASS
    bridge sweep, k56 2 model: pass 0-0.35, 0.65-0.95; fail 0.40-0.60
    make check                                      ALL HOST CHECKS PASS

## k56 10: every rate, the pads and robbed bits; the CPU figure (2026-11-21)

`test/test_answer_k56.c` `k56_rate_tests`: the client (sim/k56call, data
check as k56 9) asking r = 1, 2, 7, 12 (54000, M = 16) and 13 at pad 0;
pads 3 and 6 dB at r = 13 and 7; robbed `0x24`, `0x12`, `0x3f`, `0x1f` (5
taken as 6) at r = 13 and 3, pad 0 and 6 dB. Each: `tx_bps` 30000 + 2000 r
both sides, the J octet's pad and robbed symbols in `k56d_pad`/`k56d_rbs`
and in k56tx (`pad`, `d_pad`, `d_rbs`), the training on that pad's levels
checked octet by octet (0 bad), 16 MP and MP', E, B1, the first data
symbol on the grid of 24 from TRN1B, 100000 bits each way with no error.
Both variants: r = 1 with robbed `0x24` at pad 0 and 6 dB (no bit to rob
at 32000: ignored; fix round 1). `TEST_SHORT` (ARM): r = 13 at pad 0 and 6 dB with robbed none and `0x24`,
r = 1 at 6 dB (r = 1, pad 0 is k56 9's).

The engine (`modem/k56d.c`, k56tx, k56enc) passed every one unchanged. The
first run failed only in the test's own models (43 failures), all three
fixed test-side:

- the sim's training checks: a robbed bit makes two training octets arrive
  alike (TRN1A's `0x4e`/`0x4f`, TRN1B's pairs); the sim took the first and
  its running sum drifted (thousands of "bad" octets, E missed). It now
  breaks such a tie with the octet as it left the server, and counts it bad
  if that is neither;
- the sim's data decoding looked for the received octet among all points;
  on a robbed symbol the server sends only points with bit 0 clear
  (k56-facts "Robbed bits"), and behind a 6 dB pad another point can arrive
  alike: 40000 bad bits at r = 13. It now looks among those points only;
- the test line's echo was of our octets before the network's pad: 6 dB
  too loud behind a 6 dB pad, and the upstream (31200, SNR about 35 dB)
  took 45 to 166 bad bits in some calls (pad 2, with or without robbed
  bits). The echo is now of what the client's line card plays, after the
  pad (pad 0: unchanged).

Mutations (throwaway, reverted): `k56tx_data` given robbed 0: 84 failures;
`k56tx_got_j` given pad 0: 204 failures.

    build/test_answer_k56 (first run)    1064 checks, 43 failures (the three above)
    build/test_answer_k56                1064 checks, 0 failures (3.7 s host)
      r = 1 / 2 / 7 / 12 / 13, pad 0: 32000 / 34000 / 44000 / 54000 / 56000 down, 31200 up
      pad 3 and 6 dB at r = 13, 7: J octet 40 / 80, 0 bad
      robbed 24 / 12 / 3F / 1F on the line: J octets 09 / 24 / 3F / 37 (pad 2: 89 / A4 / BF / B7)
    a sweep (scratch, not kept): all 13 rates x pads 0, 3, 6 dB x robbed
      none, 24, 12, 3F, 1F, 08, 01: 273 calls, 0 failures
    make check                           ALL HOST CHECKS PASS
    SM56 decoder harness: make k56-dump; ~/modem-research/k56-oracle/sm56dec/check.sh build/k56dump
                                         2496 of 2496 decode to the data sent
    make test-k56-sm56 DMODEM=... MODS=k56 (SM56's own rate; no pad run, SM56
      cannot train behind one):
      RBS none                           PASS, 52000 / 31200, SM56 CONNECT 52000, RBS 00
      RBS=0x24                           PASS, 52000 / 31200, SM56 RBS 09
      RBS=0x12, 0x3f, 0x1f, 0x01         PASS, 52000 / 31200, SM56 RBS 12, 3F, 3E, 20
      DOWN=34000 / 40000 / 48000         PASS at that rate
      DOWN=36000 / 44000 / 48000 RBS=0x24, DOWN=50000 RBS=0x12   PASS at that rate

CPU: `tools/rec_k56` (`rec_x2`'s link with K56flex offered, 56000 down,
optional pad and robbed bits), `bench_v90 -k`, `make bench-k56` and
`make cost-k56` (as `cost-x2`, but K56flex's data mode starts at 16.2 s,
after V.8bis, so its second is 20 to 21; V.90's stays 15 to 16). Host and
ARM give the same transmit hash (52c83abc), 742404 bits up, 0 bad.

    make cost-k56 ARM_CC=...armv7-eabihf musl...
      k56 data mode: 34749835 ARM instructions per second of line (second 20 to 21 of the call)
      V.90 data mode: 38722338 ARM instructions per second of line (second 15 to 16 of the call)
      ratio k56 / V.90: 89 %

Under the plan's 120 %, as the design expected: the upstream receiver is
set as the V.90 answerer's (16 states, linear, minimum shaping, asked for
in our MP). Not profiled further.

    replay proof (106 recordings, against 0.4.0's v90m-replay):
      flags '', -9, -x, '-x -9': identical (.txt and .bits); -k, -k -9: 106/106 exit 0
    make -j16 check-arm ARM_CC=...armv7-eabihf musl...
      answer_k56: 396 checks, 0 failures (TEST_SHORT; 9.3 s under qemu here)
      api: 502 checks, 0 failures (pin 118512 holds)
      ALL ARM CHECKS PASS

## k56 11: the client's rate renegotiation (2026-11-21)

`modem/k56d`: the client's S in data mode starts `K56D_RENEG` (design
decision 13, k56-facts.md "Rate renegotiation"). Our data stops at the end
of the frame being sent (`k56tx_reneg`), then R2, TRN2A 2048, MARK,
TRN2B until its MP and the grid of 24, MP and MP' at 1 bit a symbol, E 24
and data (B1 first, a new encoder: Ruling C4). Upstream its S-bar, TRN, MP,
MP' and E on 4 points (V.34 11.6, as `modem/v34dx` and x2d's rate change),
then V.34 data at the rate our new MP gives. An MP with rate 0 both ways
ends the call as the far end's cleardown (`K56D_ERR_CLEARDOWN`,
`V90M_END_CLEARDOWN_FAR`); not back in data mode within `K56D_T_RENEG`
(6 s from its S) is `K56D_ERR_RENEG` (`V90M_END_K56_FAILED`).
`answer_moving` is 0 in between, so the API gives DATA_STOPPED, RATES,
DATA_MOVING. `v90m_mem_size()` unchanged (118824 x86-64, 118512 ARM).

`sim/k56call` renegotiates (`reneg_ms`, `reneg_rate`, `reneg_ms2`,
`reneg_rate2`, `reneg_rate0`, `K56C_SIL_RENEG`) and checks our answer as
it checks the training: R as a whole frame, TRN2A's length, the hidden
second bit of TRN2A's and MARK's even symbols at 2 bits a symbol (so the
training's scrambler, history and running sum are checked going on from
the last E), MP / MP' / E on the 2 magnitudes, the grid; R in data it did
not ask for is counted.

    test_answer_k56 (host): 13 -> 10 -> 12, 5 s apart, pad 0, robbed none and 0x24:
      tx_bps 56000, 50000, 54000; every training octet as the facts; MP 32, MP' 32;
      3 data segments each way, 0 bad (e.g. none: down 274985 / 246186 / 173114,
      up 153362 / 153729 / 100065 bits); rate 0: cleardown 0.26 s after its S;
      silent after its S: ended 5.98 s later; 60 s of data, no R of ours
    test_api: events 1 4 3 2 4 3 2 4 3 5, RATES at 50000 then 54000;
      CLEARDOWN_FAR; K56_FAILED
    make check: ALL HOST CHECKS PASS
    PAR=$(nproc) make check-arm ARM_CC=...armv7-eabihf musl...: ALL ARM CHECKS PASS
      (answer_k56 586 checks, api 525, TEST_SHORT)

SM56 (information, `make test-k56-sm56 MODS=k56 STRICT_RENEG=1`, 120 s of
line): it did not renegotiate. Data at 52000 / 31200 from 22.1 s, the first
4 KiB each way byte for byte, then 98 s of data mode with no S from it and
no rate change; the same with `RBS=24`. So our renegotiation is checked by
the simulated client only; what SM56 does after one is still not seen
[our observation].

## k56 12: following the client's retrain (2026-11-21)

`modem/k56d`: the client's tone B (1200 Hz, `tonedet`, more than 50 ms) on
the upstream in data mode is its retrain (design decision 14, k56-facts.md
"Retrain") -> `K56D_RETRAIN`, silent. `modem/answer`: phase `ANS_K56RT`,
70 ms of silence, then `p2ans_init_retrain` on `v34.p2` with
`p2ans_cfg.k56 = 1` and the INFO0c of the first Phase 2; on `P2N_K56` (our
INFO1a flagged again) `start_k56`: the whole training from silence 1 and
data. Any other end of that Phase 2 (INFO1c 0x200: `P2N_DONE`; a failure;
not done 9 s after the retrain began) ends the call with `k56_failed`
(`V90M_END_K56_FAILED`): no way back to V.34. `retraining` is 1 from the
retrain to data again, so the API gives DATA_STOPPED, DATA_MOVING and no
renegotiation; `retrains` counts them; `v90m_retrain` stays `V90M_ESTATE`.

Retrains in a row: the third ends the call (`MAX_RETRAINS`, as K56flex's:
`rt_recent` reaching 3). K56flex forgets them all once data mode has held
2 s (`K56_RT_DATA_MS`), not one a minute as V.90, so three retrains 5 s of
data apart go on. V.90, V.92, V.34 and x2 retrain counting is not touched.
`v90m_mem_size()` unchanged (118824 x86-64, 118512 ARM).

`sim/k56call` retrains (`retrain_ms` into data mode and into the data
after each, `retrains` of them, `retrain_200`): 70 ms of silence, then
v34peer's Phase 2 of a retrain, and after our INFO1a flagged again its
whole training afresh (`k56_starts`), checked octet by octet as the first.

    test_answer_k56 (host): one retrain 5 s into data mode, r = 13, pad 0, robbed
      none and 0x24: ANS_K56RT 0.143 s after its retrain began, Phase 2 2.03 s, data
      again 10.50 s after it (the first training: 8.33 s); every training octet as
      the facts, MP 32, MP' 32; data before and after, each way, 0 bad. Three
      300 ms apart: the third ends the call 0.143 s after it began, K56_FAILED;
      three 5 s apart: 4 data segments, 0 bad, still in data; INFO1c 0x200 in the
      retrain: ended 2.17 s after it began, K56_FAILED
    test_api: events 1 4 3 4 3 5, retrains 1, renegotiations 0, v90m_retrain
      V90M_ESTATE in it; three in a row: retrains 3, K56_FAILED
    make check: ALL HOST CHECKS PASS (answer_k56 1407 checks, api 542)
    make check-arm (armv7 musl, TEST_SHORT: the single retrain only):
      ALL ARM CHECKS PASS (answer_k56 636 checks)

## k56 13: release 0.5.0, experimental, off by default (2026-11-21)

main (5db64c4: the CI workflow) merged into k56flex first, so that the
ARMv5 job's fix edits the real `ci.yml`.

- **Label L1** (spec "Release labels"): the SM56 decoder harness and the
  SM56 full call pass (below). In CHANGELOG, README, `docs/limits.md`,
  `docs/answering.md`: "connects to Motorola's SM56 soft modem run under
  emulation, V.42 data both ways at 32000 to 52000 bit/s down and 31200
  up, with no pad and with robbed bits; its decoder takes our data at
  every rate, pad and robbed-bit pattern; not yet tried on a Rockwell or
  Lucent modem." The rates: SM56 full calls at 32000 (k56 9b, `DOWN=32000`),
  34000 to 50000 capped (k56 10), 52000 uncapped (k56 9b, k56 10, and
  this step's runs); 31200 up in every one.
- **Version 0.5.0**: Makefile, header, `v90m_version()`, test_api's version
  checks. `v90m_mem_size()` unchanged (118824 x86-64, 118512 32-bit ARM).
- **Docs**: CHANGELOG 0.5.0; README (K56flex in the list and the standards
  table, how to switch x2 and K56flex on, what was tried, the CPU table);
  DESIGN 4.2b (K56flex done, the stated exception as x2's); answering.md (a
  K56flex section), api.md (`V90M_MOD_K56`, `V90M_CONN_K56FLEX`,
  `V90M_END_K56_FAILED`, retrain and cleardown in K56flex), limits.md (a
  K56flex section: CRe, not MRe, and the delays measured in k56 5; the
  31200 upstream's margin behind a strong echo; the client's
  renegotiation on 4 points, unconfirmed; retrains forgotten after 2 s of
  data, our value; the retrain's Phase 2 limited to 9 s; SM56 in
  automode gets V.90), testing.md (K56flex's checks, `ARM_CC` for
  check-arm), the header's comments on `V90M_MOD_K56`, retrain, cleardown
  and `V90M_END_K56_FAILED`; `docs/history/k56-handover.md` (the owner's
  line tests, the v90d change, what is not settled).
- **CI, ARMv5**: on main the job took 2 h 57 min of its 180. Its limit is
  now 300 minutes (GitHub's per job: 360), and the API test's long
  K56flex calls have `TEST_SHORT` versions (one renegotiation, not the
  client silent after its S; the single retrain, not three in a row).
- **Found by the checks**: `make check-ubsan` did not build. clang 21's
  `-Wunused-but-set-variable` flagged `ones` in `sim/k56call.c` (k56 9), and
  it is a `-Werror` build. The dead variable is removed (test code only).

Checks on the release tree:

| check | result |
| --- | --- |
| `make check` | ALL HOST CHECKS PASS (api 542, p2ans 402, answer_k56 1418) |
| 0.4.0's counts (`make check` on 704b370 against this tree) | every suite of 0.4.0 the same, but api 458 -> 542 and p2ans 259 -> 402 (the K56flex tests of k56 5, 6, 9, 11, 12); new: answer_k56, k56enc, k56tx, test_k56tab, test_v8bis, test_v8bismsg |
| `make check-ubsan` | ALL UBSAN CHECKS PASS (after the fix above) |
| `PAR=4 make check-arm ARM_CC=...armv5-eabi uClibc...` | ALL ARM CHECKS PASS; api 532 (TEST_SHORT), answer_k56 636 |
| replay proof, 106 live-050 recordings, 704b370 (0.4.0 + the x2 gate) against this tree | `""`, `-9`, `-x`, `-x -9`: identical (106 .txt; 21 / 40 / 21 / 40 .bits); `-k`, `-k -9`: 106 of 106 complete |
| `make crd-scan RECS=~/sync-build/live-050` | 106 recordings, 0 CRd detections |
| `make cost-k56` (ARMv5 uClibc) | K56flex 38985309, V.90 38935592 instructions a second of line: 100 % (armv7 musl in k56 10: 89 %) |
| `make test-k56-sm56 MODS=k56`, RBS none / `24` | PASS, 52000 / 31200, 4 KiB each way identical (SM56: RBS 00 / 06) |
| `make test-k56-sm56 MODS=k56 DOWN=32000` | PASS, 32000 / 31200 |
| SM56's decoder (`make k56-dump`, `sm56dec/check.sh`) | 2496 of 2496 decode to the data sent |
| `PAR=1 tools/check-ports.sh build/ports-k56 host-gcc host-clang i686-musl aarch64be-musl mips-musl win32-mingw` | host-clang, i686-musl, aarch64be-musl (PAR=1, in `build/ports-k56`) and mips-musl, win32-mingw (PAR=3, in `build/ports-k56b`, run beside it to save hours, each against its own host-gcc run): 121 of 121 programs identical to host-gcc each; ALL PORTS PASS |

check-arm time, ARMv5 under qemu, PAR=4 as on CI's four cores, the
programs already built: 4180 s before the API test's shortening, 3218 s
after. Most of that difference is the host's other load during the first
run, not the change. The API test itself went from 8.5 s to 7.2 s. K56flex's
programs are about 40 s of some 10800 to 13500 program-seconds (answer_k56
29 to 32 s). The long ones are older: test_p34 alone takes 1250 to 1575 s,
then orig_v34, orig_retrain, orig_v32, v92a and orig_v90 take 500 to 630 s
each. So 0.4.0's 2 h 57 min on CI was not K56flex's doing. The raised limit
is what gives the headroom; shortening p34 and the orig_* tests on ARM
would be the next step.

### k56 13, fix round 1: x2-upstream merged; the live x2 call (2026-11-21)

**The owner's first live x2 call:** a U.S. Robotics 56K FAX EXT (flash
5.1.4) called the box. It ran x2 at 56000 / 28800, with PPP, for 2 min 50 s,
before the fixes below. The modem's own counters: 24 of 255 blocks resent.
Its Tone A retrain, some 153 s into data mode, went unanswered, and the call
ended ("Unable to Retrain"). The investigation is on branch `x2-upstream`;
the facts are in `docs/history/x2-facts.md`:

- 10 of the resends came from two frames lost at the client's two rate
  changes. Each was sent after fill ones with its opening flag missing.
- The other 14 were checkpoint resends after its idle polls.
- The upstream had no FCS errors.

**The confirming call** (2026-11-21): the same USR, flash 5.1.4, called
the box running a v90d 0.7.0 bench build with libv90modem af86c3f and the
`v90m_ec_line_stopped` glue. It connected x2 at 56000 / 28800 with PPP and
held 3 min 50 s, until we hung up. It made 2 rate changes up and no
retrain; the USR gave SNR 59.7 dB. Its counters, against the call before
the fixes:

| | before | after |
| --- | --- | --- |
| blocks sent | 255 | 293 |
| resent | 24 | 4 |
| link NAKs | 12 | 2 |
| BLER | 1 | 0 |
| pings across PPP answered | 76 of 90 | 90 of 90 |
| worst ping | 2.9 s | 590 ms (average 148 ms) |

The USR asked for no retrain on this call, so the cleardown on its Tone A
is tested only by the replay of the earlier recording.

Merged here (x2-upstream, af86c3f):

- the V.42 receiver takes a frame whose opening flag was lost after a run
  of ones when its FCS holds;
- `v90m_ec_line_stopped` (new, public), for the falling edge of `moving`,
  with `v90m_ec_line_restored` aborting only the frame the stop cut and
  opening with two whole flags;
- x2d ends the call on the client's Tone A in data mode
  (`V90M_END_RETRAIN_FAILED`);
- `v90m-replay` decodes x2 data from the recorded transmit and prints each
  state change.

CHANGELOG 0.5.0, README (`v90m_ec_mem_size()` 49792 on x86-64, 32 more),
error-control.md, getting-started.md, answering.md and limits.md (an x2
retrain now ends as `V90M_END_RETRAIN_FAILED`, not by the watchdog) follow
the merge. `tools/x2-peer/bridge.c` and `tools/k56-peer/bridge.c` now call
`v90m_ec_line_stopped` on `V90M_EV_DATA_STOPPED`. Review fixes: the version in
api.md and getting-started.md; the CHANGELOG preamble and wrap; the CI
comment; DESIGN's PROGRESS range; testing.md's TEST_SHORT list; README's
CPU row.

Replay proof after the merge: 704b370 rebuilt in a temporary worktree
(removed again), the 106 recordings, flags `""`, `-9`, `-x`, `-x -9`.
Every `.bits` is identical. Every `.txt` differs only by the replay tool's
new lines `modem connected C moving M retraining R`; with those removed,
424 of 424 are identical. `-k`, `-k -9`: 106 of 106 complete.

Checks after the merge: `make check` ALL HOST CHECKS PASS (api 542,
answer_x2 386, answer_k56 1418, test_hdlc 4312, test_v42 5634); `make
check-ubsan` ALL UBSAN CHECKS PASS; `PAR=8 make check-arm` (armv5 uClibc)
ALL ARM CHECKS PASS; `make test-k56-sm56 MODS=k56` PASS, 52000 / 31200.

## m68k: a GCC wrong-code bug, not qemu (0.6.0 audit, Task 5b)

The twelve m68k failures of ports-2 were the compiler's. GCC 12.3.0 for
m68k (Bootlin `m68k-68xxx--uclibc--stable-2023.08-1`, `--with-cpu=68040`),
with `-fomit-frame-pointer` (the default at -O1 and up), compiles
`(uint64_t)(int)x << 32` divided by a 64-bit libcall (`__udivdi3`,
`__divdi3`) with x an incoming stack argument as: push the divisor, `subq.l
#8,%sp` for the dividend, then load x from an sp-relative offset that
counts the pushes but not the `subq`, 8 bytes short. So it reads another
argument or the return address. In the tree:

- `modem/qamrx.c`, `qamrx_init`: `dcar` came from the address of the
  `qamrx_t` (`r`) instead of `car_num`, so the V.34 receiver's carrier ran
  at the wrong frequency and never trained: the twelve failures. The
  wrong value depends on where the linker puts the struct, which is why
  some rebuilds passed by chance (and almost certainly why "fails
  differently with `-one-insn-per-tb`" was two differently linked binaries,
  not the emulator).
- `modem/v90arx.c`, `v90arx_clock`: `ppm_q8` was the return address, so
  the answering V.90 receiver's clock read a value that depends on the
  binary's link layout (no test looked at it; a check program printed 4290672329 for
  every ppm against the host's 4294967296 + ppm * 2^32 / 256e6).

Fix: `qamrx_init` takes `(uint64_t)(uint32_t)car_num << 32` (only the low
32 bits survive the shift, so the same value; zero-extended, gcc uses a
different insn), and `v90arx_clock` divides `ppm_q8 * 2^21` by 125000
instead of `ppm_q8 * 2^32` by 256000000 (the same fraction, so the same
truncated quotient; no shift by 32). Both checked equal to the old
expressions for all 2^32 int32 inputs on the host. An m68k assembly scan
of every tracked C file (all but the two spandsp peers, which need
spandsp) at -O1, -O2, -Os and -O3, for `subq.l #N,%sp; move.l %sp,%aK`
followed by an sp-relative source, found these two sites and no others
before the change, and none after. The port matrix also builds m68k with
`-fno-omit-frame-pointer`, with which the suite passed under GCC 12.3,
against new sites; m68k gates in `make verify-phase` like every other target.

qemu-m68k was cleared: 300,000 random operand sets through `mulu.l`,
`muls.l`, `divu.l`, `divs.l` (64-bit forms, results and flags) matched a
reference written from the 68040 manual, in normal mode and with
`-one-insn-per-tb`.

A newer gcc (m68k moved to LRA in 14) was not tried; the bug was not
reported upstream (the owner's call; a draft is kept outside the
repository).

## hst 1: the facts, tagged, and the evidence plan (2026-11-24)

Docs only. `docs/history/hst-facts.md` is the single source of values for the
HST and MNP work: call start, back and forward channels, training, the V.17
long train, MNP framing and frames, the LR and 0xC0, establishment, data and
attention, timers, reversal, rate changes, cleardown, the evidence plan (R1,
R2, R3 with what each settles), the recordings table (empty) and what is still
unsettled. Each value has its source (BF §, PS §, Annex A clause, patent text
line, V.17 clause) and one tag, [FW], [PUB], [BM] or [OURS].

Sources used beyond `docs/hst/`: V.42 (03/93) Annex A (`build/hst-sources/
itu/v42-93.txt`: A.3, A.4, A.6, A.7.1, A.7.5) and ITU-T V.17 (02/91), fetched
from itu.int into `build/hst-sources/itu/V17-199102.pdf` (sha256 `2198a145...
446cc9`; the text through `pdftotext -layout`; Figure 3 read from its rendered
page). Found by us, not stated in any source: the scrambler state at the start
of V.17's segment 2 (0x2ECDD5, from Table 4's first 23 output bits, the other 9
as a check); the points A to D (-6,-2), (2,-6), (6,2), (-2,6); the Courier CRC
check value (0x906E, bytes `6E 90`) and residue (0xF0B8), and CRC-16/ARC's
check value (0xBB3D), by computation; the T401 values the plan pins (3659,
6317, 1560, 1600 ms) from Annex A.7.5.1's formula with Trt 1000 ms. BinModem's
code was read only to check the Courier LR template and the V.17 scrambler
procedure; nothing was copied.

Findings worth knowing: Annex A's Table A.9 N401 = 256 column does not follow
its own formula (noted under `Timers`); V.17's Table 3 resync row is damaged in
the PDF text layer (Task 15 uses the long train only).

    grep -c '\[FW\]\|\[PUB\]\|\[BM\]\|\[OURS' docs/history/hst-facts.md   -> 237
      (FW 44, PUB 76, BM 40, OURS 77; four of the lines are the tag
      definitions and two are the re-tag convention)
    make verify                                                          -> VERIFY PASS (log: build/verify-hst1.log)

The owner's R1 procedure is handed to the controller as
`.superpowers/sdd/2026-11-24-hst-mnp/r1-for-owner.md` (not in the repository).

## hst 2: MNP framing mode 2, frame layouts, LR parse and merge (2026-11-24)

New `v42/mnp_frame.[ch]`, registered in `V42_SRC` and in `v42/Makefile`'s
`SRC`, `HDR` and `TESTS`. From `docs/history/hst-facts.md` (`MNP framing`,
`MNP frames`, `LR and 0xC0`) and V.42 Annex A.3, A.6, A.7.1.5:

- `mnp_crc16`: x^16 + x^15 + x^2 + 1, reflected, bit at a time
  (CRC-16/ARC from 0: "123456789" -> 0xBB3D).
- Framing mode 2: `mnp2_tx_t` sends SYN DLE STX, the body with each DLE
  doubled, DLE ETX, the FCS over body and ETX; `mnp2_rx_t` hunts for SYN DLE
  STX, undoes the doubling, checks the FCS. The FCS's initial value and
  octet order sit behind `MNP_FCS2` (default `MNP_FCS2_ARC_LO`, [OURS]; the
  test passes with each of the four variants).
- Headers: standard LT, LA, LN, LNA, LD (with or without the user code),
  optimised LT and LA; parse takes both layouts by their LI, skips unknown
  parameters, returns `MNP_PARSE_UNKNOWN` for a frame of an unknown type.
- LR: build (parameters 1, 2, 3, 4, then 8 and 0xC0 if present) gives the
  Courier template back byte for byte; parse reads N401 little-endian,
  flags constant parameter 1 != 2, requires parameters 2 and 3, takes N401
  64 when parameter 4 is absent [OURS]; merge gives the lower framing mode,
  k and N401 and parameter 8 (the AND) only if the caller sent it.

    make -s -C v42 build/test_mnp_frame (before the code)  -> mnp_frame.h: No such file or directory
    v42/build/test_mnp_frame                               -> test_mnp_frame: 193 checks, 0 failures
    make check-headers                                     -> headers: 106 compile alone
    make verify                                            -> VERIFY PASS (log: build/verify-task2b.log)
    test/golden/out/v42/test_mnp_frame.txt                 -> new (a new program; nothing re-recorded)

Review fixes (round 1):

- The mode 2 receiver takes a DLE STX inside a body as the next frame's start
  flag. The cut-off frame counts as one BAD and the new one is collected. A
  body DLE followed by SYN drops the frame and looks for DLE STX next. Before
  this fix, a frame that lost its end also cost the good frame after it.
- An LR without constant parameter 2 is malformed (A.7.1.5.2). A parameter 4
  of 1 octet is taken [OURS]; one of 0 or 3 or more octets is malformed.
- The merged N401 is never above `MNP_MAX_N401`.
- New checks: the X.25 residue 0xF0B8, a zero-length frame, LI 255, a cut-off
  frame then a good one (with and without idle FF).

    v42/build/test_mnp_frame  -> test_mnp_frame: 225 checks, 0 failures
    make verify               -> VERIFY PASS (log: build/verify-task2c.log)
    test/golden/out/v42/test_mnp_frame.txt  -> one new line, and the count line

## hst 3: sim/mnppeer, an MNP peer written from Annex A and the facts (2026-11-24)

New `sim/mnppeer.[ch]`, in `SIM_SRC` and in `v42/Makefile`'s `T_SRC` (and
`T_HDR`); new program `v42/test/test_mnppeer.c` in `v42/Makefile`'s `TESTS`.
The peer includes no library header (`grep -n 'v42\|mnp_frame'
sim/mnppeer.[ch]` finds nothing): its own CRC-16/X.25 and mode 2 FCS
(CRC-16/ARC by default; `MNPP_FCS2`, numbered as the facts number
`MNP_FCS2` and following it when a build defines it), HDLC bit layer,
start-stop characters, headers and link procedure (A.7.1 to A.7.4). The
HST fields of `mnp_peer_cfg` (`c0`, `c0_flags`, `c0_rates`, `unknown_ids`)
are declared and have no effect yet (pre-flight ruling F13; Task 8).

Choices where Annex A is silent [OURS]: T401 2000 ms in establishment and
4000 ms in data (Table A.9, N401 64 at 2400), T404 3000 ms, N400 12; an LR
missing the framing mode, k or N401 is refused with LD 3; the responder that
runs out of LR tries sends LD 4; the confirming LA goes in the establishment
framing, in the layout the responder's LR set (A.6.4.2); after the LRs each
end hears only the agreed framing (the originator also the establishment one
until it hears the responder in the agreed one); a frame starts only after a
whole idle flag.

test_mnppeer, two peers on a bit loop at 2400 bit/s, 1 ms ticks: modes 3
and 2 connect in 169/211 and 234/296 ms and move 64 KiB each way intact
(FNV-1a) with no retransmission; 1 % of frames lost (mode 3) or with a bit
inverted (mode 2), still intact; negotiation (lower framing, k, N401;
parameter 8 only when both send it; bit 1 gives N401 256); release gives
LD 255 at the other end, both ways; three LN codes delivered once each, in
order, under 1 % loss; 3 s of zero credit; no reply LR (N400 3, T401 500 ms:
gives up at 1503 ms, no LD); and the originator's LR and confirming LA,
decoded off its bits by a decoder in the test, against octets written out
(the Courier template, `07 05 01 01 00 02 01 08`) with CRCs written in the
test and checked against 0x906E and 0xBB3D.

    make -s -C v42 build/test_mnppeer (before the code)  -> No rule to make target 'build/tobj/up/sim/mnppeer.o'
                                                            (test alone: undefined reference to mnppeer_init ...)
    v42/build/test_mnppeer                               -> test_mnppeer: 100 checks, 0 failures (0.9 s; 1.7 s under qemu-arm)
    -DMNP_FCS2=1, 2, 3                                   -> 100 checks, 0 failures each
    test/golden/out/v42/test_mnppeer.txt                 -> new (a new program; nothing re-recorded)
    make verify                                          -> VERIFY PASS (461 s; golden 143 identical; log: build/verify-task3.log)

Review fixes (round 1):

- Credit enforced: the peer keeps the furthest N(S) any of its LAs allowed
  (N(R) + N(k); at the start the LRs' k). An LT beyond it, whatever V(R) is,
  counts in `rx_over_credit` and is handled as one without credit (discard,
  LA; A.6.3.10, A.6.3.11, A.7.3.2.3). Every two-peer case asserts 0; a raw
  sender in the test (HDLC frames built bit by bit) sends LTs 1..6 on k 2 and
  credit 0, and the peer counts 4.
- LN queues lose nothing: `mnppeer_send_ln` returns 0 when its queue is
  full; a received LN that finds the queue full is counted (`ln_rx_full`),
  not taken, not acknowledged, and comes again on the sender's T401.
- The originator sends its LR again on a frame with a bad FCS (A.7.1.2 b).
- The responder sends LD 1 (not 3) for an LR it cannot take, and LD 1 (not
  4) when its LR tries run out: code 3 is the initiator's (Table A.5 Note,
  now in `hst-facts.md` `MNP frames`).
- `cfg.confirm_la_standard`: the confirming LA in the standard layout
  although bit 2 is agreed, as BinModem sends it [BM]; the peer as responder
  takes the confirming LA in either layout.
- The CRC comment says the peer follows -DMNP_FCS2 but not a change of
  mnp_frame.h's default.

    v42/build/test_mnppeer (new checks, before the fixes)  -> 146 checks, 8 failures
        (send_ln 9th returned 1; ln_rx_full 0 and 8 of 12 codes; confirming LA
        layout 1; rx_over_credit 0; LD 3 and LD 4 where 1 is due; 1 LR, not 2)
    v42/build/test_mnppeer                                 -> test_mnppeer: 146 checks, 0 failures
    -DMNP_FCS2=1, 2, 3; qemu-arm                           -> 146 checks, 0 failures; 1.7 s
    test/golden/out/v42/test_mnppeer.txt                   -> five new lines and the count line (100 -> 146)
    make verify                                            -> VERIFY PASS (736 s; golden 143 identical; log: build/verify-task3-fix1.log)

## hst 4: the MNP link, as responder (2026-11-24)

New `v42/mnp.[ch]` (in `V42_SRC` and in `v42/Makefile`'s `SRC` and `HDR`) and
the new program `v42/test/test_mnp.c` (in `v42/Makefile`'s `TESTS`). The link
is at frame level, like `v42_lapm`: bodies in through `mnp_rx_frame`, out
through `mnp_tx_frame`, with `tx_framing` naming the framing each one goes in.
Our LR and the frames before the link is up go in the framing the caller's LR
came in. The rest go in the agreed framing. `mnp_t` carries the mode 2 framers
(`tx2`, `rx2`) for the endpoint. Interfaces as the plan's Task 4, plus
`mnp_rx_bad` (a frame the framer dropped: our LR again in establishment,
A.7.1.3; an LA in the data phase, A.7.3.3 a) and `mnp_ln_of_brk` and
`mnp_brk_of_ln` (LN types 1, 2, 3 and `V42_BRK_*`).

What it does: it accepts the LR (constant 1 not 2 gives LD 2; an LR it cannot
take gives LD 1, since code 3 is the initiator's) and replies with the merge.
Parameter 8 bit 1 agreed means N401 256 both ways, as the peer reads it. Bit 2
gives the optimised LT and LA. It takes the confirming LA in either layout, or
an LT first (dropped, answered with an LA). It sends its LR again on T401
(2 s), on a bad frame or on a repeated LR, and the second failure gives LD 1.
In the data phase: a window of k (at most 8) slots of N401 octets, limited by
the credit of the last LA. It goes back on an LA that repeats N(R) or on T401,
and N400 tries end in LD 4. The receiver takes only V(R) and ignores the first
N(S) = V(R) - 1. It sends an LA when it is due, when it has received LTs and
has none to send, at k/2, on T402 and T404, and when credit comes back. Our
credit is k, or 0 while `mnp_set_busy`. LN and LNA: one outstanding, V(SA)
from 1, sent again every T401, each delivered once. A full queue of 4 neither
takes nor acknowledges. LD 255 on `mnp_release`. T401 comes from
`mnp_t401_ms` at the rates of `mnp_set_rates` (2400/2400 until then). T404 is
3 s at 2400 bit/s received and above, 7 s below. Every timer is held from
`mnp_line_stopped` to `mnp_line_restored`.

Not done here: Tables 4 and 5 (what a break does to the data) are left to the
endpoint (Task 5). The link carries the LN type and discards nothing.

`sizeof(mnp_t)`: 3240 octets on x86-64 and 3188 on ARMv5. The compile-time
check is ≤ 4096.

    make -s -C v42 build/test_mnp (before mnp.c)        -> No rule to make target 'build/tobj/mnp.o'
                                                           (test alone: 22 undefined mnp_* references)
    v42/build/test_mnp                                  -> test_mnp: 294 checks, 0 failures (0.6 s)
    qemu-arm (armv5), clang, -DMNP_FCS2=1, 2, 3          -> 294 checks, 0 failures; output identical to gcc's
    mutations (no credit limit, no V(R)-1 rule, no
    timer hold, LN delivered twice, no go-back)         -> each caught
    test/golden/out/v42/test_mnp.txt                    -> new (a new program; nothing re-recorded)
    make verify                                         -> VERIFY PASS (471 s; golden 144 identical; log: build/verify-task4.log)

Review fixes (round 1):

- A repeated LR during establishment only sends our LR again (A.7.1.3 c),
  at most N400 of ours. Rejection (LD 1) comes on the second failure, either
  a T401 expiry or a bad frame. Before this, our LR lost, then the T401
  resend, then the caller's own LR retry ended the call.
- On a matching LNA, `ln_send` is cleared, and the LN branch requires an
  outstanding LN. Before this, an LNA for an older LN followed by the
  matching one made `mnp_tx_frame` return -1. `sim/mnppeer` had the same
  fault, which sent an LN of type 0xFF, and is fixed the same way.
- A destructive break (LN 1) resets the link, following Tables 4 and 5 note
  a and A.6.7.1.4. Every state variable except V(SA) and V(RA) goes back to
  its start, and stored LTs and the held LT are dropped. On a received LN 1,
  the reset happens when the LN is taken, then the LNA goes. On an LN 1 we
  send, LTs that have not gone are dropped. Nothing more goes, and what the
  far end sends is discarded, until the LNA, then the reset. `resets` counts
  them for the endpoint (Task 5 flushes its rings and holds the user's data).
  `sim/mnppeer` now applies the same rules from the Annex A text, with its
  own `resets` and `brk_wait`, and empties its write and read queues as
  Tables 4 and 5 say.
- LN 1, 2 and 3 hold new data (`mnp_can_send` 0) until their LNA. LN 3 goes
  only once the LTs stored at its request are acknowledged (A.7.4.1).
- An LN fails after N400 retransmissions, as an LT does (A.7.4.8), not after
  N400 sends.
- Retries count go-backs and reset only when N(R) moves. With credit 0,
  T401 expiries count in a separate `stall`, which any LA resets. A far end
  that alternates N(k) 0 and 8 without moving N(R) now ends in N400.
- `test_mnp`'s N401 256 check asserts `mnp_can_send == 256` with the window
  empty.

    v42/build/test_mnp against the old mnp.c          -> 445 checks, many failures (I1, I2, I3, LN order, stall)
    v42/build/test_mnppeer, the peer's LNA unfixed    -> raw_lna_match: ln_t 3584 and ln_tries 2
    v42/build/test_mnp                                -> test_mnp: 445 checks, 0 failures
    v42/build/test_mnppeer                            -> test_mnppeer: 181 checks, 0 failures
    qemu-arm armv5 (0.9 s, 1.9 s), clang, -DMNP_FCS2=1,2,3 -> identical outputs, 0 failures
    sizeof(mnp_t)                                     -> 3264 (x86-64), 3204 (ARMv5)
    make verify                                       -> VERIFY PASS (597 s; golden 144 identical; log: build/verify-task4-fix1.log)

Review fixes (round 2):

- `lr_failure` counts nothing while the resend it asked for has not gone
  (`lr_due`). A.7.1.3 rejects only on a failure "after resending", so two
  bad frames before our resend no longer end establishment.
- One reset per break. A received LN 1 does not reset while our own LN 1
  waits for its LNA, since that LNA's reset is the one. An expedited LN
  (types 1 and 2) now goes ahead of an LNA owed, "immediately following the
  transmission in progress" (A.7.4.1). So when both ends break at once, each
  LN meets the other end's break still pending. The `mnp.h` contract now
  says: hold the user's data until the LNA (Table 4), and flush on each
  `resets` change.
- `sim/mnppeer`, written from the Table 4 text: the destructive break takes
  effect when it is asked for (`mnppeer_send_ln`), with the write and read
  queues emptied and data held and discarded until its acknowledgement. A
  received LN 1 during that wait does not reset. Its LN also goes before an
  LNA owed (A.7.4.1).
- Tests: LR, bad frame, bad frame, our LR, then the LA connects (and a bad
  frame after the resend rejects); a frame-level break collision; LN 1 from
  both ends in the same millisecond, against the peer and between two peers,
  with exactly one reset at each end. That case runs without loss, because
  a lost LN 1 arrives after the other end's break has finished and is a
  second break.

    test_mnp, test_mnppeer against 72df3dd's mnp.c / mnppeer.c -> 7 and 2 failures (LD 1 after two bad frames; resets 2)
    v42/build/test_mnp                                       -> test_mnp: 486 checks, 0 failures
    v42/build/test_mnppeer                                   -> test_mnppeer: 200 checks, 0 failures
    qemu-arm armv5, clang, -DMNP_FCS2=1,2,3                   -> identical outputs, 0 failures
    make verify                                              -> VERIFY PASS (695 s; golden 144 identical; log: build/verify-task4-fix2.log)

## hst 5: MNP in the endpoint: detection, configuration, status, memory (2026-11-24)

First, the two review minors of Task 4 (b4890c3): the `mnp.h` break
contract reworded (data held from our LN 1 goes after its LNA's reset,
every later `resets` change flushes), and `sim/mnppeer` starts a break
queued before connect when the link comes up (test
`raw_ln1_before_connect`).

Then MNP as a mode of `v42ec_t`:

- `lapm` moved into `union { v42lapm_t lapm; mnp_t mnp; } l`; the v42
  goldens are byte-identical (source edit only, 43 uses in `test_v42.c`).
- Detection (answerer, `mnp.enable` 1, no ODP seen): a CRC-valid LR in
  start-stop (a second hunter, `hunt2`, on the detection phase's
  characters) or in HDLC (the existing receiver's frames; also after flags,
  while LAPM has taken nothing) starts the MNP link in LAPM's place; our
  replies go in the framing the LR came in. An ODP first is LAPM. With
  `enable` 0 nothing new runs (the hunter stays zero).
- The data phase: the link is drained after every frame; busy while the
  receive ring has less than k x N401 free; on each `resets` change both
  rings are flushed, except at our own LN 1's LNA (the data the user wrote
  after asking for the break goes then). Breaks are LN 1..3; release is
  LD 255 and DISCONNECTED at once; `v42ec_tx_busy` (and so
  `v90m_ec_tx_busy`) covers an LD owed and the mode 2 framer (F24).
  `mnp_abort` added for `v42ec_abort`. LTs over HDLC are cut at 254 octets
  (`V42HDLC_MAX_PAYLOAD`), so N401 256 in the standard layout still fits.
- API: `v90m_ec_cfg.mnp` (ranges in `V90M_EC_CFG_FIELDS`;
  `t401_min_ms` 500..60000, the link's clamp); the rule "n401 256 needs
  opt bit 1" in `check_ec_cfg` after the generated ranges (F22);
  `V90M_EC_PROTO_*`; the twelve status fields.
- `v90m_ec_mem_size()`: 49792 -> 50128 on x86-64, 49568 -> 49892 on ARMv5,
  pinned in `test_api.c` (`V90M_EC_MEM_SIZE` 51200 unchanged).
- `rec_orig`'s rule listed the v42 sources by hand; it now uses `$(V42_SRC)`.

    v42/build/test_ec_mnp                 -> test_ec_mnp: 103 checks, 0 failures (13 cases)
    mutations (always flush, never flush, never busy, no LD in tx_busy) -> 1-3 failures each
    build/api_test_api                    -> api: 1025 checks, 0 failures (was 953)
    build/api_test_ec_fields              -> 14 checks; 34 fields, 49 status fields, 0 differ
    re-recorded: api/test_api.txt (count line), api/test_ec_fields.txt (the field counts)

## hst 6: MNP over V.32bis and V.22bis through the public API; milestone 1 (2026-11-24)

First, the three items carried from Task 5's review, each with its own
commit and a failing check first.

**`sim/mnppeer`'s LN 3** (review: the peer sent any LN at once). Annex A
A.7.4.1: "Non-expedited LN frames are sent after the acknowledgement of any
LT frames pending transmission or retransmission at the time of the LN
request, but before any subsequent user data"; Table 4 holds the data after
it until the acknowledgement. The peer now records, with each LN queued, the
octets written before it (`lnq_ahead`); new LTs stop there, the LN 3 goes
once they are out and every stored LT is acknowledged, and no new LT goes
while it is outstanding. A destructive break, which empties the write queue,
clears the counts. Type 2 is unchanged (expedited, no effect on data).
`test_mnppeer`'s output is unchanged.

Endpoint tests the review found missing (M3), in `test_ec_mnp`:

- `ld_const1`: an LR whose constant parameter 1 is 3: MNP, LD 2, the
  endpoint's reason `NEGOTIATION`.
- `n400_end`: the caller goes silent with 1000 octets unacknowledged,
  `n400` 3: four T401 expiries, LD 4 reaches the peer, reason `N400`.
- `ln_brk`: the peer's LN 3 arrives as an in-sequence break after exactly
  the 300 octets it wrote before asking (and the 100 after follow), its
  LN 2 as an expedited one; ours: LN 3 after exactly our 500 octets
  (`feed_mnp`'s `brk_tx_after` path), then LN 2. No reset either side.

    v42/build/test_ec_mnp, the peer unfixed -> 1 failure: the peer's LN 3 "a break after 0 of the 400 octets"
    v42/build/test_ec_mnp                   -> test_ec_mnp: 135 checks, 0 failures (16 cases)
    v42/build/test_mnppeer                  -> 210 checks, 0 failures, output identical
    re-recorded: v42/test_ec_mnp.txt (the count line and the four new lines)
    make verify                             -> VERIFY PASS (414 s; golden 146 identical)
    re-recorded: v42/test_mnp.txt, the `wrap` line only: the peer alternates LN 2 and LN 3 while
      it writes data, and its LN 3s now wait for the data written before them, so the
      frame counts move (LT in 314 -> 311, retx 15/19 -> 10/14); 300/300 LNs in order, data intact

**T400 at 300 bit/s** (review: a mode 2 LR at 300 bit/s takes about 1 s,
longer than T400's 750 ms, so a V.21 MNP caller fell to start-stop). With
MNP on, when T400 runs out while the start-stop hunter is inside a frame
(after its SYN DLE STX; new `mnp2_rx_in_frame`), T400 is held: once, until
that frame ends (an LR: MNP; anything else: start-stop at the frame's end),
and at most `V42EC_T400_LR_HOLD_MS` = 2000 ms [OURS] (an LR of about 80
characters started at once still fits at 300 bit/s; real ones are 20 to 31).
Only start-stop frames are waited for: MNP over V.21 is framing 2, and an
HDLC receiver is "in a frame" after any flag in a typed stream, which would
delay a plain caller's start-stop mode. With MNP off nothing new runs. A
plain caller that types SYN DLE STX just before T400 gets start-stop at
the frame's end (DLE ETX and two octets) or at 2750 ms; with MNP off, at
750 ms as before.

- `lr_300`: mnppeer, framing 2, at 300 bit/s: still DETECT at 750 ms, then
  MNP, 256 octets each way intact.
- `t400_hold`: at 300 bit/s, a frame that never ends: start-stop at exactly
  2750 ms; a Courier LR with a bad FCS: start-stop at its last character,
  1214 ms; MNP off: at 750 ms.
- `v90m_ec_mem_size()`: +8 on x86-64 (50128 -> 50136), +4 on ARMv5 (49892
  -> 49896), the `t400_held` flag; re-pinned in `test_api.c`.

    v42/build/test_ec_mnp before the hold -> 8 failures (lr_300: protocol ASYNC; t400_hold: 750 ms both)
    v42/build/test_ec_mnp                 -> test_ec_mnp: 146 checks, 0 failures (18 cases)
    build/api_test_api; qemu-arm $A5      -> 1025 / 1015 checks, 0 failures (the new pins)
    re-recorded: v42/test_ec_mnp.txt (the count line and the two new lines)
    make verify                           -> VERIFY PASS (450 s; golden 146 identical)

**Line rates for MNP** (review: nothing fed the rates outside HST, so T401
was computed at 2400 bit/s for the whole call; at 300 bit/s, framing 3,
about half the LTs went twice). There was no call for it. New
`v90m_ec_set_rates(ec, tx_bps, rx_bps)` (and `v42ec_set_rates`): the host
calls it after the `v90m_ec_reset` at `V90M_EV_CONNECTED` and at each
`V90M_EV_RATES`, with `v90m_state.tx_bps`/`rx_bps`. The endpoint keeps the
rates through detection, hands them to the link when MNP starts, and to the
running link at once. LAPM ignores them. Reading them through the modem was
the other choice; it would give the endpoint a handle on a `v90m_t` it has
never had (it runs over a recording or a rig as well), while the host
already handles both events. Documented in `docs/error-control.md` (a new
MNP paragraph, with the T400 hold) and `docs/api.md`.

- `rates_300` (test_ec_mnp): framing 3 at 300 bit/s, the rates given
  before MNP starts or once it is up: 4 KiB each way, no T401 expiry, no LT
  again (27 expiries, 29 LTs again with the call a stub).
- `test_api`: `V90M_OK` for good rates, `V90M_EINVAL` for 0, -1 and NULL.
- `v90m_ec_mem_size()`: 50136 -> 50144 (x86-64), 49896 -> 49904 (ARMv5).

    v42/build/test_ec_mnp, v42ec_set_rates a stub -> 6 failures (27 T401 expiries, 29 LTs again; -1 not refused)
    build/api_test_api before the API call        -> does not compile (implicit declaration of v90m_ec_set_rates)
    v42/build/test_ec_mnp                         -> test_ec_mnp: 158 checks, 0 failures (19 cases)
    build/api_test_api; qemu-arm $A5              -> 1030 / 1020 checks, 0 failures
    re-recorded: v42/test_ec_mnp.txt (count line, two new lines); api/test_api.txt (count line, 1025 -> 1030)
    make verify                                   -> VERIFY PASS (458 s; golden 146 identical)

**MNP through the public API** (the task proper): `test/api/test_mnp_call`,
the answerer from the public API with a `v90m_ec_t` on its bit pump,
`mnp.enable` 1, started as `docs/error-control.md` says: `v90m_ec_reset`
and `v90m_ec_set_rates` from the `V90M_EV_CONNECTED` callback, the rates
again on `V90M_EV_RATES` (none came in these calls). The callers carry
`sim/mnppeer` on their bits once connected: `sim/v32call` after V.8 over
`sim/v32line`, and `sim/callpeer` (V.8, then V.22bis or V.21) with
`callpeer_set_pump`. A PRBS-23 each way, every octet checked; then our
release once everything written is acknowledged, and the peer gets LD 255.
Linked with the development archive alone (its own rule, host and ARM).

- `v32bis_mode3`, `v32bis_mode2`: 14400/14400, MNP in framing 3 / 2,
  32 KiB each way intact, no T401 expiry, no LT again.
- `v22bis_mode3`, `v22bis_mode2`: 2400/2400, the same.
- `v21_mode2` (the review's 300 bit/s case through the API): MNP after the
  T400 hold (the endpoint still in DETECT at 750 ms), 2 KiB each way
  intact, no T401 expiry (rates given). The V.21 caller's LR waits 500 ms
  (`lr_delay_ms`): `sim/callpeer` counts as connected as soon as V.8 has
  chosen V.21, about 100 ms before our CONNECTED, so its first LR started
  before our endpoint and was cut (seen in the first run: start-stop).
- `mnp_off`: MNP off, the same V.32bis and V.22bis callers. In framing 2
  the LR is characters: start-stop at exactly 750 ms, as the brief says.
  In framing 3 the LR comes with flags, which mean LAPM (V.42 7.2.1.3) in
  0.6.0 as now: ESTABLISH from the flags, then `NO_SABME` after
  `establish_ms` (30.1 s and 30.2 s). The brief's "ASYNC after 750 ms"
  holds for framing 2 only; the test checks what 0.6.0 does in each.
- `TEST_SHORT` (ARM under qemu): 4 KiB each way (256 at 300 bit/s), and the
  framing 3 `mnp_off` cases stop at 750 ms. ARMv5 under qemu: 72 s (the
  simulator's callers are double precision, soft float there); the host
  short build prints the same.

BinModem's `modem-sip`: its usage text (crates/modem/src/bin/modem-sip.rs,
the `--help` string; the program is not built here) offers no MNP mode
and no modulation option; the modem is driven by `--type` AT lines. So no
second MNP peer from it for Task 10 on this evidence.

    build/api_test_mnp_call, before registration     -> did not build (V32_RALL, from v32/, not on the API include path)
    build/api_test_mnp_call, first run               -> 20 failures: tx_idle before release (harness), V.21 LR cut, mnp_off framing 3 (brief)
    build/api_test_mnp_call                          -> test_mnp_call: 161 checks, 0 failures (1.9 s)
    qemu-arm $A5 build/arm/api_test_mnp_call (short) -> 155 checks, 0 failures, identical to the host -DTEST_FORCE_SHORT build
    new golden: api/test_mnp_call.txt
    make verify                                      -> VERIFY PASS (439 s; golden 147 identical; ran in a7 short 155/0, big-endian 161/0, ubsan 161/0)

Milestone 1 gate, on fd2b148 (log: build/verify-phase-m1.log):

    make verify-phase RECS=$RECS ARM_CC=$A5 -> VERIFY-PHASE PASS (23176 s)
      armv5    ALL ARM CHECKS PASS (2468 s)
      ports    15 of 15 targets identical to host-gcc (full: armv5-uclibc s390x-glibc riscv64-musl);
               m68k-uclibc: 225 of 235 identical (8 lines only with printf's rounding), 10 skipped (quick)
      armv7hf  short-cmp: 141 programs identical
      benches  host = qemu hashes: 10 benches; crd: 106 recordings, 0 CRd detections
      costs    17 lines in build/costs.txt (V.34 107 %, k56 102 %, x2 181 % of V.90 data mode)

Review fixes (round 1), with the controller's rulings:

- m1: T400 is held only for a frame that begins as an LR: after SYN DLE
  STX, an LI of 2 to 254 and type 1 (`lr_partly_in`). This keeps the global
  constraint: a caller that sends no LR gets exactly 0.6.0's T400. Typed
  text with SYN DLE STX at 600 ms, at 300 bit/s, now gives start-stop at
  750 ms exactly, with all 11 characters (it gave nothing by 1750 ms with
  the wider hold: 3 failures). An LR's start that never ends still gives
  2750 ms; a bad LR, its end (1214 ms); `lr_300` and `v21_mode2` still
  connect. An LR of which fewer than two body octets have come by T400 is
  not waited for.
- m2: "a reset forgets the rates" in the header, `docs/api.md` and
  `docs/error-control.md`; `docs/getting-started.md`'s answering program
  calls `v90m_ec_set_rates` after the reset at CONNECTED and on RATES.
- m3: `v32bis_rates` in `test_mnp_call`: halfway through 32 KiB the V.32bis
  caller retrains offering 9600 at most; one `V90M_EV_RATES`, the endpoint
  given 9600/9600, the data stop and restart passed on, data intact (9 LTs
  again, one frame cut). The `rates_ev == 0` assertion is gone; every case
  checks `rates_calls == 1 + rates_ev` and the endpoint's rates against
  `v90m_state`.
- m4: the release drain counts `drain_ms`.
- m6: `arm/api_test_mnp_call` in `tools/ports-slow.txt`; `test-api-arm`
  now honours `ARM_SKIP` (`arm/api_<test>` lines), as the portrun runner
  already did by path.
- The plan's Task 6 Step 1 `mnp_off` text now says framing 3 starts LAPM,
  as 0.6.0 does.

    v42/build/test_ec_mnp with the wide hold -> 3 failures (typed: no start-stop at 750 ms, 0 of 11 characters)
    v42/build/test_ec_mnp                    -> test_ec_mnp: 163 checks, 0 failures
    build/api_test_mnp_call                  -> test_mnp_call: 208 checks, 0 failures (host short: 202)
    re-recorded: v42/test_ec_mnp.txt (t400_hold's line, the count), api/test_mnp_call.txt (two v32bis_rates lines, the count)
    make verify                              -> VERIFY PASS (793 s; golden 147 identical; a7 and big-endian skip api_test_mnp_call)

## hst 7: the HST mailbox; a linked endpoint hunts from the first sample and has the HST window (2026-11-25)

The tie between the endpoint and the HST pump (spec "Tying the endpoint
to the modem", D18, D19), with a stub pump; the pump itself is Task 18.

- `v42/hst_link.h`: the mailbox `hst_link_t` (the plan's fields), the get
  function type, `HST_LINK_UNDECIDED/NONE/ON`, `HST_ROLE_*`, and the
  pre-connect ring's two accessors (`hst_pre_put` for the pump, a full
  ring drops the new bit and counts it in `pre_lost`; `hst_pre_get` for the
  endpoint). It includes nothing of `modem/`.
- `v42ec_set_link(e, get, ctx)` (NULL unlinks; MNP off or an originator
  is never linked), `v42ec_linked`. Linked, the endpoint asks `get` at the
  top of `v42ec_tx_bit`, `v42ec_rx_bit` and `v42ec_tick` (F5 a): the
  ring's bits go into the receive path before the bit in hand, so an LR
  sent during our training is answered at our first transmitted bit,
  ticked or not.
- T400 is held while the state is UNDECIDED and counts from NONE on (F4:
  the modem says NONE once connected or ended on another path, so a
  non-HST call gets start-stop 750 ms after CONNECTED). On ON there is no
  T400 and no start-stop: the window, from the tick that sees `trained`,
  `V42EC_HST_WINDOW_MS` (5000), then `V42EC_R_NO_EC`, in detection and in
  LAPM's establishment (flags without an LR) alike, whatever `fallback`.
- `v42ec_reset(e, cfg)` (F2): keeps the link, the ring (it is the pump's),
  the window and an LR taken on HST, which it answers again; with
  `mnp.enable` 0 it unlinks. `v42ec_init` stays a clean start.
- The endpoint writes `agreed` (1), `agreed_rates` (0) and `back300` (0)
  when it accepts the caller's LR (no profile yet: Task 8), and takes the
  pump's `tx_bps`/`rx_bps` for T401 whenever they change (F18).
- Nothing changes for an unlinked endpoint (every path tests `link_get`).
- `v90m_ec_mem_size()`: 50144 -> 50440 on x86-64, 49904 -> 50188 on ARMv5
  (F3; `V90M_EC_MEM_SIZE` 51200 unchanged, 760 octets left on x86-64),
  re-pinned in `test_api.c` (same check count: its golden is unchanged)
  and in `docs/error-control.md`.

    v42/build/test_ec_link (before hst_link.h) -> fatal error: hst_link.h: No such file or directory
    v42/build/test_ec_link (before v42.c)      -> implicit declaration of v42ec_set_link, v42ec_reset, v42ec_linked
    v42/build/test_ec_link                     -> test_ec_link: 52 checks, 0 failures (5 cases)
    mutations (no drain in tx_bit; reset without the LR) -> 5, 3 failures
    make -s -C v42 test                        -> ALL TESTS PASS (test_ec_mnp 163 checks, unchanged)
    build/api_test_api                         -> api: 1030 checks, 0 failures
    new golden: v42/test_ec_link.txt
    make verify                                -> VERIFY PASS (442 s; golden 148 identical, replay 636 runs, bench 17, ubsan, a7, big-endian, fixed)

Review fixes (task-7-review.md):

- C1: the HST state is per call. `v42ec_reset` keeps the window and a
  captured LR only for the reset at this HST call's CONNECTED: `get` says
  ON now, nothing has been transmitted since the last start (`tx_seen`),
  and the mailbox is the same one (its ring read count has not gone back;
  the pump zeroes the mailbox per call). `link_poll` drops the captured LR
  and a pending `agreed` whenever the state is not ON; `v42ec_set_link`
  forgets any call in progress. The captured LR is dropped once MNP is
  connected (M3).
- I1: ON latches (`hst_call`) for the call: a later NONE gives neither
  T400 nor start-stop; an unstarted window starts then; no LR: NO_EC.
  `hst_link.h` says ON lasts the whole call.
- M1: a full ring drops its oldest bit; the header ties HST_PRE_BITS to
  the training. M4: a window that ran out stays at 0. M5: dead clamp and
  guard gone. M6: `establish_ms` does not run while an HST call hunts.
  M7: NONE before any ON is final for the call: `get` is not asked again
  until the next init or reset. M8: `none_same_as_unlinked`.
- `hst_link.h` states for Tasks 9 and 18: `get` has no side effects, says
  NONE from the sample that raises CONNECTED on a non-HST call; the pump
  stops appending to the ring when putbit starts.
- `v90m_ec_mem_size()`: 50440 -> 50448 (x86-64), 50188 -> 50200 (ARMv5).

    v42/build/test_ec_link on 09f74c2's v42 -> 19 failures (two_calls: LR replayed, protocol 2; on_then_none: start-stop; window_overshoot; ring_overflow: 512 wrong)
    v42/build/test_ec_link                  -> test_ec_link: 92 checks, 0 failures (10 cases)
    mutations (reset without tx_seen; without the ring-count test) -> 1, 2 failures
    make -s -C v42 test                     -> ALL TESTS PASS
    build/api_test_api                      -> api: 1030 checks, 0 failures
    re-recorded: v42/test_ec_link.txt (its own: five new lines, the count)
    make verify                             -> VERIFY PASS (441 s; golden 148 identical, replay 636, bench 17, ubsan, a7, big-endian, fixed)

## hst 8: the HST profile: 0xC0, streamlined layouts, tolerant parser, attention codes, reversal policy (2026-11-25)

- `mnp_frame`: `mnp_lr_merge(..., hst_profile, ...)` merges USR's 0xC0
  (bit 0 from both, 300 if either asks, rates ours & theirs & 0x7C) when
  the caller's has bit 0; `mnp_c0_best`. `mnp_hdr_build` and
  `mnp_hdr_parse` take `MNP_LAYOUT_STREAM` (no LI). The parser reads every
  layout except with plain `MNP_LAYOUT_STD`/`_OPT` (Annex A only, as
  before): when both readings fit, the agreed layout wins
  (`MNP_LAYOUT_STREAM`, or `_STD|MNP_RX_ALL`), `MNP_LAYOUT_ANY` lets the
  expected N(S) break the tie; an LR is always standard; an unknown first
  octet is `MNP_PARSE_UNKNOWN`.
- `mnp`: the profile state in `mnp_t` (3264 -> 3368 octets; ec memory
  unchanged, 50448/50200). Our LR offers `C0 02 01 offer_rates`;
  streamlined when both LRs carry a non-zero parameter 8; without it the
  receive layout is the caller's last LA/LN/LNA's. Attention codes as
  holder (0x1R granted, its LNA's closing flag reported; 0x2R/0x3R to
  `rate_order`) and holding back (the far holder's 0x2R/0x3R passed on, or
  above `cap_up_code` a count and our 0x2R at the cap; also after a
  reversal granted above it, at the next epoch); others an LNA and a
  count. LTs held from an attention until the epoch changes, at most 12 s.
  The reversal policy (backlog > 64, far end forward >= 2 s, no attention
  outstanding). On HST a repeated N(R) goes back once per N(R) value, and
  T401 adds the far end's one LT and the back receiver's hold-back (127
  ms at 450, 180 at 300). LD in the streamlined layout after agreement.
- `v42.c`: the offer from the mailbox at the LR, `mnp_hst_poll` at every
  poll, `ep_idle` and `rev_lna_sent` after each transmitted bit, the
  status fields, no break on HST.
- `sim/mnppeer` HST mode (F13), from the facts: 0xC0 in both roles,
  streamlined layouts (or `stream_always`), attention codes passed
  through, `unknown_ids`, and T401 from Annex A's formula when `tx_bps`
  and `rx_bps` are given (`loop_hst`).
- Decision 26 (spec, facts): a failed establishment ends with LD 1 and
  `N400`, as the code already did.
- Throughput, simulated (our endpoint at the forward rate, the peer's LAs
  at the back rate through its hold-back; tools in the task report):

      14400/450, clean:   66.3 % -> 75.7 % of the line
      9600/300, clean:    66.4 % -> 75.7 %
      14400/450, 20 ppm:  48.9 % -> 66.6 %; LTs sent again per LT lost 24.3 -> 7.2
      9600/300, 20 ppm:   49.9 % -> 66.4 %; 21.1 -> 6.0
      (N401 256 would give 96.4 % / 96.3 % clean; decision 23 keeps 64)

    v42/build/test_mnp_frame (before)  -> MNP_C0_HST, mnp_c0_best, MNP_LAYOUT_ANY, MNP_RX_ALL undeclared
    v42/build/test_ec_hst (before)     -> test_ec_hst: 113 checks, 55 failures
    v42/build/test_mnp_frame           -> test_mnp_frame: 350 checks, 0 failures
    v42/build/test_mnppeer             -> test_mnppeer: 246 checks, 0 failures
    v42/build/test_ec_hst              -> test_ec_hst: 117 checks, 0 failures (12 cases)
    mutations (literal go-back; rev_lna_sent at framing; backlog >= 64; no HST T401) -> 4, 1, 1, 4 failures
    re-recorded: v42/test_mnp_frame.txt, v42/test_mnppeer.txt (new lines and the count only)
    new golden: v42/test_ec_hst.txt
    make verify                        -> VERIFY PASS (457 s; golden 149 identical, bench 17, ubsan, a7, big-endian, fixed)

Review fixes (task-8-review.md):

- I1: the receive layout (HST, no parameter 8 from both) is learned from
  the caller's first frame that settles it once connected: one read only
  one way, or an LT with exactly one reading in sequence (closing the
  window at its first in-sequence LT). Until then a frame read two ways
  as two types, neither an LT in sequence, is not acted on (an LA asks
  again). The "LA/LN/LNA read only one way" comment was wrong (a standard
  LNA is also a streamlined LT 7) and is gone. `test_ec_hst layout_learn`:
  A (LT 4 lost, then LT 5), C (LT 2 again as `01 01 30`, a false LD), D
  (a standard LNA at V(R) 7) fail on cfa6bc4 (6 failures: 7 octets
  delivered, LD 48 and the call ended, 9 octets) and pass.
- Minor: `gb_nr` cleared when T401 fires (a repeated N(R) may go back
  again after a T401 resend).
- m1 (a standard LD from a streamlined caller is missed) recorded in the
  spec; m5: `attn_out` also checks our LN and our LD during a hold; m6: an
  R2 line for the empty-mask reply; m2, m3 needed nothing; m7 noted (the
  peer's T401 helper is shaped like ours; its pinned values were worked by
  hand in the facts).
- Decision 27: HST keeps N401 64 (decision 23 stands), with the
  reviewer's T401 figures.
- For Task 22: `rate_order` is read by role (holding back it is the far
  holder's own 0x2R/0x3R, for the receiver to follow), and the pump must
  bump `epoch` once it has followed one, or our back-channel LTs stay held
  up to 12 s (`MNP_HST_HOLD_MAX_MS`).
    make verify (fix round 1)          -> VERIFY PASS (461 s; golden 149 identical, replay 636, bench, ubsan, a7, big-endian, fixed); re-recorded: v42/test_ec_hst.txt (its own)

## hst 9: the public API for HST; milestone 2 (2026-11-25)

The public face of the HST link and the numbers HST will report:

- `V90M_MOD_HST` 0x0400: answering only (`V90M_ENOTSUP` calling), needs
  no other bit, in `V90M_MOD_ALL` only (not the defaults, not the calling
  side's); `modes_of` gives `ANS_MODE_HST` 0x400000. `ANS_MODE_HST_LINKED`
  0x800000 is the link, in the same `modes` word (`answer_t` does not
  grow). test_api's unknown-bit check moves to 0x0800.
- `V90M_CONN_HST` 12 = `ANS_MOD_HST` (the `SAME` check), its name "HST"
  (`DG_LAST_CONN`); `V90M_END_HST_FAILED` appended to `v90m_end`, and
  `ANS_END_HST_FAILED` to the answerer's enum and `ans_end[]` in the same
  place; `v90m_stats.reversals` appended (0 until the HST engine).
- `v90m_ec_link_hst(e, m)`: `V90M_OK`; `V90M_EINVAL` for either NULL, `e`
  with MNP off (or an originator, whose MNP is always off), `m` calling;
  `V90M_ESTATE` once `m` has left `ANS_V8`/`ANS_V8BIS`
  (`answer_hst_decided`). The endpoint's get is `api_hst_get`, over
  `answer_hst_link`: UNDECIDED until the modem is in `ANS_HST` (ON, from
  the HST answering branch) or connected or ended on another path (NONE,
  from the sample that raises `connected`); no side effects.
- Lifetime: `v90m_ec_init`/`_create` start clean; `v90m_ec_reset` uses
  `v42ec_reset` and no longer clears the wrapper, so a reset at
  `CONNECTED` keeps the link and an LR taken in training. A reset with
  MNP off, and `v90m_ec_destroy`, unlink and clear the modem's link bit
  while it is undecided; `v90m_reset` rebuilds the modes (link again).
  Linking to another modem first lets go of the previous one. A linked
  modem the host resets to the calling role is no HST call (get says
  NONE; its memory is not read as an answerer's).
- `v42ec_tx_bit` marks a bit sent after the link poll, which may now
  start a new call.
- The endpoint (`v42/v42.c` `link_poll`): a mailbox whose ring read count
  has gone back is a new call's, and the endpoint starts again (as
  `v42ec_reset` would for a new call). A host that drives the endpoint
  before `CONNECTED` and never resets it between calls no longer carries
  call 1's window, LR or protocol into call 2 (Task 7 review N1).
- Docs: the host pattern for an HST-capable call in
  `docs/error-control.md` ("When to start it") and
  `docs/getting-started.md`; the note that an LR begun in the last
  ~165 ms before T400 at 300 bit/s falls to start-stop; a frame
  half-received at a reset is lost and the caller repeats it.

Tests:

    make build/api_test_api build/api_test_ec_hstlink (before)
                                       -> V90M_MOD_HST, V90M_CONN_HST, V90M_END_HST_FAILED,
                                          v90m_stats.reversals, v90m_ec_link_hst,
                                          ANS_MODE_HST_LINKED, answer_hst_link undeclared
    v42/build/test_ec_link (before the link_poll change)
                                       -> test_ec_link: 105 checks, 8 failures (new_mailbox)
    build/api_test_api                 -> api: 1067 checks, 0 failures
    build/api_test_ec_hstlink          -> test_ec_hstlink: 68 checks, 0 failures (0.2 s)
    v42/build/test_ec_link             -> test_ec_link: 105 checks, 0 failures
    mutations: get always NONE -> 9 failures; reset by v42ec_init and no
               unlink at destroy -> 2 failures; get without the calling-role
               guard -> 2 failures (role_change)
    re-recorded: api/test_api.txt (the count line only), api/test_diag.txt
               (the count line only: "HST" named, 13 the first unknown),
               v42/test_ec_link.txt
               (the new_mailbox line and the count)
    new golden: api/test_ec_hstlink.txt
    ARMv5 (make test-api-arm, $A5): api 0 failures (pins 106016 / 50200),
               test_ec_hstlink 68 checks, 0 failures
    make verify                        -> VERIFY PASS (714 s; replay 636 identical, bench 17,
                                          ubsan, armv7hf, big-endian, fixed)
    make verify-phase                  -> deferred to the merge of tracks A, B and C

    test_ec_hstlink: linked_v32_odp   CONNECTED at 5425 ms, UNDECIDED before, NONE then;
                                      LAPM, 42 octets each way
                     linked_v32_noec  start-stop 750 ms after CONNECTED
                     relink_after_reset  linked 1; ec reset 1; v90m_reset 0; linked
                                      again 1; ec reset MNP off 0; ec destroyed 0
                     role_change      reset to calling while linked: start-stop
                                      750 ms later (unlinked); then as a fresh caller
                     hst_offered_now  V.22bis + MNP: CONNECTED 3978 / 3978 ms, MNP both,
                                      our samples the same

Memory: `v90m_mem_size()` 106320 (x86-64), 106016 (ARMv5), unchanged;
`answer_t.u` unchanged (45096); `v90m_ec_mem_size()` 50448 / 50200,
unchanged (the link lives in `v42ec_t` since Task 7; the modem is the
link's context).

Review fixes (task-9-review.md), on top of the merge of tracks B and C
(d327d20):

- I1: the link is two-way. `struct v90m` holds the linked endpoint
  (`hst_ec`). `v90m_destroy` and `v90m_reset` end the link from the
  modem's side; `v90m_ec_destroy`, a reset with MNP off, and linking
  elsewhere end it from the endpoint's; a second endpoint linked to a
  modem unlinks the first. Neither side keeps the address of a freed
  object, so either destroy order is safe, as is a new modem per call.
  The pointer is paid for by narrowing `answer_t`'s `x2_offered`,
  `k56_offered` and `k56_agreed` (0/1, never address-taken) to
  `uint8_t`: `answer_t` 106208 -> 106200, `struct v90m` unchanged
  (106320 x86-64, 106016 ARMv5).
- N1 gap: a call that went HST but drained no bit left `link_r` at 0, so
  the next call's zeroed mailbox was not seen as new. The endpoint now
  marks a mailbox it has polled ON (`hst_link_t.ep_seen`, written by the
  endpoint); an unmarked one is a new call's. `hst_link.h` states the
  pump's rule for Task 18: within a call it never zeroes the mailbox,
  clears `ep_seen`, or moves `pre_r` back (reversal, retrain, rate change,
  a return to the HST phase).
- m2: the never-reset paragraph of error-control.md narrowed (the
  self-restart covers only a following HST call). m5: tools/x2-peer
  bounds its name tables (`NAME()`).
- Tests: `test_ec_hstlink lifetimes` (the reviewer's four paths, a
  second endpoint, the documented order, the modem's reset), clean under
  clang ASan (`make check-asan-hstlink`, new); with the modem-side unlink
  removed ASan reports heap-use-after-free in `hst_unpair`. The
  reviewer's probe (build/tmp/rv9/uaf.c) is clean in modes 0 to 4.
  `test_ec_link`: `new_mailbox` with a call 1 that drained no bit (5
  failures before `ep_seen`), and `wrap_same_call` (the read count
  across 2^32 within a call: no restart).

    build/api_test_ec_hstlink          -> test_ec_hstlink: 93 checks, 0 failures
    v42/build/test_ec_link             -> test_ec_link: 116 checks, 0 failures
    build/api_test_api                 -> api: 1067 checks, 0 failures (golden unchanged)
    make check-asan-hstlink            -> ASAN HST LINK CLEAN
    re-recorded (this task's own): api/test_ec_hstlink.txt, v42/test_ec_link.txt
    ARMv5 (test-api-arm, $A5)          -> api 0 failures (pins 106016 / 50200), test_ec_hstlink 93/0
    make verify                        -> VERIFY PASS (513 s; golden 154 identical, replay 636,
                                          bench 17, ubsan, armv7hf, big-endian, fixed)

Review fixes, round 2 (task-9-rereview.md):

- N-I1: linking (`v42ec_set_link` with a get, which `v90m_ec_link_hst`
  calls) starts the endpoint afresh, as `v42ec_init` with its
  configuration: the link is for a call the modem has not decided yet. A
  host that ticks the endpoint from the first sample, never resets it,
  and only links again between calls no longer meets call 2 in call 1's
  end state (its training-time LR drained into a DISCONNECTED, ASYNC or
  connected endpoint and dropped). Unlinking (NULL get) changes nothing
  else. `test_ec_link relink_next_call` (stub pump: an HST call 2's LR
  from our training answered at bit 16; 5 failures before) and
  `test_ec_hstlink relink_between_calls` (real modems: a new modem for
  call 2, linked again and nothing else, LAPM and data both calls; 2
  failures before).
- N-m1: the header's `v90m_ec_init` comment: destroy an endpoint in
  caller memory before that memory is freed or initialised again.

    v42/build/test_ec_link             -> test_ec_link: 124 checks, 0 failures
    build/api_test_ec_hstlink          -> test_ec_hstlink: 109 checks, 0 failures
    make check-asan-hstlink            -> ASAN HST LINK CLEAN
    make verify                        -> VERIFY PASS (497 s; golden 154 identical, replay 636,
                                          bench 17, ubsan, armv7hf, big-endian, fixed)
    re-recorded (this task's own): api/test_ec_hstlink.txt, v42/test_ec_link.txt

### For the local nsdiald test build (D22)

What the library side provides for the BinModem-over-SIP, Courier and R3
calls. Run on this machine only (`127.0.0.1:5070`), never deployed.

- **Library:** branch `hst` of libv90modem (worktree `~/hst-wt`), the
  commit "api: relinking starts the endpoint's next call (review
  fixes)" or later (the two before it had a use-after-free on a modem
  destroyed while linked, and lost an HST caller's training-time LR on a
  relink without a reset: do not use them). `VERSION` still
  says 0.6.1; 0.7.0 will change the SONAME (structs changed: link
  statically, or rebuild nsdiald against this tree).
- **What works now:** MNP (V.42 Annex A, framing modes 2 and 3) on the
  answering endpoint, over every modulation, and the HST profile inside
  the endpoint (0xC0, streamlined layouts, attention codes). The HST
  answering branch of the modem (the pump) is not in yet: with
  `V90M_MOD_HST` offered, every caller is answered as without it (a
  Courier calling gets whatever it falls back to, V.32bis or V.22bis,
  with MNP if it sends an LR). R3 (MNP-only callers over V.32bis and
  V.22bis) can run now; HST itself needs the answering branch
  (milestone 4).
- **Enable:** `cfg.modulations |= V90M_MOD_HST` (answering) and, in the
  endpoint's configuration, `ec_cfg.mnp.enable = 1` (the other `mnp`
  fields at their defaults: k 8, N401 64, opt 2, framing 3, T401 floor
  1500 ms, N400 12).
- **The host pattern, in order:**
  1. `v90m_ec_cfg_default(&ecc, 0); ecc.mnp.enable = 1;` and
     `v90m_ec_create(&ecc, &err)`; `v90m_create(&cfg, &err)` with
     `V90M_MOD_HST` in `cfg.modulations`.
  2. `v90m_ec_link_hst(ec, m)` before the first `v90m_process_*`
     (`V90M_OK`; `V90M_ESTATE` once the modem has decided, the end of
     ANSam; `V90M_EINVAL` with MNP off).
  3. At `V90M_EV_CONNECTED`: `v90m_ec_reset(ec, &ecc)` (harmless: the
     link and an LR sent during our training are kept), then
     `v90m_ec_set_rates(ec, st.tx_bps, st.rx_bps)`; again
     `v90m_ec_set_rates` at each `V90M_EV_RATES`. Pass
     `DATA_STOPPED`/`DATA_MOVING` on as `v90m_ec_line_stopped`/`_restored`.
  4. getbit/putbit to `v90m_ec_tx_bit`/`v90m_ec_rx_bit`, `v90m_ec_tick`
     every block. Our cleardown: `v90m_ec_release`, keep the bits moving
     until `v90m_ec_tx_busy` is 0 (MNP's LD 255), then `v90m_cleardown`.
  5. Next call: `v90m_reset(m, &cfg)`, or a new modem per call
     (`v90m_destroy` then `v90m_create`): either way the modem starts
     unlinked, so link again before its first sample. Linking starts the
     endpoint's call afresh (as `v90m_ec_reset` would), so a
     `v90m_ec_reset` between calls is optional. One endpoint may serve
     every call. The link is two-way: destroy the two in either order. An
     endpoint in your own memory (`v90m_ec_init`): `v90m_ec_destroy` it
     before freeing or re-initialising that memory.
- **Log per call** (`v90m_ec_get_status`): `protocol`
  (`V90M_EC_PROTO_*`), `state`, `reason`, `mnp_framing`, `mnp_opt`,
  `mnp_streamlined`, `hst`, `hst_rates`, `hst_back300`, `attn_tx`,
  `attn_rx`, `rx_unknown`, `rx_unknown_id`, `peer_ld_reason`; and
  `v90m_stats.reversals`, `v90m_state.modulation` (`V90M_CONN_HST` 12
  on an HST call), `v90m_state.end`. Status fields only, past link-up: no
  recordings of user data (ISP recording rule).
- **End code:** `V90M_END_HST_FAILED`, an HST call that had no MNP link
  within its limit (a host that did not clear down when the endpoint gave
  up with `V90M_EC_R_NO_EC`).

## hst 11: the back channel's tables and its 8-DPSK transmitter (2026-11-24)

`tools/gen_hst_tabs.py` writes `modem/hst_tabs.[ch]` (the design is in its
header; a new table is one more function in its `TABLES`):

- `hst_bstep` = 1 6 2 5 0 7 3 4, the firmware's 0x43705261 a nibble at a
  time from the low end (BF §3) [FW].
- `hst_bpulse450` (160 phases a symbol, 3 a sample: 53 1/3 samples) and
  `hst_bpulse300` (80 phases, 1 a sample): RRC beta 0.35 over 10 symbols,
  Kaiser window beta 3, Q14 [OURS]. Ten symbols, not 8: with beta 4 over
  8 the pulse was 77 dB down at 600 Hz but its ISI through a matched
  filter reached 2.4 degrees (4.8 on a phase step); 10 symbols and beta 3
  give 67 dB and 1.4 degrees. The generator checks >= 60 dB at 600 Hz and
  above and prints the -20 dB band: 274.3-475.7 Hz (450), 282.9-417.1 Hz
  (300).
- `hst_bbpf`: 243 taps, Kaiser band-pass with cut-offs 200 and 550 Hz;
  250-500 Hz within 0.03 dB, at most -51.5 dB at <= 150 and >= 600 Hz.
  `hst_hbpf`: 189 taps, cut-offs 550 and 3700 Hz; 600-3400 Hz within
  0.08 dB, at most -41.5 dB at <= 500 Hz (our bound: 40 dB, enough for
  the detector's 10 dB ratio). The fewest odd taps that meet each with
  Q15 coefficients.

`modem/scramble.[ch]`: `scr17_scramble` and `scr17_descramble`, the
polynomial without V.22bis's 64-ones rule; `v22_*` untouched.

`modem/hst.h`, `modem/hst_back.c`: `hst_btx_t` (256 octets on x86-64, all fields fixed-width).
Each speed has its own pulse train and carrier, so that after
`hst_btx_set_speed` (taken at the old speed's next symbol boundary) the
old speed's last symbols ring out on their own pulse; both carriers run
from `hst_btx_init`. Preamble 300 ms of scrambled ones (45 symbols at
450, 30 at 300), phase and scrambler from 0; then getbit's bits, the
tribit's LSB first. `hst_btx_stop`: the pulses drain, then exact zeros.

`test_hst_back` (new; its golden file is new): `steps`, `scr17`, `filters`,
`tx_phase` (a test-side RRC matched correlator: every symbol's phase,
preamble included, on 22.5 + 45 k within 1.6 degrees, each step within 2.6),
`tx_speed`, `tx_spectrum` (Welch, 4096 points, 10 s of random data).

    make -s build/test_hst_back (before the code) -> fatal error: ../modem/hst_tabs.h: No such file or directory
    build/test_hst_back -> test_hst_back: 44 checks, 0 failures
      tx_spectrum 450: level 4001 (asked 4000), -20 dB band 275.4-474.6 Hz, >= 600 Hz -68.9 dB
      tx_spectrum 300: level 3999 (asked 4000), -20 dB band 285.2-416.0 Hz, >= 600 Hz -75.4 dB
    make check-fixed   -> fixed: 109 objects, 0 float helpers
    make check-headers -> headers: 110 compile alone
    make bench-hashes  -> bench hashes: 17 identical
    make verify        -> VERIFY PASS (692 s; golden 148 identical, test_hst_back's new;
                          armv7hf and big-endian short-cmp identical; ubsan pass)

Review fixes (round 1). `test_hst_back` now prints only exact integers
(taps, symbols, the level as the integer square root of the integer mean
square) and one pass/fail line per bound. The measured values, which come
from the test's own libm arithmetic, print only with `HST_VERBOSE` set
(make golden, verify.sh and verify-phase.sh unset it). Each case seeds its
own random data, so the cases run one at a time print exactly what the
whole run prints (checked plain and verbose). In `hst_back.c`, a negative
level is taken as 0, and `newsym` is set only when a symbol is sent. The
structs use fixed-width fields. `hst.h` notes that `hst_bbpf` is meant to
run at the receiver's 1600 Hz instants. The FFT twiddles are precomputed
and the filter grid is 2 Hz: ARMv5 under qemu takes 9.8 s, down from 15 s.
`tools/test-times.txt` has entries for both builds.

    build/test_hst_back -> test_hst_back: 42 checks, 0 failures; ARMv5 (qemu) output identical
    make verify         -> VERIFY PASS (1198 s; golden 148 identical, test_hst_back's own rewritten)

## hst 12: the back channel's receiver, 450 and 300 (2026-11-25)

`modem/hst.h`, `modem/hst_back.c`: `hst_brx_t` (1568 octets on x86-64 and
ARMv5, all fields fixed-width), `hst_brx_init/tick/band/cd/locked/snr/grid_ok`.
The spec's chain: `hst_bbpf` (folded, 122 multiplies) at every 5th sample
only, so at 1600 Hz; carrier detect on the band's power in 10 ms blocks
(on at -44 dBm0 for 3 blocks, off under -47 dBm0 for 3 blocks); AGC; a
32-bit NCO to baseband; the matched filter (Task 11's transmit pulse, +-4
symbols) evaluated only at the strobes, at each strobe's own pulse phase
(1/160 of a symbol at 450, 1/80 at 300: no interpolator); Gardner at two
strobes a symbol; differential decisions; `scr17_descramble`.
`hst_brx_band` takes a band-passed sample, so that the engine runs one
`hst_bbpf` for both receivers (hst.h said it would).

Acquisition is in three steps [OURS], each one-shot, because the loops
alone were too slow or wrong:
- Timing: four strobes a symbol over 8 symbols, the phase of the
  symbol-rate line in their power (Oerder and Meyr), then Gardner at two
  strobes a symbol. Gardner alone at a gain high enough to lock in time
  left 10-degree jitter.
- Carrier: over 10 symbols, the eighth power of the differential strobes
  gives the turn a symbol modulo 45 degrees. 45 degrees a symbol is baud/8
  Hz, 12.5 Hz at 300, where 7 Hz turns a symbol 25 degrees: a
  decision-directed loop settles there 45 degrees off (seen: 300 at -7 Hz
  locked at +5.7 Hz). The midpoint strobes settle it. A midpoint is its
  four neighbouring symbols weighted by the pulse's autocorrelation
  (computed at init from the table), each turned against it by w T/4 at
  T/2 and 3 w T/4 at 3T/2. The matched filter's output for a carrier off
  by w turns at w/2 across a pulse, so the turn is a quarter, not a half:
  with the half, the check could not tell. The right turn of r, r + 45 and
  r - 45 predicts the midpoints; the others miss them by 3 to 7 times the
  power. A cross-product estimator on strobes half a symbol apart was
  tried first: 5 Hz rms over 30 symbols, and biased.
- The estimate is taken off each differential angle (`wturn`), not put on
  the NCO. A step there reaches the strobes only through the matched
  filter's 8 symbols, and a decision-directed loop through the NCO was
  unstable for the same reason. The stored window is decided again with it,
  which fills the lock history and puts the descrambler in step: before
  that, the first bits after the lock were wrong. Once locked, a
  first-order loop on the decision error (1/32) tracks, and `wturn` drains
  into the NCO at 1/64 a symbol.
- Lock: 11 of the last 12 decisions within +-12 degrees of the grid, after
  the estimate; lost at 8 of 16 off. With no lock 24 symbols after the
  estimate, or when lock is lost, acquisition starts again.

No equaliser: `line_450` and `line_300` run v32line's line model (mu-law,
6 dB loss, the loop { 0.3, 1.0, -0.5, 0.3 }, 200 samples of delay, 20 dB
SNR, +7 Hz, +100 ppm) with no bit in error in 5 x 10^4.

`test_hst_back` (its golden file re-recorded: 42 -> 80 checks, the new lines
after the old ones, which are unchanged): `loop_450`, `loop_300` (four
lines each: +-7 Hz, +-100 ppm, white noise 20 dB below the signal over
0-4 kHz; a lighter line than v32line's, whose two windowed-sinc resamplers
cost some 200 libm calls a sample: sim/resamp's linear resampler, a
63-tap Hilbert shift, sim/noise), `lock_time`, `cd`, `line_450`, `line_300`.
TEST_SHORT: 10^4 bits on loop_*, 2 x 10^3 on line_*.

    make -s build/test_hst_back (before the code) -> error: unknown type name 'hst_brx_t';
        implicit declaration of function 'hst_brx_init' (and the other hst_brx_*)
    build/test_hst_back -> test_hst_back: 80 checks, 0 failures (12 s on the host)
    HST_VERBOSE=1: loop_*: 0 errors in 2 x 10^5 bits on all eight, locked 185-365 ms
      after the preamble's start, SNR estimate 27.5-32.5 dB; line_*: 0 errors
      in 5 x 10^4; carrier off 20-30 ms after the line falls silent
    lock time over 800 lines (scratch driver, seeds 1-50, four offsets each,
      flat and v32line's mu-law line): worst 225 ms at 450, 328 ms at 300; the
      carrier within 0.12 Hz on every one (no alias). At 10 dB SNR: 252 and 389 ms.
    ARMv5 (uclibc) and armv7hf (musl) short sets identical to the host's
      -DTEST_FORCE_SHORT; clang -fsanitize=undefined short set identical, clean;
      each case alone prints what the whole run prints
    make check-fixed   -> fixed: 109 objects, 0 float helpers
    make check-headers -> headers: 110 compile alone

CPU, ARMv5 instructions a second (qemu -d exec, a scratch bench: 450
transmitter alone, then with both receivers on its signal): two receivers
each with its own band-pass 5.1 M; with one shared band-pass
(`hst_brx_band`) 3.5 M. `hst_bbpf` alone is about 1.36 M of that (7
instructions a tap). The spec's "about 1 M a second per receiver" is
missed: the lever is a shorter `hst_bbpf` (Task 11's generator), for
`make cost-hst` to weigh against the 15 M of holding forward. (The 450
transmitter is 1.8 M.)

Review fixes (round 1).
- C1, a lock 45 degrees off. When the carrier estimate was made on noise
  (the line's noise before the caller, or a dropout under the carrier
  detect), the frequency loop pulled to a multiple of 45 degrees, and the
  receiver locked there for good: 4-25 % of the reviewer's runs.
  - The receiver now keeps the last 10 symbol and midpoint strobes while
    acquiring and locked.
  - Lock is declared only when the alias check on them prefers the turn in
    use to r +- 45 by 3:2. When it prefers r +- 45 by 2:1, the carrier is
    estimated again from them.
  - While locked, the check runs every 32 symbols. Three checks in a row for
    the same wrong turn are needed to act: one alone fired falsely about once
    in a thousand checks on a right lock.
  - A symbol 9 dB under the mean symbol power gives no bits, and two in a row
    lose the lock. Without that, a 0.1 s dropout at 300 kept the lock and
    gave some 22 wrong bits.
- I1, lock on tones and noise. Lock now needs, over the last 16 decisions:
  - 15 within +-12 degrees of the grid;
  - at least 4 different steps, none in more than 8;
  - an RMS error under 7 degrees;
  - the alias check. `grid_ok` needs the same window conditions.
  - With the 12-decision window and an 8-degree ceiling, white noise still
    locked 1-4 times a minute at 450. At 6 degrees it was 4 in 50 minutes,
    and lock failed at 8 dB. 16 decisions at 7 degrees gave 0 in 50 minutes.
- I2. `grid_ok` is 0 until the carrier estimate has been made, and while
  acquiring again. `hst_brx_tick` gives bits only while locked.
- M3. The acquisition range is in hst.h: about +-28 Hz at 450 and +-18 Hz
  at 300.
- M4. Carrier detect works on the band's power, not on symbol power as the
  spec has it [OURS]. The reason is that it is off within 100 ms: symbol
  power comes through the matched filter's 4-symbol delay, 40 ms at 300.
  The thresholds are -44 dBm0 on and -47 dBm0 off.

New cases in `test_hst_back` (97 checks):
- `noise_first`: 1 s of noise first, at 20, 15 and 10 dB; 8 seeds x +7, -7,
  +3 Hz per speed.
- `dropout`: the signal off for 0.1, 0.3 and 1 s.
- `tones`: US dial tone, mains hum and a 375 Hz tone, at -30, -20 and
  -10 dBm0, 30 s each.
- `noise`: white noise at -31 to -10 dBm0, 60 s each.
- `band`: a 300 receiver on a 450 receiver's band-pass gives the same bits
  and lock as one on its own.
- `cd_level`: on at -41 dBm0, held at -45.5, off at -50, and never on at
  -45.5 or -50 from the start.

Figures:

    noise_first, dropout: 0 of 72 lines bad at each speed (locked at the end,
      carrier within 1 Hz, 1500 bits right after the last lock or the dropout);
      the reviewer's probe set (8 seeds x 3 offsets x 16 conditions): 0 of 384,
      no wrong bit even across the dropouts
    tones: no lock, no bits, no 24-of-30 grid_ok window (dial tone at -20 dBm0
      gives a few lone grid_ok decisions)
    noise: 0 false locks a minute (10 minutes a speed; 50 minutes in the probe)
    lock time: worst 235 ms at 450, 405 ms at 300 (lock_time); 800-line stress
      252 / 369 ms flat, 245 / 348 ms mu-law, carrier within 0.13 Hz
    lock floor: reliable down to 8 dB over 0-4 kHz (about 21 dB in band);
      at 6-7 dB some lines do not lock within 2 s
    CPU, two receivers on one band-pass, ARMv5: 3.68 M a second (3.50 M before)

For Task 13 (set by the review):
- `make cost-hst-back`: the two receivers on one band-pass at most 2.0 M
  ARMv5 instructions a second. The lever is a decimation filter in place of
  the 243-tap `hst_bbpf`: about 45 taps, 60 dB down at 1124-1326,
  1874-2076, 2724-2926 and 3474-3676 Hz. `hst_hbpf` wants the same.
- An echo test of our own forward signal ("holding forward"), since the
  500-1100 Hz band then rests on the matched filter's selectivity.

Review fixes (round 2).
- N1, a lock 45 degrees off giving bits. The 3:2 alias check at lock let
  about 0.1 % of hard starts through, mostly after a 1 s dropout at 12 dB,
  and some 288 wrong bits went out before the in-lock checks caught it.
  - The check at lock is now 2:1.
  - A second check on a fresh window 10 symbols later, at 3:2, confirms the
    lock or ends it. A preference for r +- 45 means estimating again.
  - While locked, the bits are held back 19 symbols at 450 and 18 at 300,
    so no bit leaves before the second check. A confirmation at 1:1 let one
    alias through in the review's set (300, +12 Hz, seed 28).
  - An unclear second check costs a lock won again, never a bit.
  - hst.h no longer says such a lock "is never declared". It says no bit
    is given from one.
- N2, the hold floor. Holding a lock uses an RMS ceiling of 11 degrees
  (HST_BRX_RMS_HOLD); winning one still uses 7.
  - Locked at 25 dB, then the noise raised: 0 losses of lock at 8, 7, 6
    and 5 dB (10 seeds x 18 s a speed).
  - Over 4 x 58 s: at 450, BER 1.4e-4 at 6 dB and 5e-4 at 5 dB; at 4 dB,
    4 losses and BER 1.9e-3. At 300, no errors to 6 dB and 2.5e-4 at 4 dB.
  - In round 1 it was about 3 losses a minute at 8 dB.
- N3, the fade gate. A fade loses the lock only after about 75 ms in a row
  (11 symbols at 450, 8 at 300; it was 2 symbols).
  - The bits held back (above) let a shorter fade keep the lock and give
    its bits: no gap in the stream. That also settles the review's M-b.
  - A fade that loses the lock takes its own bits with it, and those of the
    8 symbols before it, over which the matched filter ramps down.
  - Faded symbols move neither the loops nor the grid history.
  - Carrier detect goes off after 6 blocks under -47 dBm0, not 3, so a lost
    50 ms packet keeps the carrier. It is still off within 100 ms.
  - 20/30/50 ms dropouts: the lock held, at most 13-25 wrong bits a line.
  - 0.1 s: the lock lost and won again, not one wrong bit given.
  - Level steps of -10, -12 and -15 dB: 0 losses (-15 dB at 300: 1 of 10).
- Minors:
  - The range at 300 is written as reliable over +-12 Hz, with what
    happens at +-15 Hz.
  - A direct test of I2.
  - The round-1 figure "0 false locks in 50 minutes" held only for my seeds.
    The review's set found 1 false lock in 40 minutes at 450 and 3 at 300 on
    noise alone, and 15 in 240 minutes with hum or dial tone in noise.
    Now 6 runs in 1910 raise `locked` for 0.07-0.1 s, and none gives a bit.

The review's sets, rebuilt on this code (build/tmp/rv12b; p5.c is p4.c,
which places an epoch shorter than 12 bits by time from an aligned one
instead of counting it all wrong):

    7800 hard starts (13 offsets x 30 seeds x 10 conditions x 2 speeds):
      no epoch from an alias anywhere; no run with more than 9 wrong bits;
      450: no wrong bit at all within +-12 Hz;
      300: within +-12 Hz, one run at 12 dB with 3 wrong first bits after
      the lock, an ordinary error with one decision of 16 off the grid; at
      +-15 Hz (beyond the reliable range) 34 such runs and 33 not locked
      at 10 s
    1910 tone and noise runs of 60 s: 0 bits given; 0 windows of 24 grid_ok
      in 30; `locked` raised for 0.07-0.1 s in 6 (noise, or tone and noise)
    lock time: worst 252 ms (450) and 369 ms (300) over 800 lines

New cases in `test_hst_back` (113 checks; 20 s on the host, 2.5 s short):
- `alias`: 112 hard starts a speed at 12 dB, -10 to +10 Hz, noise first
  or a 1 s dropout. Each must end locked on the right carrier, with not one
  wrong bit in all the bits given.
- `fades`: 20, 30 and 50 ms dropouts; and 0.1 s.
- `hold`: 8 dB and 6 dB for 60 s a speed.
- `grid_ok`: 0 before the estimate, and until the 16-decision window is
  full; 1 after that.
- `loop_*`'s third bound counts losses of lock once bits have come. Before
  that, the lock's second check may end a lock, which is not a loss.

CPU (ARMv5, two receivers on one band-pass): 3.70 M a second.

## hst 13: the detector, tools/hst_scan, the receivers' CPU; milestone 3 (2026-11-25)

First commit, the Task 12 leftovers (rulings carried from its review):
- I-A. The second alias check now sums the mismatches of two fresh
  windows of 10 symbols after the lock and confirms when the turn in use
  predicts better than r +- 45 (1:1 on the pool); one window alone was
  unclear about a right lock now and then, and dropping it made about 5 %
  of the clean starts at 300 late. The lock's own window is not pooled
  (pooling it let the review's 300 +12 Hz seed 28 alias through). The
  hold is HST_BRX_DLY = 21 symbols at both speeds (140 ms at 450, 210 at
  300), past both windows.
- `first_bit` (test_hst_back case 29): 256 clean starts at 300, +-7 Hz,
  +-100 ppm, 20 and 30 dB: the lock giving the first bit (first bit less
  the hold) within 450 ms on every one. The `loop_*` bound is strict
  again (no loss of lock after the lock).
- hst.h: a carrier stop discards the whole held ring (some 10-15 real
  symbols: 42 line bits at 450, 31 at 300 measured); Task 21 owns the fix.
  The carrier-off margin is 10 ms (80-90 ms against 100).

Second commit, Task 13:
- `hst_bbpf` is a decimation filter: 39 taps (cut-off 740 Hz, Kaiser beta
  5.8), at least 60 dB down only where it folds into 274-476 Hz at
  1600 Hz (1124-1326, 1874-2076, 2724-2926, 3474-3676 Hz; 63.9 dB),
  flat to 0.47 dB over 250-500 Hz. The rest of the selectivity is the
  matched filter's. `hst_mf450`/`hst_mf300`: the matched filter as one row
  of taps a strobe phase (a dot product; the same numbers as before).
- CPU work, outputs unchanged by each step (test_hst_back byte-identical):
  the matched filter's rows, a 32-bit mixer, one sincos, the window's
  sums kept running, the decisions' tribits from a table, the divisions
  by the symbol power in 32 bits (shifted to 12 bits), a 32-bit square
  root for the AGC, the NCO drain as t x 3/32 and t/16.
- Lock and grid_ok also need the symbols' power steady over the window
  (variance at most 1/6 of the squared mean). The detector, as first
  written, fired on two tones that beat (330 + 401 Hz, 350 + 421 Hz: 2 of
  12000 random tone mixtures); real 8-DPSK spreads at most 0.13 (6-30 dB,
  +-12 Hz), those two tones 0.24 and more.
- `hst_det` (hst.h): both stages over 100 ms (ten 10 ms blocks): the band
  (`hst_dbpf`, 43 taps at 1600 Hz on the receivers' band-pass) over
  -45 dBm0 [OURS: the spec's -43 is the weakest to be found, 2 dB margin]
  and 10 dB over the high band (`hst_hbpf` at every 3rd and 5th sample in
  turn), and 24 of the last 30 grid_ok on the 450 (or else the 300)
  receiver. The receivers carry on after it fires.
- `tools/hst_scan.c` (`make hst-scan RECS=`), `rig/bench_hst.c`
  (`make cost-hst-back`).
- test_hst_back: `filters` for the new tables; `echo` (case 30): the back
  channel at -33 dBm0 under our forward signal's echo, the library's V.32
  transmitter standing in (2400 baud at 1800 Hz; the HST forward
  transmitter is not on this branch): in HST's forward band (a test-side
  high-pass at 550 Hz with a -40 dB floor, for the beta 1/12 pulse) from
  -10 dBm0 at 6 dB ERL, locked and error-free over 2 x 10^4 bits; V.32's
  own 25 % pulse reaches into the back band, and its limit is an echo of
  -30 dBm0, 3 dB over the back channel (-28: errors on 3 lines of 24; at
  the corner the 450 receiver does not lock). `noise_first`/`dropout`:
  at most one decision (3 bits) wrong after the lock, not none: with the
  39-tap band-pass, 1 line in 360 at 40 seeds had one wrong decision in
  the first ones after the lock at 10 dB (the 243-tap: 0 in 360); the
  7800 hard starts give one such run either way; an alias gives half.
- test_hst_det (new): pos_450, pos_300 (-9, -26, -43 dBm0, 20 dB, +-7 Hz,
  +-100 ppm), quiet, mixtures, v22bis.

    make -s build/test_hst_det (before the code) -> error: unknown type name 'hst_det_t'
    build/test_hst_det -> test_hst_det: 7 checks, 0 failures
      detection: 356-416 ms at 450 (budget 450), 526-576 ms at 300 (650)
    build/test_hst_back -> test_hst_back: 120 checks, 0 failures
    make hst-scan RECS=$RECS -> 106 recordings, 0 back-channel detections;
      the closest any came: 0 grid_ok in any window (before the steady
      power rule: 17 of 30 at 300 on a 462 Hz tone, 10 at 450)
    12000 random mixtures of 1-3 tones (150-650 Hz, -45 to -5 dBm0, AM,
      mu-law, noise): no grid_ok; 1910 tone and noise runs: 0 locks
    7800 hard starts: no epoch from an alias; within +-12 Hz one run with
      3 wrong bits after the lock; 3500 clean starts, first bit less the
      hold: 300 +-7 Hz 0 late at 10-30 dB, 300 +-12 Hz 10-21 % late at
      10-20 dB (was 39-59 %)
    make cost-hst-back ARM_CC=$A5 (third second, the bench's loop of
      0.11 M taken off): two receivers on one band-pass 1.87 M (caller at
      450), 1.86 M (300); 1.98 M with the loop. Task 12: 3.70 M.
      Detector 3.68 M, of which hst_hbpf 1.26 M (a profile).
    sizeof(hst_brx_t) 1824, sizeof(hst_det_t) 4504 (x86-64 and ARMv5)
    make verify -> VERIFY PASS (see the commit)
    milestone 3: verify-phase deferred to the merge of the tracks (a port
      matrix was running in ~/hst-wt; owner's controller ruling)

Review fixes (round 1).
- I1: the steady-power rule voided grid_ok for a whole window after any
  dip before the lock (a lost VoIP packet), and a 3 dB fade at 5-10 Hz
  blocked detection outright. Now:
  - the rule measures the symbol-to-symbol change of the power (the mean
    squared change at most a quarter of the squared mean), not its spread
    over the window: a slow fade changes little from one symbol to the
    next, two tones that beat change a lot;
  - symbols 9 dB under the mean are left out of the power's sums before
    the lock as after it (12 of 16 needed);
  - through a dropout the symbol power's mean holds (8 symbols at most)
    and so does the AGC's gain (6 blocks at most): the symbols after the
    dropout were measured against a level it had pulled down (the first
    came out 6 times the mean).
  - On 216 positive lines (both speeds, +-7 Hz, dropouts of 20 and 40 ms
    at 100-400 ms, fades of 2 and 3 dB at 0.5-10 Hz, -43 dBm0) the rule
    now costs nothing: the same 15 lines late with it as without it, all
    dropouts in the acquisition (below). The reviewer's 3978 tone pairs: 0
    of 30 grid_ok at most (a quarter; at a half, 3; at 1, 17), 0 fired;
    the 72 modem signals: 3 of 30 at most, 0 fired; 12000 random mixtures:
    1 of 30, 0 fired; the recordings: 2 of 30, 0 fired (make hst-scan:
    106 recordings, 0 detections); 1910 tone and noise runs: 0 locks;
    7800 hard starts: no alias epoch, and now no wrong bit within +-12 Hz;
    3500 clean starts: unchanged.
  - Not fixed, and not the rule's (the same with it off): a dropout
    during the acquisition (140-180 ms after the preamble's start at
    either speed, about 40 ms at 450, where it lands on the timing's
    estimate) costs a second acquisition, and one while the detector's
    window fills after the lock (400 ms at 300) a refill of the window:
    detection then past the budget on some lines (up to 816 and 876 ms).
    Re-acquiring at once at a dropout's end was tried and is worse (every
    such line late).
  - test_hst_det (11 checks): impaired_450 and impaired_300, a 20 and a
    40 ms dropout early in the preamble (60 ms at 450, 100 ms at 300) and
    a +-3 dB fade at 5 and 10 Hz, within 450 / 650 ms. The fade case fails
    at 91a765f; the early dropouts pass there too.
- Threshold (controller's ruling): -45 dBm0 stays. It never binds: the
  lock stage needs the receivers' carrier detect, on at -44 dBm0, so the
  floor is -44, 1 dB under the spec's -43 (hst.h says so; the review's
  "2 dB margin" wording is gone, and test_hst_det's header says -45).
- M3: docs/limits.md carries the worst case: echo or noise holding the
  carrier on with no back channel, 1.90-1.91 M net (2.02 M with the
  bench's loop); locked 1.89 / 1.88 M (2.00 M).
- M4: the two unused 8000 Hz inputs of hst_det_t's receivers (316
  octets) stay: removing them means splitting hst_brx_t's front end out of
  it, an interface change for the merge (sizeof(hst_det_t) 4504 against
  v92d_t's).
- M5: test_hst_back's echo comment says the high-passed V.32 stand-in is
  slightly optimistic (0 errors at 0 dB ERL where hst-c's forward
  transmitter gives 17 in 160000): Task 19 asserts with the real one.
- M1, for Tasks 18/19: a 300 Hz line-edge high-pass (2nd order Q 1.2 at
  350 Hz, or 4th order at 300 Hz) then +7 Hz makes 300 detect at 836-856
  ms, past W, with the rule or without it (-7 Hz: 566-576 ms); 450 at
  436-446 ms, 4 ms inside its budget. A real-line profile should carry it.

## hst 14: the forward pulse, v32tx's pulse pointer, guard filter (2026-11-25)

`v32tx_t` gains `const int16_t *pulse; int pulse_len;` (v32tx_init: `v32_txh`,
`V32_TXL`) and `v32tx_set_pulse()`. The symbol history grows to
`V32_TXL_MAX + V32_TXD` (`V32_TXL_MAX` 30 = `HST_FL`, a compile-time check in
`modem/hst.h`); the shift and the line filter are bounded by the pulse in use,
the echo canceller's reference keeps V.32's pulse. `sizeof(v32tx_t)` 112 -> 160
on x86-64 (148 on ARMv5); `sizeof(v32_t)` 36472 -> 36520; `answer_t` and
`v90m_mem_size()` unchanged (106320 x86-64, 106016 ARMv5: test_api on both).

`tools/gen_hst_tabs.py` (extended: `tab_fpulse`, `tab_guard`; `design()` takes
the transition width that starts its search, default unchanged, so the
earlier tables are byte-identical):
- `hst_fpulse[10][HST_FL][2]`: RRC beta 1/12, Kaiser window, v32_txh's layout,
  scaled to V.32's line RMS (`v32_tx_rms_q12` 17072, so v32tx's gain holds).
  The shortest length meeting the firmware's points with margin is 30 symbols
  (window beta 1.75): +-1100 Hz -0.13..0.03 dB, +-1200 Hz -3.19 dB, <= -45.7 dB
  1400 Hz off and beyond. At 24 symbols the window pulls the -3 dB points to
  -3.5 dB (beta 2) or loses the -40 dB (beta <= 1.5).
- `hst_guard`: 407 taps, Kaiser high-pass, cut-off 530 Hz: >= 560 Hz within
  0.02 dB, <= -51.4 dB over 0-500 Hz. About 3.3 M MAC/s at 8000 Hz (half with
  the symmetry): a candidate for the "cheaper guard filter" lever.

`test_hst_fwd` (new; its golden file is new): `pulse_points` measures the
pulse from 10 s of random data with each Welch frame's power divided by that
of the symbols sent in it (a plain Welch estimate wandered +-0.6 dB from data
randomness alone); `guard`; `v32_default` (FNV-1a of 10 000 samples and
references through every transmit mode, recorded from a44426a's v32/).

    make -s build/test_hst_fwd (before the code) -> error: 'v32tx_t' has no member named 'pulse' ...
    build/test_hst_fwd -> test_hst_fwd: 16 checks, 0 failures
      pulse_points: 30 symbols; 700-2900 Hz -0.13 to 0.02 dB, 600 Hz -3.19, 3000 Hz -3.19,
                    >= 3200 Hz -45.6, <= 400 Hz -45.9; level 3036 (V.32's 3036)
      guard: 407 taps, >= 560 Hz -0.02 to 0.02 dB, 250-500 Hz at most -51.4 dB
      v32_default: line 0x62AA7B75, reference 0x3C258E9C
    api_test_api (x86-64, ARMv5 under qemu) -> 0 failures, pins unchanged
    make verify -> VERIFY PASS (1104 s; golden 149 identical, test_hst_fwd's new;
                   bench hashes 17 identical; replay 636 identical; armv7hf and
                   big-endian short-cmp identical; ubsan pass; fixed 0 float helpers)

### hst 14, review fixes (2026-11-25)

- `test_hst_fwd` prints only integers (counts, levels, hashes) and one
  pass/fail line per bound; the measured dB values only with `HST_VERBOSE`.
  Its golden file is re-recorded (still this program's own).
- The guard takes the spec's edges: >= 50 dB at 480 Hz and below (not 500),
  cut-off 520 Hz: 307 taps (was 407), >= 560 Hz within 0.02 dB, <= 480 Hz
  at most -52.7 dB. `hst_gf_tick` (new `modem/hst_fwd.c`) runs it on a
  doubled delay line with the symmetry, mirrored samples added first: 154
  multiplies a sample, 1.23 M MAC/s at 8000 Hz (was 3.3 M direct at 407
  taps). 64-bit sum (sum |h| is 92938, over 2^16), rounded to nearest,
  saturated; the test checks it against the direct form on 32 000 samples.
- `v32_default` hashes both roles (calling: line 0x93EA3185, reference
  0xC5E4CCCC, recorded from a44426a's v32/ with gcc and clang) and runs
  30 -> 12 -> 30 symbols mid-stream (the history past 18 cleared; without
  the clearing both checks fail).
- `tools/gen_hst_tabs.py` writes only when run as a program.
- The spec's "about 24 symbols" now says 30 and why.
- The pulse's cost against 24 symbols: 12 MACs a sample (96 k MAC/s), not
  60 (the first report counted all 10 phases).

CPU levers for later (the CPU task; neither changes V.32):
- v32tx computes V.32's 12-tap echo canceller reference for HST too, which
  has no canceller: 192 k MAC/s.
- V32_TXD's 6-symbol lag between the reference and the line (2.5 ms of
  transmit delay) serves no purpose in HST.

    build/test_hst_fwd -> test_hst_fwd: 23 checks, 0 failures
    make verify -> VERIFY PASS (640 s; golden 149 identical, test_hst_fwd's
                   re-recorded (its own); bench 17, replay 636 identical;
                   armv7hf and big-endian short-cmp identical; ubsan pass;
                   fixed 110 objects, 0 float helpers)

## hst 15: the forward transmitter and its provisional training (2026-11-25)

`hst_ftx_t` (`modem/hst.h`, `modem/hst_fwd.c`): v32tx on `hst_fpulse`
(`v32tx_set_pulse` after `v32tx_init`), `answer` 0 so that v32tx's own
scrambler is GPC (ruling F7: the polynomial is why, recorded in
hst_fwd.c); data through `v32_encode` at rate index code - 2.
`hst_ftx_train(t, code, long_form)` sends S1 180 (V.32's S), S2 72 (S-bar),
S3 3096 or 200, then data: 3348 symbols (1.395 s) or 452 (188 ms). S3 is
ones through GPC from zero, Table 4's dibits on all 4 points from its first
symbol (V.32's TRN starts with 256 on A and C). The constants are commented
"provisional: Task 20 / R1"; `HST_OPEN_CODE` 4. `HST_FTX_DELAY` is 70 samples
(a compile-time check; V32_TX_DELAY's 40 is V.32's pulse's).

v32tx gains one mode, `V32_TX_STATE` (send the state in `abcd`), which V.32
never sets: S3's and V.17's conditioning points need a chosen state a
symbol. V.32's output is unchanged (test_hst_fwd's v32_default hashes, the
bench hashes).

`-DHST_PROFILE_BM`: `HST_OPEN_CODE` 3, the long form V.17's (256 AB, 2976
conditioning from GPC state 0x2ECDD5 mapped 00 C, 01 D, 11 A, 10 B, 64
bridge as v32tx's rate sequence 0x0111, 48 ones at the rate: 3344
symbols, then data), `HST_3R_TRAINS` 1. (Segment 4's differential start
took the bridge's first state's 4800 label, V.17's only when that state is
B, which it always is here; the review fixes below make it V.17's for any.) `make check-hst-bm` (in `make check`) builds
`HST_BM_TESTS` (test_hst_fwd) in `$(BUILD)/bm` with it and runs them; their
golden files are `test/golden/out/bm/<program>.txt` (`print-golden` lists
`bm/<program>`).

`sizeof(hst_ftx_t)`: 200 octets on x86-64, 184 on ARMv5 (v32tx_t 160 / 148).
Not yet in `answer_t.u`; `v90m_mem_size()` unchanged.

    make -s build/test_hst_fwd (before the code) -> 36 errors: unknown type
                   name 'hst_ftx_t', 'HST_SEG_DATA', 'HST_S1_SYMS' undeclared ...
    build/test_hst_fwd -> test_hst_fwd: 91 checks, 0 failures
      train_long, code 4: S1 180 S2 72 S3 3096 ...; data from symbol 3348 (1395 ms)
      train_short, code 2..6: S1 180 S2 72 S3 200 ...; data from symbol 452 (188 ms)
      trn: long S3, 3096 symbols: 0 unlike GPC's, 2340 unlike GPA's
    make check-hst-bm -> test_hst_fwd: 96 checks, 0 failures
      bm: long training 256 2976 64 48 ...; data from symbol 3344 (1393 ms)
      bm: unlike: AB 0, first 16 0, conditioning 0, bridge bits 0, ones ... 0
    mutations: answer 1 (GPA) -> trn 4 failures; bridge word 0x0110 -> bm 1 failure
    api_test_api (x86-64; ARMv5 under qemu) -> 1030 / 1020 checks, 0 failures
    make verify -> VERIFY PASS (654 s; golden 150 identical: test_hst_fwd's
                   re-recorded (its own), bm/test_hst_fwd's new; bench 17,
                   replay 636 identical; armv7hf and big-endian short-cmp
                   135 identical; ubsan pass; fixed 110 objects, 0 float helpers)

### hst 15, review fixes (2026-11-25)

- **Ruling (I-1):** under `-DHST_PROFILE_BM` every training is V.17's long
  train: the opening, and a retrain, a reversal, 0x2R and recovery as well
  (`hst_ftx_train` ignores `long_form` when `HST_LONG_V17`): BinModem's
  receiver trains on nothing else, and BinModem itself always sends the
  long form. Recorded in hst_fwd.c. New BM case `bm_short`: a short
  training asked for at every code 2..6, from data, sends the V.17 long
  train. `s1_s2`, `train_short` and `trn` test the HST-shaped training and
  run in the default build only.
- **M-2:** segment 4's differential encoder starts from the bridge's first
  symbol's Y1 Y2 in V.17's labelling (A 11, B 01, C 10, D 00, the same at
  every rate; our 7200 set's; our 9600-14400 sets, V.32bis's, are on
  another scale and do not hold A..D): v32tx is given the state whose 4800
  label is that Y1 Y2 (A -> C, B -> B, C -> D, D -> A); at 4800 the state
  itself. The train is fixed, so that first state is always B (where the old
  start was already right). `bm` now checks: A..D in the 7200 set with those
  labels; segment 4's first Q1 Q2 against the scrambled ones by Table 1 in
  every V.17 training (Table 2 at 4800); and, with the kept state replaced
  after the bridge, all four starts at codes 3..6 (the old start: 12 of 16
  unlike).
- **M-1:** S3's 4 points from its first symbol: the reason is that our
  receiver trains decision-directed on all 4 after AB (hst-facts.md and the
  `K_TRN` comment corrected).
- **M-4:** hst_fwd.c includes v32_priv.h for `v32_label_abcd` alone; the
  comment says so.
- **M-5:** the bridge's first state is taken once, in `next_symbol` (the
  bridge's second symbol), not tested every sample.
- **Note (M-3):** the BM build (`bm/test_hst_fwd`) belongs in
  `verify-phase` once, before Task 25 ships a BM build (Task 26): no ARM or
  port run builds it yet.
- **Note (M-6):** `hst_ftx_stop` drops straight to silence; BinModem ends a
  burst with V.17's turn-off (32 symbols of scrambled ones, 48 quiet). That
  is for the reversal tasks (21-22) under the BM profile.

    build/test_hst_fwd -> test_hst_fwd: 91 checks, 0 failures
    make check-hst-bm -> test_hst_fwd: 111 checks, 0 failures
      bm short asked, code 2..6: training 256 2976 64 48 ...; segment 4's start 0
      bm: segment 4 from each of A..D at codes 3..6: 0 of 16 unlike V.17's start
    mutation: the old start (the state itself) -> 12 of 16 unlike, 1 failure
    make verify -> VERIFY PASS (931 s; golden 150 identical, bm/test_hst_fwd's
                   re-recorded (its own), test_hst_fwd's unchanged)

## hst 16: the forward receiver: AB acquisition on qamrx, decision-directed training, stored equaliser (2026-11-25)

`hst_frx_t` (`modem/hst.h`, `modem/hst_fwd.c`): `hst_gf_t` (the guard
high-pass, now in the receiver's state), `qamrx_t` (`qamrx_init(r, 2400, 3,
4)`), `v32_vit_t`. `hst_frx_init/set_code/tick/state/store/restore/snr`;
`hst_eqstore_t` (taps and carrier frequency). qamrx is not changed: the
receiver sets its taps, frequency and `running` itself, so V.34's and
V.90/V.92's outputs are untouched (golden, replay, bench identical). The
spec's design held; no fallback to v32rx was needed.

- **AB [OURS]:** single-bin detectors at 600 and 3000 Hz on the guard's
  output (exact phases: both repeat in 40 samples), each at least 1/16 of
  the power, the power at least -43 dBm0, for 40 ms. The symbol timing is
  the turn of 600 x conj(3000) (2400 Hz times a symbol centre's time);
  the carrier offset is the turn of each tone against itself 40 samples
  back (a clock offset turns them oppositely and cancels). Without the
  offset estimate, -7 Hz falsely entered data mode on 3 of 24 seeds.
- **The first equaliser:** the transmit pulse's matched filter, read from
  `hst_fpulse` (the carrier folded in) at T/3, scaled so that A..D come out
  at their size (`MF_GAIN` 29; at 4 they came out at 0.14 of it and the
  normalised carrier and timing errors were 7 times too loud: training
  never converged). Through the transmitter and the receiver as it starts
  (nothing adapted) S3's points are 32.9 dB above their scatter; through
  the test line, 13.9 dB.
- **Training:** decision-directed on A..D with the spec's flags
  (`QRX_EQ | QRX_CARRIER | QRX_TIMING | QRX_TFAST`), step 9 (8 diverges; at
  10 a short S3 left 14400 in error on 14 of 32 seeds), until the error has
  held under -20 dB for 100 symbols, then 12. Two rules the line needed:
  no equaliser update while the last 8 decisions alternate (S and S-bar
  have power at 600, 1800 and 3000 Hz only: adapted on them, the equaliser
  wandered elsewhere and S3 began with a burst of errors), and no update at
  all on a point off the 4 (the first data points, trained against A..D at
  the fast step, wrecked the taps at 14400). The error measure is our own
  (smoothed over 16 symbols from the first): qamrx's starts at 0 dB and
  falls at 1/64 a symbol, which the matched filter has long left behind.
- **Data:** at 3..6 when more than 3 of the last 8 points are farther from
  A..D than 4 times the error's RMS (1.2 to 2.5 units: 50 to 88 % of each
  rate's points, e^-16 of a training point), gated on the error before
  those 8 (under -14 dB, or the hold reached); the decoder is given the
  window from its oldest off point. At code 2 once S and S-bar have been
  over 24 symbols. Before the hold, data mode adapts at step 10 without
  QRX_TFAST; after it, 13.
- **TRN at code 2:** V.32's TRN is Table 4's dibits of scrambled ones,
  mapped directly; through the differential decoder (as 4800 data must be
  read) it gives random bits (393 ones in 758, checked), not ones as the
  spec says. So the receiver reads the 4 points directly under each quarter
  turn as well; while one of them gives TRN (8 symbols of ones, found only
  in training) its bits are given as ones, and the data descrambler is
  loaded from it, so the data after TRN descrambles from its first bit.
- **Stored equaliser:** `hst_frx_store` copies the taps and frequency;
  `hst_frx_restore` returns to listening and the next start uses them in
  place of the matched filter and the AB estimate.

Sizes (x86-64 / ARMv5): `hst_frx_t` 3200 / 3200 (guard 1232, qamrx 1112,
Viterbi 588), `hst_eqstore_t` 584 / 584, `hst_ftx_t` 200 / 184,
`hst_btx_t` 256 / 244, `hst_gf_t` 1232; with track B's two `hst_brx_t`
(1568 each) the HST state so far is 7376 octets (x86-64), under the 12 KB
target and the 45096 limit. Not yet in `answer_t.u`: `v90m_mem_size()`
unchanged (test_api).

CPU, ARMv5 (qemu `-one-insn-per-tb -d exec,nochain`, a scratch bench
replaying a recorded code-6 call: long training then data): seconds 3 to 4,
14400 data mode, 30.73 M instructions (105528861 - 74800911); training
(seconds 0.2 to 1.6) about 28 M a second. By function (tools/qprof):
`v32_vit_step` 9.35 M, `hst_gf_tick` 8.89 M, qamrx 9.93 M (produce 4.06,
eq_out 3.23, qamrx_update 2.29, qamrx_tick 0.36), the rest of hst_frx 0.7 M,
the bench's file reads 0.9 M: the receiver about 29.8 M. The spec's "holding
back" (receiver + back transmitter 1.8 M + MNP) is to be under 30 M: it is
over by some 2-3 M. Levers: the guard (307 taps, 8.9 M; spec: "a cheaper
guard filter"), the equaliser at T/2, the Viterbi's 128-point search
(v32's, shared with V.32bis). For `make cost-hst` (milestone 4) to settle.

`test_hst_fwd` (golden re-recorded: 91 -> 129 checks; bm 111 -> 123; only
the count lines and the new lines after the old ones differ): `acq_long`,
`acq_short` (nothing stored) at codes 2..6, `acq_offset` (+-7 Hz, +-100
ppm, 14400), `stored_eq` (codes 3 and 6), `trn_as_ones`, `zero_isi` (the
pulse against itself at whole symbols -49.4 dB; through the transmitter and
the receiver's start 32.9 dB, 31.7 on V.17's conditioning); in the BM
build `acq_v17` at codes 2..6 and `zero_isi`. The line: v32line, mu-law, 6
dB, the mild loop { 0.3, 1, -0.5, 0.3 }, 25 ms, noise 30 dB under the
signal. 10^5 data bits a case (BER < 1e-5), 10^4 under TEST_SHORT.

    make -s build/test_hst_fwd (before the code) -> undefined reference to
                   hst_frx_init, hst_frx_tick, hst_frx_state, ...
    with stub functions (never leaving IDLE) -> acq_long 10 checks, 10 failures;
                   trn_as_ones 2 of 2; zero_isi 1 of 2 (the pulse's half passes)
    build/test_hst_fwd -> test_hst_fwd: 129 checks, 0 failures (5 s)
    make check-hst-bm -> test_hst_fwd: 123 checks, 0 failures
    HST_VERBOSE: data mode 1462 ms after a long training began (the data's
                   arrival), 255 ms after a short one; SNR 30-31 dB; with the
                   equaliser restored, tracking before the data came
    seed sweeps (scratch, 10^4 bits): 32 seeds x acq_long, acq_short,
                   acq_offset, stored_eq, trn_as_ones: 0 failures; 24 seeds
                   x acq_v17: 0 failures
    clang -fsanitize=undefined, both profiles -> output identical to gcc's
                   (it found i << 32 with i negative in ab_freq: now a product)
    qemu-arm ARMv5 short set -> 129 checks, 0 failures, identical to the
                   host's -DTEST_FORCE_SHORT (270 s; tools/test-times.txt)
    make verify -> VERIFY PASS (725 s; golden 150 identical, bench 17,
                   replay 636; armv7hf and big-endian short-cmp 135 identical)
    make check-arm ARM_CC=$A5 (ARMv5, short) -> ALL ARM CHECKS PASS

### hst 16, review fixes (2026-11-25)

- **The receiver's own ends (ruling).** It reports them through
  `hst_frx_state` and listens for AB again; what to do about them (retrain,
  wait, end) is Task 23's scheme's.
  - `HST_FRX_NOCARRIER`: the guard's output under -48 dBm0 for 300 ms in
    training or data. A dropout is clamped from 20 ms on, or as soon as
    the power is 10 dB under its slow (0.5 s) mean: no bits, nothing
    adapted. If the power is back (over -45 dBm0, within 10 dB) before
    300 ms, all goes on. The equaliser and frequency are kept for the next
    training.
  - `HST_FRX_UNLOCKED`: in data mode the error over 256 symbols above
    `lock_mse[code]` for 1200 symbols. The thresholds, -5.8, -12.8,
    -15.8, -19.0, -21.9 dB at codes 2..6, are 1 dB under the noise floor
    of the rate's hard decisions (noise alone, decided to the nearest
    point: s^2 / 6 over the set's power, -4.8, -11.8, -14.8, -18.0, -20.9
    dB). A flat -10 dB never fires at 14400, where garbage reads -21 dB.
  - `HST_FRX_TRAINFAIL`: no data mode 3600 symbols after the start.
  - The data-mode gate's `mse0` is now the error 8 symbols back. Taken
    only when none of the last 8 points was off, it was biased low on a
    poor line, and data mode was declared at 6 dB.
- **Gain loop in data mode** (on qamrx's input):
  - A power step is set at once by the root of the power ratio, then
    G_FLUSH symbols unmeasured. It counts as a step when the points' power
    over 32 symbols is a quarter or more off, or over 128 symbols a sixth
    or more off, for 64 symbols, within 10 dB.
  - While the power is a quarter off over 32 symbols, or 30 % off over 8,
    or during the flush, nothing else adapts.
  - Otherwise the gain tracks the power, at 2^-8 or 2^-10 a symbol.
  - What the tries showed:
    - Decision-directed loops and an overshooting continuous loop both
      fed wrong decisions to the carrier loop. It was kicked a Hz off
      within 2-3 symbols and took seconds to come back.
    - The 8-symbol power is the quickest decision-free sign; flapping is
      harmless there, it only pauses adaptation.
    - Error-based freezes broke ±7 Hz tracking.
- **qamrx API (ruling), additive:** `qamrx_stop`, `qamrx_get_eq`,
  `qamrx_sync_eq` (`qamrx_sync`, then the taps loaded held to ±QRX_CMAX,
  then the frequency). No existing function changed. hst_frx writes no
  qamrx field and reads only the point (`zr/zi`). The
  taps' bound now holds however often they are kept and loaded again.
- **Spec amendment:** "Forward channel: receiver" step 4, the TRN-at-4800
  sentence replaced with the reviewer's wording.
- **Smaller fixes:**
  - `train_reset` shared by init and start.
  - `hst_frx_set_code`'s lost `V32_VDEPTH` symbols documented.
  - `hst_frx_snr` zeroed by `hst_frx_restore`.
  - AB needs both tones together at 1/4 of the power as well: 14400
    data back after a cut once passed for AB on 1/16 each.

`test_hst_fwd` (golden re-recorded: 129 -> 162 checks, the old lines
unchanged; bm unchanged at 123), new cases:

| case | what it checks |
|---|---|
| `carrier_loss` | codes 2, 4, 6: NOCARRIER within 500 ms of the caller's stop, no bits from 200 ms on |
| `carrier_return` | NOCARRIER after a 1 s cut. The data back without a training: still NOCARRIER, no bits. A short training then starts from the kept equaliser: trained before the data, no error from its 41st symbol |
| `dropout` | 100 ms, codes 2, 4, 6: data mode kept, no error from 4000 bits on; the last errors 320-927 bits after |
| `train_fail` | 15 dB, codes 3 and 4: not in training 2.2 s on. 6 dB: TRAINFAIL, no bits |
| `level_step` | -1 / +3 dB at 14400, -2 / +6 dB at 9600: data mode kept, no error from 16000 bits on; the last errors 5442 / 61 / none / 41 bits after |
| `lock_loss` | the line's carrier 40 Hz off at 14400: UNLOCKED in 611 ms, no bits |

    seed sweep (scratch, 24 seeds, 10^4 bits): acq_*, offsets, stored_eq,
                   trn_as_ones, carrier_*, dropout, train_fail, lock_loss:
                   0 failures; level_step: 92 of 96 runs clean, 4 end
                   UNLOCKED (the carrier loop kicked before the step is seen)
    clang -fsanitize=undefined, both profiles -> identical to gcc's

CPU, ARMv5, the same bench and recording (14400 data, seconds 3 to 4):
30.74 M instructions; the receiver alone about 29.8 M (the bench's reads
0.9 M), as before the fixes. By function: `hst_gf_tick` 7.66 M (8.89 before:
the four-pair unroll, output identical; a two-pointer walk measured worse,
7.96), `v32_vit_step` 9.35, qamrx 9.77, the rest of hst_frx (now the
carrier detect, the gain loop and the lock measure) 1.19 + 0.56 of 64-bit
division (0.64 before the gain loop's step became a constant division).
**The CPU task's target (milestone 4, `make cost-hst`): the forward
receiver alone ≤ 26 M**, so that holding back (with the back transmitter's
1.8 M and MNP) is under 30 M. The levers: a cheaper guard (the delayed
input less a decimated low-pass, or a shorter one with the matched
filter's own rejection counted), and an optional T/2 mode of qamrx as a new
init option (V.34 untouched). The Viterbi decoder stays as it is (V.32bis's).

    build/test_hst_fwd -> test_hst_fwd: 162 checks, 0 failures (5.5 s)
    make check-hst-bm -> test_hst_fwd: 123 checks, 0 failures (unchanged)
    qemu-arm ARMv5 short set -> 162 checks, 0 failures, identical to the
                   host's -DTEST_FORCE_SHORT (381 s; tools/test-times.txt)
    make verify -> VERIFY PASS (534 s; golden 150 identical, test_hst_fwd's
                   re-recorded (its own); replay 636 and bench 17 identical:
                   V.34, V.90/V.92 and k56d unchanged by the qamrx additions;
                   armv7hf and big-endian short-cmp 135 identical)

### hst 16, review fixes, second round (2026-11-25)

- **Carrier loss against the data's level.** A loss went unseen when the
  line's noise, left after the carrier, stayed above -48 dBm0: the
  receiver stayed in data mode at full rate (30 and 25 dB lines), or saw
  it only after 6-10 s (20 dB, -40 dBm0 of noise). The relative clamp
  compared with a mean that decayed (2^-12 a sample), so it let go after
  some 2 s; and the gain loop's freeze, which a power more than 10 dB
  down never ends, kept the lock count at 0.
  - Low is now under -48 dBm0, or 10 dB under the signal's level `pref`.
    That level is taken at the start, follows the signal only while the
    receiver is well and within 3 dB of it, and is set again with a level
    step's gain: held, not decaying.
  - Low for HST_FRX_LOSS_MS (now 200 ms) is NOCARRIER.
  - Once counting, the count goes on until the power is back (over -45
    dBm0 and within 9 dB of the level). A level between the two no longer
    clamps for ever (the dead band).
  - The lock count runs through the gain loop's freezes as well.
- **The clock through clamps and freezes.** The clamp, the gain loop's
  freezes and training's off-grid points called qamrx_update with no
  flags, and with QRX_TIMING off qamrx's clock integrator is not applied.
  A 100 ms dropout at 9600 on the harsh line at -7 Hz lost lock on 3 of
  12 seeds. They now pass the point as its own decision with QRX_TIMING
  alone (`clock_only`): a zero error, the integrator still on (the
  reviewer's variant). The harsh line: 12 of 12 recovered, 0 errors
  (`harsh_dropout`).
- **The data gate's `mse0`**: the error 8 symbols back, but taken only
  while none of the last 8 points is off, then held. Round 1's "8 back"
  rose with data's first points and shut the gate before 4 were counted:
  on a 20 dB line, where the error sits near -19 dB and the hold never
  comes, 9600's long training ended in TRAINFAIL on 2 of 24 seeds. The
  error taken from before the window does not overlap it: not the
  selection bias that declared data mode at 6 dB.
- **Gain loop division.** `g d / 2pt` is computed as `(g d / 2^21) /
  set_pw[code]`, the second a 32-bit division by a constant (two
  truncating divisions by positive numbers are one, so the result is
  exactly the 64-bit division's). There is no 64-bit division by a
  variable.
- **Names.** "The lock thresholds" (-5.8 ... -21.9 dB, `lock_mse`) and
  "the noise floor of the rate's hard decisions" (-4.8 ... -20.9 dB) are
  named so in hst.h, hst_fwd.c and here.
- **Ruling:** no fourth qamrx function. UNLOCKED, then the scheme's
  retrain, is acceptable for the level steps that do not recover.

`test_hst_fwd` (golden re-recorded: 162 -> 187 checks, the old lines
unchanged; bm unchanged), new cases:

| case | what it checks |
|---|---|
| `carrier_stop_noisy` | the caller stops, the noise left above -48 dBm0 (30 and 25 dB lines with no loss, a 20 dB line, -40 dBm0 of noise; codes 2, 4, 6): NOCARRIER at 275 ms on every one, no bits from 200 ms |
| `harsh_dropout` | 12 runs recovered, 0 errors |

    seed sweep (scratch, 24 seeds, 10^4 bits): acq_*, offsets, stored_eq,
                   trn_as_ones, carrier_*, train_fail, lock_loss,
                   carrier_stop_noisy, harsh_dropout: 0 failures;
                   level_step: 91 of 96 runs clean, 5 end UNLOCKED (-1 dB
                   at 14400 twice, +3 dB at 14400 once, +6 dB at 9600
                   twice); dropout (mild line): 2 of 24 slow at 14400
                   (errors past 4000 bits, data mode kept); on 72 more
                   seeds 3 slow, the same 3 of 72 with round 1's receiver:
                   not new
    build/test_hst_fwd -> test_hst_fwd: 187 checks, 0 failures
    make check-hst-bm -> test_hst_fwd: 123 checks, 0 failures (unchanged)
    clang -fsanitize=undefined, both profiles -> identical to gcc's
    qemu-arm ARMv5 short set -> 187 checks, 0 failures, identical to the
                   host's -DTEST_FORCE_SHORT (498 s; tools/test-times.txt)
    CPU, ARMv5, the bench as before (14400 data): 30.67 M; the receiver
                   alone about 29.7 M; no 64-bit division in the profile
    make verify -> VERIFY PASS (862 s; golden 150 identical, test_hst_fwd's
                   re-recorded (its own); replay 636 and bench 17
                   identical; armv7hf and big-endian short-cmp 135)

## hst 10: R3, MNP-only callers (2026-11-25)

Fallback taken: R3 not yet made. No MNP-only modem can reach the local
nsdiald test build, and the build itself waits on a v90d branch (`dce/dce.c`
carries the endpoint glue). Recorded under `Unsettled` in hst-facts.md;
`MNP_FCS2` and T400 unchanged. `test/golden/hst-recordings.sha256` is not
created until a recording exists.

## hst 17: sim/hstcall, an HST caller written from the facts; coexistence of the two channels (2026-11-25)

`sim/hstcall.[ch]` (in `SIM_SRC`), `test/test_hstcall.c` (in `TESTS`,
`SPLIT_TESTS` and `HST_BM_TESTS`). A calling peer in double, written from
`docs/history/hst-facts.md` and including nothing of `modem/` (A23): its own
back transmitter and receiver, forward transmitter and receiver. It shares
with the tree only what `sim/v32call` shares: `sim/v32spec.h` (V.32bis's
sets and Tables 1 and 2, hand-transcribed); MNP is `sim/mnppeer`
(originator, 0xC0; T401 3000 ms and 15 tries, BinModem's HST session [BM]).

- **Call:** the 2100 Hz tone in 10 ms blocks (present 400 ms, absent 30 ms;
  a shorter burst counts again) [BM]; then the back preamble (60 symbols of
  scrambled ones [BM]) at `start_bps` (default 450, ruling F11) and the
  forward receiver together; the LR straight after the preamble. Opening
  code 4 (3 with `bm_training`), the best agreed code from the answerer's
  next training; 300 after the confirming LA plus 250 ms [BM]. 0x1R by
  backlog (`rev_backlog`, after 2 s), forced 0x2R/0x3R, LD, carrier drop;
  as the forward holder it takes 0x1R (its LNA gone + 20 ms), 0x2R (short
  training), 0x3R (marks, or with `bm_training` a training).
- **Back channel:** RRC 0.35 +-4 symbols, Hann taper; the receiver mixes,
  low-passes (161 taps) to 1600 Hz, takes the carrier's offset from the
  lag-one correlation over 100 ms, then Gardner timing and a decision
  frequency loop.
- **Forward transmitter:** RRC 1/12 +-15 symbols (Kaiser 3); our
  provisional training or V.17's long train; marks for 0x3R.
- **Forward receiver:** linear-phase guard (241 taps), AB on 600/3000 Hz,
  timing from their phases, matched pulse, 32-tap T/2 LMS, DD carrier loop,
  Viterbi. What the line needed: a timing loop on the error's gradient
  (second order; the taps' centre of energy alone did not follow even 30
  ppm: the LMS bends the taps for a sub-tap shift without moving their
  energy, and the SNR fell to the 19 dB that hard decisions give on
  garbage); data detection only after 48 symbols of the random 4 points
  with the error from before them under -14 dB (AB and CD train the
  equaliser at two frequencies only: TRN's first points then look off the
  grid); retraining on 16 alternating symbols in data (our post-agreement
  and 0x2R trainings arrive with no gap).

Results (host, `HST_VERBOSE=1 build/test_hstcall`): back channel to
hst_brx and from hst_btx, 450 and 300, 20 dB, 0 errors in 10^5 (2 x 10^4 at
+-7 Hz +-100 ppm), SNR 24 to 27 dB; our long training at codes 2..6 to
hstcall's receiver, 0 errors in 10^5, SNR about 30 dB at 30 dB, also at
+-7 Hz +-100 ppm and under `HST_PROFILE_BM` (V.17's long train); hstcall's
trainings (long 2..6, short at 6, V.17 at 3 and 6) to hst_frx, 0 errors;
marks 6 -> 5 and 4 -> 6 between hstcall's own ends; a call through MNP to a
test answerer of our pieces, at 450 and 300, 4096 and 300 octets intact;
a reversal by backlog, 1024 and 3000 octets intact, `reversals` 1.

Coexistence (forward 14400 at -10 dBm0, back at -33 dBm0 at our side, 6 dB
ERL both ends, -54 dBm0 noise): on a linear line neither receiver loses
anything (back 25.00 against 24.97 dB at 450, 26.07 against 26.17 at 300;
forward within 0.02 dB): margin about 1 dB against the 1 dB bound. With
G.711 at our side hstcall's forward receiver still loses nothing, but our
back receiver loses 0.5 to 1.5 dB (8 lines each: 450 mean 1.11 dB, max
1.54; 300 mean 0.78, max 1.06): G.711's quantising noise on our -16 dBm0
echo, which is coded with the back channel. The test bounds that case at 2
dB and the linear one at 1 dB; the ruling is the controller's (task 17's
report).

Sizes: `hstcall_t` 165712 octets (x86-64), test side only. Host run 33 s
(the back channel's 10^5 bits through v32line's resamplers), the short set
8 s.

### hst 17, review fixes (2026-11-25)

- **I1, holder 0x2R/0x3R:** taken from the answerer, done in `hc_bit` once
  hstcall's LNA has gone and between frames (the frame in flight finishes
  [PUB, P2:561]): the marks from the next symbol; the training 20 ms after
  the LNA, as our reversal waits [OURS]. Without the gap a receiver that
  starts again on the training loses the LNA's end, still in its Viterbi
  (24 symbols): seen in `holder_rates`, where our side never got the
  0x24's LNA. Task 22 should know it of our hst_frx too.
- **I2, the equaliser across a reversal:** `hc_frx_keep` when hstcall
  leaves the back role. Getting a kept equaliser to work took four more
  fixes in the receiver: adaptation frozen once the power falls 3 dB under
  the signal's level, the equaliser kept then (normalised steps on a fading
  input wrecked the taps, and the loss used to store those: it failed more
  often at 30-33 dB than at 24); the carrier lost also at 10 dB under the
  signal's level (at a 20 dB line the noise is over -48 dBm0); on a kept
  start the carrier's phase from 8 symbols' fourth power once the delay
  line is full, and the timing loop's correction against AB's estimate
  restored; slow steps from a kept start. `stored_eq`: code 6, short
  training, 0 errors in 2 x 10^4 at 24, 27, 30 and 33 dB, 0 and -7 Hz
  -100 ppm, 4 seeds each (32 runs). At 20 dB 14400 errs on every seed
  (27 to 145 per 2 x 10^4), as the long training does (180 in 62000): the
  1e-5 point of 14400 is near 24 dB (BinModem's working 27 less 3), so the
  case runs at 24.
- **Knobs:** `lr_delay_ms`, `force_more[3]`, `ignore_lna`, `ignore_ln`,
  `ld_hold_ms` (default 1000 [BM]), each with a case (`knobs`,
  `forced_twice`, `rev_lost`); `turn_back` and `holder_rates` in the call
  harness, which now runs in steps.
- **Minors:** the confirming LA's end found to the bit (M1); the back
  receiver's matched-pulse table sized to +-4 symbols (M12, hstcall_t
  149472 octets); `marks_self` arms the receiver 36 ms after the marks begin
  (M13); header notes (M6-M8, static only); the HST96 facts row reworded
  (M10); `back_to_ours` and `back_from_ours` split per speed (19 cases).
- Host run 39 s, short set 13 s.

### hst 17, review fixes 2 (2026-11-25)

- **N1:** hstcall's forward receiver had taken over hst_frx's carrier-level
  rule (a level set at AB, followed slowly, loss at 10 dB under it). Its
  own now, from the facts' -48 dBm0 floor and its own reasoning: three
  measures of the guard's power, 5 ms, 50 ms, and the highest 5 ms value
  falling 3 dB a second; a dip (5 ms 3 dB under 50 ms) freezes the loops
  and keeps the equaliser, and a real step down clears it in some 50 ms
  (it used to hold about a second); the carrier is gone at 12 dB under the
  peak, or under -48 dBm0, for 100 ms. `stored_eq` again 0 errors in all
  32 runs (24 to 33 dB, 0 and -7 Hz -100 ppm, 4 seeds); `turn_back` passes.
- `force_more` entries each work alone; `forced_twice` uses request 0,
  `force_more[1]` alone and `force_more[2]`; `ignore_ln` has its own case;
  `rev_lost` also checks that hstcall's forward transmitter never came on.
  The case count was 19 for 17 cases since the first fix round; now 18 for
  18. LD hold 1000 ms by default: BinModem's ATH holds the line that long
  for its LD (`Cleardown`, [BM]).

## hst 18: the HST answering branch: ANS_HST_WAIT, ANS_HST, to data (2026-11-25)

- **Phases:** `ANS_HST_WAIT` and `ANS_HST` (before `ANS_NPHASES`), one
  scheme `ans_hst` (`modem/ans_hst.c`: tick, cleardown, retrain, snr_den),
  `hst_t` in `answer_t.u` with the compile-time check against `v92d_t`. At
  `V8A_RES_NO_CM` with `ANS_MODE_HST` and `ANS_MODE_HST_LINKED`: the wait
  (the mailbox zeroed there, once a call; the detector on what we hear);
  else, or when W runs out, `ans_start_no_cm` (V.22bis, V.21 or the end,
  0.6.0's branch moved into a function). `answer_hst_link` is ON in
  `ANS_HST` only (F4: UNDECIDED through the wait).
- **ANS_HST:** the detector's two receivers carry on as the back
  receivers (`hst_det_rx`: the band-pass and both receivers, without the
  detector's two bands, 1.89 M not 3.70 M); our long training at code 4,
  then 160 samples of scrambled ones before the first data bit (without
  them hstcall's receiver lost our LR every call, which cost a T401, about
  2 s: found by `lr_in_training`, where the caller's LR timer then ran
  out); connected once that is out and a back receiver is locked. The
  locked receiver's bits go into the ring until connected, to putbit from
  then on (the agreed speed's when locked, 450 before agreement); through
  the retrain too. `la_rx` with a rate agreed: the short training at the
  best code of `agreed_rates & offer_rates` as a retrain (STOPPED, MOVING,
  RATES at its end). `offer_rates` from `max_down`, `cap_up_code` from
  `max_up`. Backstop 15 s without `la_rx`: `ANS_END_HST_FAILED`; connected,
  5 s with neither back receiver locked: `ANS_END_LOST` (not the carrier
  detect, which our echo holds on: lost_fwd never ended with it).
  `v90m_retrain`: the short training at the code in force. Our cleardown:
  the transmitter stops, `ANS_END_CLEARDOWN_OURS`. `v90m_stats.reversals`
  from the engine (0). Diagnostics: the wait is the tone phase, ANS_HST
  training then data, selected HST.
- **W:** 700 ms, and on to 1500 ms while the detector's energy stage holds
  (`hst_det_energy`, the Task 13 ruling). `make hst-wsize`
  (`rig/hst_wsize.c`): 24 + 24 level lines, 84 + 84 dropout lines, 4 + 4
  line-edge lines (table in docs/limits.md). Without the extension W 700
  misses 27/84 of the 300 dropout lines and 2/4 of the 450 line-edge ones
  (860 ms); with it none at any W from 400. Step 4 as the plan has it:
  `rates` and `legacy_unchanged` at `-DHST_W_MS=400` pass (the 300-start
  caller is found through the extension); legacy callers then 400 / 370 /
  400 ms late (V.22bis, V.21, Bell 103), at 700: 700 / 670 / 700. Kept 700
  so that a clean 300 caller (24 lines, the latest at 690 ms) does not lean
  on the energy stage holding for 300 ms.
- **N3 (Task 13 review):** `test_hst_det` `late_drop_300`: 40 ms at 300
  ms and 20 ms at 200 ms on the four lines: 2 of 8 past 650 ms (766, 776
  ms at +7 Hz), both with the energy stage held from 650 ms on (what the
  extension waits for). Re-recorded: one case line, one bound, the count.
- **sim/v32line:** `alaw` (F10): the linear line with A-law at the
  answerer's interface both ways, through sim/g711's new A-law codec
  (`alaw_to_linear`, `linear_to_alaw`, from G.711 Table 1a; its decoding
  equals the library's `v90_octet_linear` on all 256 octets); mu-law and
  linear unchanged (their goldens).
- **Tests:** `test/api/test_answer_hst` (11 cases, 362 checks; host 187
  s, the TEST_SHORT set 21 s; `arm/api_test_answer_hst` added to
  tools/ports-slow.txt as `api_test_mnp_call` is); `test_clock` an HST entry (hstcall, an mnppeer responder as the
  endpoint, 14400 / 450, clocks moved on); `test_ans_table` 19 phases.
- **Memory:** `hst_t` 9840 octets x86-64, 9808 ARMv5 (mailbox 224 / 216,
  detector with its two receivers 4664, back transmitter 256 / 244,
  forward transmitter 200 / 184, forward receiver 3880, stored equaliser
  584); the union stays `v92d_t`'s 45096 / 45008; `v90m_mem_size()` 106320
  / 106016 and `v90m_ec_mem_size()` 50448 / 50200 unchanged.

### hst 18, review fixes 1 (2026-11-25)

- **I1 (W's slack):** at 700 / 1500 ms the clean 300 bit/s lines had 10 ms
  of slack under W and the 300 dropout lines 60 ms under the cap, with
  hstcall starting 30-45 ms after our tone; a slower caller would fall to
  V.22bis. `rig/hst_wsize.c` now runs every line again with the caller 50,
  100, 200, 300, 400 and 500 ms later (1568 lines, 220 s; 500 ms is the
  bound: turn-around plus a VoIP round trip, or a host's 500 ms blocks),
  and prints, for W 400..1000 and caps 1500..2000, the lines with under
  100 ms between their detection and the end of the wait that would apply
  to them, and the least such slack. The least pair with none under 100
  ms: **W 650, cap 2000** (least slack 150 ms, the 300 bit/s caller 500 ms
  late found at 1850 ms). W 600 leaves 16 lines short; a cap of 1900, two.
  With a 300 ms bound the pair would be 450 / 1900 (least slack 160): set
  first, it lost `block_size`'s call (a 4000-sample block is 500 ms of
  latency; the caller's band came after the wait had ended), which is what
  moved the bound to 500. `HST_W_MS` 650, `HST_W_MAX_MS` 2000 (the Task
  13 ruling's 1500 raised to the controller's 2000 ceiling: no slack left
  under it beyond 150 ms for a 500 ms-late 300 bit/s caller). Legacy
  callers without V.8 now 650 / 620 / 650 ms later (V.22bis, V.21, Bell
  103), from 700 / 670 / 700. docs/limits.md has both tables; the
  header's V90M_MOD_HST comment says 650 ms and 2 s.
- `test_answer_hst`'s golden re-recorded: its first line prints W (650);
  every other line the same.

### hst, the forward receiver's CPU cut (2026-11-25)

- The forward receiver in 14400 data mode cost **29.90 M instructions/s**
  against a budget of 26 M; it now costs **25.40 M** (`make cost-hst-fwd`,
  `rig/bench_hst.c`). The guard high-pass was one 307-tap filter (154
  multiplies a sample); it is now an interpolated pair: a 75-tap F on samples
  4 apart (`HST_GUARD_L`) and a 29-tap I on its sums, 53 multiplies a sample.
- The guard's flatness is 0.02 dB from 600 Hz up and within +-0.5 dB from
  560 to 600 Hz; the forward band starts at 700 Hz. The stop band has 1 dB
  of margin on the spec's; the other two bounds none (measured +-0.017 dB).
  The extra delay is 9 samples (1.1 ms), far inside every event's slack.
- Only the forward receiver's own goldens changed. Review fixes M1, M2
  (comments); M3 to M5 are wording and are left.

## hst 19: replay -H, hst_scan forward, rtp2rec, rec_hst / bench_hst, cost-hst (milestone 4) (2026-11-25)

- **`v90m-replay -H`:** HST offered and the V.42 endpoint linked (MNP on,
  `v90m_ec_link_hst` after the reset, before the first sample; the rates
  after the reset at CONNECTED and at each change; DATA_STOPPED / MOVING
  passed on). The report ends with `HST:` lines when the modem entered
  the HST wait: the wait and what the detector heard, our forward
  training's segments and codes, the back receivers' locks, connected,
  the rates, MNP's agreed rates and its confirming LA, reversals and the
  end; times in ms, integers. The phase names were Task 18's.
- **The replay proof's seventh set, `hst` (`-H`):** `tools/replay-proof.sh
  check` first failed with `replay proof: no manifest lines for hst` (a new
  pre-check: a set the manifest never recorded). `record`: 742 runs; `git
  diff test/golden/replay.sha256` is 318 added `hst/` lines and nothing
  else. Over the reports: the 105 recordings whose caller sent CM or AA
  give under `-H` exactly their `base` trace, bits and exit code (`cmp` of
  all three); the one whose caller sent neither (`v21a`, V.21 forced)
  enters HST_WAIT and its report says `no back channel detected: phase
  V22` after 650 ms. No detector hit. `diff REF` compares only the sets
  REF's own proof has (so `diff v0.6.0` leaves out `hst`).
- **`tools/rec_hst.c`, `rig/hst_side.h`, `bench_hst -c`:** a whole HST call
  (sim/hstcall at 450 calling the answerer through the public API, 10 ms
  blocks as a host runs it; MNP up, the retrain to 14400, text both ways)
  recorded in v90m-replay's format, and replayed into the answerer alone
  on the host and the target: our transmit is the recording's to the last
  codeword. `tools/bench-hashes.sh check` first failed with `bench
  hashes: no hst line in test/golden/bench.txt` (a new pre-check: a JOBS
  line the file has no hashes for); recorded: `hst tx b785bad2`, `hst rx
  2744ba91`, the other 17 lines unchanged. `v90m-replay -H` follows the
  recording until the endpoint starts sending text (8.855 s, data at 14400
  from 8.850 s).
- **`hst-scan -f`:** the forward channel on each side through hst_frx at
  every code 2..6: each training's segments by point statistics (AB, CD,
  TRN, data) and lengths in symbols, the data's constellation (the code
  that decodes it best) and how the receiver left it; a training heard in
  data restarts the receivers from their equaliser. On rec_hst's call,
  what we sent: AB 188 / CD 66 / TRN 3099 symbols, 32 points (code 4,
  9600, 33.5 dB); then AB 180 / CD 66 / TRN 203, 128 points (code 6, 14400,
  28.6 dB). Over the 106 private recordings: no training heard.
- **`tools/rtp2rec.py`:** a pcap or pcapng capture's two G.711 RTP
  directions (RFC 2198 over G.711 too) to the replay format for R1 / R2:
  placed by RTP timestamp, lined up by first arrival, lost packets silence
  (counted), the answerer's stream named (`--sent`) or found by its answer
  tone. `--self-test` (pcap IPv4; pcapng IPv6 with RFC 2198; loss,
  reordering, a wrapping timestamp; `--sent`) passes; with the redundant
  blocks left out (a stub) the RFC 2198 case fails.
- **`test/fixtures/replay-hst.sh`** (in `make check`'s fixtures): bench
  replay exact, `-H`'s section (450 found within W, trainings at codes 4
  and 6, 9600 then 14400 / 450, MNP 0x7C, transmit the recording's until
  data at 14400), no HST without `-H`, `hst-scan -b` and `-f` (segment
  lengths within 10 symbols, constellations 32 then 128). Against e1498b4's
  v90m-replay and hst-scan it fails (no `-H` section, `cannot open -f`).
  `replay-opts.sh`'s usage check now names `-H`.
- **`make cost-hst ARM_CC=$A5`: `cost-hst: holding forward 13.19 M/s`**
  (target 15). First count 16.64 M: the bench processed and polled a
  sample at a time, and `v32tx_tick` computed V.32's echo canceller's
  reference for HST's pulse too. Now the bench runs 10 ms blocks (as a
  host does) and `v32tx_tick` makes the reference only with V.32's own
  pulse (no golden or hash changed). qprof of the counted second: forward
  transmitter 3.8 M, back receivers 1.7 M, V.42 / MNP and the per-bit HST
  link polls 3.3 M, G.711 1.7 M, answerer and API per sample 1.9 M.
- **Echo corner with the real forward transmitter** (Task 13 carry):
  `test_hst_back` case 31 `echo_fwd` (first failing to compile: `echo_t`
  has no member `f`): hst_ftx at 14400, -10 dBm0, 6 dB ERL, the back
  channel at -33 dBm0: locked, 0 errors, no loss of lock at 450 and 300.
  Margin (HST_VERBOSE): error-free to -6 dB ERL (12 dB more echo), first
  errors at -7 dB. A whole call (rec_hst -e) loses an LR during our long
  training from 0 dB ERL (3 dB past the corner; costs 2.3-2.7 s).
  `test_hst_back`'s golden re-recorded: the count line and case 31's lines.
- **N1, a lasting 10-15 dB drop** (Task 13 carry): `rec_hst -x DB@MS`.
  -12 to -20 dB: every line loses the back lock 140-160 ms after the drop
  and locks again 180-1130 ms later; no call ends (5 s watchdog), MNP
  resends. -10 dB: 0 of 12 at 450, 5 of 12 at 300. No fix here: holding
  the lock through a lasting step is Task 23's.
- x86-64 only (owner's rule): `make check`, `tools/golden.sh check`,
  `tools/replay-proof.sh check` (742 identical), `tools/bench-hashes.sh
  check` (19 identical), `make check-ubsan`, `make check-fixed`; the ARM,
  big-endian and port-matrix gates (`make verify`'s armv7hf and bigend
  stages, `make verify-phase`) move to Task 26.

## hst 21: channel reversal, both roles (2026-11-25)

- `modem/ans_hst.c`: the spec's "Channel reversal" table, steps 1-4, as
  `hst_t.rev` (HST_REV_GRANT, LISTEN, GAP, TRAIN). Holding forward, the
  endpoint's granting LNA drained: 20 ms, then ones from the endpoint's
  first bit between frames (`cut`: ep_idle sampled once a sample missed
  every octet boundary when a symbol's 6 bits never ended on one, so the
  first try waited the 0.5 s bound and the caller had trained before we
  switched), then the switch: forward carrier off, the back receivers'
  held bits to putbit (`hst_brx_flush`), 8 ones (the endpoint's HDLC
  receiver aborts: its reset), back transmitter at the agreed speed
  (preamble, then ones until the far training is in), forward receiver
  from `eq_fwd`. Its data: epoch. Not heard within 3 s
  (`HST_REV_BOUND_MS`): the forward channel taken back with a short
  training, counted as a retrain (Task 23 adds the watchdogs after it).
  Holding back, our 0x1R's LNA: back transmitter off at once, the forward
  equaliser stored, 8 ones, both back receivers afresh, 200 ms, our short
  training and its 48 symbols of ones, epoch. RATES at each switch, moving
  stays 1, `reversals` counts; holding back `snr_db10` is the forward
  receiver's. `hst_fwd.c` unchanged (store/restore were there).
- Carried rulings: (1) Task 13 I-B: `hst_back.c` releases a confirmed
  lock's held bits on its loss (fade, quality, carrier detect) but for the
  symbols 3 dB or more under the mean (a cut signal halves its last one,
  whose decision is unsure: `fades` 100 ms gave one wrong bit without that
  trim); `test_hst_back` `stop_tail`: before, 33-42 (450) and 42-51 (300)
  bits short of the last sent; after, none short, 12 lines a speed.
  (2) Task 15: under `-DHST_PROFILE_BM` the forward burst ends with V.17's
  32 symbols of ones before the switch; the forward carrier's silence
  after it covers the 48 quiet. Not exercised: the BM build's API path
  fails at the base already (a BM caller never reaches data in
  `api_test_answer_hst` built with the profile at e1498b4; Task 23's
  `bm_profile`). (3) test_clock's comment: no free-running counter added.
- Each end now sends an LA first after a reversal (v42/mnp.c at the epoch,
  sim/hstcall at its switch; hst-facts `Reversal` [OURS]): without it the
  LAs lost at the switch left LTs unacknowledged, the backlog (queued plus
  unacknowledged, D14) over 64, and a needless reversal 2 s on: rev_ours
  made 4 reversals, rev_back300 6 (and, with the 0.5 s switch, lost the
  call).
- `test_answer_hst` cases 12-17: rev_caller (switch 20 ms after the LNA
  drained, the caller's long training, RATES exchanged, reversals 1),
  rev_ours and rev_back300 (our 0x16; short training 200 ms after its LNA,
  the caller's receiver from its kept equaliser; reversals 2), rev_many
  (20 reversals by 58.5 s, 25 when drained; data intact), rev_lost (taken
  back 3000 ms after the switch), snr (snr_db10 within 0.6 dB of the
  line's on a flat line at -40 dBm0; on the loop4 line 1.2 dB under, the
  equaliser's noise). Golden files re-recorded: test_hst_back (+4,
  count), test_answer_hst (+51, count), v42/test_ec_hst (`attn_out`: LAs
  4 -> 5, the LA at the epoch).
- `make check`: ALL HOST CHECKS PASS (x86-64; ARM, verify-phase and ports
  not run, the owner's rule until the release).

### hst 21, review fixes 1 (2026-11-25)

- **I1:** the BinModem profile's API path works; the base "failure" was
  the test's: built without `-DHST_PROFILE_BM` itself (the API rule took
  no EXTRA_CPPFLAGS) and with a caller that did not train as BinModem.
  `test_answer_hst` is profile-aware (`hc_defaults` sets `bm_training`;
  the S3 shown, the switch window 20 ms + 32 symbols: 33-36 ms; ms of ones
  from the host's last bit to the switch, 13-15 under the profile, 0-1
  without), and runs `rates` and `rev_caller` in `make check-hst-bm`
  (`HST_BM_TESTS` += api_test_answer_hst; new golden
  test/golden/out/bm/api_test_answer_hst.txt): the 11 calls reach data
  intact, the reversal switches 34 ms after the LNA drained, 13 ms after
  the host's last bit. With the turn-off disabled both bounds fail (21 ms,
  0 ms).
- **I2 (controller's ruling, D14 amended):** the reversal request counts
  untransmitted (queued) octets only, as the patent; our LA first after a
  reversal stays; hstcall's is an option, `la_after_rev`, off by default.
  With the old count and that caller rev_ours made 4 reversals; now 2
  (rev_back300 2), and the new `rev_ours_la` (the option on) 2. Spec D14,
  hst-facts `Reversal`, v42/mnp.h say so, tagged [OURS].
- **M1:** taking the forward channel back unasked is a retrain only;
  rev_lost pins reversals 2 (that switch and the caller's later one), the
  caller's 1, retrains 2. M2, M4: comments by `to_back` and `to_fwd`. M3,
  M6 left for Task 23. M5: the forward receiver's SNR reads 1.2 dB under
  the line's on the loop4 line (0.6 on a flat one): a known gap.
- rev_many now 23 reversals once drained (was 25); 20 by the same bound.

## hst 22a: rate changes by marks, the DSP half (2026-11-25)

- **Transmitter** (`hst_ftx_marks_switch`, `hst_ftx_marking`): 36 symbols
  of ones at the code in force, 36 at the new, then data; v32tx's mode is
  set to ones directly so that its count (and with it the scrambler,
  differential and trellis encoders) runs on. Outside data the code is
  only set; with 4800 on either side the short training instead.
- **Receiver** (`hst_frx_arm_switch`, `hst_frx_armed`, `hst_frx_mse`):
  armed in data mode, the decoder's ones are counted; where they end after
  24 symbols, the new code's Viterbi decoder (every state alike) is run
  over the last 24 points (a ring of 32 kept while armed), its descrambler
  and differential memory taken from the old code's after its last ones;
  both are decoded for at most 48 symbols, no bits given; 24 symbols of
  ones at the new code switch the receiver to the new decoder. Nothing is
  adapted from 8 symbols of ones to the switch (or 48 symbols of ones: an
  idle line). Armed: S or S-bar 16 symbols (a training instead), 2 s with
  no switch, or a lost lock (its equaliser then kept) gives the
  acquisition at the new code. `hst_frx_set_code` disarms. The receiver's
  behaviour with nothing armed is unchanged: `test_hst_fwd`'s 21 earlier
  cases print the same, and `bench_hst -r` the same line as at e1498b4.
- **Tests:** `test_hst_fwd` `marks_switch` (6 -> 5, 4 -> 6: no bit lost
  outside the marks, 276 and 216 bits of ones between the data) and
  `marks_missed` (the marks lost to a cut, a training 1 s on, at 6 -> 5 and
  4 -> 6; a training instead; armed and nothing: the acquisition at 2.0 s),
  202 checks. Both seen failing on stubs first. A scratch sweep of every
  pair of codes 3..6 at 30, 26 and 22 dB, 4 seeds (not committed): 46 of 144
  bad before the hold on adaptation, 2 after (22 dB to 14400).
- **Thresholds** (`make hst-thresh`, `test_hst_fwd thresholds` under
  -DTEST_FULL, about 20 min on the host): BER 1e-5 at 13.5 / 16.5 / 21.0 /
  23.5 dB at codes 3..6; `hst_good_mse` 1098 / 579 / 274 / 253 (the
  receiver's error 3 dB above: -17.76 / -20.54 / -23.78 / -24.13 dB).
  Table in docs/limits.md. Found on the way: the gain loop drifts against
  the equaliser on noisy lines (7200, 19 dB: the gain x3.2 in 80 s, failure
  at 120 s); measured in 20 s runs, reported, not fixed.
- **CPU** (`make cost-hst-fwd`, ARMv5 armv5-eabi--uclibc 2023.08-1): 14400
  data mode 25 396 881 instructions a second (25.40 M, unchanged); a switch
  12000 -> 14400, `bench_hst -s`/`-p`: 636 945 instructions over the 20 ms
  from the parallel decode's start, 31.70 M a second (budget 40 M).
- **Memory:** `hst_frx_t` 4080 -> 4960 octets x86-64 (a second v32_vit_t,
  588; the ring, 256), `hst_ftx_t` 200 -> 208; `hst_t` 10040 -> 10928, under
  12 KB; the union and `v90m_mem_size()` unchanged.

## hst 22c: the forward receiver's gain and equaliser no longer drift apart in long calls (2026-11-25)

- **Fault** (found in 22a): on a noisy line the data mode's gain loop
  climbed and the equaliser's taps shrank with it until the input clipped
  and the lock went (7200 at 19 dB: the gain +4.9 dB in 60 s).
- **Cause** (scratch instrumentation, not committed): the gain and the
  taps both set the points' scale, and nothing fixed it. Over 125 s at
  7200 / 19 dB the gain rose 9.6 dB while the taps' energy fell 9.7 dB,
  their sum constant within 0.3 dB. The two loops want different scales:
  the loop holds E|z|^2 at the set's power (measured 0.998 of it), the
  least-squares equaliser at the set's less its error's (orthogonality:
  E|z|^2 + E|e|^2 = E|d|^2; measured E|z|^2 + E|e|^2 = 1.006..1.010 of it,
  so the equaliser was always pulling down). The loop's quick branch (its
  slow one rounds to 0 under a 1/8 difference) acts on the 32-symbol
  power's swings below the set's, which outnumber those above; the noisier
  the line, the faster the climb, and the climb raised the error and so
  itself (it accelerated before the loss).
- **Fix** (`modem/hst_fwd.c` `gain()`, HST-local; qamrx and v32 untouched):
  the gain leaks back to `ga`, the gain as last set (G_ONE from a training,
  else `gain_set`'s), 1/32 of the way every 64 symbols (0.85 s), and is
  held within 1 dB of it. The slow scale is the equaliser's; level steps
  are still the loop's (dropping its continuous part instead lost the lock
  on `level_step`'s -1 dB at 14400 and -2 dB at 9600). After it, at 7200 /
  19 dB over 300 s: E|z|^2 + E|e|^2 = 0.996..1.007, the taps within 0.3 dB.
  New field `hst_frx_t.ga` (padding: `hst_frx_t` stays 4960, `hst_t` 10928).
- **Test:** `test_hst_fwd` case 24 `long_drift` (code 3 at 19 dB, code 4 at
  17.5 dB, 60 s each; TEST_SHORT code 4, 30 s): data mode kept, the gain
  within 1 dB of its value at 10 s, errors in the last window no more than
  in the first. Seen failing first (+4.9 and +8.5 dB). The full set,
  `make hst-drift` (`test_hst_fwd long_drift` under -DTEST_FULL, an argv
  mode so the port matrix never runs it, about 20 s): codes 3..6 at 1 and
  3 dB over BER 1e-5, 300 s each. Before: 5 of 8 lost the lock (31 to
  130 s), all 8 failed the gain bound. After: 8 of 8 pass, no error, the
  gain within -0.83..+0.63 dB.
- **Golden:** `test_hst_fwd.txt` re-recorded: the count line (202 -> 208)
  and case 24's lines only; cases 1..23 print the same.
- **Short calls:** a level-step sweep (-3..+6 dB, 9600 and 14400, 8 seeds,
  scratch) 3/96 bad before and after. `make hst-thresh` again: crossings
  13.5 / 16.5 / 21.0 / 23.5 dB, the error 3 dB above -17.80 / -20.56 /
  -23.79 / -24.13 dB (22a: -17.76 / -20.54 / -23.78 / -24.13):
  `hst_good_mse` kept.
- **CPU** (`make cost-hst-fwd`, ARMv5): 14400 data mode 25 433 679 a second
  (25.40 M -> 25.43 M, budget 26 M); the switch window 636 939 in 20 ms.
- `make check`: ALL HOST CHECKS PASS (x86-64).

## hst 22b: rate changes, the policy half: 0x3R by marks, 0x2R by retrain, the rate policy, recovery (2026-11-25)

- **Holding forward** (`modem/ans_hst.c`, `hst_t.rc`): the far end's 0x2R
  or 0x3R reaches the pump (`rate_order`) only once our granting LNA and
  its closing flag have gone (`mnp_t.rate_grant`, the reversal grant's
  path in v42.c), so the LNA goes at the old rate ahead of the change.
  From the endpoint's first bit between frames: 0x3R the switch by marks
  (no retrain, RATES), 0x2R 20 ms of ones then the short training at R (a
  retrain); then epoch. Under HST_3R_TRAINS (the BinModem profile) 0x3R
  trains too. Recovery: the endpoint raises `recover` when, holding
  forward with LTs out and credit given, N(R) has not moved for 2 x T401
  of unstopped line; with a back receiver locked, the short training at
  the rate in force.
- **Holding back** (the policy, hst.h `HST_UP_MS` ...): every 10 s the
  next agreed rate up to max_up at 90 % good, never within 10 s of a
  downshift; 15 frame errors (the endpoint's `rx_bad`, now in the mailbox)
  within 10 s of the first, or 3 failed retrains within 10 s of the first,
  one agreed rate lower. A retrain fails: not in data at R 4 s after the
  request left, its training ending without data, or the data mode lost
  within 2 s; a failed one is asked again at R. The receiver losing data
  by itself with the carrier there (UNLOCKED, TRAINFAIL) asks a retrain at
  the rate in force; no carrier is Task 23's. The holder's change
  (`rate_order`, by role: now also the LNA to our own 0x2R/0x3R) is
  followed: marks by `hst_frx_arm_switch`, a training by the new
  `hst_frx_arm_train` (armed already at the request: the LNA may be lost
  where the training is not; no time-out of its own). Data stops only
  once the training is under way, so that our request goes out. A newer
  0x2R/0x3R of the pump supersedes ours still waiting for its LNA (the LNA
  comes on the receiver that failed); an LNA arriving after its change
  was done is ignored (`rc_late`).
- **Carried items.** (1) rate_order read by role, epoch bumped after each
  change done or given up. (2) `hst_good_mse` read at c - 1: `test_hst_fwd
  upshift` (in `make hst-thresh`) measures the receiver's error at c - 1
  and c on the line where c is good: 1075/1090, 578/577, 276/274, 253/253
  (codes 3..6), so the table is used as it is; a code-5 receiver at 26.5
  dB reads 253. The 90 % rule makes 14400 come only on 29-30 dB lines (8 %
  good at 26.5, 65 % at 28.5, 100 % at 30.5), the others 1 dB over their
  line: left so, written in limits.md. (3) `hst_frx_restore` disarms. (4)
  `fallback`: 15 dB SNR from 12000: 3 failed retrains, 9600 the same,
  7200 holds, 10 s, 7 retrains, then none. (5) HST_3R_TRAINS used: the
  holder trains for a 0x3R, the receiver arms for a training.
- **22a's S detection while armed fixed** (`hst_fwd.c` `sw_training`):
  it tested A..D themselves within 1.2 units, and missed the training when
  data mode held the carrier a quarter turn round, when the loops had
  pulled S 15 degrees and 1.2 dB off a dozen symbols in, and under some
  20 dB. Now: each point the one two before and a quarter turn from the
  last, within 30 % of its power, 16 in a row (TRN 1/8 a symbol, data
  1/128 or less). `test_hst_fwd` prints the same.
- **sim/hstcall** `retrain_3r` (BinModem retrains on every rate change,
  hst-facts `Rate changes` [BM]): it carries 0x3R out by a training and,
  holding back, on the LNA to its 0x3R listens for one.
- **Tests** (`test_answer_hst` cases 19-24, each seen failing first):
  `up`, `down`, `retrain_by_2R` (T401 held: the endpoint stopped, T401 as
  it stood or stopped by an LA, no expiry), `max_up`, `recovery` (the
  training 6689 ms after the marks, 2 x T401 6354 ms), `fallback`.
  `test_ec_hst`: the 0x2R grant's order not before the LNA is out; the LNA
  to our 0x24 now gives `rate_order` (3 checks).
- **Goldens re-recorded:** `api/test_answer_hst.txt` (the six cases'
  lines appended, the count 513 -> 592; cases 1-18 print the same),
  `v42/test_ec_hst.txt` (the count 135 -> 138 only).
- **Memory:** `hst_t` 11000 -> 11096 octets (x86-64), `hst_frx_t` 4960 ->
  4976, `mnp_t` 3368 -> 3376; the union and `v90m_mem_size()` unchanged.
- `make check PAR=16`: ALL HOST CHECKS PASS (x86-64, 6 min 38 s, check-hst-bm
  in it); `tools/golden.sh check build` after the re-record: 158 identical.
  TEST_SHORT build of cases 19-24: pass. Owner rule: no check-arm, verify
  or ports.
- **Fix round 1** (review I1, M1, M4, M5, M6). I1: the supersede of an
  unanswered 0x2R/0x3R moves V(SA) on only on evidence that the far end
  has it: the pump saw the holder's training (new mailbox `ln_acted`) or
  it has gone twice; else the request waits for the LNA or the next T401.
  Written as [OURS] in the spec, hst-facts and limits.md with its failure
  mode. New `test_ec_hst` `attn_lost` (our LN lost on the line, then a new
  request: both reach the far end in order, V(SA) in step; with ln_acted
  at once): 10 checks, 7 of them fail under the old rule. M4: `up` and
  `recovery` profile-aware (HST_3R_TRAINS: 0x3R by training both ways;
  recovery from the caller's receiver dropping out, `hc_frx_relisten`),
  run by `make check-hst-bm`. M1: the S detector's false pass is about
  1/38 a symbol in data (the reviewer's Monte Carlo), 16 in a row still
  <= 4e-15. M5/M6: comments and limits.md. Goldens re-recorded:
  `v42/test_ec_hst.txt` (attn_lost's line, count 138 -> 148),
  `bm/api_test_answer_hst.txt` (up's and recovery's lines, count 187 ->
  214); `api/test_answer_hst.txt` unchanged. `make check PAR=16`: ALL
  HOST CHECKS PASS (6 min 22 s).
- **Fix round 2** (re-review). `ln_acted` was cleared only when the
  endpoint took the pump's request, and set for any training seen: a
  stale 1 let a later request supersede the endpoint's own 0x2R at the
  cap after one send (round 1's desync again). Now cleared at every LN
  sent (the pump's, the cap's, the reversal policy's) and set only for
  the pump's own request (`rc` ASK). And a held request no longer waits
  for T401, which holding back is our transmit's: 2.9 s at 14400/450 but
  11.0 s at 450/9600 and 16.0 s at 300/9600, over the 4 s bound: an LN
  sent once only goes again at once, ahead of the new request, and is
  superseded when that copy has gone. `test_ec_hst` `attn_lost` at
  14400/450, 450/9600 and 300/9600 (both LNs at the far end in order in
  7, 215 and 323 ms) and `attn_stale` (the cap's 0x24 lost, a stale
  ln_acted, then the pump's 0x23: 24 then 23, in step; 2 failures under
  round 1's clearing). limits.md's rate-policy paragraph reflowed (a
  sentence had gone in mid-paragraph), spec and hst-facts updated. Golden
  `v42/test_ec_hst.txt` re-recorded (attn_lost's line rewritten,
  attn_stale's added, count 148 -> 157). `make check PAR=16`: ALL HOST
  CHECKS PASS (6 min 35 s); 157 of 158 goldens identical before the
  re-record.

## hst 23: retrain, watchdogs, cleardown and end codes (23a) (2026-11-25)

Spec "Retrain, watchdogs, cleardown and end codes"; the controller's
carried items 1-7 (task-23-carries.md). x86-64 only (owner rule); the
milestone gate (verify-phase, ports, ARM) is Task 26's.

- **v90m_retrain holding back**: our 0x2R at the rate in force (`ask`),
  data stopped once the holder's training is under way (holding forward:
  the short training, as before). V90M_ESTATE while one is pending.
- **Watchdogs** (hst.h `HST_SILENT_ASK_MS`, `HST_SILENT_BACK_MS`):
  holding back, 1 s without the forward carrier (`hst_frx_carrier`, new)
  asks 0x2R, 5 s takes the forward channel back (a retrain), then the
  forward rule (5 s without a back lock: LOST). NOCARRIER with the
  carrier back (a dropout over Task 16's 200 ms hold) is a retrain at once.
- **RETRAIN_FAILED**: 3 failed retrains within 10 s at the lowest agreed
  rate end the call. The Task 22b review's M2: retrains for a lost data
  mode count in a 30 s window (`HST_CY_N` 4, `HST_CY_MS`); the 4th goes one
  rate lower, or ends the call at the lowest.
- **Far LD**: `link.far_ld` ends the call CLEARDOWN_FAR when the far
  carrier goes, or 2 s on. **Ours**: with MNP up, the host's bits 30 ms
  (holding forward) / 200 ms (holding back) more, then both carriers stop.
  Found on the way: `v90m_ec_tx_busy` is 0 from the LD's closing flag's
  first bit; cutting there gave the caller an abort at 450 bit/s.
- **Reversal tail** (the Task 21 review's M3): 20 symbols of ones from the
  cut before our forward carrier stops (default profile).
- **N1** (back receiver, Task 19's carry): a fade long enough to lose the
  lock whose last 8 (450) / 5 (300) symbols are steady and on the grid is
  a level step: the mean taken again, the lock kept. `rec_hst -x -12@1000`
  / `-20@1000`, 450 and 300, 2 seeds: no loss of lock, the undropped
  throughput (832 / 576 octets in 19 s).
- **Forward receiver** (level and carrier-loss paths only; 23b owns the
  training start): the level re-taken where data mode begins (Minor F).
- **Diag**: `ANS_ERR_HST_LINK`, `ANS_ERR_HST_CARRIER` appended; the HST
  row of `diag_err_answer` (RETRAIN_FAILED gives `ANS_ERR_RETRAINS`).
- **Tests**: `test_answer_hst` cases 25-31 (`retrain_back`, `back_silent`,
  `retrain_failed`, `far_ld`, `ours`, `unlock_loop`, `levels`), each seen
  failing first; diag checks in `no_lr`, `lost_fwd`; reversal switch
  expectations 28-31 ms / 8-10 ms of ones. `fallback`'s seed 2206 -> 2210:
  at 15 dB a 7200 training fails 3 times in a row on ~2 lines in 5 before
  and after (seeds 2206-2225: 12 vs 9 of 20 end at 7200). `test_hst_back`
  `lasting_drop`, `test_hst_fwd` `ab_loud`.
- **Goldens re-recorded**: `api/test_answer_hst.txt` (new cases, the
  reversal line's wording, `down`'s octet count and `fallback`'s lines
  from the moved timing/seed, count 592 -> 1091), `api/test_diag.txt` (names
  117 -> 119, count 7411 -> 7652), `test/test_hst_back.txt` (lasting_drop
  lines, 125 -> 131), `test/test_hst_fwd.txt` (ab_loud lines, 208 -> 212).
  The bm goldens identical.
- **Commands**: `make check PAR=12`: ALL HOST CHECKS PASS (7 min 2 s, with
  check-hst-bm); `tools/golden.sh check build`: 154 of 158 identical
  before the re-record (the four above); `tools/replay-proof.sh check`:
  742 runs identical; `tools/bench-hashes.sh check`: 19 identical; the
  TEST_SHORT build of cases 25-31: pass.
- **CPU** (`make cost-hst ARM_CC=$A5`, the one ARMv5 run): holding forward
  14.44 M (13.19 at Task 19), **holding back 38.44 M, over the 30 M
  target** (new: `rec_hst -R`, `bench_hst -k`), the 0x3R parallel decode
  32.04 M for 20 ms. The levers (equaliser at T/2, a cheaper guard) are
  the forward receiver's and left to after 23b: docs/limits.md.

## hst 23d: v42ec_tx_busy counts the HDLC closing flag (2026-11-25)

- **Bug** (Task 23 review, Important 2): `v42hdlc_tx_busy` read 0 from the
  first bit of a frame's closing flag, 7 bits early, against the contract
  of `v90m_ec_tx_busy` ("busy until the frame is out"). MNP framing 3's LD
  was never acknowledged, so a host stopping the line at busy 0 cut it.
  LAPM and MNP mode 3, every modulation; mode 2 was right.
- **Fix** (`v42/v42_hdlc.c`): a `closing` flag (one byte, in the padding
  after `cur`, so `v90m_ec_mem_size` is unchanged) is set while the closing
  flag is shifted out; busy stays up until its last bit is taken. Zero
  insertion before it was already inside `in_frame`. Only when busy reads 0
  changes, not a bit on the line.
- **Tests**: `test_hdlc` busy_closing_flag (stop at busy 0: the frame is
  decoded, FCS-16 and 32, ones runs), `test_ec_mnp` release_mode3 (the peer
  gets the LD); both failed before. `test_v42` detect_case fed the receiver
  16 bits after busy: now 9, the same stream as before.
- **ans_hst.c**: the 1 s ask in back_watch only with nothing pending (rc
  none, no ln_req, no rate_order); `test_answer_hst` case 32
  back_silent_pending (failed before). The cleardown tail
  (HST_CLR_FWD_MS/BACK_MS) is kept: the closing flag part of it is now
  redundant, the transmit pipeline and far decoder flush are not.

## hst 23b: the forward receiver's first training copes with unknown transmit shapes and band-edge delay (2026-11-25)

- **Model.** `sim/v32line` gets an optional band edge on the way to the
  answerer (`edge_both`: both ways), from filter theory: a Butterworth
  high-pass (`edge_hp_hz`, `edge_hp_order`, bilinear, prewarped), and/or
  a second-order all-pass with poles at 550 Hz (`edge_gd_ms`: its group
  delay at 600 Hz over 1800's; 3 ms gives 0.85 ms at 700 Hz, 0.12 at
  1000). Checked on its impulse response (scratch): 560 Hz order 10 gives
  3.0 ms at 600 Hz, -0.9 dB. All zero: the line as before (every golden
  identical). `sim/hstcall` gets `hc_ftx_rolloff` (V.17's 25 % alone);
  `hc_ftx_bm` uses it, same arithmetic.
- **Measure** (`test_hstcall sweep 5` and `sweep 5 v17`; codes 2..6, our
  pulse and V.17's roll-off, no edge, high-pass 300-650 Hz of order 4 and
  10, all-pass 0.5-3 ms). Before: everything passes but the order-10
  high-pass at 550 Hz (ours 23 and 16 of 25, V.17 roll-off 25 and 20) and
  600 Hz (3, 2; 7, 1), error stuck at -3.5..-4.5 dB, and 650 Hz order 10
  (AB never heard). Phase alone never fails. Thin by the brief's rule.
- **Failing test** `test_hstcall` case 19 `fwd_edge` (600 Hz order 10 at
  codes 6 and 3, both shapes; 575 Hz with V.17's train at code 4): 8 of 8
  checks failed before the fix.
- **Fix, rung (a), two starts.** Tried first in scratch: (b) a
  radius-directed blind start (no convergence in 1024 symbols at 600/10),
  an ungated first 512 symbols (mixed). Starting from the matched filter
  of our pulse through a 550 Hz 6th-order high-pass trains every 550-600
  Hz order-10 case, but fails on flat lines in about half the runs; so a
  choice. The error after 128 adapted symbols did not separate a slow
  start from a stuck one; the spread of each start's unadapted points'
  power over the first 128 non-alternating training symbols (A..D have
  one power) did. `hst_fmf` (tools/gen_hst_tabs.py): [0] mf_taps' old
  interpolation of hst_fpulse, the same integers; [1] the edge start.
  `mf_choose` (hst_fwd.c): both starts' points from qamrx's line, at
  symbol 128 the second chosen if its spread is under 1.25 times the
  first's; then qamrx_sync_eq again with the second, the timing from AB's
  phase carried on (`mft`; loading the taps alone, the carrier and timing
  left from the first start, failed 3 to 8 of 25). Not under
  HST_FMF_BM (the profile keeps its own start). Fields +44 octets in
  hst_frx_t.
- **After.** The sweeps: every cell passes up to 600 Hz (650 Hz order 10:
  AB, unchanged). Dense scratch sweep (500-650 Hz in 25 Hz steps, orders
  4/6/8/10, 3 seeds, both shapes and trainings): 1433 of 1680, 240 of the
  failures where AB is never heard, 7 single misses.
- **CPU.** `make cost-hst-fwd ARM_CC=$A5` (the one ARMv5 run): 14400 data
  mode 25 545 663 a second (25.43 M before; the per-sample mft test), the
  marks switch's 20 ms 32.1 M a second. Training, x86-64 per 20 ms block
  over bench_hst's recording: measuring 22.3 us at most, training
  otherwise 20.4, data 31.2: the training's peak some 0.73 of data mode,
  about 18.7 M a second on ARMv5 by that ratio.
- **Checks.** `make golden` (make check, check-hst-bm in it): ALL HOST
  CHECKS PASS; 157 of 158 identical, `test/test_hstcall.txt` re-recorded
  (case 19's 12 lines, count 106 -> 114); bm goldens identical. TEST_SHORT
  build of case 19: pass. x86-64 only (owner rule).
- **Fix round 1** (review: Important 1, Minors 2-4). With a clock offset the
  restart from the second start lost trainings on steep edges (-100 ppm:
  525/10 11 of 80, 550/10 7; +7 Hz -100 ppm: 550/10 27). Cause, measured
  with a scratch knob on the restart's timing: AB's reading is early on
  the line the second start assumes (its 550 Hz high-pass's phase at 600
  Hz against 3000 Hz: 1.5 samples, 0.45 symbol), and a slow caller clock
  adds 0.09 sample by the restart; moved 0.12-0.38 sample earlier the
  failures rose to 61 % of a cell, 0.12-1.75 samples later they were at
  most 2 of 80. The generator now computes that bias (HST_FMF_AB_SHIFT,
  2297/1536 sample) and the restart adds it. New fwd_edge runs (550/10
  -100 ppm code 6; 525/10 +7 Hz -100 ppm code 5, V.17's shape and train):
  4 of 4 checks failed before. Sweep mode takes HZ and PPM (`sweep SEEDS
  v17|long HZ PPM`) and has 525/575/10 and 550/600/625/6 rows. Scratch
  grid, 13 edges x 7 offsets x 80 runs, before / control (second start
  from AB, no choice) / now: the steep order-10 edges now fail 0 at every
  offset (were up to 27); the control failed 5-71 on flat lines and up to
  54 on edges; left: 550/6 at +-7 Hz +100 ppm 7 and 3 (no switch, as
  before), 625/6 at -7 Hz (AB never heard), single misses at 500/525 of
  order 8 and gd 3 ms, all as before. limits.md rewritten with the table,
  the Butterworth-only model, gd > 3 ms (ratio 1.53, the closest flat
  call). Wording: the rule keeps the second start unless its spread is
  1.25 times the first's or more; gen_hst_tabs.py's message says int64.
  `make golden` (make check, check-hst-bm in it): ALL HOST CHECKS PASS;
  157 of 158 identical, `test/test_hstcall.txt` re-recorded (the two runs'
  6 lines, count 114 -> 118). CPU path: one add at the restart (no ARM
  run). x86-64 only.

## hst 23c: CPU cut: holding back ≤ 30 M, the 0x3R switch ≤ 40 M (2026-11-25)

- **Measure.** `tools/qprof` over ARMv5 qemu (`-d in_asm,exec,nochain`)
  of `bench_hst -k`'s counted second (holding back, 38.50 M with the
  bench's loop) and `-p`'s 20 ms (the 0x3R window, 644 712). Holding
  back: `v32_vit_step` 9.35 M, qamrx 9.84 M (`produce` 4.06, `eq_out`
  3.23, `qamrx_update` 2.19, `qamrx_tick` 0.36), the guard 3.14 M,
  **`v32tx_tick` 2.87 M** (our forward transmitter, stopped, filtering a
  line of zeros), `hst_btx_tick` 1.72 M, `hst_frx_tick` 1.53 M, tick_hst
  1.38 M, the link polled at every bit 2.30 M (`mnp_hst_poll` 1.26,
  `link_poll` 1.05), G.711 1.65 M. The 0x3R window: `v32_vit_step` 51 %
  (two decoders and the 24 points replayed into the new one).
- **Cut 1, the idle forward transmitter** (`hst_ftx_tick`): once stopped
  and HST_FTX_QUIET (36) symbols on, v32tx's line is all zero; the tick
  keeps v32tx's count, quarter turns and newsym alone and gives 0.
  2.87 -> 0.15 M. Same samples and state as v32tx_tick (`test_hst_fwd`
  case 26 `fast_paths`).
- **Cut 2, the trellis decoder** (`hst_vit_step`, modem/hst_fwd.c, used
  by the receiver in place of `v32_vit_step`; v32/ untouched).
  v32_vit_step's distance is the plain squared distance from z >> 2 to
  the point at 128 a unit, and each subset of V.32 bis's 7200-14400 sets
  lies on one square grid (`hst_vgrid_*`, tools/gen_hst_tabs.py, read
  from v32/v32_tabs.c): each coordinate rounded to its column and row;
  half-way, or a corner the cross-shaped set lacks, the subset searched
  as before. The add-compare-select written out in v32_vit_step's order.
  First try (each coordinate's square once a symbol, a sum of two a
  point): 9.35 -> 8.04 M only; the grid 4.23 M; written-out ACS 3.32 M.
  Its decisions, metrics and history equal v32_vit_step's on 200 000
  points (codes 3..6, near the points, ties, past the clamp); mutations
  of the tie rule and of the clamp fail it (17 751 and 200 000 steps).
- **Effect on the receiver: none** (both cuts give the same output to
  the bit). `make hst-thresh` and `make hst-drift`: output identical to
  BASE 8568967's. 23b's sweeps (`test_hstcall sweep 5`, `sweep 5 v17`):
  identical. Every golden but test_hst_fwd's identical; bench hashes,
  replay proof identical.
- **ARMv5 `make cost-hst ARM_CC=$A5`**: holding forward **14.47 M**
  (14 466 039; BASE 14 442 039: the stopped test each sample); holding
  back **29.29 M** (29 288 955; 38.44 M); the 0x3R decode **31.98 M**
  for 20 ms on the whole modem (the forward receiver's 443 923 in 20 ms,
  22.05 M a second, plus the rest of holding back 9.92 M = holding back
  less the forward receiver alone at 14400, 19.37 M). On that basis BASE
  was about 45 M. `make cost-hst` now prints that figure (one more count,
  bench_hst -r).
- **Not taken:** the link polled per tick (about 2 M; v42/), a shorter
  HST interpolator in qamrx (about 1.6 M; changes the output), T/2
  (impossible for a pass-band equaliser: at 4800 Hz the 600-3000 Hz
  band's image covers 1800-4200 Hz; CPU-frx measured 5.4 dB), the 22c gain
  leak and the 23b selection (training only; not in either window).
- **Checks.** `make golden`: ALL HOST CHECKS PASS, 158 of 158 identical
  after re-recording `test/golden/out/test/test_hst_fwd.txt` (case 26's 4
  lines, count 212 -> 214); `make check-hst-bm` pass (bm goldens
  identical: case 26 is not in the profile); `tools/replay-proof.sh
  check` 742 identical; `tools/bench-hashes.sh check` 19 identical.
  x86-64 tests only (owner rule); ARMv5 for the cost runs alone.

## hst 23e: first training copes with an AB louder or quieter than the data (2026-11-25)

- **Cause (measured, `test_hst_fwd ab_sweep`, and a probe of the
  receiver's state per 16 symbols).** qamrx's gain is set from AB's power
  at the start, and the carrier's level (`pref`) from AB's level. With AB 6
  dB over the rest, the training's points after AB came 6.5 dB under A..D's
  (6.3 units against the off gate's 2.5): 16 of 16 off from symbol 79 on,
  `mse0` frozen, nothing adapted: TRAINFAIL. From 10 dB over, the power
  is under `pref` / 10: the clamp held every symbol and the off count ran
  out: NOCARRIER. 4 dB under, the points 4 dB over: off again, TRAINFAIL.
  Pass edge before: AB 3 dB under to 5 dB over the rest (3 seeds, codes
  2..6, 30 dB; at +7 Hz -100 ppm, 3 under to 9 over).
- **Fix (modem/hst_fwd.c, training only).** `start` takes `pref` 6 dB
  under AB's power (the relative clamp 16 dB under AB). `level`, on each
  training symbol: while 5 or more of the last 8 points are off and their
  mean power is over 3 dB off A..D's (40 units^2) for 8 symbols in a row,
  the next 16 symbols' power is measured; if over 1 dB off, qamrx's gain
  is set by the root of the ratio, the equaliser's line and the last 8
  points scaled by exactly new / old, `pref` taken from the power now,
  the first start's measure (`mf_choose`) begun again, nothing adapted on
  that symbol. At most 4 a training. hst_frx_t: `lvw`, `lvn`, `lvp` (16
  octets). Nothing per sample, nothing in data mode.
- **Two first versions rejected by the sweeps.** Taking the level on any
  3 dB miss for 8 symbols fired on steep edges, where the first start's
  points drift 3 dB while its equaliser adapts (23b's sweep, ours hp
  500/10 code 3: 5/5 -> 4/5); hence "most of the last 8 off". Taking it
  from the 8 points at the trigger measured the S1 -> S2 transient (5.5
  dB for a 4 dB step); on a 20 dB line code 5 then passed 4 of 10 at AB
  +4 dB; hence the 16 symbols' measure after the trigger.
- **After:** every cell from AB 8 dB under to 14 dB over, codes 2..6,
  passes (3 seeds, 30 dB; 2 seeds at +7 Hz -100 ppm). 20 dB line, 10
  seeds: codes 2..4 10/10 at every level; code 5 after a re-take 5..8/10
  as at equal levels (8/10 at 0 dB, 5/10 at -1 dB), code 6 0 at any level
  but +2/+3 (1..2 of 10). Before, AB 2..5 dB over did better there (code 5
  10/10): the points start smaller, a smaller step for the equaliser; a
  re-taken training is an equal-level one.
- **Tests.** `ab_loud` (case 25) gains AB +6, +10, +12 and -6 dB, codes
  2..6, data mode and no error in 20000 bits: 20 failures before (TRAINFAIL
  at +6 and -6, NOCARRIER at +10 and +12). Its seeds move from 1300 + c to
  1400 + c: at 1301 the 6.5 dB step at 14400 loses the lock with AB at
  the data's level as well (3 of 12 seeds at 0 dB; 4 of 12 after a re-take;
  1 of 12 with AB 4 dB over and no re-take); its two lines are unchanged.
  `test_hst_fwd ab_sweep [SEEDS [SNR [HZ PPM]]]`: the sweep (not a check).
- **No worse elsewhere.** 23b's edge sweeps (`test_hstcall sweep 5`,
  `sweep 5 v17`), `make hst-thresh` and `make hst-drift`: output identical
  to BASE 5926573's (built from `git archive` in build/tmp/base). The
  re-take never fires on a training at the data's level.
- **CPU.** Training only: a population count of the last 8 decisions'
  off bits a symbol, 8 squares while 5 or more are off, one scaling of
  the 144-sample line at a re-take. `hst_frx_tick` and the data path are
  unchanged, so holding forward and back are as at 23c (no ARM run).
- **Checks (x86-64).** `make golden`: 158 of 158 identical after
  re-recording `test/golden/out/test/test_hst_fwd.txt` (40 new lines of
  case 25, count 214 -> 234; its two existing ab_loud lines unchanged);
  check-hst-bm in it (bm goldens identical). `tools/replay-proof.sh check`
  742 identical; `tools/bench-hashes.sh check` 19 identical. TEST_SHORT
  (-DTEST_FORCE_SHORT) case 25 passes; clang -pedantic and gcc -Wconversion
  compile hst_fwd.c cleanly.

## v34cap: our V.34 transmit capped at the caller's own INFO1 projection (2026-11-25)

- **Why.** A WebTV box asked for 31200 in its MP although its own INFO1c
  projected 28800 at 3200 Bd; it could not decode our 31200, missed our
  ADP and fell back to async (`/home/georgica/v34-dbg/build/webtv-report.md`).
  V.34 takes the lower of the two MPs, so ours now carries the projection.
- **What.** Answering only: `v34ans_dx_cfg` (start-up and every retrain's
  Phase 2) and `v34_call_cfg_from_v90` (a V.90 caller selecting V.34, INFO1a
  bits 30:33) set `max_tx_rate` to `v34_tx_ceiling(cfg, projection)`;
  renegotiation keeps it. On by default; `V90M_OPT_V34_NO_TX_CAP`, an option
  bit in `v90m_cfg.modulations` (-> `ANS_MODE_V34_NO_TX_CAP` ->
  `v34ans_cfg.no_tx_cap`, a `uint8_t` beside `k56`, now `uint8_t` too),
  turns it off. Memory unchanged (106320 x86-64; 105840 -m32 i386
  before and after). Calling side not capped: our own answerer projects
  28800 on a clean line while its MP offers 33600 (the call runs at 31200), and the cap cut
  `test_orig_v34`'s clean calls from 31200 to 28800.
- **Corpus first.** 106 recordings x 7 sets, instrumented replay: no caller
  asked in its MP for more than its own projection (6 V.34 calls project 13
  and ask 13 or 12; x2b's V.90-to-V.34 INFO1a 12, MP 12). No rate changes;
  but our MP offered 14 (33600) and now offers 13, so with the cap the 6
  V.34 replays no longer connect (the recording's echo is of the old MP;
  HEAD with `-d 31200` gives byte-identical output). Hence `v90m-replay -C`
  / `v90m-fields -C` (cap off), used by `tools/replay-proof.sh` (every set)
  and `test/fixtures/fixtures.txt` (every line).
- **Tests.** `test_v34call`: INFO1c 28800 / MP 31200 -> 28800, kept over a
  renegotiation; retrain with INFO1c 26400 -> 26400; MP below projection
  unchanged; our ceiling below it wins; cap off -> MP max 14 and 31200, the
  rest of our MP byte-equal to the capped one, also after a retrain.
  `test_answer_v90_v34`: INFO1a 24000 -> 24000 at start-up and after a
  retrain (sim/p2a `v34_max_rate`). `test_api`: the option bit alone is
  EINVAL, accepted answering, ignored calling.
- **Checks.** `make golden`: ALL HOST CHECKS PASS, fixtures all pass with
  traces identical; 154 of 158 identical, 4 re-recorded: `test_v34call`
  and `test_answer_v90_v34` (new cases and counts; one SNR 38.1->37.7 in the
  latter's existing retrain line), `test_orig_v34` (4 SNR readings, 0.1-0.5
  dB) and `api/test_api` (2 SNR readings, count). Every rate unchanged; with
  the cap forced off the old lines come back byte for byte. Replay proof:
  742 runs identical; bench hashes: 19 identical. x86-64 only.

## v34cap, policy change: the cap is opt-in, with status for the host's count (2026-11-25)

- **Owner's ruling.** The cap is off by default; the server turns it on per
  call for members whose box has shown the problem 3 times, and keeps the
  count itself.
- **Switch inverted.** `V90M_OPT_V34_TX_CAP` (0x10000, the same bit)
  replaces `V90M_OPT_V34_NO_TX_CAP`; `ANS_MODE_V34_TX_CAP`,
  `v34ans_cfg.tx_cap`. Without it the modem is 0.6.1's: our MP offers the
  ceiling. `v90m-replay -C` / `v90m-fields -C` removed (the default is the
  recordings' behaviour again); `tools/replay-proof.sh`,
  `test/fixtures/fixtures.txt` and `tools/README.md` back to 8568967's.
- **Status.** `v90m_stats.v34_proj_bps`, `v34_ask_bps`, `v34_capped`
  (appended, +12 octets), filled by `answer_v34_tx_cap()`: answering V.34
  (and V.90 -> V.34: INFO1a bits 30:33 and the MP's max_ca), our transmit
  direction, from the last Phase 2 / 4; 0 for any other call, calling, or
  before V.34 training; `capped` when the option lowered our MP's offer
  below what it would be without it. `v90m_mem_size()` unchanged (106320
  x86-64; 105840 -m32), `V90M_MEM_SIZE` unchanged.
- **Tests.** `test_v34call`: the cap cases with `tx_cap` 1; the default
  case gives MP max 14 and 31200, the MP's bits equal to the capped one's
  but for the maximum. `test_answer_v34`: box B's shape (INFO1c 28800 at
  3200, MP 31200): off: 31200, projection 28800, ask 31200, capped 0; on:
  28800, capped 1. `test_answer_v90_v34`: INFO1a 24000, MP more: off
  28800 (MP max 14), on 24000, status both ways. `test_api`: zeros before
  a call, calling and on V.32bis; on our own V.34 call a projection and an
  ask, capped 0.
- **Checks.** `make golden`: ALL HOST CHECKS PASS, fixtures identical;
  goldens back to the pre-cap bytes but for new lines and check counts
  (`test_v34call`, `test_answer_v90_v34`, `test_answer_v34`, `api/test_api`
  re-recorded; `test_orig_v34` back to 8568967's). Replay proof: 742
  identical; bench hashes: 19 identical. x86-64 only.

## 0.7.0: x2's inverted JM only at first; a plain JM to a caller still sending CM (2026-11-25)

- **Cause (WebTV report, finding 1; measurements only).** With x2 offered,
  a CM with V.34 and no PCM got a JM whose carrier was inverted after
  every JM, for the whole of V.8. Two WebTV boxes (hardware V.34 modems,
  strict V.8 receivers) never decoded it: no CJ, CM until they hung up; we
  left V.8 after `V8A_JM_MS` (5 s) and Phase 2 failed after its 10 s
  (`P2N_ERR_INFO0`). 14 of 21 inverted-JM calls failed, 0 of 31 plain.
- **In simulation.** `test_v8ans` case 6: the library's own caller with
  `v8rx.jm_flip` 0 and WebTV's CM (V.34, V.32bis, V.22bis, V.23, V.21, no
  PCM, no access octet), x2 offered on mu-law: before, `V8A_RES_NO_CJ`
  after 5.000 s, 25 JMs, the caller never got JM (the production
  signature); 4 checks failing. `test_answer_x2` case 1b, the whole call
  (sim/v34peer over line34, its V.8 receiver strict): before, V.8 result 3,
  no data; 5 checks failing.
- **Fix (modem/v8ans.c, `V8A_JM`).** The sign flips at the end of a JM
  only while `t < V8A_X2_FLIP_MS` (2000 ms); a JM that begins later is
  sent with sign +. x2 (`v8ans_x2`) and the access octet are unchanged, so
  Phase 2 still offers x2. After: CJ 2.74 s after the JM began (13 JMs),
  V.34 28800 / 28800, no bit errors, the same rates as without x2.
- **x2 kept.** The x2 client answers well inside 2 s: the live USR x2
  client's CJ came 0.773 s after the JM began (live-x2 ppp1 and ppp2), the
  recorded corpus 0.606-0.641 s, the simulated callers 0.74 s. Non-x2
  callers that did take the inversion live were slow (WebTV 1.47-3.87 s);
  the plain JM after 2 s helps them too. The live x2 call (ppp1) replays
  byte-identical with the old and the new code. test_v8ans case 5
  (each JM's sign checked sample by sample), test_answer_x2, test_x2rec
  unchanged. Replay proof: 742 runs identical; in the x2 set, the 6
  recordings whose CM has no PCM all had CJ within 0.64 s of the JM.
- **Golden.** test_v8ans and test_answer_x2: one new line each (the new
  cases) and the check counts (50 -> 60, 518 -> 526). test_answer_x2's case
  1b, a whole call, runs only where TEST_SHORT is not set (not on ARM). Bench hashes 19
  identical.

## 0.7.0: V.34 Phase 3/4 time limits keep the whole round trip when the echo comes late (2026-11-25)

- **Cause (WebTV report, finding 2).** `v34dx.c` `ec_schedule`, with
  `rtd_has_io` (our own path's delay unknown, `io_delay` -1): at the start
  of the fast echo step it overwrote `cfg.rtd` with `rtd - ecd.delay - 8`,
  the far echo's lag past the near one. The same `cfg.rtd` sets the limits
  of `DX_P3_J` (600 ms + rtd + MD), `DX_P3_RX` (2800 ms + 2 rtd) and Phase 4
  (2500 ms + 3 rtd). Live (#21): 1569 -> 357, the J limit 645 ms, the
  caller's S at 649 ms: five retrains, call lost.
- **In simulation.** `sim/v34peer` gains `s_after_j_ms`: the call-side
  engine waits, silent, that long after it heard J before its S (the boxes:
  S 649-970 ms after our J began). `test_v34call`: line34 `io_delay` 1200
  (echo 150 ms late), `d_ca = d_ac = 184` (round trip 1569, as #21), our
  delay unknown, `s_after_j_ms` 470. Before: rtd 1569 -> 369, 4 retrains
  of ours, 3 of the caller's, no data in 25 s (5 checks failing).
- **Fix.** v34dx_t gains `far_rtd` (int32_t, in the 4-octet hole after
  `pw_res`: `sizeof (v34dx_t)` 23032 as before, v90m_mem_size 106320).
  `v34dx_init` sets it to `cfg.rtd`; `ec_schedule` takes our delay off it
  (as it did off `cfg.rtd`) and `far_on` and the far canceller's reference
  read it. `cfg.rtd` stays whole for the limits. After: data at 10.5 s,
  no retrain; `far_rtd` = rtd - echo delay - 8 and `far_on` as before.
  test_v34dx's far-echo lag check reads `far_rtd`.
- **Replay proof.** 2 recordings x 7 sets, .txt only (their .bits files
  are empty: these replays decide no bits, so the evidence is the trace):
  ab_cc_v34, our retrain from `DX_P3_J` 12.229 -> 12.262 s (its round
  trip 263 samples, all taken off before since the echo delay was longer:
  the limit + 33 ms); x1, our retrain from Phase 4 16.812 -> 16.911 s (the
  3-round-trip limit, + 99 ms). The same retrain in
  each, everything after it identical but for the count of transmit
  samples that differ from the recording (the recordings were made with
  the old limits). Re-recorded: test/golden/replay.sha256 (14 lines).
- **Golden.** test_v34call: one new line (the case) and its count 296 ->
  304; every other program identical. Bench hashes 19 identical.
## release 0.6.2-rc1: the WebTV fixes from the HST work, without HST (2026-11-25)

Branch `v062` from v0.6.1 (e453519). Backported, without anything HST or
MNP:

- the opt-in V.34 transmit cap (hst-b 8d424fd, 3807ac9, 418610e, as one
  commit): `V90M_OPT_V34_TX_CAP` (0x10000 in `v90m_cfg.modulations`),
  answering only, off by default. Its status is not in `v90m_stats`
  (which keeps its size within 0.6) but in a new function and struct, as
  `v90m_get_diag`: `void v90m_get_v34_cap(const v90m_t *m, v90m_v34_cap
  *c)`, `v90m_v34_cap` = `{ int proj_bps; int ask_bps; int capped; }`.
  hst-b's status read the ask 0 after an orderly end (the cleardown's MP
  exchange clears `dx.got_far_mp`; the caller's cleardown MP has no
  rate): `v34ans_t.far_ask` (a byte beside `from_v90`, now `uint8_t`)
  keeps the ask data mode last began with. test_api runs box B's shape
  through the API (a simulated caller on a mu-law line, INFO1c 28800, MP
  31200): off 31200 capped 0, on 28800 capped 1, kept after our
  cleardown, 0 after `v90m_reset`;
- x2's inverted JM only for the first 2 s (hst-c 8f7b5df, d7debbe);
- the V.34 Phase 3/4 limits keep the whole round trip (hst-c 10f3e83):
  the replay manifest changes in 12 lines, ab_cc_v34 and x1 in each of
  the 6 sets (14 on hst, which has a 7th set), the same hashes as hst-c's;
  the traces differ only in the retrain's time (12.229 -> 12.262 s,
  16.812 -> 16.911 s) and the counts of differing samples;
- `v90m_ec_tx_busy` up to the closing flag's last bit (hst 6c7e118's
  v42 part); hst's MNP mode-3 test becomes test_v42 release_busy (LAPM).

`v90m_mem_size()` 106320 (x86-64), `V90M_MEM_SIZE` unchanged; the
hst-c `far_rtd` uses the padding hole before `sdet`.

Gates (the owner: an rc now, x86-64 only): `make golden` ALL HOST CHECKS
PASS, 142 programs identical; `tools/replay-proof.sh check` 636 runs
identical; `tools/bench-hashes.sh check` 17 identical; SONAME
`libv90modem.so.0.6`, `v90m_get_v34_cap` exported. Not run: `make verify`, `make verify-phase` (ARMv5, ports,
riscv64, Windows, costs), check-arm; for the final 0.6.2 or 0.7.0.


## hst: 0.6.2-rc1 merged in, one interface for the cap's status (2026-11-25)

`v062` (v0.6.2-rc1, c566c65) merged into hst. The same fixes had landed
on both (the cap, x2's JM, the Phase 3/4 limits, tx_busy); hst's code is
kept, with v062's API and its fix:

- the cap's status is `v90m_get_v34_cap` / `v90m_v34_cap` only:
  `v90m_stats.v34_proj_bps`, `v34_ask_bps`, `v34_capped` (hst-b 418610e)
  are removed, so 0.7.0 has one interface; `v90m_stats` keeps
  `reversals` as its only 0.7.0 field. test_api checks the function
  where it checked the fields, and runs v062's box B case;
- `v34ans_t.far_ask` (the ask kept after an orderly end; `from_v90` a
  `uint8_t`) and `answer_v34_tx_cap` reading it;
- the version is 0.6.2 (0.7.0 still unreleased; CHANGELOG: a 0.6.2-rc1
  section as v062's, and 0.7.0's says it includes 0.6.2);
- test_v42 release_busy (v062's LAPM form of hst's MNP tx_busy test)
  added beside hst's MNP test.

Golden: api/test_api only: v062's two box B lines (identical to v062's),
count 1094 -> 1128 (hst's 1094 less 15 checks of the removed fields,
1079, plus 49 of v062's: 8 + 3 + 6 replacing them, 32 in box B). No
printed line changed by the fields' removal. test_v42 5708 and call_loop
0.6.2 came in with the merge and match. `make golden` 158 programs
identical; replay manifest hst's (742 runs identical, no line changed);
bench hashes 19 identical; `v90m_mem_size()` 106320 (x86-64).

## hst: the final review's fix wave (2026-11-26)

- I3 (8bba11e): HST not offered under max_down_bps 1..4799; header and
  api.md say what HST does with max_down / max_up. test_answer_hst case
  35 max_down_low.
- Minors (165426a): m1 rc_late cleared at a switch of role (case 36);
  m2 the pump's request untaken behind our own 0x1R bounded at
  HST_RC_UNTAKEN_MS 3000 (case 37 retrain_untaken: failed at 3001 ms,
  END_RETRAIN_FAILED at 45 s; sim drop_ln); m4 nsym wraps; m5 rate-0
  guard; m6 hst_fmf_bm under HST_PROFILE_BM.
- Docs (a4e3d65): CHANGELOG 0.7.0 (ABI, behaviour, evidence, 0.6.2-rc1),
  api.md for every HST/MNP symbol, error-control.md host patterns,
  limits.md ledger 140 and 211.
- m7 (25af56d): test_answer_hst as api_test_answer_hst_1..6 (BinModem
  profile 1..3), none over 32 s on x86-64; bodies byte-identical to the
  old golden files.
- Throughput: while the caller holds the fast channel at 14400, MNP's
  window (k 8 x N401 64) against 195 ms LAs at 450 bit/s gives 867
  octets/s (726 at 300) of some 1640; caller window-blocked 65 %; LAs at
  k/2 +9 %; mnp.opt 3 (N401 256) 1728. Documented in limits.md; no code
  change.

Commands: `make golden` (165 programs identical, ALL HOST CHECKS PASS,
check-hst-bm included); `tools/replay-proof.sh check` 742 identical;
`tools/bench-hashes.sh check` 19 identical. x86-64 only.

## release 0.7.0: HST answering, MNP, and #5 from main (2026-11-26)

The owner: pushed now, the all-platform verification left to GitHub CI;
live testing of HST against a real USR Courier comes after the release,
its fixes into 0.7.1.

- Merge of origin/main (9ef5089): v0.6.1 + zagdrath's #5 (v34train: the
  caller's S after a silence counts a symbol alone or as a weak/strong
  pair; test_sres, the fixture usrc1 and its golden, test_orig_v34's
  golden) + 56e0ce6 (main's manifest). hst never touched those files, so
  main's goldens stand (`make golden` 165 identical on the merge). The
  manifest, re-recorded over hst's 7 sets: 259 lines changed, all #5's:
  main's 231 lines with main's hashes in the six shared sets (15
  recordings that now take S at its start, in every set; the V.92 sets'
  diagnostics of 20 more), and 28 in the hst set (the same 15, the same
  files as base). CHANGELOG: #5's "Unreleased" entry is 0.7.0's Fixed,
  crediting zagdrath; the first live connection with a Courier
  V.Everything (V.90 49333 / 26400 through an AudioCodes MP-124E).
- Version 0.7.0 in the Makefile, the header (0, 7, 0), test_api (700),
  README, docs/api.md, getting-started.md (`.so.0.7.0`, SONAME
  `.so.0.7`), testing.md and call_loop's golden; the name tables'
  stability now reads "within 0.7.x" (header, api.md).
- The final re-review's minors: "opt bit 0 (value 1)" for parameter 8's
  256-octet bit everywhere (include/v90modem.h, v42/mnp.h, v42/mnp.c,
  api/v90modem_fields.h; docs/api.md already said so); CHANGELOG's
  behaviour list names the 3 s bound on a request behind our own 0x1R;
  docs/limits.md's overlong line rewrapped; test_answer_hst case 38
  `max_down_4800` (in part 6): max_down_bps exactly 4800 still offers
  HST, rates 0x04, opens at 9600 and retrains to 4800 (part 6's golden:
  that case's 8 lines and the count, 553 -> 570; part 6 25 s on x86-64).
- CHANGELOG 0.7.0 (2026-11-26): includes 0.6.2-rc1; no real Courier has
  called HST yet; the all-platform verification is GitHub CI's (the local
  port matrix, riscv64 and the CPU costs not run for this release).

`v90m_mem_size()` 106320, `v90m_ec_mem_size()` 50448 (x86-64).

## release 0.7.1

- Version 0.7.1 in the Makefile, the header (0, 7, 1), test_api (701),
  README, docs/api.md, getting-started.md (`.so.0.7.1`, SONAME unchanged
  `.so.0.7`: 0.7.1 is ABI-additive), testing.md and call_loop's golden.
- CHANGELOG 0.7.1 (2026-11-26): v90m_get_restarts, v90m_restarts,
  v90m_cause and v90m_cause_name; sizes unchanged on every pinned target;
  no rebuild needed for 0.7.0 builds.

## 0.7.2 (unreleased): K56flex versions offered, version 1 declined and reported (2026-11-26)

From the analysis "K56flex version 1 differences" and the WebTV firmware
findings (section 4): the version octet of our CL's K56flex block is a
version mask, and the early WebTV Plus (version 1 alone) dropped our 0x42
(version 2 alone) without a word. Design owner-approved; facts in
`docs/history/k56-facts.md`, "Versions" (ruling V1).

- `modem/v8bismsg`: `v8bis_cl_octets` takes the versions (9-octet block,
  `o[7]` = 0x40 | vers; 0x43 FCS `ff 0a`); `v8bis_ms_version`,
  `v8bis_version_of` (the highest of bits 0..4; 0x20, 0x40 not versions).
- `modem/v8bis`: `v8bis_init(.., vers, accept_v1)`; an MS that selects
  version 1 alone, with version 1 offered among others and accept_v1 clear,
  gets NAK(3) 180 ms after it (V.8bis 9.5) and `V8B_RES_DECLINED`; V.8
  follows from its own silence as for no MS. Every other MS with the block:
  ACK(1), as before (0x02, SM56's echo 0x03, no version bit).
- `answer_t.k56_agreed` became `answer_t.k56`, the same octet as flags
  (agreed, CL sent, MS with the block, declined, the chosen version in bits
  4..6; `ANS_K56_AGREED_OF`): no size change. `ANS_MODE_K56_V2_ONLY`,
  `ANS_MODE_K56_V1`.
- API: `V90M_OPT_K56_V2_ONLY` (0x20000), `V90M_OPT_K56_V1` (0x40000,
  experimental), `v90m_get_k56` / `v90m_k56` (offered, exchanged, chosen,
  declined, agreed); compile-time checks on the octet's layout and the
  option bits. `v90m_mem_size` 106320 (x86-64, gcc and clang), 106016
  (armv5 uclibc, armv7hf musl), 105860 (i686): 0.7.1's. test_api under
  qemu-arm-static on both ARM toolchains: the size checks and the new
  version table pass.
- sim/k56call: SM56 now echoes our CL's version octet less 0x40 (was the
  fixed 0x21 of D-Modem's CL; same frame length); `cfg.vers`, a Rockwell
  controller's mask, written in the sim (the highest common version; none:
  the CL dropped, no MS); `cl_ver`, `ms_ver`, `got_nak3`.
- Tests: test_v8bismsg (0x43, its FCS, SM56's test on it, 0x41 failing it,
  the version of an octet, the MS's octet); test_v8bis (NAK(3) to 0x01 and
  0x21, ACK(1) with accept_v1, to 0x02 and 0x40, and with 0x42 to 0x21 and
  0x01); test_answer_k56 `k56_version_tests` (a) to (d) below; test_api
  `test_k56_versions` (seven client / option pairs through the API, kept
  to the end, gone at reset, calling and NULL) and the option bits.
  - (a) a client of versions 1 and 2: 0x42 and 0x43 both select 2, K56flex
    32000 / 31200, 0 bad each way, no training octet wrong; with 0x43
    data mode comes 0.220 s later on that simulated line (our ANSam 3 to 4
    ms later from the longer CL, the rest from V.8's and Phase 2's
    exchanges falling later). SM56 against 0x43: MS 0x03, agreed.
  - (b) version 1 alone against 0x43: MS 0x01, NAK(3), not agreed, V.34
    28800 / 28800 as without K56flex; k56 0x1E (chosen 1, declined).
  - (c) the same with `ANS_MODE_K56_V1`: ACK(1), our start-up runs to
    data, K56flex 32000 / 31200, 0 bad (the simulated pump is version 2's:
    this pins only that the path is taken).
  - (d) version 1 alone against 0x42: the CL dropped, no MS, V.34 28800
    (the WebTV's call before 0.7.2); SM56 against 0x42: MS 0x02, agreed.
- The tests written before 0.7.2 (test_answer_k56's k_call / kd_call,
  test_api's three K56flex calls) run with 0.7.1's CL: with 0x43 every
  later sample of their calls moves (one more stuffed bit), their golden
  outputs with it, and one Review Focus 3 case lost a rate step (28800 to
  26400 down on the simulated line, noise drawn per sample). Benches
  (tools/rec_k56, rig/bench_v90 -k) likewise: the bench hashes are 0.7.1's.
- Replay: `v90m-replay -K` (0.7.1's CL) and `-1`; replay-proof's k56 sets
  use `-K` (and `-k` for a `diff` REF from before 0.7.2). None of the 106
  recordings has a CRd (`make crd-scan`: 0 detections; every k56 trace
  reads V.8bis result 2), so our replayed V.8bis never sends a CL: the
  proof is identical with `-k` too (`RP_K=-k`, 742 identical). The
  fixtures offer no K56flex. v90m-fields never offers it.
- SM56 (D-Modem's half_sm56, `make test-k56-sm56 MODS=k56`) against 0x43:
  K56flex 52000 / 31200, LAPM, 4096 octets each way identical.

Gates (x86-64): make golden 162 of 166 identical before recording, the
four that differ only by the new tests' lines and check counts
(test_v8bismsg, test_v8bis, test_answer_k56, api/test_api; none prints our
CL otherwise); replay proof 742 identical; bench hashes 19 identical;
check-hst-bm pass; check-fixed pass.
