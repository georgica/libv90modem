# The 0.6.0 audit: figures and results

Where the audit's measurements go (spec D19): the safety net's timings,
the baseline cost counts and every Phase 3 task's before and after, the
V.92 digital-side audit, the x2-with-V.42 verdict.

## Safety net (Phase 0)

`make verify` (tools/verify.sh, per commit) and `make verify-phase`
(tools/verify-phase.sh), on the 16-thread development machine, 2026-11-22.

### make verify: about 6.3 min, the big-endian suite included

Measured at the Task 4 fix-round commit (the run `build/verify-r2.txt` of
that tree): `VERIFY PASS (378 s)`. Its stages run at once; the host,
armv7hf and big-endian ones set the time:

| stage | time | line |
|---|---|---|
| host | 378 s | golden: 123 programs identical; split: 4 programs, cases concatenate to the whole |
| replay | 13 s | replay proof: 636 runs identical |
| bench | 1 s | bench hashes: 17 identical |
| ubsan | 253 s | ALL UBSAN CHECKS PASS (the fixtures too) |
| armv7hf | 370 s | 8 of 210 runs skipped (tools/ports-slow.txt); short-cmp: 113 programs identical |
| bigend | 376 s | aarch64be-musl, the same short set: 8 of 210 runs skipped; short-cmp: 113 programs identical |
| fixed | 2 s | fixed: 90 objects, 0 float helpers, allocation only in api/v90modem.o; symbols: only v90m_* (627 internal) |

Before the big-endian stage and the split-check in the host stage (Task 4's
first commit, same machine): 224 s. The run without the four split
programs (still in `tools/ports-slow.txt`) took 190 s; with them, 228 s
(D13: under 300 s, so they left the list; their cases run in parallel).
The controller accepted the 6-7 min per commit for the per-commit
cross-ABI proof. The armv7hf short suite in full took 3:44 (before the
split, 4:32).

### make verify-phase: measured at the release proof

The port matrix and the costs run only at the end of the plan (the
release proof), and once more only if a phase touches portability-sensitive
code. The one run so far, at Task 4's first commit, with every target's
whole suite and the stages one after another, took 22468 s (6 h 14 min;
not the hour the spec estimated): ports 14360 s, costs 6588 s, armv5
1274 s, armv7hf 214 s, benches 31 s, crd 1 s; `VERIFY-PHASE PASS`. Since
then (Task 4 fix round 1) the costs run beside the other stages without
the per-tick counts (`COST_TICKS=0`), and the port matrix runs the whole
suites on host gcc, armv5, s390x and riscv64 and QUICK on the rest (m68k
reported only until Task 5b; since then gating, below), slowest targets first, 2 targets at once with 8 programs
each. That configuration's time is to be measured at the release proof
and recorded here.

### m68k: a GCC wrong-code bug; gating again (Task 5b)

m68k-uclibc's twelve failing programs (all on `modem/qamrx`) were put down
to qemu-m68k at ports-2. The audit found GCC 12.3.0 for m68k at fault
(with the frame pointer omitted it reads an incoming argument 8 bytes off
around a 64-bit division libcall), in two places: `qamrx_init`'s `dcar`
(the twelve failures) and `v90arx_clock`'s `ppm_q8` (the answering V.90
clock a value that depends on the binary's link layout on m68k, which no
test saw). Both
expressions are rewritten to forms gcc compiles right, with the same value
on every input (checked for all 2^32); an asm scan of the whole tree at
-O1/-O2/-Os/-O3 finds no other site. The matrix builds m68k with
`-fno-omit-frame-pointer` too, and `make verify-phase` gates on m68k like
the other targets (a target that did not run fails the gate). Details:
`docs/history/PROGRESS.md`, "m68k: a GCC wrong-code bug, not qemu".

| check | before | after |
|---|---|---|
| test_qamrx, m68k, no extra flag | 17 of 18 checks fail | 0 failures, output identical to host gcc |
| v90arx_clock(ppm_q8), m68k | 4290672329 for every ppm | the host's values |
| m68k QUICK suite (check-ports.sh) | 12 programs differ | exit 0, 203 of 211 programs identical to host gcc, 8 skipped (QUICK), 8 lines with uClibc's printf rounding |

## Baseline (v0.5.0 code)

`build/costs.txt` of that `make verify-phase` run (armv5 uClibc compiler,
`ARM_CC=$A5`, gcc -O2; ARM instructions counted under qemu, per second of
line in data mode), in M instructions per second; compared with the dsp
audit's counts ("How it was measured") and the README's table.

| | now | dsp audit | README |
|---|---|---|---|
| x2 answering (`cost-x2`, second 15 to 16) | 82.57 | 82.46 | 81.9 (32000) |
| V.90 answering (`cost-x2`'s, second 15 to 16) | 39.20 | 39.04 | 38.9 |
| K56flex answering (`cost-k56`, second 20 to 21) | 39.27 | | 39.0 |
| V.34 answering (`cost-v34`, second 10 to 11) | 40.53 | | |
| V.90 downstream receiver (`cost-v90a`), 0 / 100 ppm | 44.62 / 48.97 | | |
| V.90 calling (`cost-v90c`), 0 / 100 ppm | 55.11 / 59.31 | | 54.6 / 58.8 |
| V.92 calling (`cost-v92c`), 0 / 100 ppm | 69.95 / 74.32 | 74.00 (100 ppm) | 69.4 / 73.9 |
| Bell 103 / Bell 212A calling (`cost-bell`, first second) | 9.86 / 4.43 | | 9.84 / 4.43 |

Every figure is the same as the earlier ones or less than 1 % above them
(x2 +0.1 % on the dsp audit's, V.90 +0.4 %, V.92 calling at 100 ppm
+0.4 %; V.90 calling +0.9 % on the README's);
where the difference comes from (code since those measurements, or the
counting) was not looked into. These are the figures Phase 3 measures
against.

The slow-tick lines of `cost-v90c` and `cost-v92c` count the ticks that
were slowest on the host (`bench_v90c -w`), which a loaded machine picks
differently from run to run; they are not part of the baseline, and
verify-phase no longer counts them (`COST_TICKS=0`).

## V.92 digital side (Task 6)

Read-only audit of the answering modem's V.92 path: `v92/v92_p34d.{c,h}`,
`v92/v92_dec.{c,h}`, `v92/v92_design.{c,h}`, `v92/v92_info.{c,h}`, the
parts of `v92/v92_rx.c` and `v92/v92_seq.c` they call with received data
(the Ru/Su detector, the training receiver, the Ja / CPt / CPu / SUVu
parsers), their tests, and `modem/answer.c`'s V.92 phases (`start_v92`,
`ANS_P3X` / `ANS_V92`, `answer_cleardown`, `answer_snr_db10`). The calling
audit left these files out (`build/audit/calling.md`, "v92/ coverage").
Tree 29b5f2e. Experiments in a scratch copy (`git archive HEAD` into
`build/scratch-v92d/`, not in the repository), clang UBSan
(`-fsanitize=undefined`): `test_answer_v92`, `test_v92real`, `test_p2_v92`
and the `v92/test` programs pass with no report (231, 19, 103 checks;
dec 368, design 198, e2e 258, info 201, rx 25463, seq 528). The
reproducers below are scratch programs built from `test/test_v92call.c`
and `test/test_answer_v92.c` (two simulated knobs added to the scratch
`sim/v92a`: the caller misses its first n CPds; the caller stops hearing
us), plus direct drivers of `v92_dsn_*`, `v92_trn_*` and `v92_p6_tick`.

### Summary

1. **Our cleardown becomes a retrain when the caller does not answer Rd**
   (Important, high, reproduced). `v92d_cleardown` runs a whole rate
   renegotiation; if the caller's Ru-bar never comes, `V92D_RENEG` times
   out into `retrain(p, 1)` and `answer.c` starts Phase 2 again. In the
   end-to-end run the host's hang-up gave `ANS_RETRAIN` 8 s later and
   `ANS_FAILED` with `cleared = 0` 18 s later; a caller that is still there
   could even be trained back into data mode. (T21.1)
2. **Repeated CPds go out back to back** (Important, medium-high,
   reproduced). Once the repeat of 9.6.1.1.3 starts, CPd' follows CPd'
   with no SUVd between, until an acknowledge comes; Figure 14 draws SUVd
   between copies and a real Agere engine cleared down on back-to-back
   CPds (BinModem). (T21.2)
3. **Signed overflow on received samples near full scale, and in the CPd
   design on a hostile channel estimate** (Important, high, reproduced
   under UBSan). `v92_p6_tick` multiplies int16 samples in int arithmetic
   (`v92_rx.c:22-27`); a caller at about 30000 peak trips it in Phase 3.
   The design's conjugate gradients overflow int32 / int64
   (`v92_design.c:190, 215, 216, 221, 260`) on a channel estimate with a
   long low-pass tail, which the caller's training signal controls.
   (T21.3, T21.4)
4. **Our quality-driven rate drop counts every earlier drop twice**
   (Important, high by reading). The ceiling is the rate now in use, which
   already carries the earlier drops, less 4000 times all drops: 4000,
   8000, 12000 bit/s down in turn, and PCM upstream is switched off for the
   rest of the call after the fourth drop from 48000. The path has no
   test (`v92/README.md` says so). (T21.5)
5. **The upstream decoder has no gain margin** (Important; high that it
   is fragile, low how many callers it hits). A caller whose data mode
   level is 15 % (1.2 dB) off the level the CPd design assumed fails B1u
   (`V92D_ERR_B1`) and the call falls back to V.90 after a retrain. Real
   implementations disagree on the reference of G by up to 9 dB
   (BinModem). (T21.7)

Counts: Bugs 3 Important (T21.1, T21.2, T21.5), 4 Minor; Robustness 4
Important (T21.3, T21.4, T21.6, T21.7), 2 Minor (R-7 lists what holds);
Refactor 4 Minor; Performance 2 Minor; plus the BinModem leads (two Minor
items, the class offset and the epsilon sign) and the test gaps.

### Bugs / correctness

**T21.1 Cleardown that the caller does not answer turns into a retrain**
(Important, high).
- Where: `v92/v92_p34d.c:816-822` (`v92d_cleardown`: `clearing = 1`, then a
  rate renegotiation of ours), `:785-801` (`V92D_RENEG` waits for the
  caller's Ru-bar; `p->t > MS(T_RENEG_MS)` gives `retrain(p, 1)`),
  `:696-712` (in `V92D_PH4` the drn = 0 CPd waits for a fresh design on 600
  symbols of TRN2u, `:700` a refused design is `V92D_ERR_DESIGN`),
  `modem/answer.c:478-492` (any `V92D_RETRAIN`, and any `V92D_FAILED` other
  than CLEARDOWN / HANGUP / CARRIER, starts a retrain).
- What happens: the host asked to hang up; if the caller misses Rd, or the
  new design is refused, or the caller vanishes, the call retrains or ends
  as lost instead of `V90M_END_CLEARDOWN_OURS`. V.90 (`modem/p34d.c:536-540,
  606-617`) sends Rd, TRN2d and four MPs with rate 0 whatever the caller
  does, then ends with `P34_ERR_HANGUP`.
- Reproduced: `test_answer_v92`'s first scenario, 1 s into data mode the
  caller's input replaced by the idle octet, then `answer_cleardown`:
  phase `ANS_V92` -> `ANS_RETRAIN` at +8.0 s -> `ANS_P2` -> `ANS_FAILED` at
  +18.1 s, `cleared = 0`, `retrains = 1`. With the caller hearing us:
  `ANS_FAILED`, `cleared = 2`, after 2.33 s. In `v92d` alone (scratch
  `test_v92call`): `V92D_RENEG` -> `V92D_RETRAIN` 8 s after the request.
- Also: `answer.h:216-218` and `docs/api.md` promise `ANS_FAILED` "within a
  fraction of a second"; V.92 takes 2.3 s when all goes well (Rd, the
  caller's Ru, 600 TRN2u symbols, a paced design, four CPds).
- Fix shape: while `clearing`, never retrain and never wait for a design:
  CPd with drn = 0 built from the CPd in use (`p->cpd`), sent after Rd /
  Rd-bar / TRN2d whether or not Ru-bar came, then `V92D_ERR_HANGUP` after
  `CLEAR_CPDS`; a time limit inside `V92D_RENEG` / `V92D_PH4` that ends
  with HANGUP, not a retrain. Signal path untouched; the replay proof does
  not reach cleardown.
- Test that fails today: `test_answer_v92`: `answer_cleardown` in V.92
  data mode while the caller no longer hears us ends in `ANS_FAILED` with
  `cleared = 2` within 2 s and no retrain (today `ANS_RETRAIN` at +8 s,
  `cleared = 0`).

**T21.2 Repeated CPd (9.6.1.1.3) is sent back to back** (Important,
medium-high).
- Where: `v92/v92_p34d.c:742-743` (the repeat starts 100 ms + rtd + 150 ms
  after the first CPd), `:745-748` (`is_cpd`: with `cpd_repeat` set and no
  acknowledge, `sel(p, cpd)` again; SUVd only once `got_ack`).
- Reproduced (scratch `test_v92call`, caller misses its first n CPds,
  transmit signal traced): n = 2: SUVd, CPd, SUVd' 256 ms, then 2 CPd' in
  a row; n = 4: 4 CPd' in a row, then Ed. The simulated caller takes them;
  BinModem's bench says an Agere engine left its SUVd flag at zero and
  cleared down on back-to-back CPds (134 to 200 sent), Figure 14 draws CPd
  then SUVd until CPu', and the Agere needs 2 to 4 s of SUVd to acquire.
  The real modems we met acknowledged the first CPd (USR 5686 9 of 9), so
  the path has not run on a real line.
- Fix shape: after each repeated CPd, SUVd (with the acknowledge bit when
  CPu is in) for the repeat interval, then one CPd again. Phase 4 only; no
  replay recording reaches it.
- Test that fails today: `test_v92call`: a caller that misses its first two
  CPds still reaches data mode, and between any two CPd / CPd' the
  answerer sends at least one SUVd' (today 2 CPd' in a row).

**T21.5 The rate drop of data mode double-counts earlier drops**
(Important, high by reading).
- Where: `v92/v92_p34d.c:773-781` (`rate_drops++`; retrain with `up_floor`
  when `up_rate - 4000 * rate_drops < 24000`), `:402-405` (design ceiling
  `up_rate - 4000 * rate_drops`). `up_rate` is the rate in use
  (`upstream_start`, `:123`), which already reflects the earlier drops.
- Arithmetic from 48000: ceilings 44000, then (44000 - 8000) 36000, then
  24000, then the fourth drop retrains with `up_floor = 1`, and
  `answer.c:491` takes PCM upstream away for the rest of the call. Meant
  (and what `p34d`'s `rate_down` does, `modem/p34d.c:265-276`): one step
  below the rate in use each time. `rate_drops` is also never cleared by a
  caller's renegotiation to a higher rate.
- Not covered by any test (`v92/README.md:545`: "The automatic path is not
  covered by a test").
- Test that fails today: `test_v92call`: upstream noise raised in data mode
  in two steps; each renegotiation of ours lowers the CPd ceiling 4000
  bit/s below the rate then in use (today the second asks for 8000 less).

**B-4. UINFO 112..127 ends the call instead of falling back** (Minor,
high). INFO1a's 7-bit Ucode (Table 18 bits 25:31) above 111 makes
`v90tx_init` refuse (`v90/v90_tx.c:440`), `v92d_init` fails
(`v92_p34d.c:83`), and `start_v92` does `fail(a)` (`answer.c:220`): the
call ends FAILED where a retrain with PCM upstream off (as for every other
V.92 start-up failure, `answer.c:491`) would keep it.

**B-5. No DIL, CPt before Ri** (Minor, low). `on_cpt` with no DIL selects
Ri-bar even when Ri was never sent (`v92_p34d.c:172-173`), so a caller that
sends CPt early (the Figure 11 reading) sees SCR then Ri-bar. And in the
few symbols before that Ri-bar starts, `V92D_TRAIN`'s "trained, send Ri"
(`:676-679`, `sig == V90TX_SCR`) can overwrite the pending Ri-bar
(`v90tx_select` replaces `pending`), leaving Ri on the line and the caller
waiting for Ri-bar until `T_TRAIN_MS`. Fix: `ri_sent`-guarded, Ri-bar only
after an Ri of at least a frame.

**B-6. E2u is looked for after any complete sequence** (Minor, high;
no effect seen). `seq_bit` arms the E1u / E2u counter after every parsed
sequence (`v92_p34d.c:246`), including an SUVu without the acknowledge;
`v92/README.md` reading 36 says only after the acknowledge. Harmless while
a sequence is always followed by the next frame sync's ones; make the code
and the reading agree.

**B-7. Cleardown refused outside plain data mode** (Minor, high).
`v92d_cleardown` returns -1 during a rate renegotiation and in Phases 3/4
(`:818`), so `v90m_cleardown` gives `V90M_ESTATE` and the host just stops:
the caller is not told. Same in `p34d`. With T21.1's fix a cleardown can
also start from `V92D_RENEG` / `V92D_PH4` after data mode was reached.

### Robustness

**T21.3 `v92_p6_tick` overflows int on loud samples** (Important, high,
reproduced).
- Where: `v92/v92_rx.c:17-27`: `X(k)` is `int32_t`, so
  `X(0) * X(6) - X(48) * X(54)` and its `2 *` are int arithmetic; with |x|
  above about 23170 the `2 *` products (in `c48`, `a48`, `c6`) exceed
  INT32_MAX before reaching the int64 accumulators. The sums of squares in
  `e48` / `e3` overflow only at -32768 (review correction).
- Reached by: Phase 3 (`V92D_WAIT_RU`, `JD`, `JP`), data mode and rate
  renegotiation in `v92d` (`v92_p34d.c:578, 642, 662, 770, 789`), on the
  echo-cancelled input; also the calling side's Sd detector
  (`modem/v90arx.c:763`), which the calling audit did not flag.
- Reproduced: scratch `test_v92call` with the simulated caller at level
  30000: UBSan `v92_rx.c:22:17: signed integer overflow: 2 * -1428553728`,
  in all eight variants tried (echo, gain, noise). With `X(k)` widened to
  int64 in the scratch copy the same runs end in an ordinary refusal
  (`V92D_ERR_DESIGN` / `V92D_ERR_CPT`) with no further report.
- Fix: `X(k)` as `int64_t`. Same values whenever nothing overflowed; the
  replay proof checks the recordings.
- Test that fails today (check-ubsan): `v92/test_rx`: a full-scale Ru
  the detector accepts (32767, and -32768 samples) through `v92_p6_tick`
  raises no UBSan report; detection is checked as well.
- Fixed in Task 21 (T21.3): `X(k)` as `int64_t`; `test_p6_full` in
  `v92/test/test_rx.c`.

**T21.4 The CPd design overflows on a long low-pass channel estimate**
(Important, high, reproduced).
- Where: `v92/v92_design.c:215-216` (`cw += (al * cp) >> 18`, `cr -= ...`
  in int32), `:221` (`cp = cr + (be * cp >> 18)`), `:190`
  (`(int32_t)(acc / 16384) + ...`), `:260` (`isi * 65536` in int64). The
  step sizes are clamped (`al` at 2^34, `be` at 2^22) but the vectors are
  not, and the iteration can diverge on an ill-conditioned channel.
- The channel is `v92_trn_h()` of the caller's own TRN1u / TRN2u: a broken
  or hostile caller chooses it.
- Reproduced (scratch driver of `v92_dsn_start` / `v92_dsn_step`, noise
  9, quantiser 3, echo 400 RMS, residual 600, Ltot 256, Lmax 192): h a
  ramp from 10^6 at tap 30 down to 0 over 40 taps: overflow at `:221`, end
  `V92_DSN_ERR_RATE` after 136 steps; ramp from tap 40 to the end: `:216`,
  `:221`, `:260`. A random fuzz of 400 configurations (six channel shapes)
  hit `:190`, `:215`, `:216`, `:221`, `:260`, every time on the ramp shape.
  No run was unbounded (the fuzz: at most 5360 steps; the ramps of R-6:
  up to 33865).
- Fix shape: saturate `cw` / `cr` / `cp` (or restart the iteration when a
  norm grows past a bound, and refuse the channel after a second time), and
  widen or pre-shift `isi` at `:260`. Outputs of designs that never came
  near the bounds stay the same; `test_design` and `test_v92call` print
  the designs and serve as the proof.
- Test that fails today (check-ubsan): `v92/test_design`: the 40-tap ramp
  channel above gives an end code (a design or a refusal) with no UBSan
  report.
- Fixed in Task 21 (T21.4): every value stored back in 32 bits (`g`,
  `cq`, `cw`, `cr`, `cp`, `w`) and the step products `al p`, `al q` are
  checked; out of range, the design ends `V92_DSN_ERR_CHANNEL`. `isi_q28`
  is formed without `isi * 65536` and saturates. Both ramps now stop in
  the first iteration (`cq`, A'A b already past 2^31).
- Found while fixing T21.4 (the audit's fuzz, run 2442: peak in the last
  tap, ISI as large as the peak): `isi_sample()` skipped the feedback taps
  with `j += lp1 + 1` and read `g[j]` past `ng` (index 224 of 224) when
  the cursor sits that late. Fixed in Task 21: the loop stops there.

**T21.6 int32 sample clocks overflow after 74.6 h** (Important, high,
reproduced). `v92d_t.now` and `.t` (`v92_p34d.c:112`, incremented every
tick at `:469`, also after `V92D_FAILED`), `trn.k` and `data_k` in data
mode (`v92_rx.h:60`, `v92_p34d.c:157`). The answering audit (its R-3)
listed `p34d`, `v34dx`, `x2d`, `k56d` but not `v92d`; this extends that
item. Reproduced: `now = t = INT32_MAX - 4000` set on reaching data mode,
UBSan `v92_p34d.c:469:11: signed integer overflow`. Test that fails today
(check-ubsan): `test_v92call`: with `now`, `t` and `trn.k` set 4000
samples short of INT32_MAX in data mode, one more second of data runs
without a report and with no error in the data.

**T21.7 No gain margin in the upstream decoder** (Important; high that it
is fragile, low how many callers it hits).
- Where: the decoder slices on the CPd points as designed
  (`v92_dec.c:65-87`); `v92d` follows the baseline (`base_step`) but not
  the gain; B1u fails on more than 8 wrong bits (`v92_p34d.c:97`).
- Reproduced (scratch `test_v92call`, the simulated caller's data mode
  amplitude times `gain_err`; 38666 bit/s up): 1.00 data mode, 0 bad
  frames; 0.85 and 1.15, and every factor tried from 0.5 to 2.8:
  `V92D_ERR_B1` 3.5 s into the call. `answer.c:491` then retrains with PCM
  upstream off. The `gain_err` knob of `sim/v92a` exists, but no scenario
  of `test_v92call` uses it.
- Real-world: the USR 5686 and the CX93001 matched our reading of 8.8.3
  (reading 35: G v = 1 at the power of TRN1u). BinModem found two V.92
  servers that built for an upstream about 9 dB louder than its own
  reading: implementations differ on this reference.
- Fix shape: estimate the caller's gain on B1u before judging it (its
  power is already measured, `sig_ms`, and the design knows the power it
  expects, `dsn.power_q14`; or a decision-directed gain over the first
  data frames, as `b1u_lead` looks at the first one) and scale the input
  of `v92_dec_put`. A data-path change: the replay proof shows whether any
  recorded call moves.
- Test that fails today: `test_v92call`: a caller whose data mode is 1.4
  times (3 dB) the designed level reaches data mode with B1u right (today
  `V92D_ERR_B1`).

**R-5. Ja's rate mask is checked only by the design** (Minor, high).
`c.rates = p->ja.rates` (`v92_p34d.c:400`); a Ja with no rate (or none
under `max_up_rate`) is found out by `set_rate` after Phase 3, the
2400-symbol TRN2u refinement and the design: some seconds of Phase 4,
then `V92D_ERR_DESIGN` and a retrain. Checking at Ja (`V92D_ERR_JA`) ends
the same way sooner.

**R-6. Design time on odd channels** (Minor, medium). The design took
33000 steps on some channels in the scratch runs (`dsn_tail`), 8 s at
`V92_DESIGN_PACE` 2, where the USR 5686 waits 3.5 s from its SUVu for CPd
(reading 61). SUVd goes out at 2.5 s regardless; the caller then retrains.
Not a defect by itself; worth a cap on steps (refuse and retrain early).

**R-7. Holds** (checked). Every state of `v92d` has a limit: `WAIT_RU`
6 s + MD, `MD`, `TRN1U` 6 s + rtd, `SD` (TRN1d runs out by itself),
`JD` / `JP` 5.1 s + rtd, `TRAIN` 8 s + rtd, `PH4` / `ED` 20 s + 6 rtd (8 s
plus the pacing in a renegotiation), `RENEG` 8 s, data mode the carrier
watch (0.5 s) and the quality check; rtd from `p2d` is at most 2 s. The
received sequences are bounded and checked before use: Ja fits
`V92D_FBUF` (2752 bits; the longest Ja, 255 segments, is about 2700),
its CRC is checked before the DIL is loaded, and `v90tx_load_dil` checks
every field; CPt / CPu go through `v90_map_from_cp` before use; CPu's
constellation count is bounded before its CRC; INFO1a's fields are at
most their code ranges (`ltot`, `lmax`, `md_len` 0..127 -> at most 35052
samples of MD). The Viterbi metrics are bounded as the header says
(input clamped to 2^20, distance to 8191, metrics renormalised). The
training receiver (`v92_trn_tick`) took a quiet TRN1u turning loud after
the cursor was chosen, and full-scale noise, without a UBSan report
(scratch `trn_jump`).

### Refactor

- **F-1 (Minor).** `isqrt64` is copied in `v92_p34d.c:58`,
  `v92_design.c:7`, `modem/anstone.c`, `modem/v22rx.c`: one helper.
  Byte-identical by construction.
- **F-2 (Minor).** `V92D_PROBE` bench code (`v92_p34d.c:253-306`) sits in
  the product file; move it next to `live/replay.c`.
- **F-3 (Minor).** `V92D_PH4`'s sequencing of SUVd / CPd / acknowledge
  (`v92_p34d.c:690-755`) is the densest logic of the file and is where
  T21.1 and T21.2 land; a small function per concern (design, what to send
  next, time limit) before those fixes. Transmit side only; Phase 4 is not
  in the replay proof, the simulated calls are.
- **F-4 (Minor).** `v92d_renegotiate` has no product caller (`answer.c`
  and the API offer no renegotiation); tests use it. Keep it as test API or
  expose it through `v90m`.

### Performance

- **P-1 (Minor).** V.92 answering data mode has no ARM cost figure (the
  baseline above has V.92 calling only; `v92/README.md` gives an x86 ratio
  to `p34d`, 0.67). Per symbol `base_step` does up to two int64 divisions
  (`v92_p34d.c:375, 378`), library calls on the ARM926; TRN1d's tail
  update does two per sample (`:493-494`). A `cost-v92d` (rec + bench, as
  `cost-v92c`) belongs to Phase 3 before anything here is touched.
- **P-2 (Minor).** The design's cost is per step and paced; the slow
  channels of R-6 are the case to measure if the design is sped up (and
  then the TRN2d floor of the leads below matters).

### BinModem's V.92 findings against our digital side

From `~/modem-research/binmodem-findings.md` section 4 and 1.15 (facts of
another implementation's bench, checked against our code; not authority):

- **6.4.2 class index offset (Agere +2, D-Modem 0).** We decode the
  printed form only (`v92_map.c:31-38`, `v92_eta_to_k`); B1u of the USR
  5686 and the CX93001 decodes with it (`test_v92real`: 1512 bits, 0
  wrong). The Agere observation is of its digital side reading BinModem's
  upstream; whether Agere analogue modems encode with +2 is not known. If
  one does, B1u fails (`V92D_ERR_B1`) and the call goes on as V.90 after a
  retrain. Minor: a B1u-time check of both readings (as `b1u_lead` does for
  the lead frame) would cover it if a recording ever shows it.
- **Ri or CPt first (9.5.2.1.10 text vs Figure 11).** With no DIL we send
  Ri after 2400 equalised symbols of the second TRN1u
  (`v92_p34d.c:676-679`), so a caller of either reading moves on; with a
  DIL we send Ri on CPt (9.5.1.1.12), on which the readings agree. B-5 is
  the one rough edge.
- **TRN2d of at least 4800 symbols.** Our constants are 400 data frames
  (2400 T) at start-up and 48 (288 T, under the clause's 2040 T) in a
  renegotiation (`v92_p34d.c:15, 17`), but SUVd also waits for the design,
  which starts after 2400 (600 in a renegotiation) TRN2u symbols and
  takes thousands of steps: TRN2d is over 4800 T on every run seen (2.3 s
  and more at pace 2). A faster design (Phase 3) would expose the
  constants; set the floors to 800 frames and at least 340 (2040 T).
- **SUVd between repeated CPds.** Not done: T21.2.
- **S-bar-u epsilon sign (Table 22).** Our Jp asks for a fixed-sign
  fraction (`v92_p34d.c:649`, reading 25). We train the second TRN1u from
  scratch (`trn_at` re-arms `v92_trn_init`), so a caller that moves the
  other way leaves the channel peak off a sampling instant (more ISI, a
  lower design rate), not a failed CPt as on BinModem's receiver.
  `test_v92call` checks the peak lands within 56/256 T; logging
  `v92_trn_peak` after the second TRN1u on real calls would show a caller
  of the other sign. Minor, no switch needed yet.
- **A louder upstream wanted by real servers.** That is about what digital
  modems expect of analogue ones; for us it shows that the reference of G
  varies between implementations, and our decoder tolerates 15 % at most:
  T21.7.
- Also from 1.15: TRN1d longer than 2040 T (the Agere turned after 266
  ms): ours is 4800 T plus rtd and a guard (`TRN1D_SYMS`), holds. The
  first B1d / B1u data frame and the Su/Ru template: our B1d is
  `v90tx`'s (8.6.1 scrambled ones) and `v92_p6_tick` tells Ru from Su by
  timetable, not shape; hold.

### Test gaps that matter

- The quality-driven renegotiation and retrain of data mode
  (`v92_p34d.c:773-781`): no test (T21.5).
- The CPd repeat of 9.6.1.1.3: no scenario makes the caller miss a CPd
  (T21.2).
- Cleardown that the caller does not answer, and cleardown outside data
  mode (T21.1, B-7).
- A caller's data mode gain off the design (T21.7): the knob exists, no
  scenario.
- Full-scale input and hostile channel estimates under UBSan (T21.3,
  T21.4): `check-ubsan` runs only well-behaved simulated callers.
- Phase 3 failures other than "no Ru" (test 4 of `test_answer_v92`): no
  Su (9.5.1.2.2), no CPt (`T_TRAIN_MS`), refused design, refused UINFO
  (B-4); each ends in a retrain or a failure no test checks.
- No ARM cost run for V.92 answering (P-1).
