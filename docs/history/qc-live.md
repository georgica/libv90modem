# Quick Connect: the live runbook (0.7.4)

Status: written, nothing run yet. Every row of the results table below says
"not run yet". The target is a local nsdiald test build, which is being set
up; this file is filled in only from status fields.

## Setup

- **Caller:** `/home/georgica/hst-wt/build/binmodem/modem-sip` (BinModem
  `2a1bfcc`). Flags, from its own `--help` and `modem-sip.rs`: `--accounts
  <file>`, `--account <name>`, `--type <line>` (repeatable, typed a second
  apart), `--send <text>`, `--hold <s>`, `--seconds <s>` (bounds the whole
  run), `--memos <file>` (V.92 memos, keyed by the dial string, one line
  each: `<dial string> uqts=N law=mu|a rtd=MS pcm_up=0|1`), `--record <wav>`.
  Environment: `BINMODEM_V92_QC` (`always`, or unset for recognised
  connections only), `BINMODEM_V92_TONEQ_MS`. The facts are in
  `qc-facts.md`, "BinModem".
- **Accounts file** `sip.txt` (the section format: `[test]`, then
  `registrar = 127.0.0.1:5070`, `username`, `password`, `register = no`;
  `laws = ...` only for an A-law run). Kept outside the repository.
- **Target:** the nsdiald test build, listening on 127.0.0.1:5070 (commit:
  to be written here when the build exists). Its modes: `V90M_MOD_V92` on
  (with V.90 and V.34); K56flex on for runs 7 and 8 only.
- **Dial string:** always `sip:modem@127.0.0.1:5070`, the memo key.
- **Privacy:** no `--record`. If one is needed to see Phases 1 and 2, it is
  cut at the end of INFO1a, kept under `build/`, and only its sha256 enters
  this file. No recording past error control up.
- **Common command**, per run, with `M=build/qc-memos/<run>.txt`:

      modem-sip --accounts sip.txt --account test --memos $M \
        --type "AT+MS=V92" [--type "AT+A8E=,,,1"] \
        --type "ATD sip:modem@127.0.0.1:5070" \
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

1. `rm -f $M`. `BINMODEM_V92_QC=always`, common command with A8E on.
2. Pass: `v8bis_ack 1` (its MS `21 89 80 80 81 c2` asks ACK(1)), then as
   run 1.

### Run 4: default, A8E on, memo kept

1. `rm -f $M`. Common command, A8E on (the memo-making call).
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

Status fields only; the dialled number and every credential stay out.

| Run | Config | Expect | Result |
| --- | --- | --- | --- |
| 1 | always, A8E off | heard 1, accepted 1, short_p2 0, V.92, bypass | not run yet |
| 2 | default, A8E off, memo | call 1 heard 0; call 2 as run 1 plus short_p2 1 | not run yet |
| 2a | always, with memo | short_p2 1, V.92 | not run yet |
| 3 | always, A8E on | v8bis_ack 1, then as run 1 | not run yet |
| 4 | default, A8E on, memo | as run 2 with v8bis_ack 1 | not run yet |
| 5 | stale memo | data reached; which path | not run yet |
| 6 | TONEQ_MS=30 | fallback 1, V.8, data | not run yet |
| 7 | run 1, K56flex on | normal call | not run yet |
| 8 | run 3, K56flex on | normal call | not run yet |
