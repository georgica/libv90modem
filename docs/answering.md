# Answering

`V90M_ROLE_ANSWER`, the default role: the called modem, and for V.90 and
V.92 the digital (server) side. This is the side that has answered real
modems: a Conexant CX93001, a U.S. Robotics 5686 and a U.S. Robotics 56K of
1998, in calls to the library inside v90d on a Grandstream HT802 (the
table in the top-level README, "Tried on real modems"). Seven of those
calls are kept as test fixtures.

How it is built is in `DESIGN.md`, sections 4.1, 4.2 and 4.2a.

## What it offers

`v90m_cfg.modulations`; the default (`v90m_cfg_default`) is everything but
V.92 and Bell:

| flag | what | default |
|---|---|---|
| `V90M_MOD_V90` | V.90, the digital side: PCM downstream, V.34 upstream | on |
| `V90M_MOD_V92` | V.92 PCM upstream on offer; only with `V90M_MOD_V90` (alone: `V90M_ENOTSUP`) | off |
| `V90M_MOD_V34` | V.34 | on |
| `V90M_MOD_V32BIS` | V.32bis and V.32 | on |
| `V90M_MOD_V22` | V.22bis and V.22 | on |
| `V90M_MOD_V21` | V.21 | on |
| `V90M_MOD_BELL103` | Bell 103, for a caller without V.8 | off |
| `V90M_MOD_BELL212A` | Bell 212A, with `V90M_MOD_V22`, for a caller without V.8 | off |
| `V90M_MOD_X2` | x2, U.S. Robotics' 56k of 1997, as its server; only with `V90M_MOD_V34` (without: `V90M_ENOTSUP`) | off |

At least one is required (0 gives `V90M_EINVAL`).

## The start of a call

200 ms of silence, then ANSam with phase reversals and V.8: the caller's
CM, our JM, its CJ. The menu decides:

- **V.90**, when the caller offers PCM (an analogue V.90 modem): Phase 2,
  then Phases 3 and 4, then data. Downstream PCM at 8000 symbols a second;
  upstream V.34 at 3000 or 3200 symbols a second.
- **V.92**, with `V90M_MOD_V92` on: the same Phase 2 says so, and a caller
  that is V.92 too and selects PCM upstream gets V.92 Phases 3 and 4 and
  data, PCM upstream at 24000 to 48000 bit/s. Every other caller goes the
  V.90 or V.34 way. Two V.92 modems that both named LAPM in V.8 skip V.42's
  detection phase (V.92 9.3.1): `v90m_state.odp_bypass` is 1, and the V.42
  endpoint should then start without detection
  ([error-control.md](error-control.md)).
- **V.34**, **V.32bis**, **V.22bis / V.22** or **V.21**, as V.8 selects.
- **No V.8 at all**: the V.22bis answer sequence after ANSam, V.32bis AA,
  V.21 after 8 s.
- **Bell 103** (`V90M_MOD_BELL103`): a caller without V.8 that sends 1270 Hz
  (the Bell 103 mark) gets 300 bit/s FSK on 2225 / 2025 Hz, start-stop and
  no error control, as V.21.
- **Bell 212A** (`V90M_MOD_BELL212A` with `V90M_MOD_V22`): when the V.22bis
  answer sequence has gone unanswered for 4 s by a caller without V.8, the
  2225 Hz Bell answer tone replaces the unscrambled ones for 4 s; a caller
  that then starts as V.22 does is a Bell 212A call at 1200 bit/s.
- **x2** (`V90M_MOD_X2` with `V90M_MOD_V34`, mu-law only): when the
  caller's CM offers V.34 and no PCM, our JM and INFO0a are an x2
  server's. A U.S. Robotics x2 client then sends its marker after our L1
  and the call goes x2 (below); any other caller still gets V.34, but
  sees an x2 server's JM (carrier inverted after each JM) and INFO0a, not
  the ones it would get without x2. Not yet tried on a live non-x2 V.34
  call.

`v90m_state.used_v8` says whether the caller spoke V.8.

## What the program sees

- `V90M_EV_CONNECTED`: data mode reached; `v90m_state` has the modulation
  (`V90M_CONN_*`) and `tx_bps` / `rx_bps` (our transmit / receive rate;
  for V.90 and V.92 that is downstream / upstream).
- `V90M_EV_DATA_STOPPED` and `V90M_EV_DATA_MOVING` around every retrain and
  rate renegotiation; `v90m_state.retraining` during a retrain.
- `V90M_EV_RATES` when the rates or the modulation changed.
- `V90M_EV_ENDED` once, with `v90m_state.end`: `V90M_END_FAILED` (no
  connection was made), `V90M_END_LOST`, `V90M_END_CLEARDOWN_FAR`,
  `V90M_END_CLEARDOWN_OURS`, `V90M_END_X2_FAILED` (x2, below). The other
  end codes are the calling role's.

`v90m_get_stats` adds the receiver's signal to error (`snr_db10`, 0.1 dB),
the round-trip delay measured in the handshake (`rtd`, samples: V.34, V.90,
V.92), the echo delay in use (V.90 / V.92), and the counts of retrains and
of rate renegotiations that stopped the data (V.90, V.92).

## x2

x2 has no published specification; it is built from the facts in
`docs/history/x2-facts.md`. It is offered only with `V90M_MOD_X2` and
`V90M_MOD_V34`, answering, to a caller whose CM offers V.34 and no PCM, on
a mu-law line: never with `alaw` set. Robbed-bit lines are not handled.

- **Start-up.** After the client's marker: its training, our sync frames
  and records, then data at the rate the client asks for, at first
  32000 bit/s (rate index 1). `V90M_EV_CONNECTED` with `V90M_CONN_X2`;
  `tx_bps` is the downstream payload (32000 to 56000), `rx_bps` the
  client's V.34 upstream (up to 28800). U.S. Robotics prints a rate one
  step of 1333 above the payload (33333 for 32000); the emulated
  Courier's CONNECT figure did not follow our rate at all (48000 or
  50666; `docs/history/x2-facts.md`, "Harness").
- **Rate changes.** The client asks for a new rate in data mode (a
  Courier about 12 s in, and again later); we answer at the rate and for
  the pad (none, 3 dB, 6 dB) its request names: `V90M_EV_DATA_STOPPED`,
  then `V90M_EV_RATES` and `V90M_EV_DATA_MOVING`, about 0.75 s later.
- **No way back to V.34.** Once the marker has come, a start-up or rate
  change that does not complete, or a request we cannot serve, ends the
  call with `V90M_END_X2_FAILED`. `v90m_retrain` gives `V90M_ESTATE`;
  `v90m_cleardown` ends the call at once (`V90M_END_CLEARDOWN_OURS`).
- **What has been shown.** At 32000, decoded by the emulated U.S.
  Robotics Courier, V.42 data acknowledged. Above 32000, octet-identical
  to the U.S. Robotics I-modem and D-Modem's server; the published
  courier-emu decodes no server above index 1; not yet confirmed on a
  real modem ([limits.md](limits.md)).
- **CPU.** x2 data mode costs about twice V.90's (the upstream the client
  dictates): one x2 call per ARM926 box ([limits.md](limits.md)).

## Retrains, rate renegotiation, cleardown

- **Retrains** in V.92, V.90, V.34 and V.32bis, begun by either end.
  `v90m_retrain` asks for one in data mode of those (`V90M_ESTATE`
  otherwise, or while one is under way). In V.90 the far end's Tone A is
  listened for through Phase 4 and data mode; our own retrain is 70 ms of
  silence and Phase 2 again from Tone B. A call is let go at the fourth
  retrain in a row (`DESIGN.md`, 4.2).
- **Rate renegotiation** in V.32bis, V.34, V.90 and V.92 (the standards
  table of the top-level README); `v90m_stats.renegotiations` counts those
  of V.90 and V.92. In V.90 and V.92 the caller's (its S) is followed;
  the upstream rate is ours to set, and when the receiver's error stays
  above what the rate needs for a second the modem renegotiates with a
  lower ceiling; when nothing lower is left, or nothing is received, it
  retrains.
- **V.92 after a retrain.** A retrain runs V.92's Phase 2 again and the
  caller selects again: PCM upstream stays on offer after any retrain of a
  call that reached V.92 data mode, whoever began it. It is withdrawn (the
  call goes on as V.90) when the V.92 start-up never reached data mode,
  when the V.92 engine failed, or when it retrains because the upstream did
  not hold even the lowest PCM rate.
- **Cleardown** in V.90 (9.7), V.92 (9.11) and V.34 (11.7), either way:
  `v90m_cleardown` returns `V90M_OK` when it has begun, and
  `V90M_EV_ENDED` follows within a fraction of a second.
  `V90M_ESTATE` means there is nothing to send (V.21, V.22, V.22bis,
  V.32bis, Bell, or not in data mode): just stop calling
  `v90m_process_*`.
- **Carrier.** V.92 has no carrier detector of its own in data mode; a
  carrier watch ends the call when the caller is gone.

## Configuration that matters here

- `codec_linear_rx` and `max_tx_code` are answering only
  ([line-interface.md](line-interface.md)).
- `max_up_bps` caps what we receive: the V.34 upstream, or V.92's PCM
  upstream (up to 48000; 33600 holds it at 33333). `max_down_bps` caps the
  PCM downstream.
- `echo_delay` -1 measures the driver's delay in every start-up.

## Limits

- No V.92 quick connect, short Phase 2 or modem-on-hold.
- K56flex is not spoken; a K56flex-only modem connects at V.34
  (`DESIGN.md`, 4.2b). x2: above, and its limits in [limits.md](limits.md)
  (A-law, robbed bits, no way back to V.34, the free sign).
- No fax (V.17, V.29, V.27ter), no V.23, no half-duplex V.34.
- Bell 212A answering was tried only on a test build with the 2225 Hz tone
  from the start, by a modem that would also have taken V.22.
- The V.92 upstream design's noise model is about 2 dB short at the top
  upstream rates, on the simulator's lines ([limits.md](limits.md)).
- Only the three modems above have called it; none of them in x2.
