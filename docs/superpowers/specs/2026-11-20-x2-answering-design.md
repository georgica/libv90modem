# x2 on the answering side: design

2026-11-20. Status: draft for the owner's approval; the builder check's questions are
answered below ("Decisions").

## Why

The owner wants the box to serve as many modems as it can. A modem that
speaks x2 (U.S. Robotics, 1997) but whose V.90 start-up fails, or that was
never upgraded to V.90, gets V.34 at 33600 at best today. x2 would give it a
56k-class downstream. K56flex is the other pre-V.90 scheme. It comes after
x2, in a design of its own, once a K56flex client modem is at hand.

The research behind this is in
`~/modem-research/reports/x2 and K56flex protocols.md`, with its notes
alongside.

## Goals

- libv90modem answers an x2 call as the digital (server) side:
  - the negotiation;
  - the server's PCM training and the record exchange;
  - downstream data mode;
  - the client's in-call rate changes.
- Upstream is V.34, as in x2.
- A caller that does not ask for x2, or whose x2 start-up does not complete,
  gets what it gets today: V.90, V.92, V.34 or lower.
- Tried against a real U.S. Robotics modem through v90d. It is the first
  stage of this library that real hardware checks before it is called done.

## Non-goals

- x2 on the calling side.
- The symmetric digital-to-digital x2 mode.
- A-law. No known client completed it, so an A-law line gets V.34.
- K56flex (later, separately).
- Rate changes or retrains that the server starts itself.

## Anti-goals: failure even if x2 works

- Any change to what the answering modem does for a caller that is not an
  x2 caller. While x2 is not offered, the replay proof must be identical.
  While it is offered, the same must hold for every recording of a real
  V.34 / V.90 / V.92 caller (see "Proof").
- Code or table files copied from D-Modem (AGPL-3.0) or courier-emu (no
  licence), or firmware disassembly pasted in as code.

## Constraints

- C99, fixed point in the signal path, no allocation after create, no global
  state, and identical output on every port target.
- Test-first: each test is seen failing before the code that passes it.
- The ARM926 budget. The x2 downstream encoder is a table lookup per
  sample. The upstream receiver is V.34's, already costed in V.90.
- GPL-2.0-only.

## Sources and what may be taken from them

The owner chose to rebuild tables first, and include what cannot be rebuilt
with its origin stated.

- **Facts** about signals, fields, timings and sequences may be taken from
  any source, cited in the code's comments and in `docs/history`. The
  sources are:
  - D-Modem `docs/dsp/x2-details.md` and `x2.md`, and PR #104;
  - courier-emu's "x2 technical specification, Draft 0.33";
  - USR's x2 troubleshooting guide and command reference;
  - Townshend's expired patents.
- **Our own disassembly** of the U.S. Robotics server firmware (the I-modem
  `Ie030002` and the Total Control Quad `QF060003`, as D-Modem and
  courier-emu used them) is allowed. Its only uses are to settle points where
  the sources disagree or are silent, and to check tables. What it shows is
  written down as facts, never transcribed as code.
- **Level tables** (the per-rate, per-pad level banks):
  1. First, derive them: from the µ-law levels, the pad and the power limit,
     by whatever rule makes our results equal D-Modem's published banks.
  2. Where a rule reproduces a bank exactly, we use the rule.
  3. Where none does, we include the bank as data, in a file of its own that
     names its origin (read from the Quad x2 firmware, as published by
     D-Modem). `docs/limits.md` and the CHANGELOG say so too.
- **Rate index 1 (32000)** is the first target. It needs only two 9-octet
  level sets, which are published in D-Modem's notes.

## Decisions (the owner's, 2026-11-20)

- **Who gets x2:** only callers whose CM offers V.34 and not PCM (V.90).
  A caller that offers V.90 gets V.90, as now.
- **Proof includes live calls:** before v90d may switch x2 on, the
  Conexant and the 5686 call the box with x2 offered (V.34, V.90, V.92, a
  few calls each), as well as the replay proof. All of them must connect
  as before.
- **courier-emu as a local test peer:** the original Courier firmware runs
  against our answerer, locally only. The ROM image stays outside the
  repository and is never published; the test is skipped when the image
  is absent, like the spandsp tests.

## Calls made in the design (from the builder check)

- **Offer x2 only when it can be served.** That means a µ-law line and
  index 1's tables present. Once a caller has sent the x2 marker, there is
  no known way back to V.34: if the start-up then fails, or the client asks
  for something we cannot serve, the answerer clears down. It is recorded
  and tested.
- **INFO0a bits 23 and 24:** in this codebase bits 21-23 are `max_sr_diff`
  and bit 24 is `cme`. Step 2 settles D-Modem's bit numbering from the
  firmware before any of them is sent. The bits are set only while x2 is
  offered, and the live calls above check the callers that are not x2.
- **JM:** when x2 is offered and the CM carries the PSTN access octet, the
  JM carries it back with the digital-network bit, as the I-modem's
  `... 8d` does. `v8_build_jm` today adds it only with a PCM octet.
- **Our record's rate mask** names only the indices we have tables for.
  If a client asks for a higher index anyway, we clear down. The
  recordings show whether a real client respects the mask.
- **Pads:** index 1's level sets for the 3 dB and 6 dB pads come in step 4,
  not step 6, so the first real call works whatever the pad on the box's
  line.
- **No robbed-bit rule:** no known MP field reports robbed bits, and the
  box's line (an ATA) should have none. A robbed-bit line is a limit stated
  in `docs/limits.md` until a recording shows otherwise.

## Approaches considered

1. **A new answering path for x2, alongside V.90's (chosen).** It reuses:
   - V.8;
   - V.34 Phase 2 (with a marker receiver added);
   - the V.34 Phase 3 receiver (with a scrambler override);
   - V.34 data upstream;
   - V.90's 6-sample frame timing and its GPC scrambler.
   
   The new parts are the PCM training script, the record framing with its
   CRC, the mixed-radix encoder, and the answer to a client's rate change.
   This fits how the answerer already branches between V.90 and V.34 after
   Phase 2.
2. **x2 as a variant inside `v90/`.** This is rejected. x2's training,
   records and mapping share almost nothing with V.90's DIL, CP and Ucode
   mapping, and the conditionals would run through the V.90 code that has
   real-call proof.
3. **Port D-Modem's x2 server.** This is excluded by its licence. It is
   also the echo chamber the research warns against: its reading tested
   against our copy of its reading.

## The design

### API

- **`V90M_MOD_X2`:** a new modulation bit. It is not in the defaults
  (like Bell), and it needs `V90M_MOD_V34`; without V.34 the configuration
  is refused with `V90M_ENOTSUP`. Answering role only for now, so it is
  refused in the calling role.
- **`V90M_CONN_X2`:** appended to `v90m_conn`.
- **Rates in `v90m_state`:** the payload rates down and up. They are not
  USR's CONNECT display figure, which is one 1333 step higher. A comment in
  the header says so.
- **`V90M_MEM_SIZE`:** grows only if the x2 state does not fit the union
  V.90 / V.92 already uses.

### In the answering modem (`modem/answer.c` and new `modem/x2*.c`)

- **V.8:**
  - When x2 is offered, the JM's carrier sign is inverted after each
    complete JM.
  - The JM octets are unchanged; they already carry PSTN access with the
    digital-network bit.
- **Phase 2:**
  - When x2 is offered, INFO0a sets bits 23 and 24.
  - After L1, a marker receiver watches for the client's 7-bit marker in an
    INFO frame. The client sends it in place of its L1, L2 and INFO1c.
  - With PCM index 6 the call is x2.
  - With no marker within 800 ms, or another index, the call is V.34, on
    the existing path.
  - The bits and the JM flip are sent only when the CM offers V.34 without
    PCM and the line is µ-law.
- **Phase 3:** the existing V.34 receiver with the GPA scrambler override.
  The server stays silent until the client's J.
- **The server's training script, `modem/x2tx`:** a fixed sequence of
  segments:
  1. preamble;
  2. 2-level TRN;
  3. `09B0` words;
  4. ones;
  5. alignment;
  6. sync frames until the client's MP;
  7. the server record until the client's E;
  8. the start segment.
  
  Each segment's length and level come from the sources. The one
  disagreement (the TRN and ones levels) is settled by the disassembly
  before the test for it is written.
- **Records, `modem/x2rec`:**
  - The format: 17 ones and a zero, then words LSB first each followed by a
    zero, then CRC-16 (start FFFF, 0x8408 reflected, not inverted).
  - The client's MP gives its rate ceiling, upstream mask, pad and MD.
  - The server's rate is the highest index both sides allow, and only 1
    until the banks for higher ones exist.
- **Data:** `modem/x2enc`. Each frame does the following:
  1. takes MD + B scrambled bits;
  2. sends the signs differentially;
  3. uses the B bits to select one level per position, in mixed radix;
  4. puts the signs on the smallest selected levels;
  5. sets the sixth sign by a running sum.
  
  Upstream is the V.34 data receiver, as in V.90.
- **Rate changes, client-initiated:** the server waits for the client's MP
  and then sends, in order:
  1. the marker frames;
  2. a scrambler reset;
  3. the record, 10 times unacknowledged and then 33 times acknowledged;
  4. 681 frames of ones at the new rate;
  5. data.
  
  It never sends the marker first. It moves only to the exact rate the
  client asked for, and when it has no bank for that rate it refuses.
- **Not settled by any source:** a full retrain asked for by the client.
  It ends the call (clears down), and the recordings show what the client
  actually does.

### Tests (all before the code that passes them)

- **Unit tests per piece:**
  - the CRC, against the records the sources print (`F33C 7FFF 0000`,
    `0344 03FE 0000 0500`);
  - the marker framing;
  - the mixed-radix mapping, against D-Modem's worked levels at index 1;
  - the training script's segment lengths;
  - the rate-change sequence's order and the refusals.
- **A simulated x2 client (`sim/x2call`):** written from the same facts.
  It checks that our pieces fit together; it does not prove x2. The test
  calls go from it to our answerer through the existing line simulator:
  - x2 at 32000;
  - the client's rate change;
  - no marker, so V.34 follows;
  - an A-law line, so V.34;
  - x2 offered but a V.90 caller, so V.90 as today.
- **Local only, not in the repository:** our answerer against courier-emu
  running the original Courier firmware, like the spandsp peer tests. This
  is the strongest check short of real hardware (agreed by the owner, with
  the ROM image kept outside the repository).
- **Real modem:** the owner's 1998 U.S. Robotics, then the 5686, through
  v90d:
  - set with `ATS32=66` for x2 only, and `ATI7` must list x2;
  - every call recorded;
  - the recordings become fixtures, decoded field by field as the
    real-caller checks are now;
  - what the recordings show is noted per item in the research report's
    "what a real modem must confirm" list.

### Proof that nothing else moved

Two runs, as before each answering-side change: every test count and the
replay of the 1613 real-call outputs.
- **x2 not offered:** identical to 0.3.0.
- **x2 offered:** for the recordings of real callers (none of them x2),
  everything the modem decides and decodes is identical. That covers the
  connection, rates, received bits and events. Only the transmitted samples
  of JM and INFO0a may differ.

The library's port matrix runs once, at the end. The fast-verification rules
apply throughout.

## Order of work, each step ending in something that works

1. **Repository.** Bring `~/libv90modem` onto the published history (the
   old `main` is kept as a branch). Work happens there or in its worktrees,
   so v90d's `../libv90modem` and GitHub agree.
2. **Settle the disagreements.** Disassemble the I-modem and Quad firmware
   to settle the TRN and ones levels, the free-sign rule, and the INFO0a /
   JM-flip question. Write the result up as facts in `docs/history`.
3. **Negotiation.** Offered only where it can be served, with the marker
   receiver. Before the training exists, a marker leads to a clear-down,
   and no marker leads to V.34 as now. Proof: the replay, then the live
   non-x2 calls with x2 offered.
4. **Training, records, data at 32000,** with the pad sets. The courier-emu
   peer, then a real call at 32000 down.
5. **Answering the client's rate change.** Before the higher banks exist,
   the server refuses a higher rate; the call stays at 32000.
6. **Higher rates.** Banks derived where possible, otherwise included with
   their origin. Real calls at each rate the line allows.
7. **Release.** CHANGELOG, docs, the port matrix, then v90d takes it up: the
   v90d agent adds the setting and the box image.

## Left to the builder

- How the x2 state shares memory with the V.90 / V.92 union.
- The exact file split under `modem/`.
- The 6-sample frame clock: shared with `v90/` or a copy of its few lines.
- The test-name and fixture layout, following the repository's.
