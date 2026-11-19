# Changelog

What each version of libv90modem changes for a program that uses it. The
API is not stable before 1.0: a 0.x version may change it, and every
change is listed here. Rebuild against the new header with every version;
`V90M_MEM_SIZE` and the configuration struct have changed in each.

"Simulation and own answerer only" means: tried against simulated lines and
this library's own answering modem, never against a real modem or server.

## 0.3.0 (2026-11-19)

The calling side gains V.92 and Bell, retrains and rate renegotiation in
V.90 / V.92, and fixes from comparing it with real callers' recordings.
Still simulation and own answerer only, all of it.

Added (calling role):
- V.92 as the analogue modem (`V90M_MOD_V92` with `V90M_MOD_V90`): PCM
  upstream when the answerer offers it in Phase 2, V.90 otherwise; rate
  renegotiation begun by the answerer; cleardown either way.
- Bell 103 and Bell 212A (`V90M_MOD_BELL103`, `V90M_MOD_BELL212A`, not in
  the defaults): on the 2225 Hz Bell answer tone, or on a carrier in the
  Bell answer channel when only Bell is offered; Bell 212A falls back to
  Bell 103 when no 1200 bit/s answer comes. The 212A handshake follows
  V.22; it was not checked against a Bell 212A specification. V.8 never
  falls back to Bell.
- V.90 and V.92: retrains begun by either end (`v90m_retrain`, or the
  answerer's Tone B) and rate renegotiation begun by either end; the
  modem begins one of its own when its receiver's error moves far from
  what the rate was chosen for, and a retrain when the line's level moved
  or the receiver is lost. Three retrains without a minute of data mode
  between them end the call.

API changes:
- `V90M_ROLE_ORIGINATE` accepts `V90M_MOD_V92` (only with
  `V90M_MOD_V90`: `V90M_ENOTSUP` without it), `V90M_MOD_BELL103` and
  `V90M_MOD_BELL212A`; nothing is refused with `V90M_ENOTSUP` any more but
  V.92 without V.90.
- `V90M_END_RETRAIN_FAILED` (appended to `v90m_end`): a calling V.90 /
  V.92 retrain failed, or one more was due past the limit.
- `v90m_retrain`: in the calling role also V.90 and V.92.
- `v90m_reset(m, NULL)` returns `V90M_EINVAL` (it crashed before).
- **Changed:** `v90m_ec_send_break` returns `V90M_OK` or `V90M_ESTATE`
  (it returned 0 or -1, and -1 is `V90M_EINVAL`). A caller testing for
  0 is unaffected; one testing for -1 must test for `V90M_ESTATE`.
- **Changed:** the answering role now refuses what the calling role
  refused: no modulations at all (`V90M_EINVAL`), and `V90M_MOD_V92`
  without `V90M_MOD_V90` (`V90M_ENOTSUP`). Before, it accepted both.
- `V90M_MEM_SIZE` 112 KiB -> 120 KiB (the calling side's receiver).
- The static library's internal symbols are renamed `v90m__i_*` instead
  of made local (some targets' relocations need them global); a program
  must not define names that begin `v90m__i_`. Still only the `v90m_*`
  functions are meant for use.
- The header's comments now say, per field and function, which role it
  applies to and every error return.

Fixed (calling role):
- The CM now carries the PSTN access category whenever it carries the PCM
  one, as every real V.90 / V.92 caller's CM does (V.8 6.3).
- The first JM after ANSam was lost (its receiver read part of the answer
  tone as a character); it is read now (V.8 8.1.2).
- V.90: data mode collapsed in some calls (the DIL's order, the band edge's
  image in the equaliser design, a stale channel estimate after a hold);
  the DIL's levels were snapped with the wrong gain behind a pad; the V.92
  timing loop moved during the DIL.
- V.32bis: S was taken on the end of the answerer's AC, and our TRN was
  too short for the answerer to choose the rate by.
- The calling side's heavy computations (the equaliser design, the DIL
  analysis, the CPt / CP designs) are spread over many samples: the
  heaviest single sample on ARMv5 went from 9.3 M to under 100 k
  instructions, V.92 data mode from 114 M to 74 M a second.

Fixed (answering role):
- V.92: bursts of upstream errors at 34666 to 37333 bit/s with a G.711
  codec on the receive path (the receiver's baseline follower).
- V.92: PCM upstream was dropped for the rest of the call after a retrain
  the answering modem began by itself.
- Phase 2's start and the end of probing cost half what they did on ARM
  (the same output).

Build and tests:
- `examples/call_loop.c`: a calling modem and an answering modem back to
  back through the public API; built by `make`, run by `make check`.
- `make check-ubsan`; `make check-arm` runs its programs in parallel;
  `tools/check-ports.sh`, the port matrix (16 targets, gcc and clang,
  little- and big-endian, 32- and 64-bit, win32): every target but m68k
  (an emulator fault) prints what the host prints.
- `CHANGELOG.md`.

## 0.2.0 (2026-11-17)

The calling role, from the moment the far end has answered (dialling and
call progress stay the program's). Simulation and own answerer only, all
of it. Bell on the answering side.

Added (calling role):
- V.8: CI (optional), CM / JM / CJ; an answer tone detector that tells
  ANSam from ANS.
- V.21, V.22 and V.22bis, after V.8 or after a pre-V.8 answer tone; V.22bis
  retrain.
- V.32bis and V.32: start-up (also Annex A after a pre-V.8 answer tone),
  retrain, the rate renegotiation the answerer asks for.
- V.34: Phase 2 with our own INFO1c decision, retrain, rate renegotiation,
  cleardown.
- V.90 as the analogue modem: the PCM downstream receiver, the DIL
  measured (pads, robbed bits, the law), CP designed, data both ways; the
  answerer's rate renegotiation followed; cleardown either way. No retrain
  yet.

Added (answering role):
- Bell 103 (`V90M_MOD_BELL103`, not in the defaults): a caller without
  V.8 that sends 1270 Hz gets 300 bit/s FSK. Tried with a real modem.
- Bell 212A (`V90M_MOD_BELL212A` with `V90M_MOD_V22`, not in the
  defaults): 2225 Hz after 4 s of an unanswered V.22bis answer sequence; a
  caller that then starts as V.22 does is a Bell 212A call at 1200 bit/s.
  Tried only with a modem that would also have taken V.22.

API changes:
- `V90M_ROLE_ORIGINATE` works (it gave `V90M_ENOTSUP` in 0.1.0) for V.21,
  V.22, V.22bis, V.32bis, V.34 and V.90; V.92 and Bell there still gave
  `V90M_ENOTSUP`.
- `v90m_cfg.send_ci` (calling: send CI before the answer tone), placed
  before `user`: the struct's layout changed.
- `V90M_END_NO_ANSWER_TONE`, `V90M_END_NO_JM`, `V90M_END_NO_COMMON`
  (appended, calling only).
- `V90M_MOD_BELL103` (0x0040), `V90M_MOD_BELL212A` (0x0080);
  `V90M_CONN_BELL103` = 8 and `V90M_CONN_BELL212A` = 9 (appended).
- The rate ceilings mean what we send (`max_down_bps`) and what we receive
  (`max_up_bps`) in either role; V.32bis takes the lower of the two both
  ways.
- `v90m_retrain` and `v90m_cleardown` in the calling role (V.22bis at 2400
  bit/s, V.32bis, V.34; cleardown in V.34 and V.90).
- `V90M_MEM_SIZE` 100 KiB -> 112 KiB.

Changed (answering role):
- The echo delay measurement and the retrain tone watch are one shared
  piece each (from v90d 0.4.1); the echo delay is looked for over a longer
  history.

## 0.1.0 (2026-11-16)

The answering modem of v90d as a library of its own, with a public API.

- Answering: V.8; V.92 and V.90 as the digital (server) side; V.34;
  V.32bis and V.32; V.22bis and V.22; V.21; callers without V.8 (the
  V.22bis answer sequence, V.32bis AA, V.21 after 8 s). Retrains in V.90,
  V.92, V.34 and V.32bis; rate renegotiation; cleardown in V.90, V.92 and
  V.34. Tried against real
  modems (a Conexant CX93001, a U.S. Robotics 5686 and a U.S. Robotics
  56K of 1998).
- V.14 start-stop framing and V.42 error control (LAPM, detection phase,
  start-stop fallback, breaks) with V.42bis and V.44 compression, as
  objects of their own on the modem's bit pump.
- API: create, or init in the caller's memory (`V90M_MEM_SIZE`, 100 KiB),
  reset, destroy; a configuration struct (role, modulations, law, linear
  receive, transmit level, power and rate ceilings, echo delay); block
  calls for G.711 codewords, a linear receive side, or linear both ways,
  any block length; events and a state poll; stats. The calling role gave
  `V90M_ENOTSUP`.
- Static and shared library, pkg-config file, `make install`; the
  `v90m-replay` tool that plays a recorded call into the modem; seven
  recordings of real calls as test fixtures.
