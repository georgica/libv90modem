# Testing

The signal path is fixed point, so the same tests print the same output on
every target; the tests check that, and they check behaviour against
simulated lines, against independent peers where there are any, and
against recordings of real calls.

## `make check`

The host suite:

- every block's own test (`test/test_*.c`, linked with the sources), each
  printing `name: N checks, 0 failures`; then `ALL TESTS PASS`;
- `test/api/test_api.c`, the public API against the static library as it
  is installed (only `v90m_*` visible), with a simulated caller from the
  development archive;
- `examples/call_loop` (a caller and an answerer back to back through the
  public API);
- the real-modem fixtures through `v90m-replay` (below);
- `check-symbols`: the static library defines only `v90m_*` (its
  internals renamed `v90m__i_*`) and the compiler's helpers;
- the tests of the subdirectories `v34`, `v90`, `v92`, `v42` and `v32`.

It ends with `ALL HOST CHECKS PASS`. The simulated peers and channels are
in `sim/` (calling modems for V.8 / V.22bis, V.32bis, V.34, V.90 and V.92;
channels with loss, delay, echo, noise, clock and carrier offset; the PCM
downstream as an analogue modem's A/D has it) and `test/v90link.h` (a
whole line between the library's analogue and digital modems). They may
use knowledge a real modem does not have: they are a test reference, not
code to ship (`DESIGN.md`, 5).

## `make check-arm`

The same tests cross-built for ARM (`ARM_CC`, default an `arm-linux-gcc`
under `../toolchain`) and run under qemu-user (`QEMU`, default
`qemu-arm-static`), `PAR` (default 8) programs at a time
(`tools/par-run.sh`), then the API test and the subdirectories' tests.
It ends with `ALL ARM CHECKS PASS`.

```
make check-arm ARM_CC=/path/to/arm-linux-gcc QEMU=qemu-arm-static PAR=8
```

On 32-bit ARM some long tests run a shorter version (`TEST_SHORT`, set in
`test/test.h` unless `TEST_FULL` is defined), so their counts differ from
the host's.

## `make check-ubsan`

The host suite (and the subdirectories') built with clang
(`UBSAN_CC`) and `-fsanitize=undefined -fno-sanitize-recover=undefined`:
a signed overflow or a shift of a negative number is what lets the same C
compute differently on another target or compiler, and any of them fails
the run. It ends with `ALL UBSAN CHECKS PASS`.

## The port matrix: `tools/check-ports.sh`

```
tools/check-ports.sh [OUT [target ...]]
```

Builds the whole suite (what `make check-arm` runs: every test, the API
test, the subdirectories' tests) for each target, runs it there
(qemu-user, wine, or the host itself), and compares each test program's
output with the reference target's, host gcc, program by program.

Targets: host gcc (the reference) and host clang; x86_64, i686, armv5
(uClibc), armv7hf, aarch64, aarch64be, mips, mipsel, ppc, ppc64, riscv64
(musl); s390x (glibc); m68k (uClibc); win32 (mingw, under wine). Big- and
little-endian, 32- and 64-bit. The cross compilers are Bootlin's
"stable-2023.08-1" toolchains under `TOOLCHAINS` (default `../toolchains`);
a target whose compiler or runner is missing is reported and skipped. The
matrix builds every target with `-DTEST_FULL`, so the whole tests run
everywhere; 32-bit x86 (i686, win32) builds with `-msse2 -mfpmath=sse`.

**"Identical output"** means each program's output byte for byte, but for
the lines that measure the host and not the modem: CPU and wall time, and
the lines that print the size of structures in memory (pointer width,
alignment). One allowance: a C library whose `printf` rounds up where the
correct rounding is down (uClibc-ng, found by `tools/printf-probe.c` on the
target itself) may print such a number one unit larger in its last digit;
those lines are counted and reported, and nothing else may differ
(`tools/ports-cmp.awk`).

Settings (environment):

| | |
|---|---|
| `OUT` | per target its build tree, its log (`OUT/<target>.log`) and each program's output; default `build/ports`; not under /tmp (`TMPDIR` is set to `OUT/tmp`) |
| `PAR=4` | targets built and run at once |
| `JOBS=2` | `make -j` for each target |
| `QUICK="t1 t2 ..."` | targets that skip the slow programs of `tools/ports-slow.txt` |
| `SKIP="prog ..."` | programs no target but the reference runs |
| `COMPARE_ONLY=1` | compare what an earlier run left in `OUT`, run nothing |
| `TOOLCHAINS` | where the cross compilers are |

**m68k.** On m68k-uclibc under qemu-m68k twelve programs fail, every one
built on the V.34 receiver core (`modem/qamrx`). The evidence points at the
emulator, not the code: the same binary fails differently when qemu is
told to translate one instruction per block; `qamrx.c` alone at `-O1`
passes, and so does the same code built and run for the 68060; clang's
UBSan and MSan report nothing there; and the fifteen other targets print
what the host prints.
There is no m68k hardware here to settle it
(`docs/history/PROGRESS.md`, ports-2).

## Other checks

- `make test-interop SPANDSP=PREFIX`: V.8 and V.22bis, both roles, against
  spandsp (host only). spandsp is a test peer, never copied.
- `make bench-v90`: a simulated V.90 call replayed through the answering
  modem on the host and under qemu, by the internals and through the
  public API (static and shared library); the transmit hashes must be
  equal. `bench-v90c`, `bench-v92c` and `bench-bell` do the same for the
  calling side, `bench-v90a` for its downstream receiver. A change that
  should not alter what the modem sends shows the same hash
  (`CONTRIBUTING.md`).
- `cost-*` and `prof-v90c` / `prof-v92c`: ARM instruction counts per
  second of line and per tick, from qemu (slow). `bench-x2` and `cost-x2`
  do the same for an x2 call of the simulated client.
- `make test-x2-courier COURIER_EMU=DIR [PAD=0|3|6] [MODS=x2]
  [STRICT_RENEG=1]`: an emulated U.S. Robotics Courier (courier-emu, kept
  outside the repository) calls the answering modem (host only;
  `tools/x2-peer/README.md`). With x2 it passes when the Courier
  acknowledges our V.42 data at 32000; `STRICT_RENEG=1` also requires it
  after the Courier's rate change, which no server passes on the published
  emulator.

## Recorded calls and the replay tool

`v90m-replay` (`make replay`, installed by `make install`) plays a
recorded call into the answering modem and reports what it made of it,
phase by phase:

```
v90m-replay [-a] [-9] [-x] [-k] [-u BPS] [-d BPS] [-l DBM0] [-e SAMPLES] [-P CODE] RECORDING
```

| | |
|---|---|
| `-a` | A-law line (mu-law otherwise) |
| `-9` | V.92 offered (PCM upstream up to 48000 bit/s unless `-u`) |
| `-x` | x2 offered (`V90M_MOD_X2`) |
| `-k` | K56flex offered (`V90M_MOD_K56`): V.8bis first; the recording is lined up with our answer tone after it |
| `-u`, `-d BPS` | upstream / downstream rate ceilings |
| `-l DBM0` | transmit level (default -12) |
| `-e SAMPLES` | the echo delay, as the recording modem was told (default: measured) |
| `-P CODE` | INFO0d maximum power code, 1..31 (default 23) |

The modem must be set up as the recording modem was: what it hears holds
the echo of what that modem sent. The last line counts the samples where
its transmit differs from the recording's; 0 until data mode means it was
set up right. The recording format (companded, or linear receive with the
`V90DLIN1` header) is in `tools/README.md`.

**Fixtures.** `test/fixtures/` holds seven recordings of real calls to the
answering modem, each ending 10 to 20 ms after the modem connected (the
handshake, no user data). `test/fixtures/fixtures.txt` lists for each the
replay options it was recorded with, the expected connection, the modem
and the date; `test/fixtures/check.sh` replays them and `make check` runs
it:

| file | modem | connected (down / up) |
|---|---|---|
| `cxa.raw.xz` | Conexant CX93001 (USB), `AT+PIG=0` | V.92 50666 / 25333 |
| `v34a.raw.xz` | Conexant CX93001, V.34 forced | V.34 31200 / 28800 |
| `v21a.raw.xz` | Conexant CX93001, V.21 forced | V.21 300 / 300 |
| `usr7.raw.xz` | U.S. Robotics 5686, firmware 5.4.62 | V.92 54666 / 40000 |
| `usr6.raw.xz` | U.S. Robotics 5686, firmware 5.4.62 | V.92 54666 / 32000 |
| `x2r2.raw.xz` | U.S. Robotics 56K of 1998, flash 5.1.4 | V.90 54666 / 28800 |
| `x2b.raw.xz` | U.S. Robotics 56K of 1998, flash 5.1.4 | V.34 28800 / 28800 |

A fault found on a call with a real modem comes with its recording as a
fixture and a test that fails without the fix (`CONTRIBUTING.md`).

**The calling side and the recordings.** There is no real modem for the
calling side to call yet. `tools/v90m_fields.c` (`make build/v90m-fields`,
not installed) reads what the caller of a recorded call sent, field by
field, and `test/test_realcheck.c` compares the library's calling side
with what real callers sent in 106 recorded calls: the formats of the
sequences, not how a real answerer responds to them. `tools/rec_orig.c`
records a simulated call of the library's caller in the same format, so
the same tools read it.
