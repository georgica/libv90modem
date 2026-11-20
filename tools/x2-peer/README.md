# x2-peer: an emulated U.S. Robotics Courier as a local test peer

`make test-x2-courier COURIER_EMU=<dir> [PAD=0|3|6] [MODS=x2]` has an
emulated Courier, running original USR firmware, dial our answering modem
and reports what both ends reached. It is a local tool: nothing of
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
- The report shows the Courier's terminal (its `CONNECT` line and `ATI11`)
  and our state. Exit 0 when both reached data mode.

Logs: `build/x2-peer/` (`bridge.log`, `courier.json`, `courier-serial.txt`,
`courier.log`). Other knobs: `COURIER_INSTRUCTIONS` (the Courier's budget,
default 400M, some 90 s of line), `TIMEOUT` (wall seconds, default 3600).
The emulator is slow: a call takes minutes of wall time.
