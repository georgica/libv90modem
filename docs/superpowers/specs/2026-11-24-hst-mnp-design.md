# HST and MNP on the answering side: design

2026-11-24. Status: draft for the owner. Branch `hst`, from the 0.6.0
release (`a44426a`). The owner set the intent, chose approach B and made
the decisions marked **(owner)**. The calls marked **(mine)** were made in
this design, each with its reason and what it costs if it is wrong.

Inputs, in this tree: `docs/hst/binmodem-facts.md` (BF §n: HST facts
digested from BinModem, whose HST came from Courier firmware);
`docs/hst/public-sources.md` (PS §n: the USR manuals, US 4,890,316,
5,008,901, 5,027,376 and 5,579,305, V.42 (03/93) Annex A);
`docs/hst/library-map.md` (LM §n: what 0.6.0 has, where HST plugs in).

## Source tags

Every fact about HST or MNP here carries one of these tags; the facts file
(milestone 1) carries them forward.

- **[FW]** From Courier firmware (a V.Everything flash build of 06/09/95),
  as BinModem reports it. Strong but second-hand: BinModem has never
  talked to a Courier.
- **[PUB]** From a public document: a USR manual, a patent, or V.42
  Annex A.
- **[BM]** BinModem's own choice. Not evidence about HST; we may match it
  to talk to BinModem, but nothing else rests on it.
- **[OURS]** Our guess or decision. A recording or a test settles it.

## Intent and why (the owner's)

Members of Netstalgia, the dial-up ISP at `sip.netstalgia.online` that
nsdiald serves with this library, own U.S. Robotics Couriers. Today a
Courier that dials in gets V.34, V.32bis or lower, and an HST-only model
(Courier HST, HST Dual Standard) gets no HST at all. The owner wants the
box to answer HST, so these Couriers connect at HST speeds: 9600 to 14400
forward, with the 450 or 300 bit/s back channel.

Two more reasons. HST gives the library a **second interop partner**:
BinModem (open source, whose HST has only talked to itself) and a real
Courier. And **no open softmodem library has HST**; this would be the
first, built clean-room from facts. HST needs MNP, so MNP comes first,
as a general endpoint (approach B) that also serves MNP-only callers.

## Goals

1. **MNP endpoint.** A general MNP error-control endpoint (V.42 Annex A,
   the "alternative procedure", MNP classes 2 to 4) in `v42/`, beside
   LAPM, over any data pump. The answering endpoint detects an MNP caller
   by its LR, beside the existing ODP/ADP detection. The host turns it on.
2. **HST answering.** libv90modem detects an HST caller after our answer
   tone; holds the forward channel at 4800, 7200, 9600, 12000 or 14400;
   receives the back channel at 450 or 300; runs MNP with the HST profile;
   reverses the channels and changes rate on request; and ends every call
   with a defined end code.
3. **Proof.** BinModem connects as a black-box peer; then a real Courier
   connects, and its call settles the provisional parts.
4. **Release** as 0.7.0, off by default.

## Non-goals (for 0.7.0)

- **HST calling** (owner): `V90M_MOD_HST` is answer-only, like x2 and K56.
- **16800** (owner), until a recording shows its 256-point set; the 0xC0
  mask never offers code 7. **MNP 5** (owner), a follow-up.
- **HST without error control**, and the fixed-rate connect (`&N6..&N9`
  with `&M0`, PS §1.4 item 5): every HST call runs MNP.
- **Sending** NAK, LTA, LT1–3, LTR, LTDR (PS §3.2); receiving them is
  handled. **The 1986 Courier HST 9600** (it may connect at 9600/300 if it
  shares the later framing, BF §7). **MNP classes above 4**, **V.42bis
  over HST**, **HST.bis** (US 5,579,305, a later, echo-cancelled modem).

## Anti-goals: failure even if HST works

- **Any change to an existing call's behaviour or output unless the host
  enables the new features.** With HST not offered and MNP off, every
  golden file, bench hash and the replay proof are byte-identical to
  0.6.0. With MNP on, a caller that sends an ODP, or no LR, gets exactly
  what 0.6.0 gives it. With HST offered, every caller that sends CM (V.8)
  or AA (V.32) is untouched ("Golden files, hashes and the replay proof").
- **Copying code or prose from BinModem** (GPL-3.0; we are GPL-2.0-only).
  Only facts cross, through `binmodem-facts.md`. The builder does not open
  BinModem's source or design notes; BinModem is used only as a built
  binary.
- **Using D-Modem's or courier-emu's HST material**, in any form.

## Constraints

- C99, fixed point in the signal path, no allocation after create, no
  global or static mutable state; identical output on x86, ARM,
  big-endian, riscv64 and Windows, gcc and clang (the port matrix once per
  milestone); every wait bounded and every end of a call with a defined
  end code ("End codes", "MNP timers").
- **Memory, modem side.** The HST engine lives in the answerer's engine
  union `answer_t.u` (≤ 45096 octets, set by `v92d_t`, LM §4.2). Nothing
  else grows `answer_t`: the new flags use the existing `modes` word. So
  `v90m_mem_size()` stays 106320 on x86-64 (106016 on ARMv5) and
  `V90M_MEM_SIZE` stays 110592; `test/api/test_api.c` pins both.
- **Memory, MNP side.** The MNP state has its own pinned figure ("MNP
  endpoint", "Memory").
- **CPU.** The ARMv5 box does about 122 M counted instructions a second;
  V.32bis data at 14400 costs 30.8 M ("CPU targets").
- **Privacy.** Private recordings are never committed, only their hashes.
  Nothing on the ISP records data mode; a recording on the box stops when
  error control comes up. The test recordings in this plan are the owner's
  own test calls (BinModem, the owner's Courier, Will Sowerbutts' Courier
  with his consent), kept only through link establishment and the first
  frames of test text.
- **Test-first** (CONTRIBUTING.md): each test is seen failing before the
  code that passes it.

## Approaches

**A. MNP built only inside HST.** A small MNP class 3 link private to the
HST engine, speaking only HST's variant. For: the fastest route to a first
HST call, with no new public object. Against: only HST callers gain
anything; the MNP code is tied to HST's unknowns (streamlined headers,
attention codes, the LR on the back channel); a second MNP would be needed
later for every other pump.

**B. A general MNP endpoint in `v42/`, with HST as a profile on top.**
MNP is V.42 Annex A over any pump (start-stop framing mode 2 and HDLC
framing mode 3, with Annex A's negotiation, sequencing and timers). HST's
extras (0xC0, streamlined headers, the attention codes that drive the
pump) are a profile that switches on only on an HST call. For: MNP-only
callers on V.32bis, V.22bis and V.22 get error control (today they fall to
start-stop after the 750 ms detection timer, LM §3.1); MNP is tested on
its own, over pumps we trust, before any HST DSP exists; MNP 5 later goes
into the same endpoint, in the union that holds V.42bis and V.44; and if
the Courier turns out to start HST with a V.22bis handshake (the manuals'
account, "Conflicts"), MNP over V.22bis already works and only the
shift-up is new. Against: a public configuration surface, more to test.

**The choice: B (owner)**, because MNP's value reaches beyond HST, it can
be tested earlier, and the design is safer against HST's unknowns.

## Decided "how"

| # | Decision | By |
|---|---|---|
| 1 | Answering side only; HST calling is a non-goal. | owner |
| 2 | Forward channel at 4800, 7200, 9600, 12000 and 14400; 16800 deferred until a Courier recording shows its 256-point set. Back channel at 450 and 300. | owner |
| 3 | BinModem first, as a black-box peer (`modem-sip` calling a local SIP address with `AT+MS=HST`, or `modem-loop`); then a real Courier (the owner's, or Will Sowerbutts' V.Everything), whose negotiation is recorded as evidence. | owner |
| 4 | The forward training is unknown to both sources: we send a provisional one, clearly marked, and plan the recording that settles it (a Courier answering BinModem, tapped at the ATA). | owner |
| 5 | MNP is enabled by a host setting, default off; nsdiald decides when to turn it on. | owner |
| 6 | MNP 5 and 16800 are non-goals for 0.7.0, listed as follow-ups. | owner |
| 7 | MNP is a mode of the existing endpoint (`v90m_ec_t`), not a sibling type. | mine |
| 8 | Endpoint and modem are tied for HST by one explicit call, `v90m_ec_link_hst()`, and talk through a mailbox inside the HST engine. | mine |
| 9 | HST is detected after ANSam, in a silent window (400 ms to start, sized in simulation; see 25), by the caller's back-channel carrier. ANSam stays (no 2225 Hz, no plain 2100 Hz), so V.8 callers are untouched. | mine |
| 10 | The forward receiver does not depend on the training's contents: it acquires on AB, trains decision-directed on the 4 training points, and uses a self-synchronising descrambler. | mine |
| 11 | No echo canceller: the channels are split by frequency, with a sharp forward pulse (RRC, β = 1/12) and guard filters. | mine |
| 12 | The forward receiver is built on `qamrx`, not `v32rx`. | mine |
| 13 | A reversal keeps `moving` at 1 and reports `V90M_EV_RATES` with tx and rx swapped; no new event; a new stats counter. | mine |
| 14 | Policies use the patents' public numbers, not BinModem's: reverse at a backlog over 64 octets after 2 s; upshift on 90 % good quality over 10 s; downshift on 15 frame errors in 10 s. | mine |
| 15 | Rate changes as the patent describes: attention 0x3R switches rate with 15 ms marks and no retrain; 0x2R retrains at the new rate. | mine |
| 16 | A test-only build profile matches BinModem's stand-ins (opening rate 7200, a V.17-shaped long train), so the BinModem runs test everything but the training. | mine |
| 17 | The BinModem-to-Courier recording is made as early as possible, alongside milestone 1, not at milestone 7. | mine |
| 18 | `v90m_ec_reset` keeps the HST link. Linking is allowed any time before the modem decides to offer HST (the end of ANSam); HST is offered only if the link exists then. On HST the endpoint hunts for an LR from the start of the line, not from `CONNECTED`. | controller |
| 19 | On HST calls the 750 ms T400 is replaced by a window: until our training ends, plus 5 s. No start-stop fallback over HST; no LR ends the call with `NO_EC`. | controller |
| 20 | The 0xC0 rate mask comes from `max_down_bps`; `max_up_bps` is enforced at reversal, through the attention rate codes. | controller |
| 21 | The post-agreement retrain and a caller's 0x2R stop data at the API (`retraining` = 1); MNP holds its timers between `line_stopped` and `line_restored`. The post-agreement retrain starts when the caller's LA arrives. | controller |
| 22 | The BinModem-over-SIP and Courier test calls (milestones 6 and 7, and R3) run through a **local nsdiald test build**: a branch with HST and MNP enabled, run only on this machine, never deployed. The ISP session builds it from this library branch when the API is ready (end of milestone 2). Loops below that level use the `sim/` peers; no separate rig program. | owner |
| 23 | N401 64 is offered by default (`opt` default without bit 1). | controller |
| 24 | `v90m_ec_mem_size` is re-pinned at the measured value; no "+16 octets" bound. | controller |
| 25 | W may grow up to 700 ms, only when HST is offered; sized in simulation. | controller |

## The answering sequence

**Today (0.6.0, LM §1).** V.8: 200 ms of silence, then ANSam for up to
5 s, listening for CM and (through `aadet`) V.32's AA. With no CM: V.22bis
(unscrambled ones at 2250 Hz, with the Bell 103 and V.21 mark listeners)
if offered, else V.21.

**With `V90M_MOD_HST` offered and an endpoint linked:**

1. As today, unchanged: V.8, ANSam; CM goes to V.8's choice, AA to V.32.
2. **New phase `ANS_HST_WAIT`.** If ANSam ends with no CM and no AA, we
   stay silent for up to **W** (400 to 700 ms, decision 25) **[OURS]** and
   run the back-channel detector. If it fires, go to `ANS_HST`; if not,
   after W, call `ans_start_v22` (or V.21) exactly as today.
3. **`ANS_HST`, the point of no return.** Start together the forward
   transmitter (long training at the opening rate, then HDLC flags) and
   both back-channel receivers (450 and 300).
4. `connected` = 1 once our training has gone out and a back receiver is
   locked: `CONNECTED` with `modulation` HST, `tx_bps` the opening rate,
   `rx_bps` 450 or 300.
5. MNP establishment in the host's endpoint, which has hunted for an LR
   since the first sample (decision 18), so an LR sent during our training
   is not lost: the caller's LR on the back channel, our LR on the forward
   channel, the caller's LA. No LR by the end of our training plus 5 s:
   the endpoint ends with `NO_EC` (decision 19).
6. The endpoint passes the agreed rate mask and back-channel speed to the
   pump through the link.
7. When the caller's LA arrives, the forward channel retrains at the
   highest agreed rate (short training): the firmware's connect step,
   "idle, set agreed rate, start the forward transmitter" (BF §2)
   **[FW]**. This retrain stops data at the API like any retrain
   (`retraining` = 1, decision 21).
8. Data.

**Why this shape (decision 9).** The firmware's caller starts its back
transmitter when the answer tone stops (BF §1) **[FW]**, and a V.22bis
caller is silent until it hears our unscrambled ones. So a silent window
after ANSam is a clean place to listen; it costs V.22bis, V.21 and Bell
callers up to W, and only when HST is offered. Keeping ANSam keeps
V.8 callers exactly as today: the box answers every caller on one number,
so it cannot send a Bell 2225 Hz or plain 2100 Hz tone just for HST. We
assume a Courier caller accepts ANSam as an answer tone **[OURS]**:
Couriers called V.34 answerers, which send ANSam, all the time. *If
wrong:* an HST caller waiting for 2225 Hz or a shorter tone never starts
its back channel, and the call goes V.22bis as today, so nothing is lost
against 0.6.0 ("Open questions" 1). The firmware's answerer starts its
forward channel blind (BF §1) **[FW]**; ours waits until it has heard the
caller, so the caller's forward receiver hears up to W more silence.

**Sizing W (decision 25).** 400 ms to start. A 300 bit/s start needs
300 ms of lock after the caller's tone-end detection, its preamble and our
AGC settling, so it barely fits; if simulation shows misses, W grows to at
most 700 ms. The trade-off, in `limits.md`: V.22bis, V.21 and Bell callers
wait up to W longer, but only where the host has offered HST.

**The point of no return** is the start of `ANS_HST`. Falling back to
V.22bis after reporting `CONNECTED` would break the API's event order. The
detector demands a locked 8-DPSK demodulator, not just band energy, so a
false start is rare. *Cost:* a false detection loses a V.22bis call (no
LR comes, so it ends with `NO_EC`), so the detector is checked against
every private recording. (A Dual Standard Courier that sends AA during ANSam gets
V.32bis as today, and with MNP on, error control even if MNP-only.)

## MNP endpoint

### Placement and type (decision 7)

MNP is a new mode of `v42ec_t`, the code behind `v90m_ec_t`, in new files
`v42/mnp.[ch]` (the link state machine) and `v42/mnp_frame.[ch]` (framing
mode 2 and the header layouts). Why one type and not a sibling:

- The answering endpoint must choose between an ODP (LAPM), an LR (MNP)
  and nothing (start-stop) on **one** bit stream before it knows the
  protocol. With two objects, the host would have to run both and
  arbitrate.
- nsdiald already wires `v90m_ec_t` (bits, ticks, `line_stopped` and
  `line_restored`, write and read, break); MNP costs it one setting.
- Ring buffers, start-stop fallback, break and status are shared.

*Cost:* `v90m_ec_cfg` and `v90m_ec_status` grow, an ABI change; 0.7.0
changes the ABI anyway (`check-soname`).

### Framings and the HDLC bit layer

- **Mode 3 (bit-oriented)** is `v42hdlc_tx_t` and `v42hdlc_rx_t`
  unchanged: flags, zero insertion, FCS-16. The Courier's CRC is
  CRC-16/X.25 with residue 0xF0B8 (BF §5) **[FW]**, which is V.42's FCS-16
  **[PUB]**.
- **Mode 2 (start-stop, octet-oriented)** **[PUB, Annex A.3]**: 10-bit
  characters through the existing `v42async_tx` and `v42async_rx`; SYN DLE
  STX (16 10 02), the body with each DLE doubled, DLE ETX (10 03), then the
  FCS on x^16 + x^15 + x^2 + 1. Its initial value and octet order are not
  public: we take CRC-16/ARC (initial 0, reflected, low octet first)
  **[OURS]**, the usual MNP convention. The four variants sit behind one
  constant, and a recorded LR settles it (open question 8).
- **Idle:** flags in mode 3, mark in mode 2; mark before the protocol is
  known (V.42 7.2.1.3, as today).

### Detection of an MNP caller

V.42 7.2.1.3 **[PUB]** says the answerer leaves detection on "receipt of
continuous flags, or of an LAPM or alternative procedure protocol frame".
0.6.0 does the first two; we add the third. With `mnp.enable` = 1, in
`V42EC_DETECT`:

- **An ODP leads to LAPM, as today.** A caller that can do both gets
  LAPM, which a Courier asks for first (PS §1.4).
- **A complete, CRC-valid LR leads to MNP**, in either framing: SYN DLE
  STX in the start-stop stream (mode 2), or an HDLC frame whose header is
  `LI 01 02 …` with LI equal to the header length (mode 3). An LAPM frame
  cannot pass: its second octet is a control field, and its third is never
  the constant 2. We reply in the framing the LR came in.
- **Nothing within T400 (750 ms) leads to start-stop, as today.**
- **On an HST-linked call (decision 19)** T400 and `fallback` do not
  apply: the endpoint hunts from the first sample until our training ends
  plus 5 s (the pump reports the end through the mailbox). There is no
  start-stop over HST: with no LR, the endpoint disconnects with
  `V90M_EC_R_NO_EC` and the modem clears down.

Why both framings: Annex A puts establishment in mode 2 **[PUB]**, while
on HST the firmware runs class 3 synchronously **[FW]** and BinModem sends
its LR in HDLC **[BM]**; a second hunter is cheap. Off HST, T400 stays
750 ms **[OURS]**, since MNP originators send their LR at once (open question
8); `t400_ms` is already a host setting. With `mnp.enable` = 0, detection
is byte-identical to 0.6.0.

### Establishment, as responder [PUB, Annex A.7.1]

- **We read** constant parameter 1 (must be 2, otherwise LD reason 2 and
  disconnect), constant parameter 2, the framing mode, k, N401 (two
  octets, **little-endian** as in the Courier template `04 02 40 00`
  **[FW]**; Annex A gives no order), parameter 8 (optimisation bitmap),
  and 0xC0 when the HST profile is active. Unknown parameters are ignored.
- **Our reply LR** carries the lower framing mode, k = min, N401 = min,
  parameter 8 (the AND) only if the caller sent it, and 0xC0 only if the
  caller sent it and the profile is active: only parameters both know.
- **Our offers:** k = 8 ("should support k = 8" **[PUB]**; the Courier
  template **[FW]**); N401 64 (decision 23; 256 only if the host sets `n401`
  256 and `opt` bit 1); parameter 8 = bit 2 only (fixed-field LT and LA);
  framing mode 3 (all our pumps are synchronous).
- **Confirmation:** the caller's LA with N(R) 0, or an LT first (lost LA:
  the link is up, that LT is dropped, an LA is sent). A repeated LR gets
  our LR again.

### Frames, data transfer and attention [PUB, Annex A.6, A.7]

- **Types:** LR 1, LD 2, LT 4, LA 5, LN 6, LNA 7. Standard layouts: LT
  `04 04 01 01 N(S)` + data; LA `07 05 01 01 N(R) 02 01 N(k)`; LN
  `07 06 01 01 N(SA) 02 01 type`; LNA `04 07 01 01 N(RA)`; LD
  `04 02 01 01 reason` (or LI 7 with a user code). Optimised (parameter 8
  bit 2): LT `02 04 N(S)`, LA `03 05 N(R) N(k)`; the LI stays. HST's
  streamlined layouts are a profile matter.
- **LT:** V(S) from 1, modulo 256; the receiver takes only V(R) and
  acknowledges with N(R); an LA repeating the previous N(R) means "send
  again from the first unacknowledged LT"; credit N(k) from the last LA,
  zero stops the sender.
- **LN/LNA:** LN has its own V(SA) from 1; one outstanding, sent again
  every T401 until its LNA; each delivered once. Outside HST, types 1–3
  are the break kinds, mapped to `v90m_ec_send_break`/`get_break`; on HST
  the octet carries the attention codes.
- **LD:** reason 255 on `v90m_ec_release()`, as the Courier does (BF §6;
  **[FW]** layout, **[BM]** reason). Annex A's advice to stay silent was
  for its installed base; a Courier expects LD. Reason 4 when retries run
  out: "remote modem retry timeout" in the Courier's S17 list (PS §2.1)
  **[PUB]**.

### MNP timers

- **T401** from Annex A.7.5's formula **[PUB]** with the Lb, Lf, Llt, Lla
  of the framing and layout in force. Where the formula has one rate we
  use our transmit rate for the LT term and the far end's for the LA term
  (over HST they differ 30-fold), recomputed at every rate change or
  reversal, never below 1500 ms **[OURS]**. Over V.22bis at 2400 this
  gives about 4 s (Table A.9) **[PUB]**.
- **Establishment:** T401 2 s **[OURS]** (Annex A allows 0.5 to 9 s).
- **T402** 0.5 × T401; **T404** 3 s at 2400 and above, 7 s at 1200
  **[PUB]**; **T403** off.
- **N400** 12 **[PUB]**; when exhausted, LD reason 4 and
  `V90M_EC_R_N400`.

### Status, end reasons, configuration

- **New status fields**, appended to `v90m_ec_status` and
  `V90M_EC_STATUS_FIELDS`: `protocol` (`V90M_EC_PROTO_NONE`, `_LAPM`,
  `_MNP`, `_ASYNC`), `mnp_framing`, `mnp_opt`, `mnp_streamlined`, `hst`
  (profile agreed), `hst_rates`, `hst_back300`, `attn_tx`, `attn_rx`,
  `rx_unknown` (valid frames of unknown type), `rx_unknown_id` (the first
  octet of the last one and nothing more: LT1 to LT3 carry keystrokes) and
  `peer_ld_reason`.
- **End reasons: none new.** Peer LD is `PEER_DISC`; retries out, `N400`;
  bad parameters, `NEGOTIATION`; a violation, `PROTOCOL`; nothing after
  detection, `NO_SABME` (its meaning widens to "no LR or SABME"); no error
  control with fallback off, `NO_EC`.
- **Configuration**, appended to `v90m_ec_cfg`, ranges in
  `V90M_EC_CFG_FIELDS`:

```c
struct {
    int enable;        /* 0..1, default 0: MNP off, the endpoint is 0.6.0's */
    int k;             /* 1..8, default 8 */
    int n401;          /* 1..256, default 64 (256 needs opt bit 1) */
    int opt;           /* parameter 8 bitmap, 0..3, default 2 (fixed-field LT/LA, no 256) */
    int framing;       /* highest framing mode we offer or accept, 2..3, default 3 */
    int t401_min_ms;   /* floor for the computed T401, >= 500, default 1500 */
    int n400;          /* 1..255, default 12 */
} mnp;
```

### Memory

`mnp_t` holds k unacknowledged frames (8 × 256), one receive frame, the
mode 2 framer and the HST profile state: under 3 KB. It shares a union
with `v42lapm_t` (8976 octets today); the two never run on one connection,
and the member is chosen when detection decides. The V.42bis/V.44 unions
are untouched (MNP 5 goes there later).

- **MNP figure:** `sizeof(mnp_t)` ≤ 4096, pinned by a compile-time check
  and a test.
- **Endpoint figure (decision 24):** `v90m_ec_mem_size()` (49784 today)
  grows by the `mnp` configuration, the link pointer and the new state.
  It is re-pinned at the measured value per architecture in `test_api.c`,
  stays under `V90M_EC_MEM_SIZE` (51200, unchanged), and the CHANGELOG
  states the new figure.

## Tying the endpoint to the modem (decision 8)

HST's link layer drives its pump: a reversal or a rate change is an MNP
attention that the pump executes (US 5,008,901, US 4,890,316) **[PUB]**,
and the endpoint needs the pump's role, rates and quality. 0.6.0 keeps
error control outside the modem (LM §3.3), so a tie is needed:

```c
int v90m_ec_link_hst(v90m_ec_t *e, v90m_t *m);
```

- **When (decision 18):** any time before the modem decides whether to
  offer HST, which is the end of ANSam. Returns `V90M_OK`; `V90M_EINVAL`
  if `e` has `mnp.enable` = 0 or `m` is a calling modem; `V90M_ESTATE`
  once that decision has been made.
- **It** sets `ANS_MODE_HST_LINKED` in the modem's existing `modes` word
  (so `answer_t` does not grow) and stores `m` in the endpoint.
- **HST is offered only with `V90M_MOD_HST` set and the link existing at
  the decision.** An unlinked modem never detects HST, so a misconfigured
  host cannot strand an HST caller: the call goes V.22bis.
- **Bits before `CONNECTED`.** On a linked HST call the endpoint hunts for
  an LR from the start of the line: from `ANS_HST` until `connected`, the
  pump hands the back receiver's bits to the endpoint's receive path
  through the link; from `CONNECTED` on, through `putbit` as usual. An LR
  sent during our 1.4 s training is therefore kept, and answered when our
  transmitter first asks for bits.
- **The mailbox, `hst_link_t`,** lives in the HST engine, in the union.
  The endpoint reaches it through an internal accessor that is NULL unless
  the modem is in `ANS_HST`; while NULL, the endpoint runs plain MNP. Every
  access happens on the host's thread, in `v90m_ec_tx_bit`/`rx_bit`
  (called from the modem's getbit and putbit) or `v90m_ec_tick`: no
  concurrency, no global state.
- **The endpoint writes** the agreed rate mask and back speed; "reversal
  LNA sent and drained"; "reversal LNA received"; "rate change R ordered";
  "far end sent LD". **The pump writes** our offered rate mask (from
  `max_down_bps`, decision 20), the end of our training, role and rates,
  quality figures, "please send LN code c" (rate policy), and "transmitter
  drained".
- **Reset and lifetime (decision 18).** `v90m_ec_reset` keeps the link
  and an LR already captured, and applies the new configuration; so the
  documented pattern of resetting the endpoint at `V90M_EV_CONNECTED`
  stays harmless. Only `v90m_ec_destroy`, or a reset with `mnp.enable` =
  0, unlinks. The host keeps the modem alive while linked. **The host
  pattern**, which `docs/error-control.md` ("When to start it") and
  `docs/getting-started.md` must state: *for an HST-capable call, link
  before the first sample; resetting at `CONNECTED` is harmless.*
- **Why not callbacks in `v90m_cfg`:** they would put the protocol's
  coupling in nsdiald's hands. The mailbox keeps it inside the library;
  the host makes one call.

## HST profile (in the endpoint)

Active when the link accessor is non-NULL and the caller's LR carries 0xC0
with bit 0 set.

- **0xC0** is `C0 02 flags rates` **[FW]**. Flags: bit 0 HST, bit 3 the
  300 bit/s back channel; other bits unknown, sent as 0 and ignored.
  Rates: bit n means 2400·n bit/s, n = 2..7.
- **Merge** **[FW]**, details **[BM]**: HST needs bit 0 from both ends;
  the back channel is 300 if either end asks; rates = ours AND theirs AND
  0x7C (16800 never agreed); the best rate is the highest bit set.
- **Our offer (decision 20):** flags 0x01; rates = the codes n = 2..6
  with 2400·n ≤ `max_down_bps` (0x7C at the default), because the mask
  governs our fast channel at connect. `max_up_bps` does not trim the
  mask: it is enforced at reversal (below).
- **No 0xC0, or an empty agreed mask:** plain MNP over the HST channels,
  and the pump stays at the opening rate. Legal and slow.

**Streamlined layouts** (US 5,008,901 **[PUB]**; layouts **[FW]**): the LI
octet is dropped from **every** frame: LT `04 N(S) data…`, LA
`05 N(R) N(k)`, LN `06 N(SA) code`, LNA `07 N(RA)`, LD `02 reason`. An LR
is always standard.

- **We send them** when HST is agreed and both LRs carry a non-zero
  parameter 8 (BinModem's rule **[BM]**, needed to talk to it); otherwise
  standard layouts, or Annex A's optimised ones if parameter 8 says so.
- **We receive every layout** **[OURS]**: standard when octet 0 is a
  consistent LI and octet 1 a known type; streamlined when octet 0 is a
  known type and the length fits; when both fit, the agreed layout wins
  and the expected N(S) breaks a tie. Why: the Courier's LR template has
  no parameter 8 **[FW]**, yet the patent streamlines all frames
  **[PUB]**, so which one a Courier sends us is unknown (open question 5).

**USR frame types we do not implement** (NAK, LTA, LT1–3, LTR, LTDR). A
CRC-valid frame with an unknown first octet is counted (`rx_unknown`,
`rx_unknown_id`) and dropped, unacknowledged **[OURS]**. The patent says
such a frame "is reformatted into a standard LT frame" when sent again
**[PUB]**, so the data arrives one T401 late. *Cost:* keystroke latency
if a Courier uses LT1–3; decoding them is a small change once FIG. 5 or a
recording gives their IDs.

**Attention codes** (US 4,890,316's 14h, 23h, 34h **[PUB]**; the low
nibble is the rate code **[FW]**):

- **0x1R** asks for the forward channel at rate code R; **0x2R** asks the
  holder to retrain at R; **0x3R** asks for rate R without a retrain.
  BinModem uses 0x3R for every rate change, with a retrain **[BM]**.
- **We send** 0x1R with R = the current forward rate for a reversal
  (reversal is "at the current speed" **[PUB]**; BinModem the same
  **[BM]**), 0x3R for upshifts, and 0x2R for downshifts and recovery.
- **As holder we accept** 0x1R (reversal), 0x2R (retrain at R), 0x3R
  (switch to R); any other code gets an LNA, a count, and no action.
- **While a reversal or rate change is pending,** the endpoint sends only
  control frames (LA, LN, LNA, LD), never an LT, until the pump reports
  the new state **[OURS]**.

**Reversal policy (decision 14)** **[PUB, US 5,008,901]**: while we hold
the back channel, we ask for the forward channel when our backlog (queued
plus unacknowledged) is over **64 octets**, the far end has held the
forward channel at least **2 s**, and no other attention is outstanding.
We prefer these to BinModem's 256 octets and 3 s **[BM]**. On the ISP,
downstream traffic dominates, so we hold the forward channel nearly all
the time.

## HST pump (in the modem)

### Files and memory

- **New:** `modem/hst.h`; `modem/hst_back.c` (8-DPSK transmitter,
  receiver, detector); `modem/hst_fwd.c` (forward training and receive
  acquisition); `modem/ans_hst.c` (the scheme: phases, reversal, rate
  changes, watchdogs); `modem/hst_tabs.c`, generated by
  `tools/gen_hst_tabs.py`.
- **Reused:** from `v32/`, `v32_encode`, the tables `v32_c4800` to
  `v32_c14400`, `v32_vit_t`, `v32_decode_bits` and `v32tx_t` (one change,
  below); the GPC scrambler on `v34_scr_t`; `qamrx_t`; the V.22bis
  scrambler (`modem/scramble.[ch]`), whose polynomial is the back
  channel's; V.42's HDLC, in the endpoint.
- **The engine,** `hst_t`, is a new member of `answer_t.u`: `v32tx_t`
  (112 octets), `qamrx_t` (1112), `v32_vit_t` (588), two back receivers
  (about 1 KB each), the back transmitter, guard filters, a stored
  equaliser (about 600), and the training, policy and mailbox state.
  **Target ≤ 12 KB**; a compile-time check keeps it ≤ `sizeof(v92d_t)`.
- **The detector** for `ANS_HST_WAIT` is part of `hst_t`, initialised at
  the window's start, when no union member is in use (V.8bis is over;
  V.22 and V.32 have not started).

### Detecting the caller's back channel

The signal is differential 8-PSK at 375 Hz and 150 baud, or 350 Hz and
100 baud **[FW]**; its preamble is unknown (a "17-tick start" exists
**[FW]**). The detector fires when both stages hold, within W:

1. **Energy:** in a 250–500 Hz band-pass, over −43 dBm0 and at least 10 dB
   above the 600–3400 Hz power, for 100 ms.
2. **Lock:** the 450 (or 300) demodulator on the same samples makes
   differential phase decisions within ±12° of the 45° grid for 24 of 30
   consecutive symbols (200 ms at 150 baud, 300 ms at 100) **[OURS]**.

The US dial tone (350 + 440 Hz) and mains hum have band energy but no
stable 8-PSK grid; V.21 and Bell 103 marks have no band energy. The
detector is tested on every private recording's caller audio after ANSam
and on synthetic dial tone, hum, V.21, Bell 103 and V.22bis callers and
silence, and must stay silent on all.

### Back channel (`hst_back.c`)

- **Modulation [FW]:** differential 8-PSK, 3 bits a symbol, at 375 Hz and
  150 baud (450 bit/s) or 350 Hz and 100 baud (300 bit/s). The phase step
  per tribit comes from the firmware's 0x43705261, a nibble at a time from
  the low end (table in BF §3); points at 22.5° + k·45°; bits LSB first;
  within a tribit, its LSB first **[BM, inferred]**.
- **Scrambler:** 1 + x^-14 + x^-17, self-synchronising **[FW]**: the
  V.22bis polynomial. Reuse `modem/scramble.[ch]` if its V.22bis rule
  (inverting after 64 ones) can be switched off; nothing says HST has
  that rule, so it is off **[OURS]**.
- **Transmit pulse:** RRC, β = 0.35 **[OURS]**: 274–476 Hz at 450,
  283–417 Hz at 300, below the forward channel's 500 Hz edge. (The
  Courier's 72-tap filter at 7200 Hz is unknown **[FW]**.) At 8000 Hz a
  symbol is 53⅓ or 80 samples: a generated polyphase table with a phase
  accumulator.
- **Preamble:** 300 ms of scrambled ones, phase and scrambler starting at
  zero **[OURS]**; then the endpoint's bits (HDLC flags when idle). Our
  receivers never rely on a preamble.
- **Receiver:** 250–500 Hz band-pass, AGC, a 32-bit NCO to baseband,
  decimation to 1600 Hz, matched RRC, Gardner timing at 2 samples a
  symbol, differential decisions; carrier detect with hysteresis on symbol
  power, thresholds set in simulation. No equaliser at first: at 150 baud
  the group-delay spread is small against a 6.7 ms symbol. If the line
  simulation shows too much ISI, add a 3-tap T/2 equaliser.
- **Both receivers (450 and 300) run while we hold the forward channel**,
  about 1 M instructions a second each. The pump delivers the agreed
  speed's bits; before agreement, the locked one's, 450 preferred. So the
  speed switch after the LR (BF §3) needs no exact timing **[OURS]**.
- **We send** at the agreed speed. We offer 450, USR's default; a switch
  to 300 that the caller asks for happens when the endpoint reports its
  LA drained.

### Forward channel: transmitter

- **Modulation [FW]:** 2400 baud, 1800 Hz carrier, V.32bis constellations,
  8-state trellis and differential encoding from V.32's own tables
  (`v32_encode`, rate index 0 to 4 for 4800 to 14400); 4800 uncoded on 4
  points **[FW, PUB]**.
- **Scrambler:** GPC (1 + x^-18 + x^-23) in both directions **[FW]**:
  `v34_scr_t` with `V34_GPC`.
- **Pulse (decision 11): RRC, β = 1/12** **[OURS, derived from FW]**. The
  firmware gives flat to 1100 Hz from the carrier, −3 dB at 1200, −36 dB
  at 1400. An RRC is flat to (1 − β)·1200 = 1100 Hz when β = 1/12, −3 dB
  at 1200, and nominally zero beyond 1300: the channel spans 500 to
  3100 Hz. `gen_hst_tabs.py` makes the table long enough (about 24
  symbols) for −40 dB at 400 Hz and below; a unit test checks the three
  firmware points.
- **`v32tx` change:** it has V.32's 25 % pulse (`v32_txh`) built in,
  reaching down to 300 Hz, on top of the back channel. `v32tx_t` gains a
  pulse-table pointer and length, defaulting to V.32's, so V.32's output
  stays byte-identical (bench hashes, golden files) **[OURS]**. The
  quarter-turn carrier rotation is unchanged (exact at 1800 and 2400).
- **No echo canceller (decision 11).** HST is band-split with no
  cancellation **[PUB, FW]**. This saves the 34 KB `v32ec_t` and about 9 M
  instructions a second. The separation comes from the sharp forward
  pulse; a guard high-pass before the forward receiver (stop below
  480 Hz, pass above 560 Hz, at least 50 dB against the back band); and
  the back receiver's band-pass (at least 50 dB down at 600 Hz and above).
  The patent's analogue filters (PS §1.2) check the order of magnitude.

### Forward training: provisional, clearly marked (decision 4)

What is known: the firmware's start-up is "180 + 72 + 200 or 3096
symbols" **[FW]**, with no contents and no rule for the length; HST.bis,
USR's successor, trains with AB 72, CD 8, TRN 1600 **[PUB, P3]**; V.32's
start-up has S (AB), S-bar (CD) and TRN on the same tables. **We send
[OURS, provisional]** the V.32 segment generators with the firmware's
lengths:

| segment | symbols | contents |
|---|---|---|
| S1 | 180 | AB alternation (V.32's S) |
| S2 | 72 | CD alternation (V.32's S-bar) |
| S3 | 3096 (long) or 200 (short) | TRN: scrambled ones as V.32 TRN dibits on the 4 points |
| then | — | data at the rate in force (HDLC flags from the endpoint) |

- **Long** for the first training of each direction (the far receiver has
  nothing stored); **short** for later ones in the same direction (the
  post-agreement retrain, 0x2R, recovery, a reversal back) **[OURS]**. Our
  receiver stores its equaliser per direction, like HST.bis's "quick
  turnaround" **[PUB, P3]**.
- Long is 3348 symbols (1.395 s), short 452 (188 ms). V.17's long train is
  3344 symbols: a hint that the long form is equaliser training, not
  proof.
- **Opening rate (before MNP agreement): 9600** **[OURS, from PUB]**: the
  manual says the modems "shift up and train at 9600" first (PS §1.4);
  BinModem uses 7200 **[BM]**. One constant, `HST_OPEN_CODE`.
- **The BinModem profile (decision 16).** For the BinModem runs only,
  `-DHST_PROFILE_BM` sets the opening rate to 7200, makes the long training
  V.17-shaped (256 AB, 2976 conditioning, 64 bridge, 48 scrambled ones, as
  in V.17 [PUB] and BF §2), and makes our rate requests 0x3R with a
  retrain, which BinModem's receiver expects. A test build only: release
  builds and golden files use the default. BinModem's forward channel is a
  stand-in; matching it lets BinModem test everything else (MNP, the
  profile, the back channel, reversal logic, the policies).

### Forward channel: receiver (decisions 10 and 12)

Built on `qamrx_t` (2400 baud, carrier 3/4) with a new acquisition front
end that does not depend on the training's contents:

1. **Guard high-pass and carrier detect** in 600–3000 Hz.
2. **AB detection:** both band-edge tones (600, 3000 Hz) for 40 ms. Every
   candidate training starts with AB (ours, V.32's S, V.17's segment 1,
   HST.bis's). It sets the AGC and starts `qamrx_sync`, timing from the
   band-edge phase.
3. **Decision-directed training on the 4 points** A–D, with
   `QRX_EQ | QRX_CARRIER | QRX_TIMING | QRX_TFAST` and a fast step until
   the MSE has stayed under a threshold for 100 symbols, then slow
   tracking. V.32's differential coding removes the 90° ambiguity.
4. **Data:** when points stop fitting the 4-point set (more than 3 of 8
   off-grid), switch to the trellis set of the rate in force (opening
   constant, MNP agreement, or the R of a reversal or rate code) through
   the Viterbi decoder. The descrambler is self-synchronising and the
   differential decoder resynchronises in one symbol, so no segment length
   is needed; HDLC finds the flags. At 4800 the data set is the training
   set, and TRN (scrambled ones) decodes as ones: harmless idle.
5. **Stored equaliser:** at a reversal keep the taps and carrier
   frequency; restore them on the next training of that direction.

Why `qamrx` and not `v32rx`: `qamrx` takes the wanted point from its
caller (exactly decision-directed training), is carrier-independent, and
has the 0.6 hardening (`QRX_BLANK`, clamps); `v32rx`'s start-up detectors,
reference sequences and AGC on S are V.32's own. *Cost:* its 72-tap T/3
equaliser is a little dearer than V.32's 32-tap T/2 one; the CPU target
allows it. *Fallback:* if `qamrx` will not acquire on AB in milestone 4,
try `v32rx` without its canceller and with a new training control, boxed
to 2 days.

### Channel reversal (decision 13)

| step | holder of the forward channel | requester (back holder) |
|---|---|---|
| 1 | — | the endpoint sends LN 0x1R on the back channel |
| 2 | sends LNA on the forward channel; when it and its closing flag have left ("drained") plus 20 ms **[OURS]**: stops the forward carrier, starts the back transmitter (preamble) and the forward receiver **[FW: firmware event 0x73]** | — |
| 3 | — | on the LNA: stops the back transmitter at once, waits 200 ms **[OURS; BinModem 400, BM]**, sends the forward training at R (long or short) **[FW: event 0x76]** |
| 4 | resets its HDLC transmitter and receiver at the switch **[BM]** | the same |

**At the API:** `moving` stays 1 (the modem does not call `getbit` while
training, and `putbit` gets ones, circuit 104 clamped as in V.32);
`bitrate` and `rx_bitrate` swap, so `V90M_EV_RATES` fires with `tx_bps`
and `rx_bps` exchanged; `v90m_stats.reversals` (appended) counts it; it is
neither a renegotiation nor a retrain. Why: hosts already handle `RATES`;
the endpoint ordered the reversal itself and needs no `line_stopped`; a
new event would make every host change for no gain. **Bound:** if the new
forward signal is not detected within 3 s of the switch, the watchdogs
take over.

### Rate changes (decision 15)

**Who asks:** the forward channel's receiver. When that is us (we hold the
back channel), the policy is **[PUB, US 4,890,316]**:

- **Upshift:** every 10 s, ask for the next agreed rate if our quality was
  "good" in at least 90 % of the 10 s; never within 10 s of a downshift.
- **Downshift:** ask for the next lower agreed rate on 15 frame errors
  (CRC failures) within a 10 s window opened by the first, or on 3 failed
  retrains within 10 s.
- "Good" is our MSE under the next rate's threshold: the SNR for a BER of
  1e-5 at that rate plus 3 dB, from our simulation **[OURS]**.

When we hold the forward channel, the caller decides. **`max_up_bps`
(decision 20):** when the caller holds the fast channel, we never ask for
a rate above `max_up_bps` (0x2R and 0x3R are capped); if a reversal's 0x1R
carries a higher R, we answer its LNA and, once the caller's training
has been received, ask 0x2R at the highest agreed rate ≤ `max_up_bps`.
We never grant a 0x3R above it either: it gets an LNA, a count, and our
own 0x2R at the cap.

- **Executing 0x3R as holder** **[PUB, US 4,890,316; FW: "without
  retraining"]**: finish the frame; 15 ms (36 symbols) of ones at the old
  rate; switch the encoder's rate index (scrambler, differential and
  trellis states continue **[OURS]**); 15 ms of ones at the new rate;
  resume.
- **Receiving it** (we asked and got the LNA): arm a detector for at least
  10 ms of ones at the old rate; then decode both rates in parallel for up
  to 48 symbols, and switch when the new rate gives 24 ones in a row. With
  no switch within 2 s, or a training instead, the generic acquisition
  takes over.
- **Executing 0x2R as holder:** the short training at R.
- **Recovery [OURS]:** holding the forward channel, with the back channel
  showing the far end alive but nothing we send acknowledged for
  2 × T401, send the short training at the current rate. This also covers
  a peer that expected a retrain where we switched by marks (BinModem on
  the default build).

### Retrain, watchdogs, cleardown and end codes

- **`v90m_retrain()`:** holding forward, the short training at the current
  rate; holding back, the endpoint sends 0x2R at the current rate.
  `retraining` is 1 from the training's start until the receiver is in
  data, giving `DATA_STOPPED`/`DATA_MOVING` as V.32 does.
- **Every retrain stops data (decision 21):** `v90m_retrain()`, the
  post-agreement retrain (started when the caller's LA arrives), a 0x2R
  either way, and recovery all set `retraining` = 1 and count in
  `v90m_stats.retrains`. The host calls `v90m_ec_line_stopped` and
  `line_restored` as for any retrain; MNP then holds T401, T402 and T404
  (no expiry, no retransmission while stopped), finishes no new frame,
  and resumes the timers from where they stood on `line_restored`. A
  reversal and a 0x3R mark switch are not retrains: `moving` stays 1.
- **Watchdogs [OURS].** Holding forward: no back carrier for 5 s ends the
  call `V90M_END_LOST` (the back channel is continuous, so 5 s of silence
  means the caller is gone). Holding back with no forward carrier: after
  1 s ask 0x2R at the current rate; after 5 s take the forward channel back
  (the default role; the caller's side of the same rule returns it to the
  back channel, which breaks a deadlock after a lost reversal), then the
  first rule applies. No call stays connected without a far signal for
  more than 12 s.
- **No LR (decision 19):** the endpoint's window ends at our training's
  end plus 5 s, with `V90M_EC_R_NO_EC`; the host clears down
  (`V90M_END_CLEARDOWN_OURS`). Backstop, for a host that does not: no MNP
  link within 15 s of entering `ANS_HST` ends the call
  `V90M_END_HST_FAILED`.
- **Cleardown, ours:** the host calls `v90m_ec_release()` (LD 255; waits
  until `tx_busy` is 0, at most 1 s), then `v90m_cleardown()`; both
  carriers stop. **Theirs:** the endpoint gets an LD and writes it to the
  mailbox; the modem ends when the far carrier drops or after 2 s,
  whichever is first.

| end | when |
|---|---|
| (no HST) | detector silent within W: V.22bis or V.21 as today, with their own end codes |
| endpoint `NO_EC`, then `V90M_END_CLEARDOWN_OURS` | no LR by our training's end + 5 s; the host clears down |
| `V90M_END_HST_FAILED` (new) | backstop: in `ANS_HST`, no MNP link within 15 s |
| `V90M_END_LOST` | the far carrier is gone (watchdogs) |
| `V90M_END_RETRAIN_FAILED` | 3 failed retrains within 10 s at the lowest agreed rate |
| `V90M_END_CLEARDOWN_FAR` | the far end sent an MNP LD |
| `V90M_END_CLEARDOWN_OURS` | after `v90m_cleardown()` |

## API additions (0.7.0)

- **`V90M_MOD_HST 0x0400u`:** answering only (`V90M_ENOTSUP` when
  calling); in `V90M_MOD_ALL`, not in `V90M_MOD_DEFAULT` or
  `V90M_MOD_ORIG`; needs no other bit; `modes_of` maps it to
  `ANS_MODE_HST` (0x400000); test_api's unknown-bit check moves to 0x0800.
- **`V90M_CONN_HST = 12`**, equal to `ANS_MOD_HST` (the `SAME` checks).
- **`V90M_END_HST_FAILED`**, appended to `v90m_end` and `ans_end[]` in the
  same order.
- **`v90m_stats.reversals`**, appended.
- **Ceilings (decision 20):** `max_down_bps` sets the 0xC0 mask, so it
  caps the forward rate we send; `max_up_bps` caps the forward rate we
  receive once the caller holds the fast channel, through the attention
  rate codes. The ceiling doc comment gains a sentence for HST.
- **SNR** reports the receiver in use: the back channel's while we hold
  forward, the forward channel's otherwise.
- **Endpoint:** `v90m_ec_cfg.mnp`, the new status fields,
  `V90M_EC_PROTO_*`, `v90m_ec_link_hst()`.
- **Internal:** phases `ANS_HST_WAIT` and `ANS_HST` (rows in
  `phase_scheme[]`, `tools/trace.c`, `tools/v90m_fields.c`); the scheme
  `ans_hst` (`tick`, `cleardown`, `retrain`, `moving`, `snr_den`);
  `mod_scheme()`; `ans_start_hst()`.
- **Docs:** CHANGELOG 0.7.0 with an "API change" list naming every
  appended constant and field, and the re-pinned `v90m_ec_mem_size`;
  README (modulation table, "Memory and CPU", "Not implemented");
  `docs/api.md`; `docs/answering.md` ("HST"); `docs/error-control.md`
  ("MNP", and the HST host pattern in "When to start it");
  `docs/getting-started.md` (the same pattern); `docs/limits.md`
  (provisional parts, tables and provenance, CPU, memory, and W's cost to
  legacy callers on HST-enabled answering); `docs/testing.md`; DESIGN.md (a new
  §4.2f, and the `v42` layer); `docs/history/PROGRESS.md`; the facts file
  `docs/history/hst-facts.md` with the four tags.

## CPU targets

Measured by a new `make cost-hst`: a simulated call recorded on the host
and replayed into the ARMv5 build under qemu, as `cost-x2` and
`make cost-arm` in `v32/` do.

| part | target, M instructions a second |
|---|---|
| holding forward: forward transmitter at 14400 + two back receivers + MNP | ≤ 15 |
| holding back: forward receiver (guard filter, `qamrx`, Viterbi at 14400) + back transmitter + MNP | ≤ 30 |
| the parallel decode during a 0x3R switch | ≤ 40, for at most 20 ms |

The design point is "no worse than V.32bis data" (30.8 M), which keeps
two HST calls per box within 122 M. If holding back misses 30 M, the
levers are the equaliser at T/2 instead of T/3, and a cheaper guard
filter. `limits.md` gets the measured figures.

## Test plan

All tests come before the code that passes them.
`docs/history/hst-facts.md` comes first, and every value a test uses is
cited there with its tag. `sim/` peers are written from that file and from
V.42 Annex A, not from the library code (the `sim/v32call.h` rule).

### Simulated peers (`sim/`, double precision, added to `SIM_SRC`)

- **`sim/mnppeer.[ch]`:** MNP originator and responder: modes 2 and 3;
  standard, optimised and streamlined layouts; LN/LNA, break, LD. Knobs:
  the Courier template LR with and without parameter 8, with 0xC0; lost
  and corrupted frames; zero credit; an LR before or after T400; an ODP
  then an LR; unknown frame types (once as an unknown ID, then as a
  standard LT, as the patent describes). It attaches to any sim pump via
  `callpeer_set_pump` or the `rig/ec_side.h` pattern.
- **`sim/hstcall.[ch]`:** an HST caller. It detects the answer tone (at
  least 400 ms), waits for its end, then sends its back channel (FW step
  table, its own pulse and preamble). It has its own forward receiver (AB
  acquisition, LMS, its own Viterbi) and a forward transmitter for
  reversals (our provisional training, or the V.17-shaped one), and runs
  `mnppeer` with 0xC0 and 0x1R, 0x2R and 0x3R in both roles. Knobs:
  backlog-driven reversals, forced rate requests, carrier loss, LD,
  300/450, HST96 (mask 0x10 with back300), a caller that never sends an LR.
- **The line:** `sim/v32line` (2-wire, mu-law, echo per side, noise) and
  `sim/resamp` for ±100 ppm of clock offset.

### Unit tests, by block

- **MNP framing:** CRC-16/X.25 check value (`6E 90` for "123456789")
  **[FW]**; CRC-16/ARC check 0xBB3D (public catalogue); DLE stuffing;
  flags and aborts; every layout encoded and parsed, ambiguous cases
  included; the Courier LR template bytes from BF §5 as a vector.
- **MNP negotiation:** every merge rule including 0xC0; constant
  parameter 1 ≠ 2 (LD reason 2); unknown parameters ignored.
- **MNP data:** window and credit; go-back-N on a repeated N(R); the
  "N(S) = V(R) − 1 first time" rule; T401 at 1200 and 2400 bit/s (Table
  A.9) and at 14400/450; N400 then LD reason 4; LN sent again and
  delivered once; break through LN.
- **Detection:** ODP → LAPM, LR in mode 2 → MNP, LR in mode 3 → MNP,
  nothing → start-stop, LR-like junk rejected. **With `mnp.enable` = 0,
  the existing v42 tests run unchanged.**
- **Back channel:** step table against BF §3; scrambler against the
  polynomial; transmit spectrum at least 50 dB down at 600 Hz and above;
  loopback at 300 and 450 with noise, ±7 Hz carrier and ±100 ppm timing
  offsets, BER < 1e-5 at 20 dB SNR; the detector's positive and negative
  cases.
- **Forward channel:** pulse spectrum against the three FW points; `v32tx`
  with V.32's table byte-identical to 0.6.0; loopback at every rate;
  acquisition from ours long, ours short and V.17-shaped trainings; the
  0x3R mark switch at both ends; the stored equaliser restored.
- **Coexistence:** forward and back channels at once on `v32line` with
  6 dB echo return loss; each receiver within 1 dB of its echo-free SNR.

### End to end (`test/test_answer_hst.c`; `test_ec_mnp.c` in `v42/`)

- An HST call to data both ways, PRBS checked through MNP, at every agreed
  rate, at 450 and 300, and HST96 meeting our offer at 9600/300.
- Reversals driven by backlog both ways; rate changes up and down, by 0x3R
  and 0x2R; every end code reached on purpose; clock offset, line levels
  from −9 to −33 dBm0, noise; the default build and `-DHST_PROFILE_BM`.
- MNP over V.32bis (`sim/v32call` + `mnppeer`) and V.22bis (`callpeer` +
  `mnppeer`), both framings.
- Unchanged behaviour: an ODP caller with MNP on gets LAPM; a caller with
  no error control gets start-stop; with HST offered, V.8, V.32-AA,
  V.22bis, V.21 and Bell 103 callers connect as before (the last three up
  to W later).
- HST host pattern: endpoint linked before the first sample and reset at
  `CONNECTED` (decision 18) loses no LR, including one sent during our
  training; a caller that never sends an LR ends with `NO_EC` at our
  training's end + 5 s; every retrain gives `STOPPED`/`MOVING` with MNP's
  timers held (decision 21).

### Golden files, hashes and the replay proof

- **`make verify` on every commit:** every existing golden file unchanged;
  new programs get new golden files (recorded, not re-recorded); bench
  hashes unchanged, plus a new `hst` `JOBS` line in
  `tools/bench-hashes.sh`.
- **Replay proof:** with HST not offered and MNP off, identical to 0.6.0
  for every private recording in all six flag sets. With `-H` (HST
  offered), identical for every recording whose caller sent CM or AA; for
  the rest the detector must not fire (a report lists any that does).
- **`tools/v90m_replay.c`:** a `-H` flag (an endpoint linked in replay),
  the phase names, and an `hst` section in the report (detector, training
  segments seen, rates, reversals).
- **`tools/hst_scan.c`:** runs our receivers over a recording and prints,
  per channel, tone frequency, length and reversals; silences; training
  segment boundaries (AB, CD, TRN, data, by point-set statistics);
  constellation size; back-channel preamble length; decoded frames as
  headers and parameters only. This is how the evidence is read.

### Interop runs

- **BinModem (milestone 6)**, outside the repository like
  `tools/k56-peer`. Build BinModem's main branch as BF §9 describes
  (headless crates). Run the local nsdiald test build (decision 22: HST and
  MNP on, this machine only, never deployed) at `127.0.0.1:5070`, and call
  it:
  `modem-sip --type "AT+MS=HST" --type "ATD sip:modem@127.0.0.1:5070"
  --send … --hold … --seconds 120`. Cases: `AT+MS=HST`, `AT+MS=HST96`,
  `AT+MS=HST,0,0,9600`, a long send to force a reversal, `--drive`
  levels. Pass: exit 0 with `CONNECT …/ARQ/HST`, test text both ways, and
  every reversal and rate change in BinModem's stderr matching our status.
  Run `-DHST_PROFILE_BM` first, then the default build, to see which steps
  the stand-in training breaks. `modem-loop` is optional. Only results are
  committed, as text in PROGRESS.md.
- **Port matrix** once per milestone; **`make cost-hst`** at 4, 5, 8.

## Evidence: the recordings that settle the provisional parts

**R1: a Courier answering BinModem, tapped at the ATA (decision 17: as
early as possible).** The owner's Courier on an ATA port (the Cisco
VG202XM preferred: RFC 2198, about 40 ms jitter buffer), answering HST at
`B1 &N0 &M4`; BinModem calls its SIP address with `AT+MS=HST`; the RTP is
captured at the ATA in both directions as G.711 (tcpdump on its VLAN, and
BinModem's `--record` as a second copy); then again at `B0`. It settles
the answerer's tone (2100 or 2225 Hz, length, `B0` against `B1`), the
silence after it, whether the fast channel starts directly or a V.22bis
carrier comes first, the training's segments and lengths, the opening
rate, the forward pulse and level, and the Courier's LR reply (0xC0,
parameter 8) if BinModem's back channel gets through. It needs none of our
code, so it runs alongside milestone 1. Even if BinModem's stand-in back
channel brings up no link, the tone and the training are recorded: they
precede any frame.

**R2: the Courier calling our answerer** (milestone 7), recorded on the box
and cut at MNP link-up. It settles whether a Courier caller accepts
ANSam; its back channel (preamble, the "17-tick start", pulse, tribit
order); its LR (template, 0xC0, parameter 8); whether it takes our
provisional training; its reversal and rate-change timing (from status
after link-up, not audio); and whether it sends LT1–3 and the like
(`rx_unknown_id`).

**R3: real MNP-only callers over V.32bis and V.22bis** (milestone 1). The
owner's modems forced to MNP (the USR with `&M4 S27=32`, V.42 detection
and LAPM off; the Rockwell/Conexant parts with the setting their manuals
give) call the test number, recorded up to link-up only. It settles the
mode 2 FCS's initial value and octet order, and the time to the first LR.

For all three: recordings stay outside the repository and only hashes are
committed; R1 and R2 use test text only; a recording that shows a fault
becomes a fixture (CONTRIBUTING.md), truncated before the first LT.

## Milestones, each independently testable

1. **MNP endpoint (Annex A).** Write `docs/history/hst-facts.md`. Build
   `v42/mnp*`: modes 2 and 3, LR detection, configuration, status, memory
   pin. Gate: `sim/mnppeer` over a bit loop, then over V.32bis and V.22bis
   in simulation; `make verify` with every golden file unchanged. Make R1
   in parallel. R3 waits for the local nsdiald test build (after 2).
2. **HST profile.** 0xC0 and the merge, the streamlined layouts and the
   tolerant parser, attention codes, the mailbox and `v90m_ec_link_hst()`
   (a stub pump in tests). Gate: `mnppeer` in HST mode against the
   endpoint over a bit loop. The API is then ready, and the ISP session
   builds the local nsdiald test build; R3 runs through it.
3. **HST back channel.** Transmitter, receiver, detector, generated
   tables. Gate: loopback, the detector's positive and negative tests and
   its scan over the private recordings, the receiver's CPU.
4. **Fast channel and the answering branch.** The `v32tx` pulse pointer,
   the HST pulse, guard filters, the provisional training, the `qamrx`
   acquisition, `ANS_HST_WAIT` and `ANS_HST`, the API constants, the
   post-agreement retrain. Gate: `sim/hstcall` to data at every rate, both
   channels on the line model, start-up and loss end codes, the replay
   proof with and without `-H`, `cost-hst` (holding forward). Analyse R1
   now; any finding changes the training table or a constant before
   milestone 5.
5. **Reversal and rate changes.** Both roles, the stored equaliser, 0x3R
   and 0x2R, the policies, watchdogs, `reversals`. Gate: the end-to-end
   suite, `cost-hst` (holding back), the port matrix.
6. **BinModem interop**, over SIP through the local nsdiald test build.
   The `-DHST_PROFILE_BM` build, then the default.
   Each fix with a test seen failing; results in PROGRESS.md.
7. **Courier evidence and fixes**, through the local nsdiald test build.
   R2 with the owner's Courier, then Will
   Sowerbutts' V.Everything. Each provisional item settled, or recorded as
   still open. Live calls from the owner's non-HST modems with HST offered
   and MNP on must connect as before.
8. **0.7.0.** Docs and CHANGELOG, `make cost-hst`, the port matrix,
   `verify-phase`. **Labels (mine):** **L1** if only BinModem has passed
   ("HST answering, experimental, off by default; tested against BinModem;
   the forward training is provisional"); **L2** once a Courier has
   connected ("connects with a Courier <model> at <rates>"). The deployed
   nsdiald turns HST and MNP on in its own change after the release, only
   after the live non-HST calls have passed. `docs/error-control.md` and
   `docs/getting-started.md` state the HST host pattern (decision 18).

## Conflicts between the sources, and what we do first

| question | sources | first | how we will know |
|---|---|---|---|
| Answer tone: 2100 or 2225 Hz | firmware: plain 2100 **[FW]**; manuals: `B1` Bell 2225, `B0` V.25 2100 + V.32 tones **[PUB]** | **ANSam** (2100 Hz with reversals), unchanged for every caller | R1 shows what a Courier answerer sends at `B0` and `B1`; R2 shows whether a Courier caller starts after ANSam. If not, test builds with a plain 2100 or 2225 Hz tone are tried on R2's setup. |
| Start-up: V.22bis at 2400 then shift up, or straight onto the fast channel | manuals: "training at 2400 bps … then shift up and train at 9600" **[PUB]**; firmware: forward channel and back receiver right after the tone **[FW]** | **straight onto the fast channel** (the firmware is newer and more specific; the manuals may describe 1986 or simplify) | R1. If the Courier sends a V.22bis carrier after its tone, we add an HST shift-up over V.22bis + MNP (an addition under B, not a redesign) and the detector moves into the V.22bis wait (LM §1.3 C). |
| LR framing in establishment | Annex A: start-stop **[PUB]**; HST: synchronous class 3 **[FW]**, LR in HDLC **[BM]** | accept both, reply in kind | R2 |
| Optimised headers | Annex A: LI kept, fixed LT/LA **[PUB]**; patent: LI dropped on all frames **[PUB]**; Courier template: no parameter 8 **[FW]**; BinModem: parameter 8 needed **[BM]** | send per agreement, parse every layout | R1 and R2 frames |
| Rate change: marks or retrain | patent: 15 ms marks at each rate **[PUB]**; firmware: no retrain **[FW]**; BinModem: retrain **[BM]** | marks for 0x3R, retrain for 0x2R, plus recovery | R2, a long call |
| Reversal thresholds | patent: 64 octets, 2 s **[PUB]**; BinModem: 256, 3 s **[BM]** | the patent's | R2 (status) |
| Symbol rate at 9600 | firmware: 2400 baud **[FW]**; HST.bis patent: 3200 baud **[PUB, P3]** | 2400 (HST.bis is a later, different modem) | R1 |
| Opening rate | manual: first training at 9600 **[PUB]**; BinModem: 7200 **[BM]** | 9600 (7200 in the BinModem profile) | R1 |

## Open questions, and how each is answered

1. **Does a Courier caller accept ANSam, then send its back channel?** R2;
   if not, the tone variants.
2. **The Courier answerer's tone and gap, any V.22bis stage, the opening
   rate (constellation size before the LR), the forward pulse and the
   forward/back level ratio.** R1.
3. **The training's contents and the long/short rule.** R1 (first
   training), R2 (after a reversal), read with `hst_scan`.
4. **The back channel's pulse, preamble, start and tribit order.** R2, or
   R1 after a reversal (BinModem sending a large file).
5. **The other 0xC0 bits; parameter 8; which layout a Courier sends.** The
   Courier's frames in R1 and R2, with `ATI7` and S-registers noted.
6. **The IDs and layouts of NAK, LTA, LT1–3, LTR, LTDR.** FIG. 5 of
   US 5,008,901, read from the patent's PDF image by a person into
   `hst-facts.md`; and R2's `rx_unknown_id`.
7. **The rate change on the line** (marks, trellis and scrambler
   continuity, where the new rate starts) **and reversal timing** (the gap
   after the LNA). R2 on a long call with a forced shift, from status
   timestamps; R1 after a reversal.
8. **The mode 2 FCS's initial value and octet order; how soon MNP callers
   send their LR** (is T400 = 750 ms enough?). R3.
9. **When a Courier moves to the 300 back channel.** R2 with `S15` bit 2
   set on the Courier.
10. **Low-band loss through ATAs** (a high-pass near 300 Hz hurts the back
    channel). The back receiver's SNR status on the HT802v2 and the
    VG202XM in R2; recorded in `limits.md`.
11. **16800's set and trellis; whether the 1986 HST shares the framing.**
    R1 or R2 with `&N9`, and a 1986 unit if one turns up. Out of scope for
    0.7.0.

## Left to the builder

The file split inside `v42/` and `modem/` and internal names; W, within
400–700 ms, from simulation (decision 25); the filter
designs in `gen_hst_tabs.py` within the stated bands and rejections, and
the table lengths; the detector's and receivers' thresholds (set in
simulation, recorded in `limits.md`); whether the back scrambler reuses
`modem/scramble.c` with an option (V.22bis's output unchanged); whether
the back receiver needs a 3-tap equaliser; `qamrx` or the `v32rx`
fallback within its 2-day box; whether BinModem offers MNP over V.32bis or
V.22bis (if so, it is a second MNP peer at milestone 1); test names,
fixture layout and `SPLIT_TESTS`; and the registration traps (`MODEM_SRC`,
`SIM_SRC`, `TESTS`, `ans_end[]`, the `SAME` checks, `phase_scheme[]`,
`tools/trace.c`, `tools/v90m_fields.c`; LM §6 item 7).

## Follow-ups after 0.7.0

MNP 5 (US 5,027,376 **[PUB]**) in the endpoint's encoder and decoder
unions, negotiated as R3 shows; 16800, once a recording shows its set and
trellis; decoding NAK, LTA, LT1–3, LTR and LTDR, once FIG. 5 or a
recording gives them; an HST shift-up over V.22bis, if R1 shows that path;
the calling side, if the owner ever wants it.
