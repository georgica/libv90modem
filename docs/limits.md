# Known limits

What is not done, not tried, or measured in simulation only, as of 0.6.0.
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
- **The V.92 answering side's own cleardown has met no real modem.** It
  sends what V.92 9.11 / 9.8.1.1.2 / 9.6.1.1.2 give (TRN2d, SUVd, on the
  caller's SUVu one CPd with drn = 0, SUVd) and is over when the caller
  acknowledges; only the simulator and this library's own caller have
  answered it. (`v92/README.md`, reading 38; `docs/history/audit-0.6.md`,
  T21.11, T21.12.)
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
- **A single impulse can start error propagation in the V.90 / V.92
  calling receiver's decision-feedback equaliser** on lines whose
  low-frequency tail sits in the feedback; recovery is by retrain, about
  7 s (V.90) to 11 s (V.92). On the simulator's 2 km line the feedback
  taps sum to about 4 in magnitude (the first 0.73): the few wrong
  decisions a click makes feed back errors larger than half a level step,
  which make more, and the error never decays. Measured: 50 units added to
  one A/D sample (42 dB under the signal) loses the receiver; 20 does not.
  One of our own A/D samples reaching full scale (our V.92 PCM upstream's
  peak in a -10 dB hybrid echo) did the same. The receiver's watch then
  retrains (12 dB over for 250 ms). A clipped sample cannot be repaired at
  the sample: replacing it by 0 or keeping it leaves an error of thousands
  of units against the 20 to 50 the decisions survive. Recovering without
  a retrain (re-seeding the feedback after a runaway, or a feedback design
  that cannot run away, or spectral shaping, V.90 Appendix I) is a design
  item after the audit. (`docs/history/audit-0.6.md`, T21.9.)

- **The V.92 answering side's own rate drop in data mode seldom comes, and
  may not hold.** It needs the upstream decoder to flag 4 or more frames
  in each of two seconds, and the modulus decoder flags few of the frames
  noise spoils (11 to 33 in 10 s with a third of the bits wrong, on the
  simulator); when a drop comes, the new design still takes the noise
  floor measured at the start-up, so on a line that stays noisy the lower
  rate can fail B1u and the call retrains without PCM upstream. A rate
  renegotiation the caller starts is not affected.
  (`docs/history/audit-0.6.md`, T21.10; deferred.)

- **Clicks on the line in data mode** (the upstream QAM receiver: V.34,
  V.90 answering, x2, K56flex). The receiver's AGC gain is set once, when
  the far end's S is detected, and is not changed in data mode. A data-mode
  sample is blanked when it exceeds about 8 times (18 dB) the input RMS
  measured over S, so a click costs the bits around it and nothing more.
  Tested with a 0.75 ms full-scale click in simulation, and on a recorded
  x2 call with the click added. Not covered: a click under that threshold.
  A full-scale click (32124) is blanked only if the line's RMS at S was
  below about 4015 (about -12 dBm0); a line hotter than that at S gets no
  protection. If the level rises by 5 to 8 dB during data mode, real peaks
  would start to be blanked (untested). The echo canceller still adapts on
  the click. Post-audit item: make the threshold track the data-mode level.
  Also not covered: an impulse during training (training is not blanked, since some of its own
  peaks are that loud); and bursts of clicks. There is no watch that ends
  an x2 or K56flex call whose upstream stays deaf. Measured by mse alone, a
  line deafened by a click looks the same as a clean low-SNR line (Task 20,
  `docs/history/audit-0.6.md`).
- **x2 on a noisy line can stay up with most upstream bits wrong.** In the
  noise test (RMS 92) an x2 call stays up for 60 s with about 46% of the
  upstream bits wrong and no end code. There is no watchdog, by ruling.

- **The calling V.34 modem behind a long round trip** gets less
  downstream: in `test/test_v34dx.c`'s far-echo case (the answer side with
  a far echo 20 dB down, the call side with its own near echo), 31200 bit/s
  at a 100 ms round trip, 28800 at 300 ms, 12000 at 400 ms. The answer side
  is clean at every one. Not investigated (0.6.0 audit, ISP-1 follow-up).

- **V.34 transmit cap: opt-in, answering only.** `V90M_OPT_V34_TX_CAP`
  (0.6.2, off by default) caps our V.34 transmit at the caller's own
  INFO1c / INFO1a projection; the host sets it per caller from its own
  count of calls that asked above their projection and failed error
  control (`docs/answering.md`). The evidence is small: of the 106
  recorded calls only 6 reach a V.34 MP, plus one V.90 caller that
  selected V.34 (x2b), and none of those asked above its projection; with
  the cap our MP would offer them 31200 where it offers 33600, and the
  recordings, which hold the echo of the MP really sent, no longer replay
  with it. The target case is confirmed: a second WebTV box's error
  control failed in 7 calls of 7 when its MP asked above its projection,
  and came up in 12 of 12 (6 of them at 31200) at or below it. **Risk
  with the cap on:** a caller that projects below what it can do loses a
  step, as a caller like this library's own answerer would (it projects
  28800 on a clean line while its MP offers 33600 and the call runs at
  31200). The calling side has no cap at all, for that reason: against
  this library's own answerer (`test/test_orig_v34.c`, good line) it would
  have cost those calls a step. The V.90 analogue caller's upstream
  follows the digital modem's CP and is not capped at its INFO1d either;
  PCM has no such projection. Real answerers' INFO1a against their MP:
  not measured (no recordings of the calling side).

## Not checked against a specification

- **The Bell 212A calling handshake** follows V.22 and what this library's
  answerer (and the U.S. Robotics 5686 that called it) accepts: no Bell
  212A specification document was to hand. The 3 s and 6 s fallbacks to
  Bell 103 are this library's choices. (README, `DESIGN.md` 4.2e.)

- **The calling V.90 / V.92 modem waits 2000 ms for Sd-bar from the start
  of Ja**, where V.90 9.3.2.4 and V.92 9.5.2.2.1 say 1500 ms. A stated
  deviation: the clause adds no round trip, and a digital modem that
  follows the text (up to 500 ms after a whole Ja, 9.3.1.3) cannot meet
  1500 ms on a round trip of about 0.9 s or more; a live server on a VoIP
  path answered about 2 s after Ja (reported by an independent softmodem).
  The wait stays bounded. The calling modem does not send S before Jd has
  arrived, as that softmodem does for servers that wait for S without a
  round trip: V.90 9.3.2.7 has S follow the reception of Jd.

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
- **V.34 (both ways), and the V.34 upstream of x2 and K56flex: the same
  one mapping frame at each S** (78 bits at 31200 bit/s). Our transmitter
  sends its mapping frame under way whole before its S; a far modem that
  cuts its frame with S loses that frame too (two in all).
  x2 and K56flex: an S before the client's B1 has been decided (some
  55 ms after its E) loses what it sent before S (nothing of it is yet
  known to be data); no client measuring our data can renegotiate that
  early (our own data is not out until some 67 ms after its E for
  K56flex), so only the simulated client has done it. A K56flex client
  that ignores our MP and sends S that early may see errors in the first
  few frames after the renegotiation (in the simulator: 11 words at 45 ms,
  the equaliser pulled by B1 under the wrong configuration). A K56flex
  client whose E comes before our MP is out (an early E) has its S
  watched for only once our MP has begun: an S before that is decoded as
  data until then (our R2 cannot be begun while our training waits for
  its MP).
- **V.92 upstream: what the caller's prefilter held when Ru began.** V.92
  begins Ru on a data frame boundary (9.8.2.1.1). The symbols reach the
  digital modem through the prefilter `dsn.dly` samples later (some 116
  on the simulator's lines), and Ru, sent in their place, cuts that tail
  off: about `dly` / 12 data frames (10 at 32000 bit/s, 480 bits) never
  reach the line. Inherent to Ru on a frame boundary; neither end can
  recover them. The answering modem then delivers the frames its decoder
  held, but for one data frame of margin before where Ru was found to
  begin (as V.90's). Measured on the simulator: 11 or 12 data frames
  (`dly` 114 to 116) in 40 renegotiations, either end's, and nothing
  wrong delivered in those or in 46 more over eight kinds of line (noise
  40, pads, a strong echo, a bump); at most `dly` / 12 + 3.
- **V.92 upstream: a caller off the designed level.** The answering modem
  measures the caller's gain on B1u and scales what it decodes (within 1 %
  from 0.4 to 3.0 of the design on the simulator), but the constellation
  stays the one designed for the level asked. A weaker caller has that
  much less margin against the line's noise (0.6 on a long line: some 100
  bits wrong in 230000); a louder one sends its largest points past the
  codec's full scale (from about 1.6 on the long lines of the simulator,
  some 0.1 % of bits wrong at 2.4; on a short line clean to 3.0). Neither
  is flagged by the modulus decoder, so the quality watch does not lower
  the rate. A design for the measured level (a rate renegotiation after
  B1u) is not done.

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
  is inverted after each JM for the first 2 s (`V8A_X2_FLIP_MS`), then
  plain, and an INFO0a with max_sr_diff 4 and cme 1, as from a U.S.
  Robotics server. Live, a strict V.8 receiver (two WebTV boxes) never
  took the inverted JM and failed V.8 (0.6.1: 14 of 21 calls); since 0.6.2
  it gets the plain JM after 2 s and loses those 2 s only (simulation).
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
- **The client's retrains:** only its tone B in data mode, or in its
  renegotiation, is taken as a retrain. A client that gives up during a
  training (the first, or a retrain's) and retrains again is not
  followed: the call ends at that training's time limit
  (`V90M_END_K56_FAILED`). SM56 does that after about half of its own
  retrains. So "the third in a row ends the call"
  counts retrains each begun from data mode; they are forgotten once data
  mode has held 2 s (our own value: no source gives one). A retrain's
  Phase 2 must end within 9 s of the retrain's start (our own value), or
  the call ends. Only the simulated client has retrained with it.
- **A client that ignores our MP: x2's upstream only.** One that sends 64
  states, non-linear, expanded shaping whatever our MP asks is decoded
  (B1 retried under that configuration; only the simulated client has
  done it), and its data mode then costs about what x2's does: one such
  call per ARM926 box. Catching up after its B1 (3 stored points every 2
  symbol periods, about 110 ms, once per call) costs up to 1.28 M ARM
  instructions per 10 ms block, 10.5 ms of the box's CPU (122 M a
  second): with nothing else running, v90d's answers come at most 11.7 ms
  after their block, inside its 20 ms of prefill (the pump's deadline
  holds, with 8 ms to spare; V.42 or PPP work in those 110 ms eats into
  it; `pump_underruns` on the first live heavy call is the check). Other
  mixes (32 states, or some options only) are
  not tried and end as a wrong B1 (`V90M_END_K56_FAILED`).
- **K56flex version 1 is declined.** Our start-up is version 2's (from
  0.7.2 our CL offers both, 0x43); a client that selects version 1 (an
  early WebTV Plus) gets NAK(3) and V.34 or V.90, as without K56flex.
  `V90M_OPT_K56_V1` accepts it, experimentally: what version 1 differs in
  on the line is not known ([answering.md](answering.md), "K56flex
  versions").
- **Every caller hears our answer tone later.** V.8bis comes first (at
  least 400 ms of silence, CRe, the wait for CRd), so ANSam comes, against
  a call without K56flex: 1.790 s later for a caller without V.8bis,
  3.807 s later for a V.8bis caller without K56flex (CRd, no MS), 3.413 s
  later for an agreed K56flex client (measured in the simulator with
  0.7.1's CL; 0.7.2's 0x43 CL is one bit longer and adds 3 to 4 ms to the
  last two); a client declined for version 1 is on the agreed timing, 3.420 s
  with 0.7.2's CL (NAK(3) ends V.8bis where ACK(1) would; with 0.7.1's CL it sent
  no MS and was on the 3.807 s one); from 0.7.4 a V.8bis caller whose MS
  carries no K56flex block, leaves the modulation to V.8 and asks for
  ACK(1) gets it, and hears ANSam about 3.46 s later (3.464 and 3.470 s in
  the simulator; 0.7.3 sent nothing and went on at the MS's end). A caller
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

## V.92 Quick Connect (answering)

From 0.7.4.

- **Simulation and own answerer only.** Every Quick Connect test is
  against the simulated caller (`sim/qca`, written from the V.92 text) and
  this library's own answerer. Recordings of real V.92 callers show that
  they send a QC1a during our answer tone (the fixtures of two U.S.
  Robotics 5686 calls, and about half of the private recordings), which
  is all that is known: how a real caller answers our QCA1d, QTS and
  ANSpcm has not been seen. The live check, with a BinModem build as the
  peer and then a real modem, is still to be made.
- **A false QC1a is possible in one pattern.** A caller that sends no QC1a
  could still be taken for one if its signal holds, twice, at least 8
  marks, the character 0x55, one character with bits 0, 1 and 4 clear and
  at least 8 marks, the two characters equal. Tested not to happen: every
  CM our tests use at -9 to -43 dBm0, our own ANSam echoed at -6 dB under a
  CM, noise at -30 and -46 dBm0 over 20 seeds, start-stop text with one
  stop bit, QC1d, one copy only, two different copies, a character after
  the information one. The one pattern that is a QC1a by Table 2 (the
  characters U and D, ten marks, U and D again) is taken as one on
  purpose. The cost of a false one is a delay, not a lost call: QCA1d,
  QTS and ANSpcm go out, nothing answers, and V.8 starts again about 2.2 s
  after the QC1a was taken.
- **TONEq needs at least 100 ms of 980 Hz**, counted as 6 detector blocks
  of 10 ms and 40 ms more. A caller whose TONEq is shorter (BinModem's
  `BINMODEM_V92_TONEQ_MS` can cut it) is not heard, and the call falls
  back to V.8.
- **The 2 s of V.92 9.2.4.3 are counted at our port**, from the last
  sample of our QCA1d, so the line's round trip is part of them. By
  Figure 3 of V.92 a caller that did not hear ANSam for a second waits a
  second of ANSpcm before TONEq; ANSpcm begins 177 ms after QCA1d, so such
  a TONEq arrives at about 1.18 s plus the round trip, and a round trip of
  up to about 0.8 s fits. In the simulator a TONEq beginning 1.90 s after
  QCA1d is taken and one beginning 2.10 s after is not (fallback).
- **A run of 980 Hz that began before the 2 s are over may finish after
  them.** It is followed to its end and taken, up to about 2.1 s after
  QCA1d (a run that began at 1.897 s was taken at 2.07 s). A TONEq that
  begins in the last 10 to 20 ms before the deadline has not yet shown
  a complete detector block when the 2 s end and is dropped: that is
  within 9.2.4.3, which asks for TONEq during the 2 s and leaves the
  rest to ANSam and V.8.
- **TONEq under a strong echo of our ANSpcm.** The echo is at 2100 Hz and
  can only hide the caller's 980 Hz, never imitate it. In the simulator,
  with the hybrid's echo at a linear gain of 0.1, 0.3 and 0.5 (6 dB down)
  TONEq is taken; at 0.7 (3 dB down) and 1.0 it is missed, and the call
  falls back cleanly to V.8 and still reaches V.92 data, about 2 s later.
  How loud a real line's echo of our ANSpcm is has not been measured.
- **Short Phase 2's recovery takes 2500 ms** from our Tone B reversal,
  with no Tone A reversal or no INFO1a we can serve, before the full
  Phase 2 begins (9.4.1.2.2, 9.4.1.2.3). Our reversal comes 50 ms after we
  enter the state, not 50 ms after Tone B began, so at most about 50 ms
  later than the minimum. The simulated caller, on a reversal that never
  comes, goes to the full Phase 2 instead of 9.4.2.2.2's retrain.
  INFO1a naming 3429 symbols a second (we do not receive it) is treated as
  none; the simulated caller's own 3429 path is not known to work.
- **One kept PCM-upstream offer.** A first start-up after TONEq that does
  not reach data mode keeps PCM upstream on offer for one retrain, on the
  ground that a caller's stored parameters, not the line, are the likely
  cause. The cost, when the line is the cause, is one more failed
  start-up before 0.7.3's rule (bit 70 cleared, V.90) applies. A fallback to
  V.8 does not use it up, and a failure in data mode does not use it.
- **Only mu-law and A-law lines through the simulator.** QTS and ANSpcm are
  law-specific codewords, checked codeword by codeword against the
  simulator's own generator for both laws; the ANSpcm table is generated
  from the formula of V.92 8.3.1 and compared with the printed tables
  (2408 of 2408 entries).
- **Not implemented:** QC2a in V.8bis, QC1d, modem-on-hold, the calling
  side. A QC1a with WXYZ 1111 is ignored, and V.8 decides.

## HST (answering)

0.7.0 work in progress: the answering branch to data (Task 18); channel
reversals, rate changes and the other watchdogs come later in the plan.
Measured against `sim/hstcall` only; no real HST modem has called it yet.

- **W, the wait after ANSam, is 650 ms, and up to 2000 ms while the back
  channel's band holds energy** (`HST_W_MS`, `HST_W_MAX_MS` in
  `modem/hst.h`; decision 25 and the Task 13 ruling, the cap raised from
  that ruling's 1500 ms to the controller's ceiling of 2000 in Task 18's
  fix round). `make hst-wsize` (`rig/hst_wsize.c`) runs `sim/hstcall`
  against the answerer and times the detector of ANS_HST_WAIT from the
  wait's start. hstcall starts its back channel 30-45 ms after our answer
  tone stops; the firmware's caller starts "once the tone has stopped" (BF
  §1, [FW]) with no figure. What reaches us later than that is the
  caller's turn-around plus the round trip of our tone and its back
  channel: a VoIP path's jitter buffers, and the host's block latency (a
  host that processes 4000-sample blocks adds 500 ms, as
  `test_answer_hst` `block_size` does). So each line runs again with the
  caller 50 to 500 ms later [OURS: the bound]. The lines: 450 and 300 bit/s
  at -9 / -26 / -43 dBm0, +-7 Hz, +-100 ppm (24 a speed); one 20 or 40 ms
  dropout 100-300 ms into the preamble at +-7 Hz (84 a speed); a 300 Hz
  4th-order high-pass, a line's lower edge (4 a speed); and (0.7.3) a
  Courier's back channel as recorded (hst-facts.md `Back channel`, [REC]):
  450 with a 17-symbol preamble at -6 / -9 / -12 dBm0, the four offsets,
  two seeds (24): 1736 lines. The latest detections, ms from the wait's
  start (0.7.3, with the back receivers' baseband rescaled when the carrier
  comes; 0.7.2's figures where they differ in brackets):

  | caller later by | 450 levels | 300 levels | 450 dropout | 300 dropout | 450 edge | 300 edge | 450 Courier |
  |---|---|---|---|---|---|---|---|
  | 0 ms | 430 | 690 | 690 | 1440 | 860 | 580 | 410 |
  | 50 ms | 940 | 1400 | 740 | 1060 | 900 | 630 | 740 |
  | 100 ms | 650 | 1400 | 790 | 1110 | 940 | 700 | 940 |
  | 200 ms | 690 | 970 | 890 | 1220 | 1060 | 800 | 680 |
  | 300 ms | 1230 | 1510 (1040) | 990 | 1740 | 1140 | 880 | 940 |
  | 400 ms | 940 (950) | 1400 | 1090 | 1410 | 1240 (1260) | 980 | 950 |
  | 500 ms | 940 (950) | 1850 | 1190 | 1510 | 1360 | 1080 | 950 |

  (A lock lost and won again in the acquisition makes the latest jump: it
  is not monotonic in the delay.) The slack of a line is the time from its
  detection to the end of the wait that would have applied to it: the
  first ms from W on without the energy stage, or the cap. Lines with less
  than 100 ms of slack (missed included), of 1736 (0.7.3; 0.7.2's 1568
  lines without the Courier's gave 466 450 249 233 36 20 at a cap of 1500,
  464 448 240 224 16 0 at 2000, the same least slack):

  | cap | W 400 | W 450 | W 500 | W 550 | W 600 | W 650 ... 1000 | least slack at W 650 |
  |---|---|---|---|---|---|---|---|
  | 1500 | 516 | 500 | 276 | 260 | 39 | 23 | -350 (missed) |
  | 1600 | 514 | 498 | 266 | 250 | 29 | 13 | -250 (missed) |
  | 1700 | 513 | 497 | 265 | 249 | 19 | 3 | -150 (missed) |
  | 1800 | 513 | 497 | 265 | 249 | 19 | 3 | -50 (missed) |
  | 1900 | 512 | 496 | 264 | 248 | 18 | 2 | 50 |
  | 2000 | 512 | 496 | 264 | 248 | 16 | 0 | 150 |

  W 650 with a cap of 2000 ms is the least pair that leaves every line
  100 ms or more (0.7.3: the Courier's lines too, so W stays); the least is 150 ms (the 300 bit/s caller at -26 dBm0 500
  ms late, found at 1850 ms). The cap is at the ceiling the controller set
  (2000 ms): a caller slower than 500 ms, or a dropout later in a slow
  start, can still meet it. W 400 leaves lines short at any cap, since
  their energy stage is not yet there when the wait ends. With the first
  choice, W 700 and a cap of 1500 ms, the slack was 10 ms on the clean
  300 bit/s lines and 60 ms under the cap with the caller on time, and a
  later caller fell to V.22bis. Every line here is found through the
  extension once past W: the energy stage holds while the back channel is
  there, and a dropout of up to 40 ms never broke it.
- **W's cost to callers without V.8:** a V.22bis, V.21 or Bell 103 caller
  is answered up to W later than without HST, only where the host offers
  HST and has linked an endpoint (`test_answer_hst` `legacy_unchanged`:
  the same modulation, rates and data, connected 650, 620 and 650 ms
  later; with W at 700 ms it was 700, 670 and 700, at 450 ms 450, 420 and
  450, at 400 ms 400, 370 and 400). None of them puts energy in 250-500
  Hz, so none waits past W; a caller with dial tone or hum on its line
  might, up to 2000 ms. Callers that send CM or AA are answered sample for
  sample as without HST.
- **The forward channel sends 48 symbols of scrambled ones after each of
  our trainings before the first data bit** (`HST_ONES_N`, [OURS], V.17's
  segment 4 length): `sim/hstcall`'s receiver enters data only on random
  points, and without them our first frame after the training (our LR)
  was lost, which cost a T401 (about 2 s) on every call.
- **The back channel's presence, for the 5 s watchdog, is a back
  receiver's lock,** not its carrier detect: our own forward signal's echo
  holds the carrier detect on with no back channel (the Task 13 review).
- **The back channel's bits go on through our retrains** (the forward
  channel alone trains): the endpoint gets them while data is stopped at
  the API.
- **Our own echo, holding the forward channel: about 12 dB of margin in
  data, 3 dB while we train.** The spec's real-line corner is our forward
  signal at -10 dBm0 coming back at 6 dB echo return loss (-16 dBm0, 17 dB
  over a back channel received at -33 dBm0, with white noise 20 dB under
  it). With the real forward transmitter (`hst_ftx`, data at 14400) as
  the echo, `test_hst_back` `echo_fwd` gives no bit in error and no loss
  of lock at both speeds (2 x 20000 bits a speed); with `HST_VERBOSE` it
  lowers the ERL a dB at a time over 4 lines (+-7 Hz, +-100 ppm), 80000
  bits a step: none in error down to -6 dB ERL (echo -4 dBm0, 29 dB over
  the back channel), the first errors at -7 dB (8 at 450, 3 at 300) and
  -8 dB (239, 70), never a loss of lock. That margin is our pulse's: it is
  some 40 dB down at 400 Hz and below (V.32's pulse, the Task 13 stand-in,
  failed at 13 dB less echo). A whole call (`tools/rec_hst -e ERL -b -33
  -n -53`, our level -12 dBm0, 6 seeds a step) is tighter while we train:
  the caller's LR arrives during our long training, and its AB (600 Hz
  and 3000 Hz) costs the back receiver its lock where data does not. Up
  to 1 dB ERL (echo -13 dBm0, 20 dB over the back channel) every call
  reaches 14400 at the same time as on a clean line; at 0 dB the 300
  bit/s calls (6 of 6) and 2 of 6 at 450 lose the LR once, and MNP's
  resend puts data 2.3-2.7 s later; the calls still come up. Measured
  against `sim/hstcall` and our own transmitter, not a real line.
- **A lasting level drop of the back channel keeps its lock** (the Task
  13 review's N1, settled in Task 23). Before, a drop of 12 dB or more
  for good lost the lock 140-160 ms after it (the fade rule took the drop
  for the back channel's end) and found it again 0.2-1.1 s later; MNP
  resent what was lost. Now, when a fade has run long enough to lose the
  lock (11 symbols at 450, 8 at 300), its last 8 (450) or 5 (300) symbols
  are looked at: steady (the strongest at most twice the weakest's power)
  and on the decision grid but for one at most, they are a step, the
  symbols' mean is taken from them and the lock kept; noise alone passes
  that 1 time in 10^5 at 450 and 1 in 550 at 300 (then held until the
  quality rules end it, a few symbols' bits wrong). `test_hst_back`
  `lasting_drop`: 12, 15 and 20 dB down for good (30 dB SNR before, +7,
  -7, +3 Hz, +100 ppm, 8 seeds), the lock held on every line, at most 40
  wrong bits, at both speeds; the dropouts (`fades`, 100 ms off: lock
  lost, no wrong bit) as before. A whole call (`tools/rec_hst -p 450|300
  -x -12@1000` and `-20@1000`, 2 seeds): no loss of lock, and the answerer
  gets 832 octets in 19 s at 450 and 576 at 300, an undropped line's.
- **Rate changes by marks (0x3R, Task 22a, the DSP half; the policy
  follows):** our transmitter sends 36 symbols of ones at the old code and
  36 at the new, its scrambler, differential and trellis encoders running
  on (`hst_ftx_marks_switch`); our receiver, armed (`hst_frx_arm_switch`),
  waits for 24 symbols of decoded ones, decodes the new code as well from
  where they end (the last 24 points again, then at most 48 symbols of
  both), and switches on 24 symbols of ones at the new code, its
  descrambler and differential memory taken from the last of the old
  code's ones. Nothing is adapted from 8 symbols of ones on until the
  switch: fed the old code's decisions on the new code's points for 20-odd
  symbols, the carrier loop was kicked and the data after the marks lost
  for 200 symbols (46 of 144 switches in a sweep of every pair of codes 3..6
  at 30, 26 and 22 dB, 4 seeds each; with the hold, 2 of 144, both at 22 dB
  to 14400, under 14400's 23.5 dB). Codes 3..6 only: with 4800 on either
  side the transmitter trains instead. Armed, S or S-bar for 16 symbols (a
  training instead of the marks, as BinModem does) or 2 s without a
  switch gives the acquisition at the new code (`test_hst_fwd`
  `marks_switch`, `marks_missed`). Never run against a real HST modem: R2
  (spec, "Evidence") is to show where the new rate starts.
- **"Good" for the upshift** (`hst_good_mse`, `make hst-thresh`): our
  receiver on v32line, a long training on a 40 dB line, then white noise
  added to the SNR (against the signal at the receiver, 0-4 kHz), runs of
  20 s of data each until 2 000 000 bits or 200 errors a point (a lost
  lock counts as 200), going down by 0.5 dB to the second point over
  1e-5:

  | code | rate | SNR at BER 1e-5 | the receiver's error 3 dB above | `hst_good_mse` |
  |---|---|---|---|---|
  | 3 | 7200 | 13.5 dB | -17.76 dB | 1098 |
  | 4 | 9600 | 16.5 dB | -20.54 dB | 579 |
  | 5 | 12000 | 21.0 dB | -23.78 dB | 274 |
  | 6 | 14400 | 23.5 dB | -24.13 dB | 253 |

  Near the crossing the errors come as events (a burst, or a lost lock),
  not as a smooth curve: at 12000, 20.5 dB had 26 errors in 2 000 000 bits,
  20.0 and 19.5 none, 19.0 a lost lock. The crossing is taken at the
  first point over 1e-5 going down, so it errs high. The receiver's error
  flattens at about -24.5 dB (28 dB of SNR and up), so 14400's "good"
  (-24.13 dB) needs a line of 26.5 dB or better and has only 0.4 dB in
  hand on the cleanest line.
- **The forward receiver in long calls** (`test_hst_fwd` `long_drift`;
  the full set `make hst-drift`). Until Task 22c the data mode's gain loop
  and the equaliser drifted apart on a noisy line: the loop held the
  points' power at the set's, the equaliser (least squares) at the set's
  less its error's, and nothing fixed the scale between them, so the gain
  climbed and the taps shrank with it (7200 at 19 dB: +4.9 dB in 60 s;
  300 s at codes 3..6, 1 and 3 dB over BER 1e-5: 5 of 8 calls lost the
  lock, at 31 to 130 s). The loop now leaks back to the gain as last set
  (0.85 s) and stays within 1 dB of it; the slow scale is the
  equaliser's. The same 8 calls: no lock lost, no error in 300 s, the
  gain within -0.83..+0.63 dB of its value at 10 s, the receiver's error
  at the end within 0.2 dB of a frozen gain's. Short calls are unchanged:
  `test_hst_fwd`'s 23 earlier cases print the same, level steps of -3..+6
  dB at 9600 and 14400 (8 seeds each, a scratch sweep) fail 3 of 96 before
  and after, and `make hst-thresh` again gives the crossings above and
  the receiver's error 3 dB above them within 0.04 dB (-17.80, -20.56,
  -23.79, -24.13 dB), so `hst_good_mse` stands. The thresholds' runs of
  20 s were chosen to keep the drift out; they are kept.
- **The forward receiver's first training and the band's lower edge**
  (Task 23b and its fix round; `test_hstcall fwd_edge`, `test_hstcall
  sweep [SEEDS [v17|long [HZ [PPM]]]]`, `sim/v32line`'s `edge_*`). The
  first training starts from a matched filter; a line's high-pass near 600
  Hz (channel banks, ATAs) bends the phase where the forward band starts
  (500-700 Hz). The edge model is Butterworth only (high-passes of even
  order, and a second-order all-pass): elliptic or Chebyshev filters, with
  more phase ripple near the corner, are not modelled, and neither are
  real ATAs' or channel banks' filters, nor a real Courier's pulse (we
  assume the sharp one).

  Before (one start, our pulse's matched filter), codes 2..6, our pulse and
  V.17's 25 % roll-off, hstcall's long training and V.17's, 5 seeds, a
  mu-law line at 30 dB, no offset: phase alone (an all-pass, 0.5 to 3 ms at
  600 Hz over 1800's) never failed; a high-pass of order 4 never failed
  from 300 to 650 Hz; of order 10 it failed at 550 Hz (39 and 45 of 50
  passing, our pulse and V.17's) and nearly always at 600 Hz (5 and 8 of
  50), the error stuck near -4 dB.

  Now the receiver measures, over the first 128 training symbols, its own
  pulse's matched filter and that pulse's through a 550 Hz 6th-order
  high-pass, by the spread of each one's point power, and starts again
  from the second unless its spread is 1.25 times the first's or more
  (`mf_choose`; the rule leans to the second start). The restart takes
  AB's symbol timing later by `HST_FMF_AB_SHIFT` (0.45 symbol): the second
  start's own high-pass reads AB's two tones that much early. Without it
  (Task 23b as first committed) a caller's clock 100 ppm slow moved the
  timing the rest of the way out: on steep edges 3 to 34 % of first
  trainings failed (below). A scratch probe of the restart's timing
  showed the failures on the early side: 0.12 to 0.38 sample earlier than
  AB's reading failed up to 61 % of a cell at -100 ppm, while 0.12 to
  1.75 samples later failed at most 2 of 80 at -100, 0 and +100 ppm
  (the shift, 1.5 samples, sits inside that).

  Failures out of 80 a cell (codes 2..6, both shapes, both trainings, 4
  seeds; failure = no data, or a bit in error in the first 2000 or so;
  scratch grid, the same `sweep_one`), before this round and now:

  | Hz, ppm | hp 525/10 | 550/10 | 575/10 | 600/10 | 550/6 | 625/6 | 500/8 | 525/8 | gd 3 ms |
  |---|---|---|---|---|---|---|---|---|---|
  | 0, 0 | 0 / 0 | 1 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 1 / 1 | 2 / 1 | 0 / 0 |
  | 0, +100 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 5 / 5 | 4 / 4 | 1 / 1 |
  | 0, -100 | 11 / 0 | 7 / 0 | 8 / 0 | 2 / 0 | 0 / 0 | 32 / 0 | 1 / 1 | 0 / 0 | 0 / 0 |
  | +7, -100 | 10 / 0 | 27 / 0-1 | 13 / 0 | 4 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
  | -7, +100 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 7 / 7 | 78 / 78 | 0 / 0 | 0 / 1 | 0 / 0 |
  | +7, +100 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 / 3 | 0 / 0 | 0 / 0 | 0 / 0 | 2 / 2 |
  | -7, -100 | 12 / 0 | 3 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 74 / 74 | 0 / 0 | 6 / 0 | 0 / 0 |

  (+7 Hz, -100 ppm at 550/10: 0 or 1 of 80 between runs of the grid.)
  No edge, hp 500/4 and hp 300/10: 0 at every offset, before and now.
  What is left:
  - 550 Hz order 6 with the carrier 7 Hz off and the clock +100 ppm: 7
    and 3 of 80. The rule keeps our pulse's start there (its spread
    ratio 1.26 and up, the review's measure), and it fails as it did
    before Task 23b.
  - 625 Hz order 6 with the carrier -7 Hz off: AB is never heard (74, 78
    of 80), and the same at 625-650 Hz of order 10 and 650 Hz of order 6
    and 8 with no offset (a 3-seed scratch sweep: 240 of 240). The 600
    Hz tone is too far down for the detector, so there the margin's edge
    is AB, not the training; such a cut also takes the bottom of the
    forward band.
  - 500 and 525 Hz of order 8, and the all-pass at 3 ms: a few single
    misses, none of them switching, the same before and now.
  - Pure group delay is the closest call on a line with no high-pass: at
    3 ms and 20-22 dB the second start's spread is 1.53 times the first's
    (the review's sweep; the rule switches under 1.25). The 0.5-3 ms the
    brief asked for are covered; more delay than that is not measured, and
    there a switch would hurt: started from the second filter with AB's
    timing as read, the 3 ms line failed 68 to 103 of 160 (the review's
    control).
  - Noise: the review swept 18-30 dB with offsets (11 200 runs); no line
    without a steep edge ever switched (the largest spread ratio 3.3 at 18
    dB with no edge), and on steep edges the choice held at 18-22 dB.

  Lines with no edge, and every existing case, train exactly as before
  (the second start is never chosen: only test_hstcall's golden output
  changed, by fwd_edge's lines).
- **"Good", read at the rate in force** (Task 22b; `test_hst_fwd
  upshift`, which `make hst-thresh` runs after the sweep). The policy asks
  for code c while running at c - 1, so it reads `hst_good_mse[c]`, a
  figure measured at c, off a receiver at c - 1. On the line where c is
  good (its SNR of BER 1e-5 + 3 dB), 4 runs of 20 s each, the receiver's
  error at c - 1 and at c:

  | up to | line | error at c - 1 | error at c | `hst_good_mse[c]` |
  |---|---|---|---|---|
  | 3 (from 4800) | 16.5 dB | 1075 (-17.85 dB) | 1090 | 1098 |
  | 4 | 19.5 dB | 578 (-20.55 dB) | 577 | 579 |
  | 5 | 24.0 dB | 276 (-23.75 dB) | 274 | 274 |
  | 6 | 26.5 dB | 253 (-24.13 dB) | 253 | 253 |

  The error is the line's, not the code's (within 0.06 dB), so the table
  is used as it is, read at c - 1: no table per code. A code-5 receiver
  on a 26.5 dB line reads 253, 14400's threshold exactly. But "good" is
  90 % of the time, and the error over 256 symbols swings about its mean:
  at the line of each threshold the receiver is under it 32 to 69 % of
  the time, and 1 dB higher 99 to 100 % at codes 3..5; at code 6 it is 8 %
  at 26.5 dB, 25 % at 27.5, 65 % at 28.5 and 100 % at 30.5. So the
  upshift to 14400 comes on lines of about 29 to 30 dB, 3 dB more than
  its definition, the others 1 dB more. Left so (it errs safe: 14400's
  BER 1e-5 is at 23.5 dB); lowering 14400's threshold would need the
  error's spread at 26.5 dB, not its mean.
- **The rate policy** (`modem/ans_hst.c`; spec "Rate changes"; hst.h
  `HST_UP_MS` ...). Holding the back channel: an upshift every 10 s at 90 %
  good, none within 10 s of a downshift; one agreed rate lower on 15
  frame errors (the endpoint's `rx_bad`: bad FCS, too long, not parsed)
  within 10 s of the first, or on 3 failed retrains within 10 s of the
  first. A retrain fails when the receiver is not in data at its rate 4 s
  after the request left (`HST_RC_BOUND_MS`; not counted while the
  request waits in the mailbox), when its training ends without data
  (TRAINFAIL), or when it loses the data mode it reached within 2 s
  (`HST_RC_HOLD_MS`); a failed one is asked again at the same rate. The
  receiver losing data mode by itself with the carrier there (UNLOCKED,
  TRAINFAIL) is a retrain at the rate in force. At 15 dB SNR from 12000
  (`test_answer_hst` `fallback`): three retrains at 12000 fail (data, then
  the lock lost within 0.5 s), then at 9600 the same, then 7200 holds;
  10 s from the noise to 7200, 7 retrains. At 15 dB a short training at
  7200 fails three times in a row on about 2 lines in 5 (seeds 2206-2225:
  9 to 12 of 20 end at 7200, the rest go on to 4800), before Task 23 as
  after it: the training's start, not the policy. At the lowest agreed
  rate, 3 failures within 10 s end the call `V90M_END_RETRAIN_FAILED`
  (`retrain_failed`: 9600 alone agreed, 3 dB SNR, the end 6.6 s after the
  noise). A retrain that holds data over 2 s is a success, so a line that
  unlocks every few seconds retrained at one rate for ever (the Task 22b
  review's M2): now each retrain for a lost data mode (UNLOCKED, TRAINFAIL,
  NOCARRIER with the carrier back) counts in a 30 s window opened by the
  first (`HST_CY_N`, `HST_CY_MS`), and the 4th asks one agreed rate lower,
  or at the lowest ends the call likewise (`unlock_loop`: 10 dB SNR for
  0.8 s every 4 s at 12000: 3 retrains there, then 9600, 3 more, the end;
  18 retrains at 12000 in 70 s before). Untested against a real HST: what
  a Courier counts as good, and whether it retrains or marks (R2).
- **Watchdogs, cleardown and the end codes** (Task 23; spec "Retrain,
  watchdogs, cleardown and end codes", [OURS] each). Holding forward, 5 s
  without either back receiver locked ends the call `V90M_END_LOST` (5.0
  to 5.3 s after the caller's carriers stop). Holding back, 1 s without
  the forward carrier (the guard's output under -48 dBm0 or 10 dB under
  its level for 20 ms, `hst_frx_carrier`) asks 0x2R at the rate in force;
  5 s takes the forward channel back with a short training (a retrain),
  and the first rule follows: LOST 10.0-10.3 s after the caller went
  (`back_silent`); a caller gone at our switch to the back channel: taken
  back after 3 s, LOST at 8 s. No call stays up without a far signal for
  more than 12 s. A dropout over 200 ms in data ends the forward receiver
  in NOCARRIER (Task 16's hold); the carrier back without a training, our
  0x2R at once. The far end's LD (`far_ld`) ends the call
  `V90M_END_CLEARDOWN_FAR` when its carrier goes (the back receivers' lock
  lost within 300 ms; the forward carrier within 100 ms) or 2 s after our
  endpoint had it. Ours: the host releases the endpoint (LD 255), waits for
  `v90m_ec_tx_busy` 0 (at most 1 s), then `v90m_cleardown`; with MNP up the
  host's bits go on 30 ms holding forward, 200 ms holding back, then both
  carriers stop (`ours`: the caller has the LD both ways). `tx_busy` is 0
  from the LD's closing flag's first bit: a host that stopped the modem
  then would cut the flag (the caller saw an abort at 450 bit/s; Task 23
  found it). Every end has its `v90m_get_diag` error: `ANS_ERR_HST_LINK`
  (no MNP link in 15 s), `ANS_ERR_HST_CARRIER` (the watchdogs' LOST),
  `ANS_ERR_RETRAINS` (RETRAIN_FAILED), 0 for an orderly cleardown.
- **A dropout in a long AB starts the forward receiver** (0.7.3, the
  review's M1): the AB stage starts the receiver when AB ends after 16 ms
  (`HST_FRX_AB_MIN_MS`, a Courier's 72-symbol AB), and a silent dropout in
  a long AB ends it too: the receiver starts in the gap. Over a sweep of
  our 180-symbol AB with a dropout of 5 to 40 ms at 40 to 76 ms into the
  training (codes 4 and 6, 208 cells x 6 line variants), runs that reach
  data clean went from 661 (0.7.2, which restarted AB and often never
  started) to 1080 of 1248, but runs that reach data with a burst of bit
  errors from 3 to 118: 10 ms dropouts at 52 to 58 ms enter data at some
  139 ms with errors MNP takes back. Requiring AB to end into CD (power
  held) would give the gain up with the burst; holding the AB count
  through a short power drop instead is for 0.8.0.
- **AB louder than the data** (the Task 16 review's Minor F): the forward
  receiver's level, against which a fade or a loss is judged, is taken
  again where data mode begins, so a far end whose AB is up to 4 dB over
  its data loses nothing (`test_hst_fwd` `ab_loud`: AB 4 dB over, then a
  6.5 dB step down in data, kept; from AB's level it was a carrier loss).
  Our training itself (Task 23e) now takes the level again from the
  training's own points: qamrx's gain is set from AB's power; when 5 or
  more of the last 8 points are off and their power stays over 3 dB off
  A..D's for 8 symbols, the next 16 symbols' power sets the gain again
  (the equaliser's line and the points scaled by the root of the ratio; at
  most 4 such measures a training, each taking the level or not); the carrier's level starts 6 dB under AB's (the
  relative clamp 16 dB under AB) and is taken again then. Before, the pass
  edge was AB 3 dB under to 5 dB over the rest: 6 to 9 dB over gave
  TRAINFAIL (every point off the 4, nothing adapted), 10 dB and more
  NOCARRIER (the clamp 10 dB under AB's level), 4 dB under TRAINFAIL. Now
  `test_hst_fwd ab_sweep` passes every cell from AB 8 dB under to 14 dB
  over, codes 2..6 (3 seeds, 30 dB; 2 seeds at +7 Hz -100 ppm). On a 20 dB
  line, code 5 (the Task 23e review, 30 seeds): with AB 4 and 5 dB over
  the data, 18/30 and 25/30 pass, against 30/30 for BASE (no re-take, the
  points starting smaller: a smaller equaliser step) and 24/30 at equal
  levels; a re-taken training matches an equal-level one only from about
  6 dB off (10 seeds: 5..8 of 10 from 6 dB on, 8 at 0 dB). The training's
  own scale on a noisy line is untuned. Unchanged from BASE: AB 2 to 3 dB
  quieter than the data fails code 5 on a 20 dB line (10 seeds: 0/10 at
  -2 and -3 dB, before and after).
  Limits: measured on our own caller only, a real Courier's AB level
  against its data is unknown; a level 3 to 5 dB off that leaves the points
  on the 4 is not taken (the equaliser takes it, a slower training at code
  2); a level step of 10 to 16 dB later in a training is not clamped (a cut
  under -48 dBm0 still is); its points are off and nothing adapts on them.
- **The level-step residuals** (a 6.5 dB drop at 14400 on a 30 dB line
  loses the forward receiver's lock on about 15 % of calls: 6 of 40 seeds,
  the Task 23e review; Task 16 had counted about 5 in 96 over a mix of
  steps and rates; 14400 recovering slowly from some 100 ms
  dropouts): left as they are. An UNLOCKED is now a retrain at once (0x2R,
  counted as above), and a slow recovery keeps data mode while MNP
  resends. `levels` (`test_answer_hst`: -9 to -33 dBm0, +-100 ppm, 25 dB,
  60 s each with 25 reversals and 5 rate changes) delivers every octet at
  every level with no retrain beyond those asked.
- **Frame errors** are the endpoint's broken frames alone: hst-facts
  (P2:538) also counts 50 ms blocks of fill characters, which the spec
  narrows to CRC failures.
- **An attention superseded** (v42/mnp.c, [OURS], a departure from Annex
  A's "sent again every T401 until its LNA"). Holding back, a failed
  retrain's next 0x2R/0x3R replaces ours still waiting for its LNA, V(SA)
  moving on without it: that LNA comes on the forward receiver that
  failed, and waiting for it ran to N400 and the link's end. It does so
  only on evidence that the far end has the LN: the pump saw the
  holder's training for it (`ln_acted`, set only for the pump's own
  request and cleared at every LN sent, the endpoint's cap and reversal
  requests included), or it has gone twice (both lost on the healthy back
  channel: p squared). Sent once only, it is sent again at once, ahead of
  the new request, and superseded once that copy has gone: not at its
  T401, which holding back is 11 s at 450/9600 and more at 300 (our
  transmit rate is the back channel's), well over the 4 s bound (that
  bound does not run while the request waits; the wait itself is bounded
  next). The failure mode if the guess is wrong: our N(SA) one ahead of
  the far V(RA), so the far end acknowledges every later LN, a 0x1R too,
  and acts on none, for the rest of the call (`v42/test/test_ec_hst`
  `attn_lost`: our LN lost, then a new request: both reach the far end in
  order; the rule without the evidence fails it so). Task 23 and the
  interop runs (R2) watch for a far end that acknowledges attentions and
  does nothing.
- **A request behind our own 0x1R** (the final review's m2; `hst.h`
  `HST_RC_UNTAKEN_MS`, [OURS]). The supersede above covers 0x2R/0x3R
  only: while our 0x1R waits for its LNA, holding back, the endpoint takes
  no request of the pump's. Such a request untaken for 3 s counts as a
  failed retrain (an 0x2R: asked again, 3 failures then one agreed rate
  lower, at the lowest the end) or is dropped (an 0x3R). A far end that
  never acknowledges our 0x1R thus ends the call with
  `V90M_END_RETRAIN_FAILED` some 9 s an agreed rate after our first
  request (45 s from 14400 with five rates agreed; `test_answer_hst`
  `retrain_untaken`), where before the request waited for that LN's N400
  x T401, some 2 min at 450 bit/s. The cost: a far end that delays its LNA
  to our 0x1R by 3 s or more (an LNA lost, resent at T401) while our
  receiver asks for a retrain steps the rate down for nothing; never
  seen in simulation.
- **Receive throughput while the caller holds the fast channel** (Task 24
  run 4: some 700 B/s from BinModem at 14400 after its reversals, no
  frame lost or sent again). MNP's window, not a fault: the far sender
  may have k LTs of N401 octets unacknowledged, k 8 and N401 64 by default
  (`mnp.k`, `mnp.n401`, `mnp.opt` 2), 512 octets, and every
  acknowledgement (LA) crosses the 450 or 300 bit/s back channel: 11
  octets with flags in the standard layout, 195 ms at 450 bit/s; 6, 107
  ms, streamlined. Simulated (`sim/hstcall` holding the fast channel at
  14400 after its reversal, a full backlog, we send nothing; 20 s; a
  scratch harness over `test_answer_hst`'s line, not a committed test;
  hstcall sends no parameter 8, so these LAs are the standard layout's):
  867 octets/s with the 450 bit/s back channel, 726 with 300, against
  some 1640 the line carries (64 data octets in about 71 on the line,
  bit stuffing). Our LAs go back to back (102 in 20 s, every one moving
  N(R) on), and the sender is blocked by its window 65 % of the time: it
  sends its 8 LTs in some 310 ms and the LA covering the last of them
  reaches it some 280 ms later (the LA's length plus both receivers'
  delays), 8 LTs every 586 ms. Our acknowledging changes little: an LA
  on a 450 bit/s channel takes longer than the window's 8 LTs leave room
  for; holding each LA until k/2 LTs wait (A.7.3.3's other trigger), not
  sending it at once when we have nothing else to send, gave 947 and 762
  octets/s in the same runs, +9 and +5 %, not worth leaving the rule for.
  What does: N401 256, parameter 8 bit 0 (`mnp.opt` 3, `mnp.n401`
  256), when the caller's LR offers it (BinModem's does, parameter 8 = 3,
  [BM]): 1728 octets/s in the same run at 450 and at 300 bit/s. Not the
  default, because it is both ways (A.6.4.1.8): our own LTs on the back
  channel then carry up to 256 octets, 4.6 s each at 450 bit/s (7 s at
  300), and an LA waits behind the one going out; T401 grows with N401.
  For an ISP whose callers mostly download, `mnp.opt` 3 is the setting to
  try. k is at most 8 (`MNP_MAX_K`). BinModem's LR carries parameter 8 =
  3, so with our default (2) its calls agree the streamlined layout, LAs
  of some 107 ms, and still gave about 700 octets/s: its own delays (its
  back receiver, its turnaround) are longer than hstcall's.
- **The armed receiver's S detection** (`hst_frx_arm_switch`,
  `hst_frx_arm_train`; Task 22b, which found both limits of 22a's): a
  training is seen in data mode as S or S-bar, 16 symbols each the point
  two before and a quarter turn from the last, within 30 % of its power.
  22a's test of A..D themselves, 1.2 units, missed it when data mode held
  the carrier a quarter turn round (S showed as D A), when the data-mode
  loops had pulled the points 15 degrees and 1.2 dB off A..D a dozen
  symbols in, and on any line under some 20 dB (the difference of two
  points carries twice the noise). A random point passes the test with a
  chance of 1 in 8 at 4 points (TRN) and about 1 in 38 in data (16 to 128
  points, the tolerance taking in neighbours; the Task 22b review's Monte
  Carlo): 16 in a row, 4e-15 or less.

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

## Included tables (HST)

HST has no published specification. Its tables are in `modem/hst_tabs.c`,
generated by `tools/gen_hst_tabs.py` (`docs/history/hst-facts.md`, "Back
channel"):

- **The back channel's phase steps** (`hst_bstep`, 8 octets): the Courier
  firmware's constant 0x43705261 read a nibble at a time, as BinModem's
  author reports it (BF §3, [FW]); a fact, written as the generator's one
  number. Nothing else of BinModem's is used.
- **Everything else is our own design** [OURS], named in the generator's
  header: the transmit pulse (root-raised-cosine, beta 0.35, 10 symbols,
  Kaiser window; polyphase for 150 and 100 baud at 8000 Hz, and the
  receivers' matched filter read from it at 1600 Hz), the receivers'
  decimation band-pass (39 taps, 60 dB down only where it folds into the
  back channel at 1600 Hz), and the detector's 250-500 Hz band (at 1600
  Hz) and 600-3400 Hz high band (Kaiser-windowed FIRs). The Courier's own
  72-tap filter is unknown, so
  our pulse's band (274-476 Hz at 450 bit/s, 283-417 Hz at 300, at
  -20 dB) is chosen to sit under the forward channel's 600 Hz edge, not
  taken from it.

## Memory

- **The answering side's state sets the memory of a modem.** One modem,
  either role, is `v90m_mem_size()` octets: 106320 on x86-64, 106016 on
  ARMv5, within `V90M_MEM_SIZE` (108 KiB) on every port target. The
  calling side's state, whose modulations after V.8 share memory, is some
  52 KiB: shrinking the calling side further (such as a union inside the
  V.90 / V.92 analogue modem's state, some 6 KB) no longer lowers
  `v90m_mem_size()`; only a smaller answering state (its union of engines,
  or the V.34 and Phase 2 state outside it) would.
  (`docs/history/audit-0.6.md`, Task 29.)
- **HST's engine** (`hst_t`, in the answering side's union of engines) is
  9840 octets on x86-64 and 9808 on ARMv5, against the union's 45096 /
  45008 (V.92's engine); `v90m_mem_size()` is unchanged.

## Bounds and clocks

Every wait of a call is bounded, and every end of a call has an end code
(0.6.0). The limits a program may meet, beyond each standard's own:

- **Phase 2, answering** (V.90, V.92, V.34, x2, K56flex): the INFO
  exchange ends 20 s after its start (or a restart V.34 counts), and V.90's
  recovery (9.2.1.2.3) is tried at most 4 times, and a caller's INFO0a
  after the INFO0 exchange (9.2.1.2.1: it lost our INFO1d, or started over)
  takes Phase 2 back to INFO0 at most twice (0.6.1). Behind those, a backstop
  ends the call when V.90's or V.92's Phase 2 (a retrain's included) or
  V.34's lasts 30 s, or V.90's or V.92's Phases 3 and 4 last 60 s. A normal
  Phase 2 takes 3 to 5 s.
- **A retrain's Phase 2, calling** (V.90, V.92, V.34): each repeated INFO0
  of the answerer counts as one of Phase 2's 4 restarts, and a retrain's
  Phase 2 ends after 30 s: `V90M_END_RETRAIN_FAILED`. Start-up as a whole
  has the 60 s limit.
- **V.22bis, both roles:** the wait for the end of a far S1 ends after 1 s
  (S1 is 100 ms).
- **V.92 cleardown, answering:** a caller that never acknowledges our
  cleardown is given 3.5 s.
- **LAPM** (`v90m_ec`): `n400` recoveries in a row with I frames
  unacknowledged release the link (`V90M_EC_R_N400`, some 11 s with the
  defaults); a busy peer (RNR) is not bounded, by V.42 8.4.7
  ([error-control.md](error-control.md)).
- **Long calls:** the sample clocks and counters wrap as unsigned values or
  stop at their limit; the receivers bring their sample and symbol numbers
  back every 2^30 in data mode. No call length has a limit of its own; up
  to 0.5.0 the V.34 receiver stopped after 58 to 62 hours of data mode and
  the V.90 calling receiver after 74.6 hours. A host that keeps calling
  `v90m_process_*` after `V90M_EV_ENDED` meets no overflow either.
- **What is not bounded, by ruling:** an x2 call whose upstream decodes
  badly but holds its carrier stays up (no watchdog on x2's error rate,
  above); V.42 with T403 off (the default) does not notice a silent peer
  while nothing is unacknowledged: the modem's loss of carrier does.

## CPU

All figures are ARM instructions counted under qemu (ARMv5, gcc -O2);
cycles on a real ARM926 have not been measured. v90d's box, an ARM926EJ-S
at 400 MHz, ran 0.5.0's answering V.90 data mode at 32 % of its CPU, which
makes it about 122 M counted instructions a second. 0.6.0's figures
(`docs/history/audit-0.6.md`, "after Phase 3"; README, "Memory and
CPU"); 0.5.0's in brackets:

- **One V.92 call on the calling side is some 51 to 55 % of that box**
  (62.12 M a second at 0 ppm clock offset, 66.52 M at 100 ppm, data mode;
  0.5.0: 69.95 / 74.32); V.90 calling 38 to 42 % (46.97 / 50.95 M). One
  call fits with room; two V.92 calls at once (2 x 62.12 = 124.24 M) do
  not. (`docs/history/PROGRESS.md`, cpu.)
- The heaviest single tick of a calling V.90 / V.92 call was some 98 k
  instructions at 0.5.0 (8 % of a 10 ms block of the box), after the
  heavy designs were spread over many samples; not measured again.
- The answering V.92 modem measures the caller's gain once per B1u. In
  any call whose B1u does not sit well on the designed points at scale 1
  (21 of the 26 recorded calls that reach B1u, working ones among them)
  that is one tick of some 175 k instructions (14 % of a 10 ms block);
  14 k otherwise. Not yet spread over ticks.
- **x2 data mode: one x2 call per box.** 56.58 M instructions a second
  at 32000 (0.5.0: 82.57), about 1.8 times V.90 answering's 31.19 M
  measured the same way (`make cost-x2`, second 15 to 16 of the call).
  With V.42 running at both ends (`make cost-x2-v42`, our endpoint
  counted), at 32000 / 56000:
  58.89 / 61.50 M with no compression, 69.24 / 77.19 M with V.42bis,
  89.25 / 108.78 M with V.44, on text that compresses far beyond real
  traffic (a ceiling). Two x2 calls with V.42bis would need 2 x 77.19 =
  154.37 M of about 122 M, and even without compression 2 x 61.50 =
  123.0 M: one x2 call per box.
  The cause of the difference from V.90 is the upstream the client
  dictates: V.34 with the 64-state trellis, non-linear encoding and
  expanded shaping, which x2 gives the server no way to refuse.
  (`docs/history/PROGRESS.md`, x2 6 and x2 8; `docs/history/audit-0.6.md`.)
- **K56flex data mode costs about what V.90's does.** 32.12 M
  instructions a second at 56000 (0.5.0: 39.27), against V.90 answering's
  31.19 M (`make cost-k56`, ARMv5, second 20 to 21 of the call,
  after V.8bis). It asks the client in its MP for the upstream V.90 has
  (16 states, linear encoding, minimum shaping), and SM56 honours that. A
  client that ignores the request and sends x2's upstream is decoded (see
  "K56flex (answering)" above) and costs what x2 does: 58.60 M (0.5.0:
  84.43).
- **V.42:** an endpoint at 56 kbit/s both ways costs 5.21 / 9.08 / 9.48 M a
  second with no compression / V.42bis / V.44 (`make cost-v42`; before
  0.6.0's V.42 work: 7.96 / 11.81 / 12.19).
- **HST's forward receiver (0.7.0 work):** 25.43 M instructions a second
  in 14400 data mode (`make cost-hst-fwd`; the budget 26 M; 25.40 M before
  the gain loop's leak, Task 22c), unchanged by the switch by marks while
  none is armed. A switch by marks from 12000 to
  14400 costs 636 939 instructions in the 20 ms from where both codes are
  decoded (the new code's decoder run over 24 points kept, then both
  decoders, the switch 23 symbols in): 31.70 M a second for those 20 ms
  (the bench's loop taken off), against the plan's 40 M.
- **HST's back channel (0.7.0 work):** the two back
  receivers (450 and 300) on one band-pass, as the pump runs them while it
  holds the forward channel, cost 1.89 M instructions a second with the
  caller at 450 and 1.88 M at 300 (`make cost-hst-back`, the third second
  of `rig/bench_hst`'s recording, its own loop of 0.11 M taken off; 2.00 M
  with it); the target was 2.0 M, the spec's "about 1 M a second each".
  The worst case is not the locked one: echo (our forward signal at
  -16 dBm0) or noise at -30 dBm0 holding the carrier detect on with no
  back channel keeps both receivers acquiring, 1.90 / 1.91 M net (2.02 M
  with the loop; the Task 13 review's recordings). Task 12's receivers,
  with a 243-tap band-pass, were 3.70 M (some 1.1 M of that the bench
  reading its file inside the counted second). The detector of
  ANS_HST_WAIT (the two receivers, its 250-500 Hz band and the 189-tap
  600-3400 Hz band at every 4th sample) is 3.70 M, for at most W (650 ms,
  2000 ms while the band holds energy) when nothing else runs; its high
  band is 1.26 M of that. In ANS_HST the detector's receivers carry on
  without its two bands (`hst_det_rx`): the 1.89 M above.
- **HST holding the forward channel (0.7.0 work): 13.19 M instructions a
  second** (`make cost-hst`, target 15 M): a whole simulated call
  (`tools/rec_hst`: `sim/hstcall` calling, MNP up, the retrain to 14400,
  text both ways) replayed into the answerer alone through the public API
  in 10 ms blocks (`rig/bench_hst -c`), the second from 1 s into data at
  14400 counted, the bench's own loop and its endpoint's text included.
  By qemu's profile (`tools/qprof`) about 3.8 M is the forward
  transmitter (`v32tx_tick` with HST's 30-symbol pulse 3.15 M), 1.7 M the
  two back receivers, 3.3 M V.42 / MNP with the HST link (the link polled
  at every bit: `link_poll` 1.05 M, `mnp_hst_poll` 0.82 M, `v42ec_tx_bit`
  0.90 M), 1.7 M G.711 (encoding what we send, decoding what we hear),
  1.9 M the answerer's and the API's per-sample work. The first count was
  16.64 M: a bench that processed and polled a sample at a time (1.3 M of
  harness) and V.32's echo canceller's reference computed for HST too
  (1.12 M; `v32tx_tick` now makes it only with V.32's own pulse: HST has
  no canceller). Polling the HST link once per tick rather than per bit
  is the next lever (some 2 M). Task 23's run of the same bench: **14.44
  M** (14 441 425; the reversal, rate-change, watchdog and cleardown
  logic of Tasks 21 to 23 in the per-sample path since Task 19's count;
  not split further).
- **HST holding the back channel: 29.29 M instructions a second**
  (target 30 M; `make cost-hst`: `tools/rec_hst -R`, the caller taking
  the forward channel and keeping it, our endpoint writing nothing;
  `rig/bench_hst -k`, the second from 1 s into the caller's data at 14400
  coming in; 2688 octets received in 3 s). Task 23's run was 38.44 M.
  Task 23c's profile of that second (`tools/qprof`): the forward receiver
  25.5 M (its trellis decoder `v32_vit_step` 9.35 M, qamrx's interpolator,
  equaliser output and update 9.5 M, the guard 3.14 M), our idle forward
  transmitter 2.87 M (`v32tx_tick` filtering a line of zeros), the back
  transmitter 1.72 M, the HST link polled at every bit 2.3 M
  (`link_poll`, `mnp_hst_poll`), G.711 1.65 M, the answerer's and the
  API's per-sample work and MNP the rest. Two cuts, both with the same
  output to the bit (every golden, bench hash and replay identical):
  the trellis decoder as `hst_vit_step` (each subset's nearest point by
  rounding on its grid, `hst_vgrid_*`; the add-compare-select written
  out): 9.35 -> 3.32 M; the forward transmitter stopped and rung out keeps
  its count alone: 2.87 -> 0.15 M. The forward receiver alone in 14400
  data is now 19.37 M (cost-hst-fwd's -r). Not done, the next levers: the
  link polled per tick rather than per bit (about 2 M, in v42/), a
  shorter interpolator for HST in qamrx (about 1.6 M, changes the
  receiver's output). The equaliser at T/2 is not one: qamrx's equaliser
  is pass-band, and at 4800 Hz a 600-3000 Hz signal's image falls on
  1800-4200 Hz, half the band (Task CPU-frx measured 5.4 dB through the
  receiver's start against 33.0 at T/3).
- **The 0x3R parallel decode: 31.98 M a second for its 20 ms, the whole
  modem** (target 40 M; Task 23's run 32.04 M for the forward receiver
  alone, about 45 M on this basis). `make cost-hst` gives it as the
  forward receiver's 20 ms (cost-hst-fwd's -p: 443 923 instructions,
  22.05 M a second, the two trellis decoders and the 24 points replayed
  into the new one) plus the rest of holding back (holding back less the
  forward receiver alone in 14400 data: 9.92 M).
- **HST holding the forward channel after Task 23c: 14.47 M** (target 15
  M; 14.44 M before it: the transmitter's stopped test on every sample).

## Not implemented

- Modem-on-hold, either role. V.92 Quick Connect and its short Phase 2 only
  as the answering digital modem (above, "V.92 Quick Connect"): not QC2a,
  not QC1d, not the calling side.
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
- The answering V.90, V.92, x2 and K56flex modems' echo search (echo delay
  -1) reaches about 3510 samples (439 ms) after the sample is sent; a far
  echo later than that is not cancelled. With a round trip over 128 ms it
  covers only the round trip less 128 ms to the round trip plus 32 ms, so
  a near echo earlier than that is not cancelled
  ([line-interface.md](line-interface.md), "Echo delay"). V.34 answering
  has a second canceller for the far echo and reaches about 500 ms.
- The answering side (V.34, V.90, V.92) still reads the caller's L2 for
  460 ms of the 500 V.34 11.2.1.2.8 / V.90 9.2.1.1.5 allow; the calling
  side reads 280 ms since 0.6.0. Shortening it moves the answerer's Tone A
  and its probe analysis, which changes 624 of the 636 replays of recorded
  calls, so it waits for those to be checked against the recordings.
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
