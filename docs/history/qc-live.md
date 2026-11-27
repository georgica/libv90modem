# Quick Connect: the live runbook (0.7.4)

Status: run on 2026-11-27 against a local nsdiald test build; 30 of 30 QC
requests accepted. This file holds status fields only, no recordings.

## Setup

- **Caller:** `<binmodem-build>/modem-sip` (BinModem
  `2a1bfcc`). Flags, from its own `--help` and `modem-sip.rs`: `--accounts
  <file>`, `--account <name>`, `--type <line>` (repeatable, typed a second
  apart), `--send <text>`, `--hold <s>`, `--seconds <s>` (bounds the whole
  run), `--memos <file>` (V.92 memos, keyed by the dial string, one line
  each: `<dial string> uqts=N law=mu|a rtd=MS pcm_up=0|1`), `--record <wav>`.
  Environment: `BINMODEM_V92_QC` (`always`, or unset for recognised
  connections only), `BINMODEM_V92_TONEQ_MS`. The facts are in
  `qc-facts.md`, "BinModem".
- **Accounts file** `sip.txt` (the section format: `[test]`, then
  `registrar = 127.0.0.1:5072`, `username`, `password`, `register = no`;
  `laws = ...` only for an A-law run). Kept outside the repository.
- **Target:** the nsdiald test build, branch `qc-local-test` at 170f4a2,
  linked to libv90modem `v074` at 13ecce7, listening on 127.0.0.1:5072
  (podman, host network, no tun, no routing, no recording). Its modes: `V90M_MOD_V92` on
  (with V.90 and V.34); K56flex on for runs 7 and 8 only.
- **Dial string:** always `sip:modem@127.0.0.1:5072`, the memo key.
- **Corrections found by the runs:**
  - Every call needs `BINMODEM_V92_PCM_UP=1`. Without it BinModem never asks
    for PCM upstream, so the call is V.90, no memo gets `pcm_up=1` and no
    short Phase 2 happens.
  - Run 3's `v8bis_ack 1` needs K56flex on: with it off our side sends no CRe
    and BinModem has no V.8bis to answer (`v8bis_ack 0`, the call is run 1's).
    With K56flex on, K56flex is offered 3 times but not agreed, and QC is
    still offered and accepted.
  - K56flex on without `+A8E` (run 7): BinModem ignores our CRe and its CM
    starts 1.8 s later (6.82 s against 5.05 s), so the call takes 16.3 s
    online against 13.9-14.5 s with K56flex off.
  - BinModem has no PPP client: it sends `hello` as text and the host logs
    its menu. PPP did not log in on any call; error control (LAPM, V.44) was
    up on every call.
- **Privacy:** no `--record`. If one is needed to see Phases 1 and 2, it is
  cut at the end of INFO1a, kept under `build/`, and only its sha256 enters
  this file. No recording past error control up.
- **Common command**, per run, with `M=build/qc-memos/<run>.txt`:

      BINMODEM_V92_PCM_UP=1 modem-sip --accounts sip.txt --account test \
        --memos $M \
        --type "AT+MS=V92" [--type "AT+A8E=,,,1"] \
        --type "ATD sip:modem@127.0.0.1:5072" \
        --send "hello" --hold 20 --seconds 120

  `--type "AT+A8E=,,,1"` goes before the ATD, only for the runs marked
  A8E on. `BINMODEM_V92_QC=always` is set in front of the command only where
  a run says so. Start each run from a fresh memo file unless the run says
  to keep it.
- **What to read** (nsdiald's log, one line per call): `v90m_get_qc`
  (`heard`, `accepted`, `toneq_ms`, `short_p2`, `fallback`, `bypass`,
  `v8bis_ack`), `v90m_get_k56`, `v90m_get_stats` (`odp_bypass`), the connect
  line (modulation, rates), and BinModem's stderr.

## Procedures

Runs 2, 2a, 4 and 5 start with the memo-making call, as numbered steps.

### Run 1: QC always, no memo, A8E off

1. `rm -f $M`. `BINMODEM_V92_QC=always`, common command, A8E off.
2. Read the nsdiald line and the connect line.
3. Pass: `heard 1, accepted 1, toneq_ms > 0, short_p2 0` (BinModem asks for
   short Phase 2 only with a memo that recorded PCM upstream, never with
   no memo), V.92 data, `bypass` equal to both P bits (LAPM both ends:
   `odp_bypass 1`).

### Run 2: default, A8E off, memo kept

1. `rm -f $M`. No `BINMODEM_V92_QC`. Common command (the memo-making call).
2. Pass: full start-up, `heard 0`, V.92 data with PCM upstream. `$M` now
   holds one line for the dial string with `pcm_up=1`; if it does not, the
   run stops here and the result is "no memo written" (BinModem's rule: a
   memo is written when a V.92 call it placed reaches PCM upstream data).
3. Same command again, same `$M`, no other change.
4. Pass: as run 1 (`heard 1, accepted 1, toneq_ms > 0`), plus `short_p2 1`.

### Run 2a: the short Phase 2 call, QC forced

1. `rm -f $M`. Common command with A8E off (memo-making call), then
   `BINMODEM_V92_QC=always` with the same `$M`.
2. Pass: `short_p2 1`, `accepted 1`, V.92 data, no retrain.

### Run 3: QC always, A8E on

1. `rm -f $M`. K56flex on in the test build. `BINMODEM_V92_QC=always`,
   common command with A8E on.
2. Pass: `v8bis_ack 1` (its MS `21 89 80 80 81 c2` asks ACK(1)), then as
   run 1.

### Run 4: default, A8E on, memo kept

1. `rm -f $M`. K56flex on in the test build. Common command, A8E on (the
   memo-making call).
2. Pass: V.8bis line as run 3 (`v8bis_ack 1`), full start-up, `heard 0`,
   `$M` written.
3. Same command again with the same `$M`.
4. Pass: as run 2 step 4, with `v8bis_ack 1`.

### Run 5: a stale memo

1. Make the memo as run 2 steps 1 and 2 (A8E off).
2. Edit `$M`: change the line's `uqts=` to another value and `rtd=` to
   about three times its value (for example `rtd=1500` over 500) so the
   stored parameters no longer fit. Keep `pcm_up=1`.
3. Same command, same `$M`.
4. Read: data reached or not, and which way: our short Phase 2 recovery, or
   our retrain, or BinModem's rescue (it waits 2.5 s plus the round trip,
   forgets the memo and sends CM with no QC1a; `qc-facts.md`). Pass: data
   reached; record which path and `fallback`.

### Run 6: TONEq cut short

1. `rm -f $M`. `BINMODEM_V92_QC=always BINMODEM_V92_TONEQ_MS=30`, common
   command, A8E off. BinModem raises 30 to its 50 ms minimum, still below
   our 100 ms hold.
2. Pass: `heard 1, accepted 1, fallback 1` (our TONEq hold not met), then
   V.8 and data by the ordinary path.

### Runs 7 and 8: K56flex on

1. nsdiald's test build with K56flex on (and V.92).
2. Run 7 is run 1, run 8 is run 3, with that build.
3. Read `v90m_get_k56` too. Record what happens to QC after our CRe (a
   K56flex call is not offered QC, `qc-facts.md` readings 1); pass is a
   normal call with `heard`/`accepted` as the design says, no hang.

### Later (not this plan)

Conexant CX93001 and U.S. Robotics 5686 with `AT+PQC` (scope 5.3).

## Results

Status fields only. Every call: `lapm 1`, `odp_bypass 1`, `bypass 1` where
`accepted 1`. Counts are calls. "Online" is the host's answer-to-LAPM time.

| Run | Config | Calls | Result |
| --- | --- | --- | --- |
| 1 | always, A8E off | 2 | PASS: heard 1, accepted 1, toneq_ms 607-647, short_p2 0, V.92 56000/42666, online 13.9 and 14.5 s |
| 2 | default, A8E off, memo | 6 | PASS: first call of a fresh memo heard 0 (full start-up), then heard 1, accepted 1; short_p2 0 with a `pcm_up=0` memo and 1 with `pcm_up=1` |
| 2a | always, with memo | 2 | PASS: short_p2 1, V.92, 0 retrains, 11.7 and 12.0 s |
| 3 | always, A8E on, K56flex on | 2 | PASS: v8bis_ack 1, then as run 1; 18.3-18.4 s |
| 4 | default, A8E on, memo, K56flex on | 5 | PASS: v8bis_ack 1; full start-up first (heard 0), then heard 1 and short_p2 1; 15.6-15.9 s |
| 5 | stale memo | 8 | PASS (data reached) but no recovery path exercised, see below |
| 6 | TONEQ_MS=30 (raised to 50) | 2 | PASS: heard 1, accepted 1, toneq_ms 0, fallback 1, then V.8 and a full start-up |
| 7 | run 1, K56flex on | 2 | PASS: as run 1, 16.3 s |
| 8 | run 3, K56flex on | 2 | PASS: the same configuration as run 3, two more repeats |
| A-law | A-law account, memo | 3 | PASS: full start-up, then short Phase 2 with Table 18, A-law, V.92 56000/41333 |
| base | QC off | 5 | baseline: see the start-up times and the V.90 drops below |

QC requests accepted: 30 of 30 (31 of 31 with the K56flex-off reference
call r3off, which is excluded from the count). Short Phase 2: 19 of 19 calls with a
`pcm_up=1` memo, none of the 11 without one. Run 6 fell back as designed
(2 of 2).

### Start-up times (answer to LAPM up, K56flex off, mu-law)

| Case | Online | Saved |
| --- | --- | --- |
| Full start-up, trained cleanly (5 calls) | 15.5 s | - |
| QC with a full Phase 2 | 13.9-14.5 s | 1.0-1.6 s |
| QC with short Phase 2 | 11.7-12.0 s | 3.5-3.8 s (about 23 %, 22.6-24.5 %) |
| A-law, QC with short Phase 2 | 11.7-12.0 s | the same |
| K56flex on, `+A8E`: full | 19.7 s | - |
| K56flex on, `+A8E`: QC | 18.3-18.4 s | 1.3 s |
| K56flex on, `+A8E`: QC with short Phase 2 | 15.6-15.9 s | 3.8-4.1 s |
| K56flex on, no `+A8E`, QC | 16.3 s | - |

Repeats differed by at most 0.6 s. The container had no real-time
scheduling and the host load was about 2.

### Run 5 did not exercise a recovery path

The stale memos never made the short start-up fail: a different UQTS (87,
61) is a different legal request and we honoured it; an RTD of 40, 1500 or
5000 ms changed nothing on a loopback round trip of about 0.42 s; a memo
`law=a` on a mu-law call is overridden by the call's law and BinModem
rewrote it. Data was reached 8 of 8, by the ordinary short Phase 2. Our
short-Phase-2 recovery, our retrain and BinModem's rescue are therefore
proven in simulation only.

### Observation: V.90 drops in calls without QC (not confirmed)

4 of 9 calls with a full start-up (no QC) ended as V.90 56000/28800 after a
retrain: BinModem reached V.92 Phase 3, no Jp came for 4.4 s and the host
retrained. In all four BinModem's stderr said the downstream is read on
"sample 1 of every 2"; in the 36 other calls it said sample 0. No QC call
was affected (0 of 30), and it predates 0.7.4. The likely side is BinModem's
16 kHz sample phase, but that is not confirmed without a recording, which we
do not take. Not a QC fault, and no server change; worth a word to the
BinModem developers.
