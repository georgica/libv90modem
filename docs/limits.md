# Known limits

What is not done, not tried, or measured in simulation only, as of 0.4.0.
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
- **x2 (answering): no real modem yet.** At 32000 (rate index 1, the
  start-up rate) x2 is decoded by the emulated U.S. Robotics Courier, V.42
  data acknowledged. Above 32000 it is octet-identical to the U.S.
  Robotics I-modem and D-Modem's server; the published courier-emu decodes
  no server above index 1; not yet confirmed on a real modem. A Courier
  asks for a higher rate about 12 s into data, so a real U.S. Robotics
  call decides the rates above 32000. (`docs/history/x2-facts.md`,
  "Harness" and "Oracle: D-Modem's mapper"; `tools/x2-peer/README.md`.)

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

## x2 (answering)

- **Offered only** with `V90M_MOD_X2` and `V90M_MOD_V34`, answering, to a
  caller whose V.8 CM offers V.34 and no PCM, on a mu-law line. Not in the
  defaults.
- **A-law: not supported.** x2 is not offered when `alaw` is set; no
  A-law x2 client is known.
- **Robbed bits: not handled.** No field of the client's MP for them is
  known; on a line with robbed-bit signalling the downstream would carry
  errors. The pad (none, 3 dB, 6 dB) the client reports is handled.
- **A non-x2 V.34 caller sees an x2 server's JM and INFO0a.** With x2
  offered, a caller whose CM offers V.34 and no PCM gets a JM whose carrier
  is inverted after each JM, and an INFO0a with max_sr_diff 4 and cme 1, as
  from a U.S. Robotics server. It still connects at V.34 in simulation and
  on recordings. Not tried on a live non-x2 V.34 call: a V.8 receiver that
  cannot take the inverted JM would fail V.8, as this library's 0.3.0
  calling side did. Check one before x2 is offered by default.
- **No way back to V.34.** Once the client's marker has come, a start-up
  or rate change that does not complete, or a request we cannot serve
  (rate index 0, a pad other than none / 3 / 6 dB, MD other than 5),
  ends the call with `V90M_END_X2_FAILED`. A full retrain asked for by the
  client during x2 is not handled either: the design clears down. The
  quiet or MSE watchdog ends that call, so the end reason is
  `V90M_END_LOST`, not `V90M_END_X2_FAILED`.
- **The free sign differs from the I-modem's.** In data mode the sign of
  the one position left free is chosen from a running sum, as D-Modem's
  server does; the I-modem's choice differs in about 25 % of frames (219
  of the 863 frames of a rate change on the emulated line, x2 8b; D-Modem
  gives some 87 % agreement for the data levels). The client is believed
  not to check it (RE, D-Modem), which is unproven above index 1. A known
  difference, not a known fault. (`docs/history/x2-facts.md`, "Free sign";
  `docs/history/PROGRESS.md`, x2 8b.)
- **Behind 3 dB** we send the Quad's octets `dc` for the level `e2` and
  `ed` for `f1`, as the Quad and the I-modem do; under the pad model they
  arrive one code off (below).

## Included tables (x2)

x2 has no published specification. Where no rule reproduces a table, it is
included as data, in a file that names its origin
(`docs/history/x2-facts.md`, "Mapping", "Pads" and "Banks"):

- **Levels A** (sync, records), `a5 a7 ad af b7 bd c5 cf e5`, in
  `modem/x2tab.c`: as D-Modem publishes them, and the table at DSP
  `d238`-`d240` of the I-modem firmware Ie030002, read by our disassembly.
- **The level banks** of every rate (index 1 to 15, 32000 to 56000 bit/s)
  behind each pad the client can report (none, 3 dB, 6 dB), in
  `modem/x2bank.c` (4.5 KB): read from the Quad x2 firmware QF060003, as
  published by D-Modem (its bank constructor run in courier-emu), and
  generated from those published values by `tools/gen_x2_banks.py`. No
  rule we tried reproduces them, and the firmware builds them at run time
  rather than storing them (x2-facts.md "Banks"). For no pad and indices
  1, 8, 11, 12, 13 and 14 they are the I-modem's lists as seen on the
  line; index 1 with no pad is levels B, the I-modem's data levels at
  32000.

Checked, not taken on trust: index 1's octets sent behind each pad equal
those the pad rule of `tools/x2_pad.c` (`x2_pad_octet`) derives, and every
bank's octets sent arrive as its levels under the pad model, except two
levels of the 3 dB banks that the Quad and the I-modem send one code off
the model (`test/test_x2map.c`).

## Included tables (K56flex)

K56flex has no published specification either. Its data mode's tables,
per rate (32000 to 56000 bit/s) and the pad the client reports (none,
3 dB, 6 dB), are in `modem/k56tab.c` (`docs/history/k56-facts.md`,
"Tables"):

- **The ring lists** (39 lists, 468 octets: the constellation group of
  each of the shell mapper's rings) and **the constellations** (39
  lists, 1382 octets: the mu-law magnitude of each point), about 1.9 KB:
  Motorola's SM56 driver's tables as published by D-Modem
  (github.com/D-Modem/D-Modem `dsp/k56flex_tables.cpp` at `c06a380d`),
  generated from those published values by `tools/gen_k56_tables.py`.
- K, u and r of each rate and the number of rings per pad are the facts'
  and are checked by the generator and `test/test_k56tab.c` (the frame
  identity K + 8 (u - 1) + 8 - 8 / r = R / 1000, M^8 >= 2^K, each ring list
  a permutation, each constellation M << (u - 1) magnitudes without
  repeats).

Tried as a rule first, without success: the rings in order of their
group's energy matches none of the 39 lists; the magnitudes as a
minimum-distance ladder in linear terms behind the pad reproduces 25 of
the 39 constellations, and only when each table's first point and
spacing are given, and none of the 3 dB tables but 32000's.

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
- **x2 data mode costs about twice V.90's: one x2 call per box.** 81.9 M
  instructions a second at 32000 and 83.7 M at 56000, against V.90
  answering's 38.9 M measured the same way (`make cost-x2`, second 15 to
  16 of the call); some two thirds of the box. The cause is the upstream the
  client dictates: V.34 with the 64-state trellis, non-linear encoding and
  expanded shaping, which x2 gives the server no way to refuse.
  (`docs/history/PROGRESS.md`, x2 6 and x2 8.)

## Not implemented

- V.92 quick connect, short Phase 2 and modem-on-hold, either role.
- K56flex: not supported; a K56flex-only modem connects at V.34
  (`DESIGN.md`, 4.2b).
- x2's calling (client) side. The answering (server) side is there since
  0.4.0 (above).
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
