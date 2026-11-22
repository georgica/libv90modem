# K56flex: the hand-over (0.5.0)

K56flex on the answering side is in the library as 0.5.0, experimental and
off by default (`V90M_MOD_K56`). K56flex (answering, experimental, off by
default) connects to Motorola's SM56 soft modem run under emulation, V.42
data both ways at 32000 to 52000 bit/s down and 31200 up, with no pad and
with robbed bits; its decoder takes our data at every rate, pad and
robbed-bit pattern; not yet tried on a Rockwell or Lucent modem.

What is left needs a real line and a real K56flex modem. This page is the
list: for the owner, the calls to make and what to keep from each; for the
v90d side, the small change that lets the owner switch K56flex on; and the
questions no source settles, with the recording that answers each. The
design is `docs/superpowers/specs/2026-11-21-k56flex-answering-design.md`
("On the line, later", "Not settled by any source"); the facts are
`docs/history/k56-facts.md`; the limits are `docs/limits.md`, "K56flex
(answering)".

## For the owner: the line tests

Every call is recorded as for x2 (v90d's `-w FILE`): what the modem heard
and what it sent. The recordings become fixtures, decoded field by field.
Keep the modem's own output of each step too, as text.

The owner's K56flex modem is a **Zoom 2948**: Rockwell, to be confirmed by
`ATI` (no source at hand states its chipset). The owner will also
expose the line bridge, so that a later session can call the modem from
the box.

- [ ] **1. Identify the modem** (the Zoom 2948), in a terminal, before any
  call.
  - `ATI0` to `ATI11`: keep all the output. `ATI3` is usually the firmware
    string (for example `V2.210-K56_...`).
  - `AT+MS=?` and `AT+MS?`. A list with `56` is a Rockwell / Conexant
    K56flex controller (`+MS=56`).
  - No `+MS`, but `ATS38?` answers: a Lucent controller (K56flex by `S38`:
    1 automatic, 2 to 14 a fixed rate; with `S109`).
  - Record: the chip (Rockwell or Lucent: `ATI` settles which the Zoom
    2948 carries), the firmware string, the `+MS` list or the `S38` /
    `S109` values.
- [ ] **2. Baseline, K56flex not offered** (v90d as today): two calls. It
  should connect V.34 or V.90 as any caller does.
  - Record: the recordings; the CONNECT lines; `AT&V1` (Rockwell) or
    `ATI11` (Lucent / Zoom) after each call.
- [ ] **3. K56flex offered, the modem forced to K56flex** (Rockwell
  `AT+MS=56,0`; Lucent `ATS38=1`, and V.90 off if it has it): a few calls,
  recorded.
  - After CONNECT: `+++`, then `AT&V1` (or `ATI11`). Keep the whole report:
    the Flex field, the RBS pattern, the digital pad, the line "V8bis
    K56Flex successful" (or what it says instead), the rates, the retrain
    and renegotiation counts.
  - Then one call at each fixed rate: `AT+MS=56,0,,,32000,32000` and so on
    up to 56000 (Lucent: `S38=2` to `14`). Record which rates connect and
    the `AT&V1` of each.
  - One long call, 10 minutes, with data moving, for renegotiations and
    retrains. Record: when each happened (the v90d log), `AT&V1` at the end.
  - Record what v90d shows: `V90M_CONN_K56FLEX`, `tx_bps` / `rx_bps`, the
    events, `retrains`, and the end code (`V90M_END_K56_FAILED` says the
    start-up, a renegotiation or a retrain did not complete).
- [ ] **4. Automode** (`AT+MS=56,1`, V.90 left on): two calls. It should get
  V.90 when it offers V.90 (V.8 decides; spec decision 1).
  - Record: the recordings, CONNECT, `AT&V1`.
- [ ] **5. The other modems with K56flex offered:** the Conexant CX93001
  and the U.S. Robotics 5686 (V.90 and V.92), the U.S. Robotics of 1998,
  and that one at `ATS32=66` with x2 offered too. A few calls each; each
  must connect as it does without K56flex (only its answer tone later:
  1.790 s for a caller without V.8bis, 3.807 s for a V.8bis caller without
  K56flex).
  - Record: the recordings and CONNECT lines; any call that fails or
    connects lower than without K56flex.
- [ ] **6. If V.8bis fails** in step 3 (no "V8bis K56Flex successful", the
  call goes V.34 or V.90): repeat step 3 with the test build that sends the
  other NS block, the 7-octet form SM56 itself sends:

      make EXTRA_CPPFLAGS=-DV8B_NS_DEFAULT=V8B_NS7

  (v90d built against that library). Record the same as in step 3.

What to send back: the recordings, the terminal output of step 1, every
`AT&V1` / `ATI11`, and v90d's log lines of each call.

## For the v90d side: switching it on

Decision 9 of the spec: **a separate, small v90d change after this
release**, not part of the library branch.

- Copy x2's setting: the same name pattern (x2's option, with `k56` in its
  place), the same place in the UI, **off by default**.
- When on, v90d adds `V90M_MOD_K56` to `v90m_cfg.modulations` (with
  `V90M_MOD_V34`, which the default has). Calling: not supported
  (`V90M_ENOTSUP`); on an A-law line it is not offered (no error).
- The API names: `V90M_MOD_K56` (0x0200), `V90M_CONN_K56FLEX` (the
  modulation in data mode; `tx_bps` the downstream payload, 32000 to
  56000, `rx_bps` the V.34 upstream), `V90M_END_K56_FAILED` (the end code
  after the point of no return). In K56flex `v90m_retrain` gives
  `V90M_ESTATE`, and `v90m_cleardown` ends the call at once with nothing
  sent. Events as for x2: `V90M_EV_DATA_STOPPED`, `V90M_EV_RATES`,
  `V90M_EV_DATA_MOVING` around the client's renegotiation; `retraining` and
  `retrains` for its retrain.
- `V90M_MEM_SIZE` is unchanged (120 KiB), and so is `v90m_mem_size()`
  (118824 on x86-64, 118512 on 32-bit ARM): no change to how v90d places
  the modem.
- Order: the live non-K56flex calls with K56flex offered (step 5 above)
  come before K56flex is on by default anywhere; it may be on by default
  only after the owner's K56flex modem has connected (label L2) and those
  calls have passed.

## Not settled by any source, and the recording that answers it

From the spec's list, as it stands after the work:

| question | where it stands | answered by |
| --- | --- | --- |
| What a real server sent in V.8bis | settled by Gough Lui's recording: CRe, a CL with the 9-octet block and the S field `81 c2` (`k56-facts.md`, "Recording"); we send exactly that | step 3: the client's CRd and MS, and "V8bis K56Flex successful" in `AT&V1` |
| Whether a hardware client takes our NS block (the real server's 9-octet form; SM56 takes it) | assumed | step 3; step 6 if not |
| The meaning of INFO1a's x bit, and of NS octets 3, 4, 8 and 9 | unknown; we send x = 0 and the real server's octets | step 3: the client's INFO1c and MS decoded; a client that wants x = 1 goes V.34 |
| Whether a hardware client takes the 3-bit TRN1B, SM56's segment lengths (the real server's differ), the TRN1C length and the 16-level Js | assumed (SM56 takes them) | step 3: a client that refuses fails right after our flagged INFO1a (`V90M_END_K56_FAILED`); the recording shows where |
| Lucent clients' tables | assumed the same as SM56's | step 3 on a Lucent modem: data errors at once would say no |
| A-law | not offered | none on the owner's mu-law line |
| A K56flex cleardown | none known; our cleardown is silence | step 3: what the client sends when it hangs up (V.42's DISC first, expected) |
| How a real client renegotiates (its MP, MP' and E on 4 points assumed, V.34 11.6) | sim client only; SM56 never renegotiated | step 3's long call: `AT&V1`'s counts and the recording of each |
| How a real client retrains, and whether 3 in a row, forgotten after 2 s of data, suits it | sim client only | step 3's long call |
| How a real client takes a server's renegotiation or retrain | not tried: we start neither | nothing to record until we do |
| Whether the 400 ms before CRe meets a client's blind-dial wait on the box's line | SM56 waits about 2 s after dialling; the ringing should cover it | step 3: a client that ignores CRe goes V.34 or V.90 with no V.8bis in its recording |
| Whether non-K56flex callers mind the later answer tone | sim and recordings only | step 5 |
