# x2 answering side: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** libv90modem answers an x2 caller as the digital (server) side:
negotiation, PCM training, records, downstream data at every rate it has
level banks for, and the client's in-call rate changes. A caller that is not
x2 gets exactly what it gets today.

**Architecture:**
- Offering x2 adds three things to the answer path: a JM carrier flip in
  V.8, two INFO0a bits, and a marker receiver after L1 in `p2ans`.
- A marker hands the call to a new engine, `modem/x2d`. It plays the same
  part for x2 that `p34d` plays for V.90:
  - transmit: G.711 octets from `x2tx` (the training script) and `x2map`
    (downstream frames);
  - receive: the caller's V.34 Phase 3 / 4 and data, through
    `qamrx` + `v34/`, as in `p34d`.
- The x2 records reuse V.34 MP framing (`v34mp`), with their own word
  count.

**Tech stack:** C99, fixed point, the existing `modem/`, `v34/`, `v90/` and
`sim/` code, and make.

**Spec:** `docs/superpowers/specs/2026-11-20-x2-answering-design.md`
(approved 2026-11-20). Research: `~/modem-research/reports/x2 and K56flex
protocols.md` and its notes. Facts: `~/modem-research/sources/dmodem-x2-details.md`.

**Worktree:** `~/x2-wt`, branch `x2` (from `x2-design`, on GitHub's `main`).

## Global Constraints

- C99, fixed point in the signal path; no allocation after create; no
  global state.
- Identical output on every port target: fixed-width types, no left shift of
  negative numbers, no byte-order assumptions, PRId64.
- Test-first: every test is seen failing before the code that passes it.
- Nothing copied from D-Modem (AGPL-3.0) or courier-emu (no licence) as code
  or as table files. Facts are cited by URL in comments and in
  `docs/history/x2-facts.md`.
- **Tables:** derived where a rule reproduces them. Otherwise included as data
  in a file that names its origin ("read from the Quad x2 firmware
  QF060003, as published by D-Modem"), and listed in `docs/limits.md`.
- x2 is offered only when all of these hold:
  - `V90M_MOD_X2` is configured; it needs `V90M_MOD_V34` and is answering
    only, otherwise `V90M_ENOTSUP`;
  - the CM offers V.34 and no PCM;
  - the law is µ-law.
- After the marker, any failure clears down (there is no way back to V.34),
  with an end reason.
- Our record's rate mask names only the indices that have banks. A request
  above it clears down.
- **Fast verification:**
  - host `make check` per commit;
  - `make check-arm` (parallel, short tests) at the end of a task that
    touches the signal path;
  - the port matrix once, in the release task.
- **Commits:** on branch `x2` only. Nothing is pushed until the release task
  says so. Every message ends with:

  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01SYiJ2pke9RRDZbEapAFvX3
  ```

  Author: `George Partene <partene.george@gmail.com>`
  (`git -c user.name=... -c user.email=...`).

## Review Focus

1. **A plain V.34 caller with x2 offered:** it sends no marker and must
   connect V.34 exactly as before. Test in Task 4:
   `sim: v34 caller, x2 offered -> V.34, same rates`.
2. **An A-law line, or a CM with PCM, with x2 offered:** no INFO0a x2 bits
   and no JM flip, so the output is identical. Test in Task 4.
3. **A marker with a bad CRC, one later than 800 ms, or an index other than
   6:** V.34 continues, with no crash and no hang. Test in Task 4.
4. **A client MP asking for an index above our mask, or reporting pad 3 or
   more:** a clean clear-down with `V90M_END_X2_FAILED`. Test in Task 6.
5. **The client going silent at any point after the marker:** the call ends
   within the timeout with an end reason, never stuck. Test in Task 6.

---

### Task 1: Repository on GitHub's history (done)

`~/libv90modem` now has `origin` = GitHub, and `main` = `origin/main`. The
old `main` is kept as `pre-publish-main`. Branch `x2` is checked out in
`~/x2-wt`.

### Task 2: The facts, settled and written down

**Files:**
- Create: `docs/history/x2-facts.md`
- Create: `tools/x2_pad.c`, a host tool, not built into the library: it
  derives the pad octet pairs.
- Test: `test/test_x2_pad.c` (host)

**Interfaces:**
- Produces: `docs/history/x2-facts.md`. Every later task takes its values
  from it, by section name:
  - `Phase 1`, `Phase 2 marker`, `Phase 3 script`, `Records`, `Mapping`,
    `Free sign`, `Rate change`, `Pads`, `Banks`, `Upstream`.
  - Each value has its source (URL or firmware address) and a tag:
    documented, reverse-engineered (by whom), our disassembly, or derived.
- Produces: `int x2_pad_octet(int recv_octet, int pad_mode)`. It returns
  the µ-law octet the server must send so that, after a pad of
  0 / 3 / 6 dB (`pad_mode` 0 / 1 / 2: decode, scale, re-encode to
  nearest), the client receives `recv_octet`. It returns -1 when no octet
  does that.

- [ ] **Step 1: Collect the facts.** Gather them from:
  - `~/modem-research/sources/dmodem-x2-details.md` and `dmodem-x2.md`;
  - courier-emu's `docs/` and `artifacts/x2-spec-20261004/` (clone
    `github.com/cryan209/courier-emu` into `~/modem-research/sources/`);
  - USR's `x2shoot4.pdf` and `x2comref.pdf`;
  - D-Modem `dsp/x2.cpp` and `dsp/v34_x2.h`, read for values only, never
    copied.

  Write `docs/history/x2-facts.md` with the sections above. It must have
  exact values for:
  - the preamble's 22 octets;
  - the alignment pattern;
  - TRN, J-word and ones levels and lengths;
  - levels A and B;
  - the record words;
  - the marker's INFO-frame layout (fill, sync, 7 bits, CRC, closing ones)
    and its timing after our L1;
  - the start segment;
  - the rate-change sequence;
  - the per-pad octet pairs at index 1.
- [ ] **Step 2: Settle the disagreements with our own disassembly.** Use the
  Ie030002 / QF060003 images from courier-emu's `firmware/` or D-Modem's
  `dsp/test/`, and a disassembler (`objdump`, or `r2` if installed). The
  disagreements:
  - TRN ±0x31 and ones ±0x41, against courier-emu's "lower amplitude";
  - the free-sign rule in levels A and in the data levels;
  - whether INFO0a needs both bits 23 and 24 (the facts give:
    `max_sr_diff` = 4, `cme` = 1).

  Record what the firmware does, at which address, as facts. No code is
  transcribed.
- [ ] **Step 3: Write the failing test** `test/test_x2_pad.c`:

  ```c
  /* pad 0 is the identity */
  CHECK(x2_pad_octet(0xa5, 0) == 0xa5);
  /* for each of levels B at index 1, pads 1 and 2: the octet the server
     sends, decoded, scaled by 10^(-3/20) or 10^(-6/20) and re-encoded,
     gives the level back */
  for (i = 0; i < 9; i++) for (m = 1; m <= 2; m++) {
      int o = x2_pad_octet(levels_b[i], m);
      CHECK(o >= 0 && pad_apply(o, m) == levels_b[i]);
  }
  /* and equals the pair the facts list for index 1 (x2-facts.md "Pads") */
  ```

  Run `make build/test_x2_pad && build/test_x2_pad`. Expected: it fails to
  link (no `x2_pad_octet`).
- [ ] **Step 4: Implement `x2_pad_octet`** in `tools/x2_pad.c`, using
  `sim/g711.c`'s decode and encode. The pad model is the network's: decode,
  multiply in Q15 by 23198 (−3 dB) or 16423 (−6 dB), round, encode to
  nearest. If no octet maps onto the level exactly, return -1, and the
  facts file says the Quad's pair is used instead.
- [ ] **Step 5: Run the test.** Expected: all checks pass, and the result
  says whether every pair at index 1 is derivable. Note in
  `x2-facts.md "Pads"` which pairs are derived and which are taken from
  the Quad.
- [ ] **Step 6: Commit.** Message: "x2: the facts, settled, and the pad
  octets derived".

### Task 3: courier-emu as a local test peer

**Files:**
- Create: `tools/x2-peer/README.md`. It says how to set up courier-emu and
  the ROM image outside the repository.
- Create: `tools/x2-peer/bridge.c`. It runs our answerer through the public
  API against the emulated Courier over a digital line: µ-law octets each
  way, optional pad.
- Modify: `Makefile`. Add a `test-x2-courier` target that skips with
  "COURIER_EMU not set" when `COURIER_EMU` is unset. It is not part of
  `check`.

**Interfaces:**
- Consumes: the public API only (`v90m_create`, `v90m_process_g711`,
  `v90m_get_state`).
- Produces: `make test-x2-courier COURIER_EMU=<dir> [PAD=0|3|6] [MODS=x2]`.
  It prints the Courier's CONNECT line, its `ATI11`, and our state, then
  exits 0 when both sides reached data mode, otherwise 1.

- [ ] **Step 1: Set up courier-emu locally.** Clone it into
  `~/modem-research/sources/courier-emu`, build its native part, and find
  how D-Modem's harness drives it as an analogue client: `courier_peer`,
  `COURIER_PEER_ANALOG`, the dial command, the line interface.
- [ ] **Step 2: Write the bridge** with x2 not offered. Expected: the
  Courier calls our answerer and connects V.34, which proves the harness.
  The run gives the Courier `S32=66` and the settings D-Modem used
  (`S54=0S58=48&A3&B1Q0`).
- [ ] **Step 3: Run it.** Expected: `CONNECT ... V34` and exit 0. Record
  the run's log in `docs/history/x2-facts.md "Harness"`.
- [ ] **Step 4: Commit.** Message: "x2: courier-emu as a local test peer
  (outside the repository)".

### Task 4: The API, and negotiation with V.34 fallback

**Files:**
- Modify: `include/v90modem.h`:
  - `#define V90M_MOD_X2 0x0100u`;
  - `V90M_CONN_X2`, appended to `v90m_conn`;
  - `V90M_END_X2_FAILED`, appended to `v90m_end`;
  - comments for each.
- Modify: `api/v90modem.c`:
  - `check_cfg`: X2 without V34 gives `V90M_ENOTSUP`; X2 in the originate
    role gives `V90M_ENOTSUP`.
  - Map to `ANS_MODE_X2`.
- Modify: `modem/answer.h`:
  - `#define ANS_MODE_X2 0x100000u`;
  - `#define ANS_MOD_X2 10`;
  - phase `ANS_X2`;
  - `int x2_offered`.
- Modify: `modem/v8ans.c` and `.h`:
  - when the answerer offers x2, the CM selects V.34 without PCM and the
    law is µ-law: invert the carrier after each complete JM, and return the
    PSTN access octet with the digital-network bit;
  - expose `int v8ans_x2(const v8ans_t *)`.
- Modify: `modem/p2ans.c` and `.h`:
  - `p2ans_cfg.x2`: INFO0a gets `max_sr_diff = 4` and `cme = 1`;
  - after our L1, a marker receiver reads one INFO frame (fill, sync,
    7 bits, CRC) within 800 ms;
  - new state `P2N_X2`, with `p2ans_t.x2_marker` set (0x00..0x7f) when the
    frame is valid and bits 1..3 = 6;
  - otherwise the V.34 path is unchanged.
- Modify: `modem/answer.c`:
  - `P2N_X2` goes to `ANS_X2`;
  - until Task 6, `ANS_X2` clears down with `V90M_END_X2_FAILED`.
- Modify: `tools/v90m_replay.c`: `-x` offers x2.
- Test:
  - `test/api/test_api.c` (config refusals);
  - `test/test_v8ans.c` (JM flip, access octet);
  - `test/test_p2ans.c` (INFO0a bits, marker accepted and refused);
  - `test/test_answer_x2.c`, new, the sim calls in the next step.

**Interfaces:**
- Produces:
  - `V90M_MOD_X2`, `V90M_CONN_X2`, `V90M_END_X2_FAILED`;
  - `ANS_MODE_X2`, `ANS_X2`;
  - `p2ans_t.x2_marker`;
  - `int p2ans_x2_info_frame(uint8_t marker7, uint8_t *bits)`: builds a
    marker frame, for the tests and the simulated client;
  - `int v8ans_x2(const v8ans_t *)`.

- [ ] **Step 1: Write the failing tests.**
  - API: X2 alone gives `V90M_ENOTSUP`; X2 with originate gives
    `V90M_ENOTSUP`; X2 | V34 answering gives `V90M_OK`.
  - V.8: with x2 offered and a V.34-only CM, the JM samples after each
    complete JM are the negation of today's. With a PCM CM, or A-law, they
    are identical to today's.
  - Phase 2:
    - INFO0a with x2 has bits 21..23 = 4 and bit 24 = 1. Without x2 it is
      identical to today's.
    - A marker frame `0x4d` 170 ms after L1 begins gives `P2N_X2` with
      `x2_marker == 0x4d`.
    - A bad CRC, a marker 900 ms after L1, or `0x0d` (index 0) leave the
      V.34 path unchanged.
  - Sim (`test_answer_x2`), covering Review Focus 1 to 3:
    - the existing V.34 sim caller with x2 offered connects V.34 at the
      same rates and bits as without x2;
    - with an A-law line the transmit samples are identical, and so they
      are with a CM that has PCM.

  Run `make check`. Expected: the new checks fail and the rest pass.
- [ ] **Step 2: Implement** the files above.
- [ ] **Step 3: Run `make check`.** Expected: all pass.
- [ ] **Step 4: Replay proof.** Run `v90m-replay` over the 106 recordings in
  `~/sync-build/live-050/*.raw` with `-9` and without it, then again with
  `-x`. Compare against main's tool:
  - without `-x`: byte for byte identical;
  - with `-x`: identical for every recording except the transmit lines of
    JM and INFO0a, since none of the recordings is an x2 caller.

  Record the commands and counts in `docs/history/PROGRESS.md "x2 4"`.
- [ ] **Step 5: `make check-arm`.** Expected: ALL ARM CHECKS PASS.
- [ ] **Step 6: Commit.** Message: "x2: offered in V.8 and Phase 2, the
  marker received; V.34 otherwise".

### Task 5: Records, CRC and the downstream mapper

**Files:**
- Create: `modem/x2rec.c` and `.h`
- Create: `modem/x2map.c` and `.h`
- Create: `modem/x2tab.c` and `.h`: level sets A and B, and the index 1 pad
  pairs, with their origin noted per the facts.
- Test: `test/test_x2rec.c`, `test/test_x2map.c`

**Interfaces:**
- Produces:
  - `int x2rec_build(const uint16_t *w, int nw, int ack, uint8_t *bits)`:
    returns the number of bits;
  - `int x2rec_parse(const uint8_t *bits, int n, uint16_t *w, int nw)`:
    returns words, or `X2REC_SHORT` (0) or `X2REC_BAD` (-1);
  - `uint16_t x2_crc16(const uint8_t *bits, int n)`;
  - `typedef struct { uint16_t w0, w1, w2, w3; } x2_mp_t;` with accessors
    `x2_mp_rate(w0)` (bits 2..5), `x2_mp_pad(w0)` (bits 11..14),
    `x2_mp_ack(w0)` (bit 15) and `x2_mp_md(w3)` (high byte);
  - `void x2map_init(x2map_t *m, int index, int pad_mode, int levels_a)`;
  - `void x2map_frame(x2map_t *m, const uint8_t *bits, uint8_t out[6])`:
    takes MD + B bits that are already scrambled;
  - `int x2map_bits(const x2map_t *m)`: B + MD;
  - `int x2map_unframe(x2map_t *m, const uint8_t in[6], uint8_t *bits)`:
    the inverse, for tests and the simulated client.

- [ ] **Step 1: Write the failing tests.**
  - The CRC and the framing reproduce the published records: the server
    `F33C 7FFF 0000` and `733C 7FFF 0000`, and the Courier MP
    `0344 03FE 0000 0500`. Parse(build(w)) == w. A flipped bit gives
    `X2REC_BAD`.
  - The mapper at index 1, levels B:
    - 24 bits give 6 octets whose magnitudes all come from levels B;
    - the 5 data signs are differential and sit on the smallest levels,
      with the first position winning a tie;
    - `unframe(frame(x)) == x` for 10000 random frames;
    - in levels A, the sixth sign follows the facts' rule (negative when
      the sum of the other five, as 14-bit linear values, is above 805).
  - Pads: with `pad_mode` 1 and 2, the octets sent are the pad pairs'
    sending octets.
- [ ] **Step 2: Implement.** Scrambling stays outside `x2map`: the caller
  runs `v90/v90_scr` (GPC). Mixed radix: B bits, least significant first,
  select a digit for each position in turn.
- [ ] **Step 3: Run `make check`.** Expected: all pass.
- [ ] **Step 4: Commit.** Message: "x2: records with their CRC, and the
  downstream mapper at 32000".

### Task 6: The x2 engine to data mode at 32000, and a simulated client

**Files:**
- Create: `modem/x2tx.c` and `.h`: the training script, segment by segment.
- Create: `modem/x2d.c` and `.h`: the engine. It is modelled on `p34d`
  (one received linear sample in, one octet out) and takes its receive
  parts from `p34d`:
  - Phase 3 S, S̄, PP, TRN, J with the GPA descrambler;
  - Phase 4 S, S̄, TRN (4 then 16 points), the MP as an x2 record, E;
  - data through `qamrx` + `v34/` with the caller's options.
- Create: `sim/x2call.c` and `.h`, a simulated x2 client on the digital
  side. It sends:
  - V.34 Phase 1 and 2 up to the marker;
  - Phase 3 and 4 and V.34 data, from the existing `sim/v34peer` pieces.

  It receives our octets through an optional pad, unframes them, and
  checks training, records and data.
- Modify: `modem/answer.c`:
  - `ANS_X2` runs `x2d`;
  - connected with `ANS_MOD_X2`, the rate down from `x2map` and up from the
    V.34 receiver;
  - `answer_cleardown` and the end reasons.
- Modify: `modem/answer.h`: `x2d_t` joins the `u` union. If the
  `V90M_MEM_SIZE` check fails, raise it and say so in the CHANGELOG.
- Modify: `api/v90modem.c`: `V90M_CONN_X2` and the state rates.
- Test: `test/test_x2tx.c` and `test/test_answer_x2.c`, extended.

**Interfaces:**
- Consumes: `x2rec_*`, `x2map_*`, `v90_scr` (GPC), and `p34d`'s receive
  parts (`qamrx`, `v34train`, `v34/`).
- Produces:
  - `int x2d_init(x2d_t *d, const x2d_cfg *cfg)`;
  - `uint8_t x2d_tick(x2d_t *d, int16_t rx)`;
  - `x2d_state`: `X2D_WAIT_J`, `X2D_TRAIN`, `X2D_MP`, `X2D_REC`,
    `X2D_START`, `X2D_DATA`, `X2D_RENEG`, `X2D_ENDED`;
  - `int x2d_down_rate(const x2d_t *)` and `int x2d_up_rate(const x2d_t *)`;
  - `int x2d_cleardown(x2d_t *)`;
  - `sim/x2call`: `x2call_init(x2call_t *, const x2call_cfg *)` with
    `pad_mode`, `max_index` and `up_mask`, then
    `int16_t x2call_tick(x2call_t *, uint8_t octet_from_server)` and
    `x2call_state(const x2call_t *)`.

- [ ] **Step 1: Write the failing tests.**
  - `x2tx` segment lengths and contents per `x2-facts.md "Phase 3
    script"`:
    - 363 preamble octets;
    - TRN of 20004, GPC from 0, positive for a scrambled 0;
    - 1200 J-word samples decoding to `09B0` ×99, then `099F`;
    - 6000 ones;
    - 1152 alignment;
    - sync frames that decode as ones in levels A.
  - Sim calls with `x2call` against the answerer:
    - pads 0, 3 and 6 dB each give connected `V90M_CONN_X2`, 32000 down and
      24000 up, with 100000 random bits each way equal end to end;
    - an MP asking for index 5 when our mask is `0x0002` clears down with
      `V90M_END_X2_FAILED` (Review Focus 4);
    - an MP with pad 3 clears down the same way;
    - the client going silent after the marker, at J, after its MP, and in
      data, ends the call within 10 s with an end reason (Review Focus 5).

  Run `make check`. Expected: the new checks fail.
- [ ] **Step 2: Implement** `x2tx`, `x2d`, `x2call` and the answer and API
  glue.
- [ ] **Step 3: Run `make check`.** Expected: all pass.
- [ ] **Step 4: The courier-emu peer** at `PAD=0`, `3` and `6`, x2 offered.
  Expected for each: the Courier prints `CONNECT .../x2/...` and its ATI11
  shows x2. Exit 0. If it differs from the sim, the sim is wrong: fix the
  sim and the code from the facts, then add a fact to `x2-facts.md`.
- [ ] **Step 5: `make check-arm`**, and the CPU figure. Run `bench-v90` with
  an x2 recording made by the sim (`rig/rec_x2` modelled on
  `rig/rec_v90`). Expected: the x2 data mode costs no more than V.90's.
  Record the numbers.
- [ ] **Step 6: Commit.** Message: "x2: training, records and data at
  32000, against a simulated client and the emulated Courier".

### Task 7: The client's rate change

**Files:**
- Modify: `modem/x2d.c`, state `X2D_RENEG`.
- Test: `test/test_answer_x2.c`, extended; `sim/x2call.c` gets
  renegotiation.

**Interfaces:**
- Consumes: Task 6's engine.
- Produces: renegotiation behaviour only, with no new names.

- [ ] **Step 1: Write the failing tests.**
  - The sim client renegotiates 12.7 s into data, asking for index 1 (the
    only one with a bank so far). We answer, per `x2-facts.md "Rate
    change"`, with:
    1. 8 frames `a5 a5 a5 25 25 25` and one `25 25 25 a5 a5 a5`;
    2. the scrambler from zero with the last sign positive;
    3. our record in levels B, 10 times with `733C` and then 33 times
       with `F33C`;
    4. one frame and then 681 frames of ones at the new rate;
    5. data.
    
    The bits run end to end before and after.
  - The upstream goes to the rate of the client's new mask (28800 with
    `1FFE`).
  - We never send the marker before the client's S: no marker frame
    appears before its MP.
  - A request for an index without a bank clears down with
    `V90M_END_X2_FAILED`.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Run `make check`, then the courier-emu peer** (the Courier
  renegotiates on its own timer). Expected: the call carries on after the
  Courier's renegotiation, at index 1 down and 28800 up. If the Courier
  asks for an index above our mask, note what it does in
  `x2-facts.md "Harness"`.
- [ ] **Step 4: Commit.** Message: "x2: answering the client's rate change".

### Task 8: Higher rates

**Files:**
- Modify: `modem/x2tab.c`: banks for indices 2..15 and pad modes 0..2.
  Each one is either derived (the rule written in `x2-facts.md "Banks"`) or
  included with its origin.
- Modify: `modem/x2d.c`: the record's mask becomes every index with a
  bank, and the rate change moves to the asked index.
- Test: `test/test_x2map.c` and `test/test_answer_x2.c`, extended.

- [ ] **Step 1: Try to derive the banks.** Find a rule (from the facts, the
  Quad's bank constructor as our disassembly reads it, the µ-law levels,
  the pad and the power limit) whose output equals the published banks for
  1, 8, 11, 12, 13 and 14. Where it equals all of them, use the rule.
  Otherwise include each bank as data, with its origin.
- [ ] **Step 2: Write the failing tests.**
  - Each index: B per the facts' table (19, 22, 25, then 22 + n), the
    level counts per position per the table, and
    `unframe(frame(x)) == x`.
  - Sim calls: the client asks for 8, then for 14, behind pads 0, 3 and
    6 dB, and data runs end to end at 46666 and then 54666.
- [ ] **Step 3: Implement. Then run `make check` and the courier-emu peer**
  at each pad. Expected: the Courier's renegotiations are followed to the
  rate it asks for. Record each run's rates.
- [ ] **Step 4: `make check-arm`. Then commit.** Message: "x2: rates up to
  56000 (banks: <derived | from the Quad firmware>)".

### Task 9: Release 0.4.0

**Files:**
- Modify:
  - `CHANGELOG.md` (0.4.0);
  - `README.md` (x2 in the list, how to enable it);
  - `docs/answering.md`, `docs/api.md`, `docs/limits.md` (A-law, robbed
    bits, retrain, table origins);
  - `DESIGN.md` 4.2b (x2 done; K56flex still not);
  - `docs/history/PROGRESS.md` (a section per task, with its figures);
  - `Makefile` (`VERSION := 0.4.0`);
  - `include/v90modem.h` (version 0.4.0).

- [ ] **Step 1: The full checks.**
  - `make check`, `make check-ubsan` and `make check-arm`.
  - The port matrix, one target at a time (`PAR=1 tools/check-ports.sh`,
    the six release targets).
  - The replay proof again: without `-x` identical to 0.3.0; with `-x` as
    in Task 4.
  - Expected: all pass.
- [ ] **Step 2: Write the docs and the CHANGELOG.** x2 is labelled "tried
  against the emulated U.S. Robotics Courier firmware and a simulated
  client; not yet against a real modem". The owner's real calls go
  through v90d.
- [ ] **Step 3: Commit, and merge into `main` locally** (fast-forward).
  Message: "libv90modem 0.4.0: x2 on the answering side".
- [ ] **Step 4: Hand over.**
  - To the owner: `ATI7` / `ATS32?` on the U.S. Robotics modems, the live
    calls with x2 offered from the Conexant and the 5686, and a real x2
    call with `ATS32=66`.
  - To the v90d agent: the setting, and the image.
  - The push and the tag wait for the owner's word, after the live calls.
