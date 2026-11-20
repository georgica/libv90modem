# x2-peer: an emulated U.S. Robotics Courier as a local test peer

`make test-x2-courier COURIER_EMU=<dir> [PAD=0|3|6] [MODS=x2]` has an
emulated Courier, running original USR firmware, dial our answering modem
and reports what both ends reached. With `MODS=x2` our modem offers x2
(`V90M_MOD_X2`) and the run passes when the Courier's CONNECT line and its
ATI11 say x2 and the Courier acknowledges the V.42 data we send it after
its rate change (below). It is a local tool: nothing of
courier-emu or of any firmware image is in this repository, and the target
is not part of `make check`. Without `COURIER_EMU` it prints
`COURIER_EMU not set` and exits 0.

## Setting up courier-emu (outside the repository)

courier-emu (github.com/cryan209/courier-emu) has no licence; use it only
as a black box on your own machine, and keep it and every ROM image out of
this tree.

    git clone https://github.com/cryan209/courier-emu ~/modem-research/sources/courier-emu
    cd ~/modem-research/sources/courier-emu && ./courier --help

`./courier` makes its own virtualenv (Python 3.10 or later; unicorn and
capstone from PyPI) and builds its native C5x core with the host's C++
compiler on first use. The Courier image the run uses by default is the one
in its checkout, `artifacts/courier-board-21210-capture-403/courier-board.rom`
(supervisor 7.4.16, DSP 3.1.2, with x2); `COURIER_ROM=<file>` picks another.

## What runs

- `tools/x2-peer/run.sh` starts the Courier with
  `courier run <rom> --line-link <socket> --line-listen --with-dsp ...`,
  `COURIER_LINE_DIGITAL=1` and 20 ms line frames, types
  `ATX1S32=66S54=0S58=48&A3&B1Q0DT5551234` (override the part before the dial
  with `COURIER_AT`), and after its `CONNECT` sends `+++` and `ATI11`.
- `tools/x2-peer/bridge.c` (`build/x2-bridge`, public API only) connects to
  the socket. The line carries 8000 Hz 16-bit samples of the Courier's codec
  each way, in frames with hook and call state. The bridge is the exchange:
  the Courier's samples go to mu-law at the levels of its codec and DAA,
  our octets come back through the optional digital pad (0, 3 or 6 dB:
  decoded, scaled in Q15, encoded to the nearest octet) and are decoded to
  the Courier's codec. It answers when the Courier's call rings.
- With `MODS=x2` the bridge runs `-k KB` (`KB`, default 4): once LAPM is
  up, a control burst of KB KiB of incompressible octets, and the same after
  each x2 rate change once data moves again. LAPM acknowledges while the
  Courier sits in command mode after `ATI11`. Each burst is logged when the
  Courier has acknowledged all of it, with its I frames, the 20 ms line
  frames in which the acknowledged count advanced (the Courier's N(R)s),
  I frames sent again, REJs and protocol violations from the Courier
  (an SREJ would be one), T401 expiries, and the bad frames our receiver
  saw; every 2 s while it waits, and at the end if it never completed.
- The report shows the Courier's terminal (its `CONNECT` line and `ATI11`)
  and our state. Exit 0 when both reached data mode and, with `MODS=x2`,
  there was a rate change and every burst was acknowledged with nothing
  sent again, no REJ, no violation and no T401 expiry.

Logs: `build/x2-peer/` (`bridge.log`, `courier.json`, `courier-serial.txt`,
`courier.log`). Other knobs: `COURIER_INSTRUCTIONS` (the Courier's budget,
default 400M: the call ends there, at 116 s of line, some 35 s of wall
time), `TIMEOUT` (wall seconds, default 3600), `KB` (0: no bursts).
