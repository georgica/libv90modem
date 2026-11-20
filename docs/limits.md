# Known limits

What is not done, not tried, or measured in simulation only, as of 0.3.0.
Each item names where it is recorded.

## Not tried

- **The calling side: simulation and own answerer only.** Every modulation
  of the calling role has run against simulated lines and this library's
  own answerer, never against a real modem or a real server. What it sends
  has been compared field by field with what real callers sent in 106
  recorded calls, which checks the formats of its sequences, not how a real
  answerer responds to them. (Header, README, CHANGELOG.)
- **What real servers send in a retrain is unknown.** V.92 9.7 has a
  retrain "proceed in accordance with the full Phase 2 start-up procedure";
  this library's answerer sends no INFO0 in a retrain's Phase 2, and the
  real callers recorded with it answered that the same way. What a real
  digital modem sends there cannot be seen in those recordings (all are of
  this library's answerer). The caller takes either form, tried only
  against its own answerer made to send INFO0d.
  (`docs/history/PROGRESS.md`, answer-fix 3.)
- **Answering, real modems:** only a Conexant CX93001, a U.S. Robotics 5686
  and a U.S. Robotics 56K of 1998 have called it (README, "Tried on real
  modems"). Bell 212A answering was tried only on a test build with the
  2225 Hz tone from the start, by a modem that would also have taken V.22.

## Measured in simulation only

- **The V.92 answering side's noise model is about 2 dB optimistic at the
  top upstream rates.** The design of the caller's upstream constellations
  assumes less error than the residual interference of the channel
  estimate makes on the simulator's lines (error RMS 9.7 at the small
  points, 7.6 assumed). The trellis carries it, and a margin would lower
  every V.92 call's rate, so none was added. Measured on the simulator
  only. (`docs/history/PROGRESS.md`, answer-fix 1 and 3.)
- **The calling V.90 / V.92 receiver's watch** (3 dB over the design noise
  for 1 s, 12 dB for 250 ms, 6 dB under for 5 s) has thresholds that come
  from the simulator's lines only. (`docs/history/PROGRESS.md`, robust 2c.)

## Not checked against a specification

- **The Bell 212A calling handshake** follows V.22 and what this library's
  answerer (and the U.S. Robotics 5686 that called it) accepts: no Bell
  212A specification document was to hand. The 3 s and 6 s fallbacks to
  Bell 103 are this library's choices. (README, `DESIGN.md` 4.2e.)

## CPU

All figures are ARM instructions counted under qemu (ARMv5, gcc -O2);
cycles on a real ARM926 have not been measured. v90d's box, an ARM926EJ-S
at 400 MHz, runs the answering modem's V.90 data mode at 32 % of its CPU,
which makes it about 123 M counted instructions a second.

- **One V.92 call on the calling side is some 56 to 60 % of that box**
  (69.4 M a second at 0 ppm clock offset, 73.9 M at 100 ppm, data mode);
  V.90 calling 44 to 48 %. One call fits with room; two V.92 calls at once
  do not. What is left is per-tap work of the receiver; further cuts would
  be numeric and cost rate. (`docs/history/PROGRESS.md`, cpu; README,
  "Memory and CPU".)
- The heaviest single tick of a calling V.90 / V.92 call is some 98 k
  instructions (8 % of a 10 ms block of the box), after the heavy designs
  were spread over many samples.

## Not implemented

- V.92 quick connect, short Phase 2 and modem-on-hold, either role.
- x2 and K56flex, the 56k schemes from before V.90: neither has a published
  specification; a modem with nothing newer connects at V.34 (`DESIGN.md`,
  4.2b).
- Fax (V.17, V.29, V.27ter), V.23, half-duplex V.34.
- A public call for a rate renegotiation of the calling side's own: the
  analogue V.90 / V.92 modem does one by itself when its receiver needs it.
- V.8 never falls back to Bell: a caller that has heard ANSam with a V.8
  modulation on offer goes V.8, and a V.8 exchange with nothing in common
  ends the call (`V90M_END_NO_COMMON`).
- The calling V.90 / V.92 modem does not measure the echo delay: it uses
  `v90m_cfg.echo_delay` as a fixed value, -1 taken as 0 (`modem/orig.c`).
- After the answering V.92 modem retrains because the upstream did not hold
  even the lowest PCM rate, the call goes on as V.90 for its rest, by
  design (`DESIGN.md`, 4.2a).

## Ports

- **m68k fails under qemu.** On m68k-uclibc, under qemu-m68k, twelve
  programs built on the V.34 receiver core (`modem/qamrx`) fail; every
  other target of the port matrix prints what the host prints. The
  evidence points at the emulator (the result changes with how qemu cuts
  the program into blocks); there is no m68k hardware to settle it.
  ([testing.md](testing.md); `docs/history/PROGRESS.md`, ports-2.)

The API itself is not stable before 1.0 (`CHANGELOG.md`).
