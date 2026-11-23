# The 0.6.0 audit: figures and results

Where the audit's measurements go (spec D19): the safety net's timings,
the baseline cost counts and every Phase 3 task's before and after, the
V.92 digital-side audit, the x2-with-V.42 verdict.

## Safety net (Phase 0)

`make verify` (tools/verify.sh, per commit) and `make verify-phase`
(tools/verify-phase.sh, per phase), on the 16-thread development machine,
2026-11-22, at the commit that made them (Task 4).

| | wall time | result |
|---|---|---|
| `make verify` (warm build trees) | 228 s | `VERIFY PASS (228 s)` |
| `make verify`, first run (every stage's tree built from nothing) | 190 s | `VERIFY PASS (190 s)`, with the four split programs still in `tools/ports-slow.txt` |
| `make verify-phase` | 22468 s (6 h 14 min) | `VERIFY-PHASE PASS` |

`make verify`'s stages run at once; the armv7hf one is the longest and
sets the time:

| stage | time | line |
|---|---|---|
| host | 118 s | golden: 123 programs identical |
| replay | 10 s | replay proof: 636 runs identical |
| bench | 2 s | bench hashes: 17 identical |
| ubsan | 149 s | ALL UBSAN CHECKS PASS (the fixtures too) |
| armv7hf | 228 s | 8 of 210 runs skipped (tools/ports-slow.txt); short-cmp: 113 programs identical |
| fixed | 3 s | fixed: 90 objects, 0 float helpers, allocation only in api/v90modem.o; symbols: only v90m_* (627 internal) |

D13: the measured time is under 300 s, so no program moved into
`tools/ports-slow.txt`; the four split programs (test_p34, test_orig_v92,
test_orig_seeds, test_orig_bell, now 32, 14, 18 and 29 runs) left it, since
their cases run in parallel. The armv7hf short suite in full took 3:44
(before the split, 4:32); its longest run is now arm_test_orig_retrain (128 s).

`make verify-phase`, stage by stage:

| stage | time | line |
|---|---|---|
| armv5 | 1274 s | ALL ARM CHECKS PASS (the longest program, arm_test_orig_retrain, alone takes most of it) |
| ports | 14360 s | 16 targets identical to host-gcc; m68k (report only): 129 of 211 programs identical, 82 differ (the V.34 receiver under qemu-m68k, README "Ports") |
| armv7hf | 214 s | short-cmp: 121 programs identical |
| benches | 31 s | host = qemu hashes: 10 benches |
| costs | 6588 s | 34 lines in build/costs.txt |
| crd | 1 s | k56-crd-scan: 106 recordings, 0 CRd detections |

The spec expected about an hour. The port matrix (every target's whole
suite, `-DTEST_FULL`, under qemu) takes four hours: i686-musl (qemu-i386),
armv5-uclibc and m68k-uclibc run the long calling-side programs for tens of
minutes each. The costs take nearly two: `cost-v90c` and `cost-v92c` count
every instruction of each of their slow ticks in a qemu run of its own.

## Baseline (v0.5.0 code)

`build/costs.txt` of the `make verify-phase` above (armv5 uClibc compiler,
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

Every figure is the same as or up to 0.9 % above the earlier ones (x2
+0.1 % on the dsp audit's, V.90 +0.4 %, V.92 calling at 100 ppm +0.4 %);
where the difference comes from (code since those measurements, or the
counting) was not looked into. These are the figures Phase 3 measures
against.

The slow-tick lines of `cost-v90c` and `cost-v92c` count the ticks that
were slowest on the host (`bench_v90c -w`), which a loaded machine picks
differently from run to run; they are not part of the baseline.
