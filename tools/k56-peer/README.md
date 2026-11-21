# k56-peer: Motorola's SM56 soft modem as a local test peer

`make test-k56-sm56 DMODEM=<dir> [MODS=k56|none] [RBS=<hex>] [DOWN=<bps>] [STRICT_RENEG=1]`
has Motorola's SM56 soft modem (a K56flex client) dial our answering modem
and reports what both ends reached. It passes when both sides reached data
mode and 4 KiB went each way, byte for byte, over V.42. It is a local tool:
nothing of D-Modem, of `sm56.ko` or of any recording is in this repository,
and the target is not part of `make check`. Without `DMODEM` it prints
`DMODEM not set` and exits 0; without a 32-bit toolchain (`gcc -m32` and
`glibc-devel.i686`) it prints `no 32-bit toolchain (glibc-devel.i686)` and
exits 0.

## Setting up D-Modem (outside the repository)

D-Modem (github.com/D-Modem/D-Modem, AGPL-3.0) carries `sm56.ko` (Motorola
sm56-06.09.05, no licence: `dsp/test/NOTICE.md`) and `half_sm56`, which runs
the module in user space. Use them as black boxes on your own machine and
keep them out of this tree.

    git clone https://github.com/D-Modem/D-Modem ~/modem-research/D-Modem
    cd ~/modem-research/D-Modem && git checkout c06a380d
    cmake -B build -DCMAKE_C_FLAGS=-mstackrealign -DCMAKE_CXX_FLAGS=-mstackrealign
    cmake --build build -j16

`-mstackrealign`: with GCC 15, `half_sm56` built without it faults at once
(an aligned SSE store on the 4-byte-aligned stack the module calls back on).
The run uses `$DMODEM/build/dsp/test/half_sm56` (`HALF_SM56=<file>` for
another).

## What runs

- `tools/k56-peer/run.sh` runs `tools/k56-peer/bridge.c` (`build/k56-bridge`,
  public API only), which starts `half_sm56` with the line on pipes (its fd 3
  in, fd 4 out: 16-bit samples at 8000 Hz) and its AT commands on the pty it
  names. The line runs in lock step: each block SM56 gives is answered with
  as many samples.
- The bridge is the network: SM56's samples to the nearest mu-law octet into
  `v90m_process_g711`, our octets decoded back to SM56 through an analogue
  loop model: a delay of 1 + `LOOP` samples (4-point Lagrange; default
  `LOOP=0`, a plain one-sample delay) and a first-order 300 Hz high pass, a
  line transformer's (`LOOP=<fraction>|none`; none: the codes as they are).
  With `RBS` set, the octets toward SM56 are robbed as netsim's
  `NET_RBS_TO` (in the samples whose count mod 6 has its bit in `RBS`, the
  least significant bit is set to 1), then go through the same loop.
- SM56: `ATZ`, `ATX3`, `AT+MS=K56FLEX,0` (`SM56_MS=<command>` for another),
  `ATD`; we answer 3 s of line after `ATD` (SM56 answers V.8bis only after
  its 2 s blind-dial wait). With `AT+MS=K56FLEX,0` SM56 offers K56flex by
  V.8bis and, when nothing answers that, V.34 only in its CM.
- `MODS=k56` offers K56flex (`V90M_MOD_K56`; until the library has it the
  bridge says so and fails); `MODS=none` (default) does not. `DOWN` is our
  `max_down_bps`.
- After both CONNECTs each side sends `KB` KiB (default 4) of its own
  xorshift32 pattern: SM56's through its pty, ours through a V.42 endpoint
  (`v90m_ec`). `run.sh` compares what each side sent with what the other
  received. Then `+++` and `AT&V1`: SM56's own report of the call.
- `STRICT_RENEG=1`: after the first burst the call must also see a rate
  change (`V90M_EV_RATES`), and a second burst each way once data moves
  again must arrive as well.

Logs: `build/k56-peer/` (`bridge.log`; `sm56.log`, half_sm56's stderr
(`SM56_DEBUG=1` for its printk, `SM56_TRACE=1` for its K56flex front end);
the four `.bin` streams; `call.rec`, the call from our answer on in
`v90m-replay`'s format: 80 octets heard and 80 sent per 10 ms. The replay
finds the answer tone only after more than 1 s of recording with a sound
before its 200 ms of silence, so give it a pre-roll first, for example 110
blocks of 80 `ff` heard and 80 `fe` sent). Other knobs: `LINE_SECONDS` (default 120),
`TIMEOUT` (wall seconds, default 600).

## Known

- **The line model decides whether SM56 gets past our TRN2A** (k56 9b,
  `docs/history/k56-facts.md` "Harness"). With the codes as they are
  (`LOOP=none`), with the earlier model (a second-order high pass,
  fraction 0.45), and with some other fractions and extra delays, SM56 stays
  in its front end state 19, sends no MP and gives up with cause `0x21`.
  D-Modem's own server fails the same way under the same conditions
  (through its netsim: `NET_LOOP` unset or 0; with `AT+MS=K56FLEX,0` also
  0.45, 0.6 and 0.75), so it is not our modem. The default model (`LOOP=0`,
  first-order high pass) passed every call of the gate tried: 32000 and
  52000, robbed bits `0x08` and `0x24`, 12 of 12 repeats. But no model passed
  for every extra delay added toward SM56 (the default fails with 30 to 100
  extra samples, passes with 0 to 13). If the gate fails with SM56 in state
  19 (`SM56_TRACE=1`, `sm56.log`), try another `LOOP` before suspecting the
  modem.
- The earlier second-order high pass also spoils SM56's PCM decisions above
  32000 bit/s (its data error 80 to 140 against 5 to 7, then it drops the
  call).
- `SM56_MS=AT+MS=K56FLEX,1` (automode) adds V.90 to SM56's CM, and our
  answerer then connects V.90, not K56flex. `AT+MS=V90` against our V.90
  answerer depends on the line model as well: it passed with the earlier
  high pass at fraction 0 (50666), failed with it at 0.45 and with the
  default (Phase 2: no INFO1a from SM56), and passes through D-Modem's netsim
  (`NET_LOOP` 0.45 or 0.2: 45333 and 50666). Not investigated further.
- On this line SM56 and our V.34 settle at 28800 both ways.
