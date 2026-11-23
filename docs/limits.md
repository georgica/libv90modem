# Known limits

What is not done, not tried, or measured in simulation only, as of 0.5.0.
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
- **x2 (answering): one real modem, one line.** The owner's U.S. Robotics
  56K FAX EXT (flash 5.1.4) called the box (v90d with this library,
  2026-11-21): x2 at 56000 / 28800 with PPP, held 3 min 50 s until we hung
  up, two rate changes up, no retrain, the modem's SNR 59.7 dB; its counters
  293 blocks sent, 4 resent, 2 link NAKs, block error rate 0; pings across
  PPP 90 of 90 (average 148 ms, worst 590 ms). Not shown on a live call: a
  client's retrain (it ends the call, as `V90M_END_RETRAIN_FAILED` within
  some 70 ms of its Tone A in the replay of an earlier live call; no live
  call since has asked for one), a pad or robbed bits, the rates between the
  start-up and 56000 held for long, any other x2 modem.
  (`docs/history/PROGRESS.md`, "k56 13, fix round 1";
  `docs/history/x2-facts.md`; the emulated Courier and the oracle before it:
  `tools/x2-peer/README.md`.)
- **K56flex (answering): no real modem yet.** K56flex (answering,
  experimental, off by default) connects to Motorola's SM56 soft modem run
  under emulation, V.42 data both ways at 32000 to 52000 bit/s down and
  31200 up, with no pad and with robbed bits; its decoder takes our data
  at every rate, pad and robbed-bit pattern; not yet tried on a Rockwell
  or Lucent modem. (Below, "K56flex (answering)"; `docs/history/k56-facts.md`,
  "Harness"; `tools/k56-peer/README.md`; what the owner's line tests are to
  record: `docs/history/k56-handover.md`.)

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

## Data across a rate renegotiation or cleardown (no error control)

With V.42 the frames below are sent again; without it they are lost. The
downstream loses nothing (V.90 and V.92: the frames before Rd are all
delivered).

- **V.90 upstream: one mapping frame at each S** (about 2.5 ms; 72 bits at
  28800 bit/s). The answering modem delivers what its decoder held when it
  heard S, but where S begins is found from S's points, which a real
  channel turns for the first few symbols (up to 37 degrees in a
  recording): the frame before the start found is held back, so that no
  frame S cut or that holds S is delivered. The calling modem sends the
  mapping frame under way whole before its S.
- **V.92 upstream: what the caller's prefilter held when Ru began.** V.92
  begins Ru on a data frame boundary (9.8.2.1.1). The symbols reach the
  digital modem through the prefilter `dsn.dly` samples later (some 116
  on the simulator's lines), and Ru, sent in their place, cuts that tail
  off: about `dly` / 12 data frames (10 at 32000 bit/s, 480 bits) never
  reach the line. Inherent to Ru on a frame boundary; neither end can
  recover them. The answering modem then delivers the frames its decoder
  held, but for one data frame of margin before where Ru was found to
  begin (as V.90's). Measured on the simulator: 11 or 12 data frames
  (`dly` 114 to 116) in 40 renegotiations, either end's, nothing wrong
  delivered; at most `dly` / 12 + 3.

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
  client during x2 (its Tone A, in data mode or in its rate change) is not
  served either: the call ends at once, with `V90M_END_RETRAIN_FAILED`.
  A live U.S. Robotics client did this some 150 s into a call and
  reported "Unable to Retrain" (`docs/history/x2-facts.md`).
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

## K56flex (answering)

K56flex (answering, experimental, off by default) connects to Motorola's
SM56 soft modem run under emulation, V.42 data both ways at 32000 to 52000
bit/s down and 31200 up, with no pad and with robbed bits; its decoder
takes our data at every rate, pad and robbed-bit pattern; not yet tried on
a Rockwell or Lucent modem.

- **Offered only** with `V90M_MOD_K56` and `V90M_MOD_V34`, answering, on a
  mu-law line. Not in the defaults; v90d does not offer it until the
  owner's line tests (`docs/history/k56-handover.md`).
- **A-law: not offered.** With `alaw` set no CRe is sent and the call
  starts as without K56flex. A real K56flex call recorded on an A-law line
  exists (`docs/history/k56-facts.md`, "Recording"), but no A-law levels
  or tables were checked.
- **The included tables** (below, "Included tables (K56flex)") are SM56's
  as D-Modem published them. **Lucent clients are assumed to use the same
  tables;** that is unverified.
- **No renegotiation or retrain of ours.** `v90m_retrain` gives
  `V90M_ESTATE`, and the modem never starts a renegotiation: after a
  server's renegotiation SM56's data error rises tenfold and it retrains,
  and SM56 does not take a server's tone A as a retrain. So an upstream
  that degrades cannot be stepped down from our side: the watchdog ends
  the call (`V90M_END_LOST`).
- **The client's renegotiation: MP, MP' and E on 4 points** (V.34 11.6),
  as in V.34's and x2's rate changes: an assumption, confirmed by no real
  client. SM56 did not renegotiate in two calls of 120 s, so only the
  simulated client has renegotiated with it.
- **The client's retrains:** only its tone B in data mode is taken as a
  retrain. A client that gives up during a training (the first, or a
  retrain's) and retrains again is not followed: the call ends at that
  training's time limit (`V90M_END_K56_FAILED`). SM56 does that after
  about half of its own retrains. So "the third in a row ends the call"
  counts retrains each begun from data mode; they are forgotten once data
  mode has held 2 s (our own value: no source gives one). A retrain's
  Phase 2 must end within 9 s of the retrain's start (our own value), or
  the call ends. Only the simulated client has retrained with it.
- **Every caller hears our answer tone later.** V.8bis comes first (at
  least 400 ms of silence, CRe, the wait for CRd), so ANSam comes, against
  a call without K56flex: 1.790 s later for a caller without V.8bis,
  3.807 s later for a V.8bis caller without K56flex (CRd, no MS), 3.413 s
  later for an agreed K56flex client (measured in the simulator). A caller
  that gives up in that time is not known; the live calls with K56flex
  offered, before v90d offers it, are to show it.
- **A line that answers with no ringing may meet SM56's blind-dial wait.**
  SM56 answers V.8bis only some 2 s after it dials; on the telephone
  network the ringing covers that. Our CRe comes 400 ms after we answer,
  so a client that is still waiting ignores it and connects V.34 or V.90
  instead. The SM56 harness answers 3 s after dialling.
- **A client in automode gets V.90.** SM56 with `AT+MS=K56FLEX,1` offers
  V.90 in its CM too, and V.8 gives it V.90 (V.90 wins there), as for any
  caller offering PCM while V.90 is offered.
- **The 31200 upstream has little margin behind a strong echo.** In the
  simulator, with the line's echo 6 dB too loud (the echo of our octets
  before a 6 dB pad), the 31200 bit/s upstream took bursts of errors at
  about 35 dB SNR: the V.34 receiver's margin with a strong hybrid echo,
  not a K56flex fault. V.42 recovers such bursts; a real line's echo
  decides.
- **SM56's acceptance of our TRN2A depends on the harness's line model.**
  With some models of the analogue loop toward it, SM56 sends no MP after
  our TRN2A, and D-Modem's own server fails the same way under the same
  conditions (`tools/k56-peer/README.md`, "Known"). Whether a real client
  is as sensitive is not known.
- **The training is SM56's.** The real server of the recorded call sent
  other segment lengths (`docs/history/k56-facts.md`, "Recording"); whether
  a hardware client takes SM56's training, a 3-bit TRN1B and the
  16-level Js is not known.
- **Untested: K56flex begun in a V.34 retrain.** An agreed call that went
  V.34 (an unflagged INFO1c, or 3000 baud up) whose client flags its
  INFO1c in a later V.34 retrain gets our INFO1a flagged there and goes
  K56flex mid-call (`V90M_EV_RATES`). Designed, not driven by any test.
- **No calling (client) side.**

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
  The project's basis for including them: they are the numbers SM56's
  driver uses, as D-Modem read them out, taken as data (facts); none of
  D-Modem's code or text is used, only the values, in our own layout.
  This is the project's position, not legal advice.
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
- **K56flex data mode costs about what V.90's does: about a third of the
  box.** 39.0 M instructions a second at 56000, against V.90 answering's
  38.9 M measured the same way (`make cost-k56`, ARMv5, second 20 to 21
  of the call, after V.8bis). It asks the client in its MP for the
  upstream V.90 has (16 states, linear encoding, minimum shaping), and
  SM56 honours that. A client that ignored the request and sent with 64
  states, non-linear encoding and expanded shaping, as an x2 client
  dictates, could not be decoded today; once the receiver followed its MP
  instead (configuration: x2's receiver decodes that upstream), it would
  cost what x2 does, about twice V.90's (some 82 M a second, one call per
  box). (`docs/history/PROGRESS.md`, k56 10.)

## Not implemented

- V.92 quick connect, short Phase 2 and modem-on-hold, either role.
- The calling (client) side of x2 and of K56flex. Their answering (server)
  sides are there since 0.4.0 and 0.5.0 (above).
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

- **m68k: build with `-fno-omit-frame-pointer`.** GCC 12.3 for m68k (Bootlin
  stable-2023.08-1), with the frame pointer omitted (-O1 and up), reads an
  incoming argument from the wrong stack slot around a 64-bit division.
  The two places in the library where it did are written so that it
  compiles them right (an assembly scan of the tree finds no other), and
  the port matrix builds m68k with the flag as well; other versions of gcc,
  or new code, may meet it elsewhere.
  Only qemu-m68k has run it; there is no m68k hardware here.
  ([testing.md](testing.md); `docs/history/PROGRESS.md`, "m68k".)

The API itself is not stable before 1.0 (`CHANGELOG.md`).
