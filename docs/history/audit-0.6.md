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
reported only), slowest targets first, 2 targets at once with 8 programs
each. That configuration's time is to be measured at the release proof
and recorded here.

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
