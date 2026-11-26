# HST on the answering side: a map of libv90modem 0.6.0

Read-only study of this tree (branch `hst`, `a44426a`, 0.6.0), made before any
HST code. It says what exists, where HST would plug in, and what adding a
modulation costs. File:line references are to this tree. Nothing here is a
fact about HST itself: values for HST must come from a facts document written
the way `docs/history/x2-facts.md` and `k56-facts.md` were (tagged sources,
nothing copied; `CONTRIBUTING.md` "Originality").

Sizes below were measured with a scratch program (x86-64, gcc), not read from
documents.

Contents: 1 start-up; 2 DSP; 3 error control and the pump; 4 public API and
what a new modulation touches; 5 test infrastructure; 6 open questions.

---

## 1. The answering side's start-up

### 1.1 The flow

`answer_tick` (`modem/answer.c:209`) counts `a->t`, calls the scheme of the
current phase (`a->s->tick`, set by `ans_enter`, `answer.c:44`) and applies the
backstop (`phase_budget`, `answer.c:92`: only P2, P3 and V.34's Phase 2 have
one). One sample in, one sample out; the output is linear unless the scheme
sets `pcm_out` (`ans_scheme.h:34`; only V.90, V.92, x2 and K56flex do). HST
would be a linear-out scheme like V.32bis.

Order of events for a caller (`answer_init`, `answer.c:125-162`;
`tick_v8`, `answer.c:180-205`):

1. `ANS_V8` (or `ANS_V8BIS` first with K56flex, `k56_offer`, `answer.c:113`):
   `v8ans` (`modem/v8ans.c:41-101`) sends 200 ms silence, then ANSam (2100 Hz
   with reversals, `ansam_init(..., 1)` at `v8ans.c:12`) and listens for CM on
   V.21(L).
2. **During ANSam**, one hook already exists for callers without V.8: if
   `V8_MOD_V32` is offered and `v8.state == V8A_ANSAM`, `v32_aadet_tick`
   (`answer.c:186`, detector `v32/v32_rx.c:488-515`, doc `v32/v32.h:334-351`)
   listens for 1800 Hz (at -43 dBm0, 12 dB over 2400 Hz, 100 ms) and goes to
   `ANS_V32_SIL` (75 ms silence, then `ans_start_v32`, `ans_legacy.c:214-216`).
   `answer_t.aadet` (`answer.h:174`) is that detector's state. It is fed only
   while ANSam runs, because our own V.21 JM has a tone at 1850 Hz.
3. A valid CM: JM, CJ, then `tick_v8` dispatches on `v8.selected`
   (`answer.c:198-203`): PCM -> `ans_start_p2`; V.34 -> `ans_start_v34`; 3
   (V.32 family) -> `ans_start_v32`; 4 -> `ans_start_v22`; 12 -> `ans_start_v21`;
   else `ANS_END_NO_COMMON`. x2 and K56flex are *not* separate V.8 selections;
   they ride on V.34 (x2: JM and INFO0a variants decided in `v8ans.c:30-37`,
   `p2ans`; marker in Phase 2 -> `ans_start_x2`, `ans_x2.c:11`).
4. No CM in 5 s (`V8A_ANSAM_MS`, `v8ans.h:20`; `v8ans.c:69-72` sets
   `V8A_RES_NO_CM`): `answer.c:191-196` goes to `ans_start_v22` if V.22 is
   offered, else `ans_start_v21`, else fails. This is the legacy path.

### 1.2 The legacy path (`modem/ans_legacy.c`)

One scheme, `ans_legacy` (`ans_legacy.c:232`), serves `ANS_V22`, `ANS_V21`,
`ANS_V32_SIL`, `ANS_V32` (dispatch `ans_legacy.c:209-219`).

- `ans_start_v22` (`:42`): V.22bis answerer (`v22m_init(V22_ANSWERER ...)`,
  600 baud, unscrambled ones at 2250 Hz) and, in parallel, three listeners
  behind the 128-tap band-pass `v21_bandpass` (`:93`): `a->bell` (1270 Hz,
  Bell 103 mark), `a->v21mark` (980 Hz, V.21 mark) (tone detector
  `modem/tonedet.c`), and the Bell 212A answer tone (2225 Hz, `fmod`).
  **`tick_v22` (`:138-195`) is the closest precedent for "detect a caller with
  no V.8 by what it sends and branch"**: the block at `:152-162` runs only
  while `!connected && !used_v8 && v22.state == V22S_WAIT`, tests
  `a->bell.blocks >= BELL103_MARK_BLOCKS` or
  `a->v21mark.blocks >= V21_MARK_BLOCKS` and jumps to `start_fsk(a, 1)` or
  `ans_start_v21`. Fallback to V.21 after `V22_LEGACY_MS` (8 s, `:11`, `:183`),
  give-up after `V22_TOTAL_MS` (30 s).
- `start_fsk` (`:55`) / `tick_v21` (`:104`): 300 baud V.21 / Bell 103 using
  `v21mod`/`v21demod` + `arx_t` (the characters are rewrapped as 10 start-stop
  bits for `putbit`, `:127-131`).
- `ans_start_v32` (`:73`): sets `v32_cfg` (rates mask from `max_down/max_up`,
  `echo_delay`) and `v32_init(&a->u.v32)`; `tick_v32` (`:197`) copies
  `connected`, `bitrate`, `rx_bitrate`, `retraining`, `retrains` into the
  answerer. `ans_legacy.retrain` is `v32_retrain` (`:221`).

### 1.3 Where an HST caller would be detected and branched to

Candidate points, in the order the call reaches them (design choice for the
HST design document; the facts, not the library, say which signal an HST
caller sends):

| # | Where | What exists | Fit |
|---|---|---|---|
| A | during `V8A_ANSAM`, `tick_v8` (`answer.c:186`) | AA detector for V.32, `aadet` | Same shape: a second detector on the received samples while the answer tone runs; a new member beside `aadet` (`answer.h:174`, outside the union: +size, see 4.5). Fastest branch: skips the 5 s wait. |
| B | `V8A_RES_NO_CM` (`answer.c:191`) | picks V.22 or V.21 | Insert "HST offered -> `ans_start_hst`" ahead of V.22; only after 5 s of ANSam. |
| C | inside `ANS_V22`'s wait (`ans_legacy.c:152-162`) | Bell 103 and V.21 mark detectors behind `v21_bandpass` | Precedent for detecting a signal while our V.22bis ones go out. The band-pass (`v21_bpf.h`, `bell103_bpf.h`) passes ~980 or 1270 Hz; a new band needs a new filter (`tools/gen_v21_bpf.py` generates them). |
| D | the V.8 CM path | V.8 menu octets (`modem/v8msg.c`) | Only if an HST caller speaks V.8 at all; unknown. |

What the answerer *sends* before a branch also matters: ANSam is 2100 Hz AM
with reversals (`ansam.h`); Bell answer tone 2225 Hz exists
(`BELL103_A_MARK`, `v21.h:17`). A different answer tone for HST would be a new
tone generator in `v8ans` (`v8ans_offer_x2`, `v8ans.c:23`, is the precedent
for a pre-start flag that changes what is sent).

A branch is made by (all in the pattern of `ans_start_x2`, `ans_x2.c:11-22`):
set up an engine in `a->u`, `ans_enter(a, ANS_HST); a->t = 0;`, return.

### 1.4 What a new phase and scheme require

- New value in `ans_phase` (`answer.h:60-80`), *before* `ANS_NPHASES`; a row in
  `phase_scheme[]` (`answer.c:31-40`; the compile-time check at `:42` catches a
  missing last row, `test/test_ans_table.c` the others); the trace name in
  `tools/trace.c:14` (size check `:17`) and `tools/v90m_fields.c:260`
  (`phn_check`).
- Scheme `ans_hst` of type `ans_scheme_t` (`ans_scheme.h:25-34`): `tick`
  (required), and optionally `cleardown`, `retrain`, `moving`, `snr_den`,
  `rtd`, `echo_delay`, `pcm_out`. Declared in `ans_scheme.h:35`, listed in the
  header comment `:1-14`, and in `mod_scheme()` (`answer.c:65-77`) keyed by a
  new `ANS_MOD_HST` (`answer.h:102-113`; 12 is next). `moving` defaults to
  `connected && !retraining` (`answer.c:272-276`): channel reversal that stops
  data must say so here (see 3.4).
- `ans_start_hst(answer_t *)` declared in `ans_scheme.h:67-73` list.
- Offer flag: `ANS_MODE_HST` next to `ANS_MODE_K56 0x200000u`
  (`answer.h:100`; `0x400000u` is the next free bit), carried in
  `answer_cfg.modes` (`unsigned`, 32 bits; V8_MOD_* take the low 16).
  `answer_init` has the pattern: `x2_offer` (`answer.c:104`), `k56_offer`
  (`:113`), set before the first sample and frozen by
  `before_first_tick` (`:167`) / `answer_v90_cfg` (`:174`).
- End codes: `ANS_END_*` (`answer.h:116-128`); `ans_fail` (`answer.c:46`),
  `ans_lost_or`, `ans_retrain_or` (`:47-48`), retrain bookkeeping
  `ans_rt_begin`/`ans_rt_data_tick` with an `rt_policy_t` (`ans_scheme.h:55-62`).
- `answer_t` outputs the engine must keep current: `connected`, `modulation`,
  `bitrate` (our transmit), `rx_bitrate` (receive; asymmetric links, set for
  V.90 upstream and x2: `answer.h:182-183`), `retraining`, `retrains`,
  `end`. The API layer reads exactly those (4.2).

---

## 2. Reusable DSP

All fixed point, sample driven at 8 kHz, no allocation, state in the caller's
struct.

### 2.1 V.32 / V.32bis (`v32/`): the forward channel

`v32/v32.h` is the whole interface; internals in `v32_priv.h`. 2400 baud, 1800
Hz carrier: the same numbers as the HST forward channel as described, so the
transmitter is directly reusable and the receiver is reusable but fixed at
1800 Hz.

| Piece | Where | Notes on reuse |
|---|---|---|
| Transmitter `v32tx_t` | `v32.h:80-106`, `v32/v32_tx.c` | `v32tx_init(t, answer, level, getbit, user)`, `v32tx_set_mode`, `v32tx_tick`, `v32tx_symbol_due`. Modes `V32_TX_*` (`v32.h:75`) include `V32_TX_DATA` (scramble, encode, point, pulse) and `ONES`. 2400 Bd at 10/3 samples per symbol: pulse phase accumulator `acc` (`v32_tx.c:92-101`); carrier is applied as rotation by quarter turns, `(-j)^k`, `:78-80`, which is exact only for 1800 Hz at 2400 Bd (3/4 cycle per symbol). Pulse tables `v32_txh`/`v32_ech` (`v32_tabs.c:49,62`) are 1800 Hz-specific (passband). Transmit-only use: yes, the transmitter is separate from the receiver and the echo canceller; `answer` only selects scrambler polynomial (`GPA`/`GPC`). The reference output `t->ref` (`:98`) is for the canceller and can be ignored. |
| Scrambler | `v32_scramble`/`v32_descramble` (`v32/v32_code.c:20-26`), on `v34_scr_t` (`v34/v34.h:47-95`: GPA `1+x^-5+x^-23`, GPC `1+x^-18+x^-23`) | Polynomials are the V.32 ones, selected by `answer`. A different HST scrambler is a third polynomial: `v34_scr_init(poly)` takes only `V34_GPC`/`V34_GPA` (`v34.h:42-45`), `modem/scramble.[ch]` is V.22bis `1+x^-14+x^-17`. A new polynomial means a new small scrambler. |
| Differential + trellis encoder | `v32_encode` (`v32_code.c:62-86`), `v32_trellis_next` (`:50`), tables `v32_c4800..v32_c14400` (`v32_tabs.c:6-47`, `int8_t [N][2]`) | 8-state code of V.32bis Figure 1, rates 4800 (uncoded, 4 pts), 7200, 9600, 12000, 14400 (trellis, 16..128 pts), `bits per symbol = 2 + rate` (`v32.h:19`). Not present: V.32 uncoded 16-point 9600; anything above 14400 (HST 16800 = 7 info bits/symbol needs a 256-point table; `v32_c14400` is the largest and `v32_vit_step` loops all points of the table, `v32_viterbi.c:16-30`, `npts = 1 << (3+rate)`); a different HST trellis. Tables are generated: `v32/tools/gen_tabs.py` (constellation read from the Recommendation's figures). |
| Viterbi + differential decoder | `v32_vit_t` (`v32.h:54-65`), `v32_vit_step`, `v32_decode_bits` (`v32_viterbi.c:16,62`) | Self-contained: input one equalised point per symbol in `V32_UNIT` (512)/unit, output the label decided `V32_VDEPTH` (24) symbols back. Usable behind any front end that produces symbol-rate points. `rate` index picks the constellation. |
| Receiver `v32rx_t` | `v32.h:134-217`, `v32/v32_rx.c` | Front end hard-wired to 1800 Hz: `r->dph = 1800/8000` (`v32_rx.c:67`), RRC filter bank `v32_rxg[48][33]` (`v32_tabs.c:77`) tuned to it, band-edge resonators at 600 and 3000 Hz (`v32_rx.c:31-34`, `timing()` `:268`). Per symbol: T/2 LMS equaliser (`V32_EQN` 32 taps, Q24), second-order carrier loop (`FREQ_MAX` +-10 Hz, `:29`), AGC from the first symbols of S (`:386-390`), data decisions into the Viterbi (`symbol()` `:177`, `data_bits` `:146`). Training detectors (AA/CC tone, S, S-bar, TRN, rate signals) are V.32 start-up signals. **A 2400-baud receiver at another carrier would need a new filter bank table and resonator constants**; a one-way V.32 *receiver* (without the canceller) is fine: `v32rx_tick(r, sample)` takes any sample, the canceller is only in `v32_tick` (`v32.c:341-352`: `e = v32ec_tick(...); v32rx_tick(&rx, e)`). The receive-side state machine (training reference sequences S/S-bar/TRN, `ref_state` `:157`) is V.32's: HST training would replace it. |
| Echo canceller `v32ec_t` | `v32.h:108-132`, `v32_ec.c` | Near 96 + far 96 taps (`V32_EC_NEAR/FAR`), 2 s transmit history (`V32_EC_HIST` 16384 samples), `v32ec_set_bulk`, `v32ec_set_near_delay`, `v32ec_tick(e, tx_ref, rx, mu_shift)`. 34352 bytes: the bulk of `v32_t` (36472). Independent of the modulation (it needs a reference sample per tick; V.32's is `v32tx_t.ref`). If HST's two channels are frequency separated, an echo canceller may not be needed at all (rejection by a band-pass instead); that is a facts question. The V.90-side canceller `modem/ec.[ch]` (96 taps, `ec_tick(e, tx, rx, mu)`) and `ecdly` (delay search) work on PCM codewords and are used by `p34d`, `x2d`, `k56d`, `v34dx`. |
| Whole modem `v32_t` | `v32.h:271-322`, `v32.c` | Answer start-up AC/CA/AC -> S, S-bar, TRN -> R1 .. R3, E, B1, data (`v32.c:120-339`); call mode `v32_call.c`. Not reusable for HST as a state machine (its start-up is V.32's), but it shows the pattern for a modem: `tx_control` once per transmit symbol (`v32.c:120`), `rx_control` per sample (`:229`), `v32_state` enum (`v32.h:220-253`), error codes `V32_ERR_*`, retrain/carrier-loss logic in `V32S_DATA` (`v32.c:276-305`). |
| AA detector | `v32_aadet_t` (`v32.h:342-350`, `v32_rx.c:480-515`) | 1800 Hz tone detector with 2400 Hz guard comparison; template for a tone detector that listens during ANSam. |

### 2.2 The V.34 family (`modem/`): QAM at 2400 baud with any carrier ratio

These are parameterised by symbol rate and carrier, where the V.32 receiver is
not, but they are built for V.34's differently shaped data path.

- `v34mod_t` (`modem/v34mod.h:20-47`, `v34mod.c`): QAM modulator,
  `v34mod_init(m, sym_rate, car_num, car_den, preemph, level, pwr_red)`;
  sym_rate 2400/3000/3200, carrier any `num/den` of it (so 3/4 of 2400 =
  1800 Hz), RRC 10 % excess, optional pre-emphasis; `v34mod_due/put/tick`
  (symbols in at `V34MOD_REF` = 4096 units). Used by the V.34 answerer and the
  upstream of V.90/x2. Transmit-only. The pulse is V.34's, not V.32's.
- `qamrx_t` (`modem/qamrx.h:30-65`, `qamrx.c`): receiver core: interpolation to
  T/3 (`v34_interp` bank, `modem/v34_tabs.h`), AGC to RMS 2048
  (`qamrx_sync`, `qamrx.c:62-`), 72-tap T/3 complex equaliser, carrier loop,
  timing loop. `qamrx_init(r, sym_rate, car_num, car_den)` takes any rate and
  carrier ratio (tested at 2400 in `test/test_v34mod.c:13,108`); it does **no
  decisions**: the caller supplies the wanted point per symbol
  (`qamrx_update(r, dr, di, flags, mu_shift)`; flags `QRX_EQ|CARRIER|TIMING|
  TFAST|DENSE`, `qamrx.h:20-26`). That is the natural socket for a trellis
  decoder of the V.32 family (`v32_vit_step` takes `zr/zi` and gives labels), and
  the only receiver here that is carrier-independent. It needs a start
  (`qamrx_sync` with a known training symbol delay) and training symbols for
  the equaliser; the V.34 sequences (`v34train`, `v34up`) provide those for
  V.34 and are not reusable for HST. Mind the clamp/blanking rules in
  `qamrx.h:46-65`, `QRX_BLANK` (hostile-audio hardening, audit 0.6).
- `v34up_t` (`modem/v34up.h`): the whole V.34 upstream receiver on `qamrx`
  (`ec`, `v34train`, V.34 coding in `v34/`), used by `p34d`, `v34dx`, `x2d`,
  `k56d`. V.34's trellis (16/32/64 states) and shell mapping: not V.32's.
- V.34 data coding `v34/` (`v34.h`): scrambler `v34_scr_t` (inline, shared),
  differential (`v34_tmod_t`, `v34_tdem_t`), constellation, trellis, shell
  mapping, non-linear encoder, precoder. None of it is V.32's code.

### 2.3 Low-speed modems (back channel candidates)

| Modem | Where | Properties | Reuse for a 300/450 bit/s back channel |
|---|---|---|---|
| V.21 / Bell 103 FSK | `modem/v21.[ch]` | `v21mod_init(m, f_mark, f_space, fs, baud, amp)` and `v21demod_init(d, f_mark, f_space, fs, baud)` take any frequencies and baud; non-coherent correlators, correlation window `V21_MAXWIN` 64 samples (`v21.h:48`), `d->energy` for carrier detect; `arx_t` (`modem/async.h`) slices characters at 8N1; used in `ans_legacy.c` and V.8. | Parameterised for FSK at any tones and baud the 64-sample window allows (>= 125 baud at 8 kHz); no DPSK. Fine only if the back channel were FSK. |
| V.22bis | `modem/v22tx.c`, `v22rx.c`, `v22bis.c`, header `v22bis.h` | 600 Bd QAM (4/16 pts), `v22tx_init(t, fc, ...)` any `fc` for the carrier phase accumulator but pulse and receive filter banks (`v22_rx_lo`, `v22_rx_hi`, `modem/v22_tabs.h`) exist for 1200 and 2400 only (`v22rx_init`, `v22rx.c:56-64`); `V22_EQN` 15 taps; scrambler `scramble.[ch]` | The closest thing to a DPSK/QAM low-rate receiver: carrier loop, timing, AGC (`gain` Q12, `agc_acc`), burst gate. Another baud or carrier needs new tables (`tools/gen_v22_tabs.py`). 600 baud is not 150/225/300. |
| DPSK for INFO frames | `modem/dpsk.[ch]` | `dpsktx_t`/`dpskrx_t`: 600 bit/s binary DPSK (phase reversal = 1) on a tone at `DPSK_FC_B` 1200 or `DPSK_FC_A` 2400 (`dpsk.h:20-21`, `dpsk.c:135`); the receive matched filter is `v22_rx_lo/hi[0]`; the frame layer is V.34 INFO (CRC, sync) | The tx model (level in {-1,0,1} smoothed by an RRC step response, any change at any sample, `dpsk.h:1-10`) is general: a differentially encoded PSK at some other symbol rate and a low carrier would need a different step response and filter. Not parameterised for rate. |
| Tone detection | `modem/tonedet.[ch]` (`tonedet_init(t, dph)`: any frequency, 10 ms blocks, 6 in a row, >50 % of block power in the 100 Hz bin); `anstone` (2100 Hz, ANS vs ANSam, caller side); `belltone` (2225 Hz) | Cheap detector per frequency; the precedent for "a tone of N ms in a known place" (`ans_legacy.c:46-47`). Block time and run counts are compile-time constants. |
| Tone generation | `modem/ansam.[ch]` (2100 Hz AM + reversals), `v21mod` (continuous phase FSK), `sintab` (1024-entry Q15 sine, 32-bit phase accumulators everywhere), `v8bis`/`v21mod` for V.8bis | Any tone is `phase += dph; sintab[phase >> 22]`. |

### 2.4 AGC, timing recovery, carrier loops: where each lives

There is no shared AGC or timing module; each receiver owns its own.

- V.32: AGC from the average power over symbols 16..48 of S (`v32_rx.c:386-390`, gain Q16); timing: band-edge resonators (`timing()` `:268`, `te`, `t_integ`, `tnext`); carrier: `theta/freq` second order (`:191-230`).
- V.22: AGC `gain` Q12 (`v22bis.h:72`), timing detector `on_r/mid_r`, carrier loop `theta/freq`, burst gate (`mse_slow`, `gated`).
- `qamrx`: AGC fixed at `qamrx_sync` from the power seen (`qamrx.c:62-`), timing from equaliser error (flag `QRX_TIMING`, `QRX_TFAST` acquisition), carrier from the decision point (`QRX_CARRIER`).
- `dpskrx`: timing hunted on a 600 Hz grid in `hunt[3]` (`dpsk.h:84-91`).

### 2.5 Sizes

Measured (x86-64): `v32_t` 36472 (canceller 34352, receiver 1616, Viterbi 588,
transmitter 112); `v22bis_t` 1144; `v22rx_t` 944; `dpsktx_t` 748; `dpskrx_t`
1104; `qamrx_t` 1112; `v34mod_t` 328; `x2d_t` 20360; `k56d` member 22848;
`v92d_t` 45096; `v8ans_t` 1520. A V.32-transmitter + V.32-receiver pair
**without** the canceller is under 3 kB; an FSK or DPSK back channel a few
kB. See 4.5 for what fits.

---

## 3. Error control and how a pump attaches

### 3.1 What `v42/` implements

- **LAPM**: `v42_lapm.[ch]` (1152 lines): window k, N401 to 256, XID, SABME,
  recovery with T401/T403/N400, break, FCS-16/32 (`v42.h:90-100`
  for the options; `v42_hdlc.h:32-35` FCS constants).
- **HDLC framing**: `v42_hdlc.[ch]`: *bit-level* flags, zero insertion, abort,
  FCS16/FCS32; octets LSB first (`v42_hdlc.h:1-8`). Bit pump only.
- **Detection phase**: `v42_detect.[ch]`: ODP (DC1 even/odd parity) / ADP
  ("EC", "E NUL"), start-stop characters (`v42async_rx_t/tx_t`,
  `v42_detect.h:12-34`); detector `v42det_rx_t` (`:68-80`).
- **V.42bis** (`v42_comp.[ch]`), **V.44** (`v44_comp.[ch]`): both inside LAPM
  negotiation (V.42 12.2 / V.44 Annex A, `v42_lapm.c:260,374`).
- **Whole endpoint** `v42ec_t` (`v42.h:130-167`, `v42.c`, 764 lines): states
  `V42EC_DETECT/ESTABLISH/CONNECTED/ASYNC/RELEASING/DISCONNECTED`
  (`v42.h:76-83`), `v42ec_tx_bit/rx_bit` (and octet forms), `v42ec_tick(ms)`,
  `v42ec_write/read`, `v42ec_line_stopped/restored`, release, abort, break.
  49784 bytes (`V42EC_TXBUF` 2048, `RXBUF` 4096, `v42.h:69-74`).
- **MNP: none.** No occurrence of "MNP" in the source tree or docs
  (`grep -rniE '\bmnp\b'` is empty). V.42's alternative MNP-interworking
  detection (the Annex the task calls "Annex A") is not implemented
  (`docs/error-control.md`, `v42/README.md:115` name only V.42 clause 12.2 and
  V.44 Annex A). In `v42det_rx_t` the answerer waits for the ODP; with
  neither ODP nor flags in `t400_ms` (750 ms) it goes to `V42EC_ASYNC`
  (`v42.h:24-31`) or disconnects (`fallback = 0`). An MNP originator therefore
  today ends in V42EC_ASYNC after 750 ms (or `V42EC_R_NO_EC`).
- Reusable for MNP 4/5: the start-stop character framer
  (`v42async_*`; MNP's link-request/ack exchange is in async chars before
  sync), the octet ring buffers (`v42ec_ring_t`, `v42.h:126`), the timer/tick
  convention, the `line_stopped/restored` handling, V.42bis (MNP 5 uses its
  own compression, which is a different algorithm: not here), CRC code only if
  the polynomial matches (MNP uses a byte-synchronous framing
  (`DLE`/`STX`/`ETX`, octet stuffing) and a different CRC-16 than V.42's FCS;
  confirm in the facts document). New would be: the MNP link state machine,
  framing, class 4 adaptive packet sizing, class 5 compression; probably a
  `v42/mnp.[ch]` with an interface mirroring `v42ec_t`, and then a decision
  whether the answerer runs v42ec detection and MNP side by side (whichever
  pattern arrives first, as ODP vs MNP LR).

### 3.2 The modem's data interface: a bit pump

`answer_cfg` (`answer.h:130-143`): `getbit`, `putbit`, `user`
(`v22_getbit_fn`/`v22_putbit_fn`). Contract (`include/v90modem.h:18-25`,
`v14.h`, `v42.h:11-17`): once connected, call `getbit(user)` for every
transmitted line bit at the moment it is needed and `putbit(user, bit)` for
every received bit at the moment it is decided, from inside the tick. Bits
only; no frames, no octets, no rate in the callback.

What a new pump must provide, learned from V.22bis (`v22bis.h:122-123`) and V.32:

- Call `getbit` only while the transmit side is ready (`tx_ready`); until the
  receive side is ready (`rx_ready`) deliver binary 1 to `putbit` ("circuit
  104 clamped", `v32rx_t.clamp`, `v32.c:334`) rather than nothing. For V.32 `connected`
  becomes 1 only when both are ready (`v32.c:335-339`).
- Set `connected`, `modulation`, `bitrate` (what we send), `rx_bitrate` (what
  we receive; the API reports `rx_bps = rx_bitrate > 0 ? rx_bitrate : bitrate`,
  `api/v90modem.c:250-254`) in the scheme's tick, as `tick_v32` does
  (`ans_legacy.c:197-207`).
- Keep `retraining` true between data stopping and data moving again with
  `connected` staying 1, so the API reports STOPPED/MOVING instead of an end
  (`api/v90modem.c:302-345`).
- Idle line: V.22 and V.32 data scramblers run on `getbit` bits, so the
  endpoint above sends mark (ones) when idle (`v42ec_tx_bit`).
- A V.14 start-stop modem (`modem/v14.c`, `v90m_async_*`, `api/v90modem.c:467-500`)
  is just another consumer of the same two callbacks. V.21 and Bell 103 do
  character conversion inside the modem (`ans_legacy.c:126-134`, see 1.2) and
  must therefore not be run with LAPM (no error control): HST's own
  asynchronous modes would be similar.

### 3.3 How the API layer attaches error control

It does not. `api/v90modem.c` knows nothing about V.42 inside the modem: the
host passes `cfg.getbit/putbit/user` pointing at a `v90m_ec_t` or
`v90m_async_t` it created (`v90m_ec_tx_bit`/`v90m_ec_rx_bit` or the
`v90m_async_*` pair: `api/v90modem.c:550-551, 487-488`), calls `v90m_ec_tick`
every ms, and on `V90M_EV_DATA_STOPPED`/`DATA_MOVING` calls
`v90m_ec_line_stopped/restored` (`include/v90modem.h:476-477`). `v90m_ec_t`
is a `v42ec_t` plus an owned flag (`api/v90modem.c:503`). The X-macro lists in
`api/v90modem_fields.h:17-60` cover **the EC configuration and status only**
(`V90M_EC_CFG_FIELDS`, `V90M_EC_STATUS_FIELDS`), not the modem's. Wiring for
test and bench: `rig/ec_side.h` (a `v42ec_t` on a simulated call's pump; used by
`rig/bench_v90 -e`).

So a new pump needs nothing in `api/` for error control. A new *endpoint* (MNP)
would be a new public object beside `v90m_ec_t` (`include/v90modem.h:422-470`
pattern: cfg default, create/init/reset/destroy, `tx_bit/rx_bit/tick`,
`write/read`, status), or a mode of `v90m_ec_t`, with its own memory pin
(`V90M_EC_MEM_SIZE` 50 KiB, `include/v90modem.h:251`;
`v90m_check_ec_mem`, `api/v90modem.c:585`).

### 3.4 Points that matter for HST

- **Channel reversal and `moving`.** The fast direction swaps with the traffic.
  The API reports changes of `modulation`, `tx_bps`, `rx_bps` as
  `V90M_EV_RATES` (`api/v90modem.c:317-321`) and counts a data stop that is not a
  retrain or the end as a *renegotiation* (`reneg_pending`,
  `:323-343`; docs: `v90m_stats.renegotiations`). If a reversal is a brief
  data stop, the EC endpoint sees `line_stopped/restored`; LAPM aborts the
  frame in flight at the stop and resumes with flags
  (`v42.h:188-199`). For MNP the host-visible behaviour (does data keep moving
  through a reversal?) is a design decision; if the pump keeps `moving` = 1
  and swaps `bitrate`/`rx_bitrate`, only `V90M_EV_RATES` fires, with
  `tx_bps` and `rx_bps` exchanging.
- The API assumes one `tx_bps` and one `rx_bps` per call; HST's slow channel
  rates (300/450) fit as `rx_bps`/`tx_bps`.
- `v42ec` is rate-agnostic (bit pump + 1 ms ticks); asymmetry costs it nothing.
  Throughput-based timers (T401 1000 ms default) are not tuned to 300 bit/s
  return channel ACKs: with N401 128 octets at 450 bit/s an acknowledgment of a
  frame takes seconds; `t401_ms`, `k`, `n401` are cfg fields (`v42.h:90-100`) and
  `V90M_EC_R_N400` ends a link that makes no progress (`include/v90modem.h:453`).
  Relevant for LAPM over HST (a caller that has no MNP).

---

## 4. The public API and what adding a modulation touches

### 4.1 Public surface (`include/v90modem.h`)

- Version `0.6.0` (`:55-57`); `check-version` ties it to the Makefile and
  README (`Makefile:187`).
- Modulation bits `V90M_MOD_*` (`:103-178`): V21 0x01, V22 0x02, V32BIS 0x04,
  V34 0x08, V90 0x10, V92 0x20, BELL103 0x40, BELL212A 0x80, X2 0x100, K56
  0x200. Next free: `0x0400u`. `v90m_cfg.modulations` is `unsigned`.
- Connection kinds `v90m_conn` (`:181-188`): appended values keep the old
  numbers; `V90M_CONN_K56FLEX` = 11; next 12. Must equal `ANS_MOD_*`: checked at
  compile time (`api/v90modem.c:20-24`, `SAME`).
- End codes `v90m_end` (`:~310-335`); `ans_end[]` (`api/v90modem.c:272-275`) maps
  `ANS_END_*` by index, so a new `ANS_END_HST_FAILED` is appended in both lists
  in the same order (`answer.h:116-128`).
- Events: CONNECTED, RATES, DATA_STOPPED, DATA_MOVING, ENDED (`:195-201`),
  derived in `after()` (`api/v90modem.c:302-345`) only from `connected`,
  `moving`, `retraining`, `modulation`, `tx/rx bitrate`, `end`; nothing
  modulation-specific, so a new modulation that fills those gets events free.
- State/stats (`:338-373`): `v90m_state` (connected, moving, retraining,
  modulation, tx/rx, used_v8, odp_bypass, end); `v90m_stats` (snr, rtd,
  echo_delay, retrains, renegotiations). Each comes from a scheme function
  (`snr_den`, `rtd`, `echo_delay`) via `mod_scheme` (`answer.c:248-266`), or -1.
- Config validity `check_cfg` (`api/v90modem.c:97-124`): rules for combinations
  (x2/K56 need V.34; `ENOTSUP` for the calling role). `modes_of`
  (`:126-141`) maps `V90M_MOD_*` to `V8_MOD_*` / `ANS_MODE_*`. `V90M_MOD_ALL`,
  `V90M_MOD_DEFAULT` (not x2, K56, Bell, V.92), `V90M_MOD_ORIG` (calling:
  all but x2, K56) at `:68-78`. HST would be answering only, outside the
  defaults, and is *not* in `V90M_MOD_ORIG` (otherwise a calling HST would
  be implied; the origin check `v90m_check_orig` `:80` lists the shared ones).
- `setup()` (`:143-189`): builds `answer_cfg` (`ac.modes`, getbit/putbit/user,
  `max_tx_code`, ...) and calls `answer_init`; for A-law copies `p2cfg` with
  `alaw = 1`.
- `v90m_cfg` (`:204-244`): `tx_level`, `max_down_bps` / `max_up_bps` (named from
  the server side: for HST "down" would be our transmit), `echo_delay`,
  `alaw`, `codec_linear_rx`; the doc comment on ceilings
  (`:222-231`) states per modulation how each is used. A new modulation adds a
  sentence there.

### 4.2 The memory pin and the union

- `struct v90m` (`api/v90modem.c:54-67`) = `v90m_cfg` + flags + `union {answer_t a;
  orig_t o;}`. Measured: `answer_t` 106208, `orig_t` 58488, `struct v90m` 106320
  on x86-64 (pinned `test/api/test_api.c:191-197`; 106016 on ARM), cap
  `V90M_MEM_SIZE` = 108 KiB = 110592 (`include/v90modem.h:250`; compile-time check
  `api/v90modem.c:584`). Headroom on x86-64: **4272 bytes**; documented in
  `README.md:237-241`, `docs/limits.md:369-379`, `docs/api.md:76`,
  `docs/getting-started.md:323-330`.
- The engine union inside the answerer (`answer.h:173`):
  `union { v32_t v32; v92d_t v92; x2d_t x2; v8bis_t v8b; k56d_t k56; } u;`
  = **45096 bytes** (set by `v92d_t`). `v32_t` is 36472, `x2d_t` 20360, `k56d` 22848.
  Comment at `answer.h:165-172` argues the members never meet in one call. **An
  HST engine of up to 45096 bytes fits in the union at no cost to the pin**,
  provided it is mutually exclusive with the others: an HST caller has no V.8
  (or a different V.8 menu), so it never is V.92, x2, K56 and not V.32 (the
  union would be reused by `ans_start_v32` only on another call after
  `v90m_reset`, which zeroes the memory, `api/v90modem.c:143-150`).
  Anything *outside* the union (a new detector like `aadet` in 1.3 A, flags
  like `x2_offered`) raises `answer_t` and thereby the pin (tens of bytes each:
  still room), and must be accounted: the pin, README, limits.md, `test_api.c`
  (and `was = 118824` / 118512 on ARM, lines 192-199), per CPU-architecture.
- `check-fixed` forbids soft float and libm in the library
  (`Makefile:222-233`, `tools/check-fixed.sh`): generated tables only.

### 4.3 What adding a modulation touches (checklist from x2 and K56flex)

Commits that added them: x2 `6265875` (answer.c, answer.h, api, header, p2ans,
v8ans, docs), K56flex `5ecc779` (+ `sim/k56call`, `tools/k56_crd_scan.c`,
`tools/v90m_replay.c`). For HST, by file:

| Area | Files | What |
|---|---|---|
| New engine | `modem/hst*.c/.h` (picked up by `$(wildcard modem/*.c)`, `Makefile:99`, and by `check-headers`, `:203`) and, in the **test archive**, `MODEM_SRC` (`Makefile:118-125`, an explicit list: a new `modem/*.c` must be added there or tests will not link) | engine, tables (generated by a `tools/gen_*.py`; `docs/limits.md` "Included tables" for provenance) |
| Dispatch | `modem/answer.h`, `answer.c`, `ans_scheme.h`, `ans_hst.c` (new) | 1.4 |
| API | `include/v90modem.h` (`V90M_MOD_HST`, `V90M_CONN_HST`, end code, doc comment, `V90M_MEM_SIZE` only if needed), `api/v90modem.c` (`SAME` list, `V90M_MOD_ALL`, defaults, `check_cfg`, `modes_of`, `end_of` table, mem pin) | 4.1 |
| Tools | `tools/trace.c:14`, `tools/v90m_fields.c:260`, `tools/v90m_replay.c` (flag, e.g. `-H`, `:241`), `rig/bench_v90.c:70-79` (modes, `a.phase ==` lists) or a new bench, `tools/rec_*.c` (recorder), `test/test_clock.c:173` (clock wrap test per engine) | |
| Tests | `test/test_*.c` registered in `Makefile:129` (`TESTS`) or a `SUBS` Makefile (`v32/Makefile:19` style), `test/api/test_api.c` (cfg rules 92-140, mem pin 183-199), `test/test_ans_table.c` is automatic, `test/golden/out/...` new files, `test/golden/bench.txt` + `tools/bench-hashes.sh:30-40` (a JOBS line) | 5 |
| Docs | `CHANGELOG.md` (an entry per release, "API change" listing every appended constant; precedent: 0.4.0 / 0.5.0), `README.md` (modulation table at `:111-120`, "Memory and CPU" `:235-282`, "Not implemented" `:160`), `docs/api.md:112,181,200`, `docs/answering.md` (a section per modulation: "x2", "K56flex"), `docs/limits.md` (per-modulation limits, included tables, memory, CPU `:415-462`), `docs/testing.md` ("K56flex's checks" `:224`), `docs/history/PROGRESS.md` and a facts file (`docs/history/hst-facts.md`), `docs/superpowers/specs|plans` (x2 and K56 each have a design and a plan), `DESIGN.md` (4.1, 4.2b) | |
| Version/ABI | `Makefile:VERSION`, `V90M_VERSION_*`, `README.md` "This is X", `docs/getting-started.md` `.so` names; `check-version` / `check-soname` (every 0.x changes the ABI) | only for a release |

Golden files: a new test program's own golden file is not a re-record; but
any change that alters other programs' output (e.g. the memory-size lines,
`answer_t` size in logs, which `tools/out-filter.sh` filters) must say so.

---

## 5. Test infrastructure for a new modulation

### 5.1 Simulated peers (`sim/`, double precision, "independent" per convention)

- Rule: a peer is written from the Recommendation independently of the
  library code, so it checks against the specification and not against
  itself (header comment `sim/v32call.h:1-8`). For HST the peer must come from
  the facts document.
- Existing peers to model on: `sim/v32call.[ch]` (718 lines: a V.32bis *calling*
  modem: its own transmitter from hand-transcribed tables `sim/v32spec.h`, own
  trellis, scramblers, receiver with echo canceller, Viterbi; `vc_cfg`,
  `v32call_init/tick/retrain`), `sim/v32line.[ch]` (2-wire line with mu-law
  codec on the answerer's side, echo/delay/noise per side; the line cfg in
  `test/test_answer_v32.c`), `sim/x2call.[ch]` (x2 client, 470 lines),
  `sim/k56call.[ch]`, `sim/v34peer`, `sim/callpeer.[ch]` (V.8 -> V.22bis/V.21
  caller with `callpeer_set_pump` to take over the bit stream: `callpeer.h:26-31`),
  `sim/v8call.[ch]`, `sim/qammod.[ch]` (double QAM modulator for V.34 rates,
  `qammod.h`), `sim/channel.[ch]` (PCM line, V.90), `sim/dschan`, `sim/g711`,
  `sim/noise`, `sim/filt`, `sim/resamp` (clock offset).
- A new `sim/hstcall.[ch]` (an HST caller, plus perhaps an HST answerer for
  the future caller role) would join `SIM_SRC` (`Makefile:115-117`, an explicit
  list) and be linked into every test through `libtest.a`.
- Independent peers for interop: spandsp only (V.8, V.22bis,
  `test/interop_*`, `make test-interop`, host only); a real-peer harness
  pattern for x2 / K56 sits outside the repo (`tools/x2-peer/`, `k56-peer/`,
  `make test-x2-courier`, `test-k56-sm56`, host only, need external
  checkouts). HST's equivalent would be an emulated modem or real recordings.

### 5.2 Test programs and registration

- Top-level programs `test/test_*.c`, built by the pattern
  `$(BUILD)/test_%` (`Makefile:276-277`), each linked with `libtest.a` (all
  of `SIM_SRC` + `MODEM_SRC`); listed in **`TESTS`** (`Makefile:129`); ARM build
  `arm_test_%`. Output `test: N checks, 0 failures` via `T_REPORT`
  (`test/test.h`); `TEST_SHORT` on 32-bit ARM / `-DTEST_FORCE_SHORT`;
  `TEST_FULL` for the port matrix (`test.h:10-17`).
- Subdirectory suites: `SUBS := v34 v90 v92 v42 v32` (`Makefile:140`), each with
  a `Makefile` (`v32/Makefile:19` `TESTS`, `build-tests`, `print-tests`,
  `cost-arm`, `check-fixed`). A new `hst/` directory would be a new SUBS member
  (and be added to `HDR_ALONE`, `Makefile:203`, `CORE_SRC` wildcards, `INC`
  `:97`, the README/DESIGN layer table) or HST lives in `modem/` and `test/`.
- Typical answerer test: `test/test_answer_v32.c` (an answerer with the
  simulated V.32bis caller on `v32line`, PRBS in both directions, checker
  `chk_t`, runs for V.8 and no-V.8 callers, rate ceiling, retrain, loss).
  Other templates: `test/test_answer_x2.c`, `test_answer_k56.c`,
  `test_answer.c` (V.22/V.21/Bell). API: `test/api/test_api.c`
  (`API_TESTS`, `Makefile:139`, built against the static library, only
  `v90m_*` visible).
- `SPLIT_TESTS` (`Makefile:137`) for slow programs (`--cases`, `prog N`).
- `test/test_ans_table.c` fails a phase without a scheme row;
  `test/test_clock.c` runs every engine across the sample-counter wrap.

### 5.3 Golden files and verification

- `test/golden/out/<dir>/<program>.txt` (dirs: `api examples test v32 v34
  v42 v90 v92`), 140 programs, filtered by `tools/out-filter.sh`; `make golden`
  (`Makefile:378-381`) runs `make check` and `tools/golden.sh check`; `make
  golden-record` re-records, only when a change explains it and the commit
  names each file and why (`docs/testing.md:44-70`, `CONTRIBUTING.md`).
  `tools/golden.sh` takes its program list from `make print-golden`
  (`Makefile:382`), which prints `TESTS`, the `SUBS`' `print-tests`, `API_TESTS`
  and `examples/call_loop`: a new program is in the list once registered, and
  `golden: N programs identical` then wants its new golden file recorded
  (not a "re-record").
- Bench hashes: `test/golden/bench.txt` (tx/rx FNV hashes of a recorded
  simulated call per modulation: v90, x2, k56, v34, v90a, v90c, v92c, bell103,
  bell212) with a `JOBS` table in `tools/bench-hashes.sh:30-40`; each job =
  recorder `tools/rec_*.c` + replayer `rig/bench_*.c`, host and under qemu (`make
  bench-x2` `Makefile:520`, `bench-bell` `:780`). A change that should not
  alter what the modem sends must leave the existing hashes unchanged
  (`CONTRIBUTING.md`).
- `make verify` (`tools/verify.sh`, per commit, 6-7 min): host check + golden +
  split-check, replay proof, bench hashes, UBSan, armv7hf and aarch64be short
  suites, check-fixed + check-symbols. `make verify-phase`: release proof (hours).
  `make check-arm`, `make check-ubsan` (`Makefile:404-419`); port matrix
  `tools/check-ports.sh`: every target must print the same output (x86, ARM,
  big-endian, s390x, riscv, Windows, gcc+clang).
- Required hygiene for every new source: C99, no signed overflow or shifts of
  negatives (UBSan), tables generated into headers, `check-fixed`
  (no soft float / libm), `check-headers` (every header compiles alone with
  `-I. -Iinclude`; includes relative to the file, e.g. `../v32/v32.h`),
  hidden visibility and `v90m__i_*` renaming (`check-symbols`).

### 5.4 The replay tool (`tools/v90m_replay.c`)

Plays a recording (per 10 ms: 80 codewords heard then 80 sent; or
`V90DLIN1` linear receive; format in `tools/README.md`) into the answerer
configured as the recording modem was, and reports phase by phase; options
`-a -9 -x -k -B -M -F -u -d -l -r -e -P -s -b`. The mode flags are mapped
at `tools/v90m_replay.c:241` (`opt_x2 ? V90M_MOD_X2 ...`): `-H` for HST would
be one line there, plus documentation in its header comment, `docs/testing.md`
(table at "Recorded calls and the replay tool"), and `tools/README.md`. The
report's phase name comes from `tools/trace.c:14`. It is built from sources with
`-DP34D_TRACE`. Fixtures (`test/fixtures/`, `check.sh`, `fixtures.txt`): real
recordings with the options they need, one test per recording; CONTRIBUTING:
a quirk found on a real modem comes with its recording. The 106 private
recordings go through `replay-proof.sh` in six flag sets (new flag sets
would be a new manifest: `test/golden/replay.sha256`). Privacy: recordings of
the data phase must not be kept (memory note).

### 5.5 CPU cost targets

- The box: ARM926EJ-S 400 MHz, about **122 M counted instructions per second**
  (`README.md:246-250`). Figures (answering, data mode, M instr/s): V.90 31.19,
  V.34 33.42, x2 (32000) 56.58, K56 32.12, V.42 endpoint 5.21 (none) / 9.08
  (V.42bis) / 9.48 (V.44) at 56 kbit/s, Bell 103 calling 9.88; one call must fit
  and (where claimed) two (HT802 runs one modem per port; `DESIGN.md:12-14`).
- V.32bis for reference (`v32/README.md:484-505`, from `make cost-arm` in
  `v32/`): data at 14400 3855 instructions/sample = 30.8 M/s (19.9 % by that README's older box estimate, about 25 % of the
  122 M figure above); at 4800 20.7 M/s; TRN1 (canceller training)
  46.5 M/s. The canceller's share is significant: dropping it, if HST permits,
  is the cheap thing.
- How: `make cost-x2`, `cost-k56`, `cost-v34`, `cost-v90a`, `cost-v90c`,
  `cost-v92c`, `cost-bell`, `cost-v42` (`Makefile:528,582,601,640,707,734,786,818`):
  a recorder (`tools/rec_*`) records a simulated call on the host, the bench
  (`rig/bench_*`) replays into the ARM build under qemu with
  `-one-insn-per-tb -d exec,nochain`, instructions counted between two sample
  numbers (`bench_v90 -e` adds V.42). `qprof.c` gives per-function counts
  (`make prof-v90c`). The host replay and the ARM replay must print the same
  tx hash (that is the portability check). For HST: a `rec_hst`/`bench_hst`
  pair, a `JOBS` line, a `cost-hst` target, rows in README "Memory and CPU"
  and `docs/limits.md` "CPU".

### 5.6 Conventions (DESIGN.md, CONTRIBUTING.md)

- Every fix comes with a test seen failing first (`CONTRIBUTING.md:3-6`);
  real-modem quirks come with a recording fixture.
- Originality: written from the ITU recommendations; no code from other
  modems; spandsp only as a test peer. For HST: facts-only digest (the x2 and
  K56 precedent, `docs/history/x2-facts.md` "Tags and sources": documented /
  RE / our disassembly / derived; the AGPL D-Modem code only for values).
- Portable C99, fixed point in the signal path, no allocation after create,
  no global or static mutable state (two modems in one process independent),
  nothing outside `include/v90modem.h` is API (`DESIGN.md:9-24`).
- `DESIGN.md:658-680`: Q15 sine table (1024 entries), 32-bit phase
  accumulators, equaliser input RMS 2048, taps Q20/Q24, tables generated by
  `tools/gen_*.py`, clocks through `modem/clock.h`, builtins only through
  `modem/compiler.h`, 0 dBm0 = RMS 15770.
- A change is ready when `make check` passes, plus `make check-arm` for signal
  path code; same transmit hash from `make bench-v90` and no golden file
  changed for refactors; `make verify` runs everything.

---

## 6. Open questions and risks for the HST design

1. **Detection signal and answer tone.** Nothing in the tree says what an HST
   caller sends or hears. The legacy path (1.3 A / C) can detect a tone but
   the answer tone offered first is ANSam (2100 Hz AM); whether an HST
   caller accepts it, or needs another tone (a new generator in `v8ans`), is a
   facts question and decides between hook A and hook B/C.
2. **Receiver at another carrier or rate.** The V.32 receiver is fixed at
   1800 Hz / 2400 Bd. If HST's forward channel is on 1800 Hz at 2400 Bd,
   `v32tx_t` + `v32rx_t` + `v32_vit_t` + `v32_encode` are reused as they
   are, with HST's own training and rate signalling around them; if its
   shaping or scrambler differs (a third scrambler polynomial, a different
   trellis, rates above 14400), the table and code additions are in
   `v32/` (`gen_tabs.py`, `v32_code.c`, `v32_viterbi.c`'s all-points loop).
   For the back channel there is no DPSK/FSK-at-300 to 450 bit/s receiver
   except V.21-style FSK (any frequencies/baud) and the V.22 QAM (600 Bd,
   fixed banks): expect a new small modem (carrier at a few hundred Hz also
   needs a different band-pass than `v21_bpf.h`) and a duplex front end
   (separating the forward carrier from the return channel without
   `v32ec_t`, or with it).
3. **Duplex and echo.** `v32ec_t` is 34 kB and ~9 M instr/s of the
   30 M/s of V.32bis data: reuse only if the facts require it.
4. **Channel reversal** is a first-class event in neither `answer_t` (one
   `bitrate`, one `rx_bitrate`) nor the API (`V90M_EV_RATES`,
   `renegotiations`): decide whether it keeps `moving` = 1 (the HST pump
   stays up, only the rates exchange) and whether it counts as a
   renegotiation; this also decides what LAPM/MNP sees (`line_stopped`).
5. **MNP.** New code is unavoidable (3.1); the question is its place (a
   `v42ec_t` mode, or a new endpoint object and public API beside
   `v90m_ec_*`), the memory pin (`V90M_EC_MEM_SIZE`), and the detection
   between V.42 ODP and MNP at the same endpoint (T400 750 ms).
6. **Memory.** An HST engine up to 45 kB fits in `answer_t.u` with no change to
   `v90m_mem_size` = 106320 (4272 bytes of headroom to `V90M_MEM_SIZE`).
   Anything added outside the union moves the pin (`test_api.c:192-199`,
   README, limits.md) and must be held to the ARM figure (106016).
7. **Registration traps.** `MODEM_SRC`, `SIM_SRC`, `TESTS` (and `HDR_ALONE`'s
   `find` list if a new directory) are explicit lists in the Makefile: a new
   file compiles into the library through wildcards but is silently missing
   from the test archive until added. `ans_end[]` and the `SAME` checks are
   positional; `phase_scheme[]`, `tools/trace.c` and `tools/v90m_fields.c`
   have compile-time length checks that name the omission.
8. **Calling side.** `V90M_MOD_ORIG` excludes x2 and K56 (answering only);
   the same for HST, and `check_cfg`'s `ENOTSUP` rule (`api/v90modem.c:105-117`)
   needs HST in `~V90M_MOD_ORIG`. No change to `orig*.c` is needed for an
   answering-only HST (the orig union is 58488 bytes).
