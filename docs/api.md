# API reference

The authority is [`include/v90modem.h`](../include/v90modem.h): every
field and function is commented there with the roles it applies to and
its error returns. This page groups the same content by topic and adds
nothing the header does not say, except where it names another source.
The API is not stable before 1.0; any 0.x version may change it
(`CHANGELOG.md`).

Rules the library keeps: C99, fixed-point arithmetic in the signal path,
no memory allocation after `v90m_create()` / `v90m_ec_create()`, no global
state (any number of modems in one process, one thread per object at a
time). Callbacks (`getbit`, `putbit`, `on_event`) are only ever made from
inside `v90m_process_*()` on the calling thread.

What a callback may do to the modem that calls it: `v90m_retrain`,
`v90m_cleardown`, `v90m_get_state`, `v90m_get_stats`, `v90m_get_diag`,
`v90m_get_v34_cap`, `v90m_get_restarts`, `v90m_get_k56`, `v90m_get_qc` (and anything on another object: every `v90m_ec_*` and `v90m_async_*` function).
`v90m_reset` returns `V90M_ESTATE` and changes nothing. `v90m_destroy`
takes effect at the end of the sample under way: no event after it, the
rest of the block is silence (linear 0, mu-law 0xFF, A-law 0xD5), the
modem is freed (when it came from `v90m_create`) and `v90m_process_*`
returns `V90M_OK`. After a `v90m_destroy` from `getbit` or `putbit` the
rest of that sample still calls `getbit` and `putbit` with the host's
`user` pointer: free the user object (a `v90m_ec_t`, a `v90m_async_t`)
only after `v90m_process_*` returns, and never from a callback of a modem
that goes on. `v90m_init` on the modem's own memory is not allowed.

NULL: every function checks the object it is given (and the struct that
`v90m_get_state`, `v90m_get_stats`, `v90m_get_diag`, `v90m_get_v34_cap`,
`v90m_get_restarts`, `v90m_get_k56`, `v90m_get_qc` and `v90m_ec_get_status` fill): with
NULL a function returning `int` returns `V90M_EINVAL`, one returning
`size_t` or `unsigned long` 0, a `void` one does nothing. Except the four
called for every bit, `v90m_ec_tx_bit`, `v90m_ec_rx_bit`,
`v90m_async_getbit` and `v90m_async_putbit`: their object must not be
NULL. `V90M_EINVAL` is -1, which is true in C: the functions read as yes
or no, `v90m_ec_tx_idle`, `v90m_ec_tx_busy` and `v90m_ec_get_break`, say
yes for NULL, and a loop `while (v90m_ec_get_break(e, &o, &l))` with a
NULL `e` never ends and leaves `o` and `l` unset. Compare with 1, or check
the object first.

## Version

| | |
|---|---|
| `V90M_VERSION_MAJOR`, `V90M_VERSION_MINOR`, `V90M_VERSION_PATCH` | the header's version: 0, 7, 3 |
| `const char *v90m_version(void)` | "0.7.3": the version of the library linked in (may differ from the header) |

## Error codes

Negative returns.

| | value | meaning |
|---|---|---|
| `V90M_OK` | 0 | |
| `V90M_EINVAL` | -1 | a NULL or misaligned argument, or a configuration value out of range |
| `V90M_ENOTSUP` | -2 | a combination not implemented in this version: V.92 without V.90; x2 or K56flex without V.34, or calling; answering Bell 212A without V.22; calling, no modulation the calling role has |
| `V90M_ENOMEM` | -3 | create could not allocate, or init was given too little memory |
| `V90M_ESTATE` | -4 | not possible in the present state of the call |

## Lifecycle

| | |
|---|---|
| `void v90m_cfg_default(v90m_cfg *cfg)` | answering, the default modulations, `tx_level` 3000, `echo_delay` -1, the rest 0 or NULL |
| `v90m_t *v90m_create(const v90m_cfg *cfg, int *err)` | one modem; the only allocation the library makes for it. NULL on failure with `*err` (`err` may be NULL): `V90M_EINVAL` (cfg NULL or a value out of range), `V90M_ENOTSUP`, `V90M_ENOMEM`; `V90M_OK` otherwise |
| `size_t v90m_mem_size(void)` | octets `v90m_init` needs |
| `v90m_t *v90m_init(void *mem, size_t len, const v90m_cfg *cfg, int *err)` | the modem in memory of the caller's, at least `v90m_mem_size()` octets, aligned for any type; nothing is allocated, and `v90m_destroy()` then does nothing. Errors as `v90m_create`, and `V90M_EINVAL` for mem NULL or misaligned, `V90M_ENOMEM` for len too small |
| `int v90m_reset(v90m_t *m, const v90m_cfg *cfg)` | back to the start of a new call (the line went off-hook), same memory, the role and everything else from cfg. `V90M_OK`; or `V90M_EINVAL` (m or cfg NULL, a value out of range), `V90M_ENOTSUP`, or `V90M_ESTATE` (from a callback of m's), the modem left as it was |
| `void v90m_destroy(v90m_t *m)` | NULL is fine; from a callback it takes effect at the end of the sample under way (above) |

Memory bounds for this version, on every platform (they change with the
version; the init functions check the length given):

| | |
|---|---|
| `V90M_MEM_SIZE` | 108 KiB, for `v90m_init` |
| `V90M_EC_MEM_SIZE` | 50 KiB, for `v90m_ec_init` |
| `V90M_ASYNC_MEM_SIZE` | 2048, for `v90m_async_init` |
| `v90m_align` | a union for the alignment: `union { v90m_align a; uint8_t b[V90M_MEM_SIZE]; } mem;` |

## Configuration: `v90m_cfg`

| field | |
|---|---|
| `role` | `V90M_ROLE_ANSWER` (0, the called modem) or `V90M_ROLE_ORIGINATE` (1, the calling modem) |
| `modulations` | `V90M_MOD_*`; default all but V.92, Bell, x2 and K56flex. Either role: at least one (0 gives `V90M_EINVAL`), and V.92 only with V.90 (else `V90M_ENOTSUP`); x2 and K56flex answering only and with V.34 (else `V90M_ENOTSUP`) |
| `alaw` | the codec's law: 0 mu-law, 1 A-law |
| `codec_linear_rx` | answering: 1 = the codec receives in 16-bit linear, with no G.711 quantiser on the receive path (V.92 then designs the upstream constellation without its share of the noise); 0 = the received samples went through G.711, also when handed over as linear values. Calling: unused |
| `tx_level` | transmit RMS outside PCM modes, 16-bit units, 0 .. `V90M_TX_LEVEL_MAX` (16384; else `V90M_EINVAL`); default 3000. Near the top the peaks of V.34-family signals clip: a calling V.90 modem's V.34 upstream at 15360 or 16384 goes over full scale some tens of samples a call, and whether its B1 then gets through depends on the upstream rate the answerer picks. Back to back at 16384 (mu-law) the answerer picked 26400 and B1 failed up to 0.5.0; since 0.6.0's start-up it picks 24000 and the call connects. That is luck of the measurement, not headroom: 15360 is the level tested to connect |
| `max_tx_code` | answering, V.90 / V.92: the most downstream power the caller may ask for, -0.5 dBm0 less 0.5 dB per step, 1..31; 0 = 23 (-12 dBm0). Calling: unused (0..31 accepted) |
| `max_down_bps`, `max_up_bps` | rate ceilings, bit/s, >= 0; 0 = none. `max_down` caps what we send, `max_up` what we receive (named from the answering, server side), in V.34, V.90 and V.92, either role: V.92's PCM upstream (up to 48000) under `max_up` answering, `max_down` calling; 33600 holds it at 33333. V.32bis (either role): one rate both ways, under the lower of the two. V.21, V.22, V.22bis, Bell: unused. With x2 or K56flex, `max_up` 1..2399 is `V90M_EINVAL` (their V.34 upstream starts at 2400). HST (answering): `max_down` 1..4799 (under its lowest rate) and HST is not offered, the modem answers as without `V90M_MOD_HST`; from 4800 the call still opens at 9600, and from the retrain after MNP's LRs our forward rate is the best agreed one under `max_down`; the far end's forward channel (after a reversal) is held under `max_up`, at least 4800. A plain MNP agreement (the caller's LR with no HST rate in common) has no retrain: it stays at 9600 for the call |
| `echo_delay` | samples from a sample leaving to its echo starting back (the driver's buffering), >= 0; -1 = measure it in every start-up. Calling V.90 / V.92: never measured, used as given, 0 .. 511 (-1 taken as 0, more as 511); see [line-interface.md](line-interface.md) |
| `getbit` | `int (*)(void *user)`: the next bit to send, 0 or 1 |
| `putbit` | `void (*)(void *user, int bit)`: a bit received |
| `on_event` | `void (*)(void *user, v90m_event ev)`; may be NULL: poll `v90m_get_state()` instead |
| `send_ci` | calling: send CI before the answer tone (V.8 7.1) |
| `user` | passed to `getbit`, `putbit` and `on_event` |

### Modulations: `V90M_MOD_*`

| | value | |
|---|---|---|
| `V90M_MOD_V21` | 0x0001 | |
| `V90M_MOD_V22` | 0x0002 | V.22 and V.22bis |
| `V90M_MOD_V32BIS` | 0x0004 | V.32bis and V.32 (both roles) |
| `V90M_MOD_V34` | 0x0008 | both roles |
| `V90M_MOD_V90` | 0x0010 | answering: the digital (server) side; calling: the analogue (client) side |
| `V90M_MOD_V92` | 0x0020 | with `V90M_MOD_V90` (alone: `V90M_ENOTSUP`): PCM upstream on offer (answering), or asked for (calling); not in the defaults |
| `V90M_MOD_BELL103` | 0x0040 | not in the defaults, either role |
| `V90M_MOD_BELL212A` | 0x0080 | not in the defaults, either role; answering only with `V90M_MOD_V22` (without: `V90M_ENOTSUP`) |
| `V90M_MOD_X2` | 0x0100 | answering only (calling: `V90M_ENOTSUP`), with `V90M_MOD_V34` (without: `V90M_ENOTSUP`), not in the defaults: x2 as its server, offered on a mu-law line to a caller whose CM offers V.34 and no PCM; see [answering.md](answering.md), "x2" |
| `V90M_MOD_K56` | 0x0200 | answering only (calling: `V90M_ENOTSUP`), with `V90M_MOD_V34` (without: `V90M_ENOTSUP`), not in the defaults; experimental: K56flex as its server, offered on a mu-law line in V.8bis before V.8 (every caller then hears our answer tone later: 1.790 s without V.8bis, 3.807 s for a V.8bis caller without K56flex, 3.413 s for a K56flex client, as measured with 0.7.1's CL; 0.7.2's adds 3 to 4 ms to the last two, and a client declined for version 1 hears it 3.420 s later; from 0.7.4 a V.8bis caller whose MS has no K56flex block, leaves the modulation to V.8 and asks for ACK(1) gets it and hears it about 3.46 s later); see [answering.md](answering.md), "K56flex" |
| `V90M_MOD_HST` | 0x0400 | answering only (calling: `V90M_ENOTSUP`), needs no other bit, not in the defaults: HST, U.S. Robotics' asymmetric modem (a fast forward channel and a 450 or 300 bit/s back channel, MNP error control), as its answering side. Offered only when an MNP endpoint is linked with `v90m_ec_link_hst` before the modem decides (the end of ANSam), and `max_down_bps` is 0 or 4800 and up. A caller that sends neither CM nor AA is listened to for its back channel for 650 ms after ANSam (up to 2 s while 250-500 Hz energy is there): heard, HST (`V90M_CONN_HST`); not heard, V.22bis or V.21 as without HST, that much later. Callers that send CM or AA are answered exactly as without HST. No MNP link within 15 s: `V90M_END_HST_FAILED`. See [error-control.md](error-control.md) for the host's side |
| `V90M_OPT_V34_TX_CAP` | 0x10000 | not a modulation (alone: `V90M_EINVAL`; bits 16 and up are options); off by default. Answering, V.34: cap our V.34 transmit at the caller's own INFO1c / INFO1a projection (our MP offers the lower of `max_down_bps` and the projection); for a host to set per call, see [answering.md](answering.md), "The V.34 transmit cap"; calling: ignored |
| `V90M_OPT_K56_V2_ONLY` | 0x20000 | an option (added in 0.7.2), off by default. Answering, with `V90M_MOD_K56`: our V.8bis CL offers K56flex version 2 alone (version octet 0x42) instead of versions 1 and 2 (0x43, the default from 0.7.2 on), and every K56flex selection is acknowledged: 0.7.1's exchange, for a replay of a call 0.7.1 answered or a client that dislikes 0x43; `V90M_OPT_K56_V1` then changes nothing; see [answering.md](answering.md), "K56flex versions"; calling: ignored |
| `V90M_OPT_NO_QC` | 0x100000 | an option (added in 0.7.4), off by default. Answering, with `V90M_MOD_V92`: a caller's V.92 Quick Connect request (QC1a) is not taken, and the call starts with V.8 as in 0.7.3. Without it Quick Connect is part of V.92: a QC1a heard during our answer tone is answered with short Phase 1 (QCA1d, QTS, ANSpcm), and a caller that does not finish it within 2 s gets the answer tone again and V.8. A safety valve per port, and what a replay of a call answered by 0.7.3 or earlier needs (`v90m-replay -Q`). 0x80000 is kept free; calling: ignored |
| `V90M_OPT_K56_V1` | 0x40000 | an option (added in 0.7.2), off by default, **experimental**. Answering, with `V90M_MOD_K56`: a client that selects K56flex version 1 is acknowledged and our (version 2) start-up runs; it may fail in training, past the point where V.34 could still be had (`V90M_END_K56_FAILED`). Without it such a client is declined (NAK(3)) and goes on to V.8 as without K56flex. For a test number; see [answering.md](answering.md), "K56flex versions"; calling: ignored |

What each does in each role: [answering.md](answering.md),
[calling.md](calling.md), and the header's comments above `v90m_role` and
`V90M_MOD_BELL103`.

## Sample I/O

```c
int v90m_process_g711(v90m_t *m, const uint8_t *rx, uint8_t *tx, size_t n);
int v90m_process_lin_rx(v90m_t *m, const int16_t *rx, uint8_t *tx, size_t n);
int v90m_process_linear(v90m_t *m, const int16_t *rx, int16_t *tx, size_t n);
```

`n` received samples in, `n` samples to send out. Any `n`, 1 included: the
result does not depend on how the samples are cut into blocks. Use one of
the three for a call: `_g711` codewords both ways (the interface for a
codec, and the one V.90 / V.92 need); `_lin_rx` a receive side in 16-bit
linear (Table 1 scale: what `v90m_g711_decode` gives), codewords out;
`_linear` linear both ways (a PCM modulation then sends the linear value of
its codewords). Return `V90M_OK`, or `V90M_EINVAL` (m NULL, or rx or tx
NULL with n > 0). A `v90m_destroy` from a callback ends the block early
(above). See [line-interface.md](line-interface.md).

### G.711

| | |
|---|---|
| `uint8_t v90m_g711_encode(int alaw, int16_t linear)` | the nearest codeword |
| `int16_t v90m_g711_decode(int alaw, uint8_t codeword)` | its linear value |

On the scale of V.90 Table 1 (16-bit, the scale the modem works in);
`alaw` 0 = mu-law, 1 = A-law.

## Events and state

`v90m_event`, reported to `on_event` from inside `v90m_process_*()` right
after the sample at which the change happened, before the next one is
processed:

| | |
|---|---|
| `V90M_EV_CONNECTED` (1) | data mode reached: `v90m_state` has the modulation and rates |
| `V90M_EV_RATES` | the rates or the modulation changed (renegotiation, retrain) |
| `V90M_EV_DATA_STOPPED` | data stopped moving (retrain, rate renegotiation) |
| `V90M_EV_DATA_MOVING` | data moves again both ways (error control: say so to V.42) |
| `V90M_EV_ENDED` | the modem stopped: see `v90m_state.end` |

`void v90m_get_state(const v90m_t *m, v90m_state *st)`: cheap, fine to read
after every sample, and after the end (`end` says why).

| `v90m_state` | |
|---|---|
| `connected` | data mode reached, and kept through retrains |
| `moving` | user data moves both ways now |
| `retraining` | a retrain is under way (connected, no data moves); 0 in a rate renegotiation |
| `modulation` | `v90m_conn` in force (during a retrain: the one before) |
| `tx_bps`, `rx_bps` | our transmit / receive rate (rx = tx where symmetric) |
| `used_v8` | the far modem spoke V.8 |
| `odp_bypass` | answering, V.92 9.3.1: the caller skips V.42's ODP / ADP (start the V.42 endpoint without detection); calling: 0 |
| `end` | `v90m_end` |

### `v90m_conn` (the modulation of a connection)

`V90M_CONN_NONE` 0, `V90M_CONN_V21` 1, `V90M_CONN_V22` 2,
`V90M_CONN_V22BIS` 3, `V90M_CONN_V90` 4, `V90M_CONN_V34` 5,
`V90M_CONN_V32BIS` 6, `V90M_CONN_V92` 7, `V90M_CONN_BELL103` 8,
`V90M_CONN_BELL212A` 9 (V.22 at 1200 bit/s begun on the 2225 Hz Bell answer
tone), `V90M_CONN_X2` 10 (x2: PCM downstream, `tx_bps` 32000 to 56000;
V.34 upstream), `V90M_CONN_K56FLEX` 11 (K56flex: PCM downstream, `tx_bps`
32000 to 56000 in steps of 2000, the figure a Rockwell modem prints; V.34
upstream in `rx_bps`), `V90M_CONN_HST` 12 (HST: `tx_bps` the channel we
send on, `rx_bps` the one we receive; they swap at a channel reversal;
`tx_bps` 9600 at `CONNECTED`, the opening rate, then the best agreed rate
after the retrain that follows MNP's LRs). The last five were appended;
the values above them stay as they were.

### `v90m_end`

| | |
|---|---|
| `V90M_END_NONE` (0) | |
| `V90M_END_FAILED` | no connection was made (start-up failed) |
| `V90M_END_LOST` | the connection was lost (any loss once connected but those below) |
| `V90M_END_CLEARDOWN_FAR` | the far modem cleared down |
| `V90M_END_CLEARDOWN_OURS` | after `v90m_cleardown()` |
| `V90M_END_NO_ANSWER_TONE` | calling only: no answer tone came |
| `V90M_END_NO_JM` | calling only: ANSam came, but no JM in answer to CM (V.8) |
| `V90M_END_NO_COMMON` | V.8 found no modulation in common with the far modem |
| `V90M_END_RETRAIN_FAILED` | V.90 / V.92, V.34 (either role) (appended): a retrain failed (in its Phase 2, 3 or 4), or one more was due past the limit (see `v90m_retrain`); answering x2: the client asked for a retrain in data mode or its rate change (its Tone A), which x2 cannot serve; answering HST, holding the back channel at the lowest agreed rate: 3 failed retrains within 10 s, or the 4th loss of data mode within 30 s |
| `V90M_END_X2_FAILED` | answering (appended): the client took up x2 (its marker came) and the x2 start-up, or a rate change of the client's, did not complete |
| `V90M_END_K56_FAILED` | answering (appended): K56flex was agreed and went past the point of no return (our INFO1a flagged), and the start-up, a rate renegotiation or a retrain of the client's did not complete, or the client retrained a third time in a row |
| `V90M_END_HST_FAILED` | answering (appended): an HST call had no MNP link within its limit, 15 s after HST began (a host that did not clear down when the endpoint gave up, `V90M_EC_R_NO_EC`) |

On an HST call in data mode no call stays up without the far modem's
signal for more than 12 s (`V90M_END_LOST`); the far modem's MNP LD ends
it with `V90M_END_CLEARDOWN_FAR` once its carrier goes (2 s at most).

## Stats

`void v90m_get_stats(const v90m_t *m, v90m_stats *st)`:

| `v90m_stats` | |
|---|---|
| `modulation`, `tx_bps`, `rx_bps` | as in the state |
| `snr_db10` | receiver signal to error, 0.1 dB, 0..600; -1 = none. While connected, the engine's figure, in a retrain too; where it has none, the last measured while data moved. After the end of the call, that last value (-1 only if none was measured this call), until `v90m_reset` |
| `rtd` | round-trip delay measured in the handshake, samples; -1 = none (answering: V.34, V.90, V.92, x2, K56flex; calling: V.32bis, V.34, V.90, V.92); live or kept as `snr_db10` is |
| `echo_delay` | answering V.90 / V.92 / x2 / K56flex: in use (configured or measured) by the engine running now, during a retrain or after the end the call's; samples; -1 = none, and always -1 calling |
| `retrains` | begun, this call |
| `renegotiations` | rate renegotiations that stopped the data, this call: counted when data moves again, when it becomes a retrain, or when the call ends in it; a cleardown's stop, ours or the far end's, is not one (answering: V.34, V.90, V.92, x2, K56flex; calling: V.32bis, V.34, V.90, V.92) |
| `reversals` | (appended in 0.7.0) HST: channel reversals, this call; 0 otherwise |

`v90m_stats` grew in 0.7.0 (`reversals`, appended): rebuild against the
new header. The V.34 transmit cap's status is `v90m_get_v34_cap` (below).

## Start-up diagnostics

Added in 0.6.1. `void v90m_get_diag(const v90m_t *m, v90m_diag *d)`: how
far the call got, what it chose and why it ended, either role, in one
place, for a log line such as `start-up failed (P2D_ERR_INFO1A, phase2,
V.92)`. Cheap, fine after every sample, and kept after the end until
`v90m_reset`. A struct and a function of their own: `v90m_state` and
`v90m_stats` keep their size within 0.6 (a program built against 0.6.0
runs on 0.6.1's shared library), and so does `v90m_diag` from 0.6.1 on.

**The start-up failed if, and only if, the call ended and data mode was
never reached**: `v90m_state.end != V90M_END_NONE && !d.data_reached`. A
retrain that fails after data mode (`V90M_END_RETRAIN_FAILED` in the
retrain's Phase 2, 3 or 4) is not a start-up failure: `data_reached` is 1
(and `phase`, the furthest reached, `data`). So in C:

```c
v90m_state st; v90m_diag d;
v90m_get_state(m, &st); v90m_get_diag(m, &d);
if (st.end != V90M_END_NONE && !d.data_reached)
    log("start-up failed (%s, %s, %s)", v90m_err_name(d.error),
        v90m_phase_name(d.phase), v90m_conn_name(d.selected));
```

| `v90m_diag` | |
|---|---|
| `error` | why the call ended, as the modem's engines say it: `v90m_err_name(error)`. 0 while the call goes on, and after an orderly cleardown (ours or the far end's, `V90M_END_CLEARDOWN_OURS` / `_FAR`) once data mode was reached. A start-up that failed: the deepest cause with a name (an engine's own, such as `P2D_ERR_INFO1A`, or the modem's own limit, such as `ANS_ERR_TIMEOUT`). After data mode: how the call was lost (`P34_ERR_CARRIER`, `ANS_ERR_RETRAINS`, ...) |
| `phase` | `v90m_phase`: the furthest phase the call reached, in the order of the enum; it never goes back (a retrain's Phase 2 leaves it as it was) and is kept after the end. V.92's and V.34's failed trainings are retrains: a V.92 call whose training failed in Phase 3 and which died in the retrain's Phase 2 reads `phase3`, its `error` the final cause (`P2D_ERR_REV`, say) |
| `selected` | `v90m_conn`: the modulation chosen, from V.8's JM or, for a modem without V.8, once its signal is heard, whether or not its training finishes; `V90M_CONN_NONE` before. It then follows the call to the modulation in force: V.92 once Phase 2 agrees PCM upstream, V.34 once a V.90 caller's INFO1a selects it, x2 or K56flex at their point of no return, the connection's once connected. V.8's V.22 item (V.22 or V.22bis): `V90M_CONN_V22BIS` until the connection says which |
| `data_reached` | 1 once data mode was reached this call (user data moved both ways: `v90m_state.moving`), kept after the end |

`v90m_phase` (its `v90m_phase_name` in brackets):

| | |
|---|---|
| `V90M_PHASE_NONE` (0, `none`) | before the first sample |
| `V90M_PHASE_TONE` (`tone`) | answering: the silence and our answer tone (with K56flex, V.8bis before them); calling: listening for the answer tone |
| `V90M_PHASE_V8` (`v8`) | V.8: CM, JM, CJ |
| `V90M_PHASE_PHASE2` (`phase2`) | V.34, V.90, V.92, x2, K56flex: Phase 2, a retrain's too |
| `V90M_PHASE_PHASE3` (`phase3`) | ... Phase 3 |
| `V90M_PHASE_PHASE4` (`phase4`) | ... Phase 4 |
| `V90M_PHASE_TRAIN` (`train`) | V.21, V.22, V.22bis, V.32bis, Bell, which have no phases: their start-up; also a caller without V.8 being waited for, and calling, the wait for a pre-V.8 or Bell answerer's signal |
| `V90M_PHASE_DATA` (`data`) | data mode, rate renegotiations included (and V.22bis's and V.32bis's retrains) |

`const char *v90m_phase_name(v90m_phase p)` and `const char
*v90m_conn_name(v90m_conn c)`: short names for a log line, never NULL,
`"?"` outside the enum, the same within 0.7.x. Modulations: `none`,
`V.21`, `V.22`, `V.22bis`, `V.90`, `V.34`, `V.32bis`, `V.92`, `Bell 103`,
`Bell 212A`, `x2`, `K56flex`, `HST`.

`const char *v90m_err_name(int code)`: the name of an `error`, `"none"`
for 0, `"?"` for a number that is no error; never NULL. The names are
diagnostic text, not an enum: the numbers behind them may change in any
version, the names do not within 0.7.x. Most are the engines' own (the
header named in brackets, for the detail); `make check` fails when an
engine's header has an error without its name here
(`tools/check-diag-names.sh`):

| prefix | where | names |
|---|---|---|
| `V8_ERR_` | V.8, either role | `NO_TONE` (calling: no answer tone), `NO_JM` (calling: ANSam, no JM in answer to CM), `NO_COMMON` (nothing in common), `NO_CM` (answering: a caller without V.8, and neither V.22 nor V.21 offered) |
| `V21_ERR_` | V.21, Bell 103 | `NO_CARRIER` (the far carrier never held), `CARRIER` (lost in data mode) |
| `V22_ERR_` | V.22bis, V.22, Bell 212A | `NO_ANSWER` (the start-up did not complete in time), `HANDSHAKE` (it failed), `RETRAIN` (a retrain in data mode never completed), `CARRIER` (lost in data mode) |
| `V32_ERR_` | V.32bis, either role (v32/v32.h) | `NO_TONE`, `TRAIN`, `NO_RATE`, `CARRIER` |
| `P2D_ERR_` | answering V.90 / V.92 Phase 2 (modem/p2d.h) | `INFO0`, `REV`, `TONE_A`, `INFO1A`, `PCM_UP`, `TIME` |
| `P34_ERR_` | answering V.90 Phases 3, 4, data (modem/p34d.h) | `NO_S`, `JA`, `UINFO`, `NO_S2`, `DIL_END`, `CPT`, `CP`, `RATE`, `B1`, `CLEARDOWN`, `CARRIER`, `HANGUP` |
| `V92D_ERR_` | answering V.92 with PCM upstream (v92/v92_p34d.h) | `NO_RU`, `JA`, `UINFO`, `NO_SU`, `CPT`, `DESIGN`, `CPU`, `B1`, `CLEARDOWN`, `HANGUP`, `CARRIER` |
| `P2N_ERR_` | answering V.34 Phase 2 (modem/p2ans.h) | `INFO0`, `REV`, `INFO1C`, `NO_RATE`, `TIME` |
| `DX_ERR_` | V.34 Phases 3, 4, data, either role (modem/v34dx.h) | `CFG`, `NO_S`, `RATE`, `B1`, `CLEARDOWN`, `CARRIER`, `HANGUP` |
| `V34A_ERR_` | answering V.34 (modem/v34ans.h) | `PHASE2`, `TRAINING` (only where Phase 2's or the training's own cause is missing), `RETRAINS`, `CLEARDOWN`, `CARRIER`, `HANGUP` |
| `X2D_ERR_` | answering x2 (modem/x2d.h) | `CFG`, `NO_J`, `NO_S4`, `NO_MP`, `RATE`, `PAD`, `UP`, `NO_E`, `B1`, `CARRIER`, `RENEG`, `HANGUP`, `RETRAIN` |
| `K56D_ERR_` | answering K56flex (modem/k56d.h) | `CFG`, `NO_S`, `NO_J`, `J`, `NO_MP`, `RATE`, `NO_E`, `B1`, `CARRIER`, `RENEG`, `CLEARDOWN`, `HANGUP` |
| `P2L_ERR_` | calling V.34 / V.90 / V.92 Phase 2 (modem/p2call.h) | `INFO0`, `REV`, `INFO1A`, `NO_RATE` |
| `V34O_ERR_` | calling V.34 (modem/v34orig.h) | `PHASE2`, `TRAINING` (as V.34 answering's), `RETRAINS`, `CLEARDOWN`, `CARRIER`, `HANGUP` |
| `V90A_ERR_` | calling V.90 / V.92 (modem/v90a.h) | `PHASE2` (as above), `SD`, `TRAIN`, `JD`, `JDP`, `DIL`, `RI`, `PHASE4`, `B1D`, `RATE`, `CLEARDOWN`, `HANGUP`, `CARRIER`, `JP`, `CPD`, `RETRAINS` |
| `ANS_ERR_` | the answering modem's own | `TIMEOUT` (a phase ran past its limit: the backstop of Phases 2, 3 and 4, a K56flex retrain's time), `RETRAINS` (one more retrain was due past the limit; HST: the retrains at the lowest agreed rate failed), `START` (the next engine could not start with what was agreed), `K56_RETRAIN` (the K56flex client's retrain did not take K56flex up again), `HST_LINK` (HST: no MNP link within 15 s, `V90M_END_HST_FAILED`), `HST_CARRIER` (HST: the far modem's carrier went, the watchdogs' `V90M_END_LOST`) |
| `ORIG_ERR_` | the calling modem's own | `TIMEOUT` (the start-up ran past the calling modem's limit for its phase: no modulation after ANS, no Bell answerer, V.32bis, V.34, V.90) |

## The V.34 transmit cap

Added in 0.6.2. `void v90m_get_v34_cap(const v90m_t *m, v90m_v34_cap *c)`:
answering, V.34 (and V.34 selected by a V.90 caller's INFO1a), what the
caller said of our transmit direction and whether `V90M_OPT_V34_TX_CAP`
acted on it, for the host's count in [answering.md](answering.md), "The
V.34 transmit cap". From the last Phase 2 and Phase 4 of the call, kept
after the end until `v90m_reset`; cheap, fine after every sample. All
three are 0 when calling, on any other call (x2 and K56flex included) and
before V.34 training began. A struct and a function of their own, as
`v90m_get_diag`: `v90m_stats` keeps its size within 0.6, and so does
`v90m_v34_cap` from 0.6.2 on.

| `v90m_v34_cap` | |
|---|---|
| `proj_bps` | the caller's own projection for our transmit direction at the symbol rate in use, bit/s (2400 x INFO1c bits 30:33, or INFO1a bits 30:33 of Table 11/V.90 when a V.90 caller selected V.34), from the last Phase 2; 0 = none |
| `ask_bps` | the maximum the caller's MP asks of our transmit direction, bit/s, from the last Phase 4; 0 until its MP came; after a cleardown (ours or the caller's, whose MPs carry no rate), and in a renegotiation until the new MP comes, the one data mode last began with |
| `capped` | 1 when `V90M_OPT_V34_TX_CAP` lowered our MP's offer below what it would have been without it (`proj_bps` below our ceiling); 0 otherwise, and always without the option |

A caller that "asked above its own projection" is `c.ask_bps > c.proj_bps
&& c.proj_bps > 0`.

## Who began a renegotiation or retrain

Added in 0.7.1. `void v90m_get_restarts(const v90m_t *m, v90m_restarts
*r)`: of `v90m_stats.renegotiations` and `retrains`, those the far modem
began and ours, with the cause of the last one and where it began; either
role, every modulation that has them. Cheap, fine after every sample, kept
after the end until `v90m_reset`. A struct and a function of their own, as
`v90m_get_v34_cap`: `v90m_stats` keeps its size within 0.7, and so does
`v90m_restarts` from 0.7.1 on. For a host that logs, say, "renegotiation
(far) at 22.4 s": a WebTV box's own modem renegotiating every 22 s reads
`reneg_far` rising, with the rates unchanged.

| `v90m_restarts` | |
|---|---|
| `reneg_ours`, `reneg_far` | rate renegotiations, counted as `v90m_stats.renegotiations` is (they add up to it): the far end's when its signal stopped the data (its S, its Rd or Ru; x2's and K56flex's clients and a V.32bis answerer, which alone begin them, always), else ours |
| `retrain_ours`, `retrain_far` | retrains begun (they add up to `v90m_stats.retrains`; calling, after a V.22bis or V.32bis call has ended, where that reads 0, these still read the call's): the far end's when its signal began it (Tone A or Tone B, V.32bis's 1800 Hz or AC, V.22bis's S1, K56flex's tone B; HST: its 0x2R or 0x3R carried out with a training, our grant of its request or the training it holds forward for ours), else ours |
| `retrain_host` | of `retrain_ours`, those `v90m_retrain` asked for |
| `last` | `v90m_cause` of the one begun last (a renegotiation once it counts); `V90M_CAUSE_NONE` before the first |
| `last_retrain` | 1: that one is a retrain; 0: a renegotiation |
| `last_ms` | where it began (a renegotiation: where the data stopped), ms from the first sample since `v90m_create`, `v90m_init` or `v90m_reset` (it wraps after 2^32 samples, 6.2 days); 0 with `V90M_CAUSE_NONE` |

`v90m_cause` (its `v90m_cause_name` in brackets):

| | |
|---|---|
| `V90M_CAUSE_NONE` (0, `none`) | none yet |
| `V90M_CAUSE_FAR` (`far`) | the far modem began it |
| `V90M_CAUSE_OURS` (`ours`) | ours: a step of a start-up, retrain or renegotiation failed or ran out of time (Phase 2's own retrains, a time limit of Phases 3 and 4 or of a renegotiation, a failed V.92 start-up, a calling V.90's failed Phase 3, 4 or renegotiation), HST's training after MNP's agreement and its upshifts, the calling V.90 / V.92 receiver's watch taking the rate back up once the line is good again, or an engine that says no more |
| `V90M_CAUSE_RX_ERRORS` (`rx_errors`) | ours: our receiver's errors too many for the rate. V.34 and answering V.90: the rate drop (a renegotiation, or a retrain with no lower rate; the only renegotiation of ours these begin); V.92's upstream drop; the calling V.90 / V.92 receiver's watch (a renegotiation down, or a retrain); V.32bis's; HST's frame errors (our 0x2R one rate lower) |
| `V90M_CAUSE_RX_LOST` (`rx_lost`) | ours: our receiver lost the far end's data or signal (V.34's and answering V.90's dead receiver; HST holding back: the receiver out of data, or the forward carrier gone) |
| `V90M_CAUSE_HOST` (`host`) | ours: `v90m_retrain` |

V.22bis's retrains are counted calling only (answering, `v90m_stats.retrains`
has none either). The far end's and the host's counts stop at 65535 (where
the far end's would, the ours, the totals less them, read too high). An
accepted `v90m_retrain` makes the next retrain counted the host's when it is
ours; any retrain counted after it settles the request (a far end's that came
first took its place). V.22bis (calling) counts a retrain once it is done;
`last_ms` is still where it began.

`const char *v90m_cause_name(v90m_cause c)`: never NULL, `"?"` outside the
enum, the same within 0.7.x.


## K56flex's version exchange

Added in 0.7.2. `void v90m_get_k56(const v90m_t *m, v90m_k56 *k)`:
answering with `V90M_MOD_K56`, what our V.8bis CL offered and what the
client's MS chose (see [answering.md](answering.md), "K56flex versions").
Settled when V.8bis ends (2 to 4 s into the call), kept after the end until
`v90m_reset`; cheap, fine after every sample. All 0 when calling, without
`V90M_MOD_K56`, on an A-law line (no V.8bis) and before V.8bis ended. A
struct and a function of their own, as `v90m_get_restarts`: `v90m_stats`
keeps its size within 0.7, and so does `v90m_k56` from 0.7.2 on.

| `v90m_k56` | |
|---|---|
| `offered` | the versions our CL offered, a mask (bit 0 version 1, bit 1 version 2): 3 by default, 2 with `V90M_OPT_K56_V2_ONLY`; 0 when no CL went out (the caller sent no CRd) |
| `exchanged` | 1: the client's MS selected K56flex (it carried the K56flex block) |
| `chosen` | the version that MS selected: the highest version bit of its version octet (the octet's low five bits; 0x20 and 0x40 are not versions), 1 or 2 (3 to 5 for bits not yet defined); 0 with no such MS, or no version bit in it |
| `declined` | 1: we declined it (version 1 without `V90M_OPT_K56_V1`: V.8bis NAK(3)); V.8 followed as without K56flex |
| `agreed` | 1: we acknowledged it (ACK(1)); K56flex's Phase 2 follows, and the call is `V90M_CONN_K56FLEX` once the client's INFO1c takes it up |

A WebTV Plus of early flash (version 1 alone) is expected to read `offered
3, exchanged 1, chosen 1, declined 1, agreed 0` and connect in V.34 (from
the disassembly of its controller, V1.101, and the simulator; not yet seen
on a live call); a version-2 client
`3, 1, 2, 0, 1`; before 0.7.2 (or with `V90M_OPT_K56_V2_ONLY`) the WebTV
read `2, 0, 0, 0, 0`: it dropped our CL without a word.

## Quick Connect

Added in 0.7.4. `void v90m_get_qc(const v90m_t *m, v90m_qc *q)`:
answering, what V.92's Quick Connect did in the call (see
[answering.md](answering.md), "V.92 Quick Connect"). Settled field by
field as the call goes, kept after the end until `v90m_reset`; cheap, fine
after every sample, also from a callback. All 0 when calling and, but for
`v8bis_ack` (V.8bis comes first), before a QC1a has been heard. A struct
and a function of their own, as `v90m_get_k56`: `v90m_stats` keeps its
size within 0.7, and so does `v90m_qc` (ten ints). Quick Connect is part
of `V90M_MOD_V92`; `V90M_OPT_NO_QC` turns it off. The QC1a reported is the
first heard in the call, the one acted on: a QC1a sent again after a
fallback is not taken.

| `v90m_qc` | |
|---|---|
| `heard` | 1: a QC1a (two equal copies) during our answer tone, whether or not we acted on it (with `V90M_OPT_NO_QC` or after K56flex was agreed it is heard and ignored) |
| `uqts` | its UQTS (WXYZ) as the Ucode QTS uses, 61 to 87; -1 for WXYZ 1111 (a call coming off modem-on-hold) or an octet that is not an analogue modem's QC1a: ignored, and V.8 goes on; 0 when not heard |
| `lapm` | its P bit (LAPM asked for); 0 when not heard |
| `accepted` | 1: we answered it with QCA1d, QTS and ANSpcm |
| `level_db10` | the ANSpcm level our QCA1d named (LM), in 0.1 dBm0: -95, -120, -150 or -180; 0 when not accepted |
| `toneq_ms` | milliseconds from the end of our QCA1d to the start of the caller's TONEq (a run that began within the 2 s, so 1 to 2000; it may be taken up to about 2.1 s); 0 for none |
| `fallback` | 1: no TONEq within 2 s: our answer tone again and V.8 (9.2.4.3) |
| `bypass` | `v90m_state.odp_bypass` once a QC1a is heard: V.42's ODP / ADP skipped. For an accepted call without a fallback that is both P bits set (9.2.5); otherwise V.8's rule (V.92 at both ends, LAPM in CM and JM, 9.3.1), so after a fallback it can be 1. 0 when no QC1a was heard, even where `odp_bypass` is 1 (a V.92 call that sent none) |
| `short_p2` | 0: short Phase 2 not run; 1: completed (9.4.1); 2: begun, then the full Phase 2 took over |
| `v8bis_ack` | 1: with `V90M_MOD_K56`, the client's V.8bis MS left the modulation to V.8 (no K56flex block) and got our ACK(1) (V.8bis 9.7, 9.9.1), with or without a QC1a after it. `v90m_k56.exchanged` still means the MS carried the K56flex block |

`odp_bypass` under Quick Connect: set when both P bits are (the caller's in
its QC1a, ours mirroring it in QCA1d), before Phase 2 is over; the V.42
endpoint should then start without ODP / ADP detection
([error-control.md](error-control.md)). Without a QC1a it is 0.7.3's rule.

A typical Quick Connect call reads `heard 1, uqts 70, lapm 1, accepted 1,
level_db10 -120, toneq_ms` between 1 and 2000, `fallback 0, bypass 1, short_p2
1`; one whose caller never sent TONEq, `heard 1, accepted 1, fallback 1,
toneq_ms 0, short_p2 0`.

## Control calls

| | |
|---|---|
| `int v90m_retrain(v90m_t *m)` | ask for a retrain. Answering: in data mode of V.92, V.90, V.34, V.32bis, HST (holding the fast channel our short training; holding the back channel our MNP attention 0x2R at the rate in force, the far modem's training answering it; not during a reversal, a rate change or retrain) (x2, K56flex: `V90M_ESTATE`; a K56flex client's own retrain is followed, see [answering.md](answering.md)). Calling: once connected in V.22bis at 2400 bit/s, V.32bis, V.34, V.90, V.92. `V90M_OK`, or `V90M_ESTATE` (none of those, or one under way). Calling V.90 / V.92: 70 ms of silence and Tone A, Phase 2 to data mode again (some 7 to 11 s); also during a rate renegotiation; `V90M_ESTATE` when one is under way or three have run without a minute of data mode between them. A retrain that fails, or one more due past that limit (the far end's, or one the receiver needs), ends the call with `V90M_END_RETRAIN_FAILED` (the receiver's after a cleardown, V.90 9.7 / V.92 9.11) |
| `int v90m_cleardown(v90m_t *m)` | hang up, telling the far modem where the modulation has a way to (V.90 9.7, V.92 9.11, V.34 11.7, either role): `V90M_OK` when that has begun (`V90M_EV_ENDED` follows within a fraction of a second; V.92 answering: the cleardown is a rate sequence (CPd) with data rate 0, drn = 0, and a caller that does not acknowledge it is given up to 3.5 s); `V90M_ESTATE` when there is nothing to send (V.21, V.22, V.22bis, V.32bis, Bell, or not in data mode): just stop calling `v90m_process_*`. HST (answering; the host has released its endpoint, `v90m_ec_release`, and waited for `v90m_ec_tx_busy` 0 first): the host's bits 30 ms more holding the fast channel, 200 ms holding the back one, so that MNP's LD reaches the far modem, then both carriers stop and the call ends (`V90M_END_CLEARDOWN_OURS`); `V90M_OK`. x2 and K56flex (answering, in data mode or a rate change): `V90M_OK`, and the call ends at once with nothing sent |

## Start-stop characters (V.14 basic)

Characters (1 start, 8 data LSB first, 1 stop, mark idle) on the bit pump
of a connected modem, without error control: pass `v90m_async_getbit` and
`v90m_async_putbit` as the modem's `getbit` / `putbit` with the object as
`user`. Fixed 512-octet queues each way; a character received with the
receive queue full is dropped and not counted.

| | |
|---|---|
| `v90m_async_t *v90m_async_create(int *err)` | NULL on failure, `*err` as for `v90m_create` |
| `size_t v90m_async_mem_size(void)` | |
| `v90m_async_t *v90m_async_init(void *mem, size_t len, int *err)` | `*err` as for `v90m_init` |
| `void v90m_async_reset(v90m_async_t *a)` | |
| `void v90m_async_destroy(v90m_async_t *a)` | NULL is fine |
| `int v90m_async_getbit(void *a)`, `void v90m_async_putbit(void *a, int bit)` | the bit pump |
| `size_t v90m_async_write(v90m_async_t *a, const uint8_t *buf, size_t n)` | octets accepted |
| `size_t v90m_async_read(v90m_async_t *a, uint8_t *buf, size_t max)` | octets delivered |
| `size_t v90m_async_tx_pending(const v90m_async_t *a)` | queued or being sent |
| `unsigned long v90m_async_frame_errors(const v90m_async_t *a)` | |

## V.42 error control (LAPM, MNP)

One endpoint, on the bit pump of a connected modem: feed it the modem's
bits (`v90m_ec_tx_bit` from getbit, `v90m_ec_rx_bit` from putbit), tick it
every millisecond, and move user octets with write / read. With V.42bis or
V.44 compression when both ends agree; falls back to start-stop (no error
control) when the far end has no V.42 and fallback is on. Answering, with
`mnp.enable` 1 (added in 0.7.0), a caller's MNP LR (start-stop or HDLC)
leads to MNP (V.42 Annex A); an ODP still leads to LAPM. How to use it: [error-control.md](error-control.md).

### `v90m_ec_cfg`

| field | |
|---|---|
| `originator` | 1 = the calling end, 0 = the answering end |
| `detect` | run the detection phase (V.42 7.2.1), 0..1 |
| `fallback` | when detection fails: 1 = start-stop, 0 = disconnect |
| `t400_ms` | detection phase timer, >= 0; 0 = the default, 750 |
| `establish_ms` | answerer: give up when no SABME comes this long after detection, >= 0; 0 = never |
| `comp_policy` | `V90M_COMP_AUTO` (0) / `V90M_COMP_ALWAYS` (1) / `V90M_COMP_NEVER` (2) for our encoder |
| `lapm.use_xid` | originator: negotiate with XID before SABME, 0..1 |
| `lapm.k_tx`, `lapm.k_rx` | window, 1..32 (`k_tx`), 1..127 (`k_rx`) |
| `lapm.n401_tx`, `lapm.n401_rx` | information field, octets, 1..256 |
| `lapm.fcs32` | 0..1 |
| `lapm.v42bis_tx`, `lapm.v42bis_rx` | V.42bis wanted, our send / receive direction, 0..1 |
| `lapm.v42bis_p1`, `lapm.v42bis_p2` | codewords 512..2048, string length 6..250 |
| `lapm.v44_tx`, `lapm.v44_rx` | V.44 wanted (preferred over V.42bis), 0..1 |
| `lapm.v44_p1_tx`, `lapm.v44_p1_rx` | V.44 codewords, 256..2048 |
| `lapm.v44_p2_tx`, `lapm.v44_p2_rx` | V.44 string length, 32..255 |
| `lapm.v44_p3_tx`, `lapm.v44_p3_rx` | V.44 history, octets, 512..4096 |
| `lapm.t401_ms`, `lapm.t403_ms`, `lapm.n400` | `t401_ms` >= 1; `t403_ms` >= 0, 0 = off; `n400` >= 1 |
| `mnp.enable` | (appended in 0.7.0, as the whole `mnp` struct) answering: MNP on, 0..1; default 0, MNP off: the endpoint is 0.6's. An originator's endpoint has MNP off |
| `mnp.k` | window offered, LTs, 1..8; default 8 |
| `mnp.n401` | information field offered, octets, 1..256; default 64. 256 needs `mnp.opt` bit 0 (value 1) set, else `V90M_EINVAL` |
| `mnp.opt` | LR parameter 8 offered (its bitmap), 0..3; default 2 (fixed-field LT and LA, no 256-octet LTs). Bit 0 (value 1) agreed by both: N401 256 both ways |
| `mnp.framing` | the highest framing mode offered or accepted, 2 (start-stop) .. 3 (HDLC); default 3 |
| `mnp.t401_min_ms` | the floor of the T401 computed from the rates (`v90m_ec_set_rates`), 500..60000; default 1500 |
| `mnp.n400` | retransmissions, 1..255; default 12 |

A value outside these ranges, or `mnp.n401` 256 without `mnp.opt` bit 0,
is `V90M_EINVAL` from create, init and reset. `v90m_ec_cfg` grew in 0.7.0
(`mnp`, appended): rebuild against the new header.

### Functions

| | |
|---|---|
| `void v90m_ec_cfg_default(v90m_ec_cfg *cfg, int originator)` | |
| `v90m_ec_t *v90m_ec_create(const v90m_ec_cfg *cfg, int *err)` | NULL on failure, `*err` as for `v90m_create` (`V90M_EINVAL` for a value outside the ranges above) |
| `size_t v90m_ec_mem_size(void)` | |
| `v90m_ec_t *v90m_ec_init(void *mem, size_t len, const v90m_ec_cfg *cfg, int *err)` | `*err` as for `v90m_init` |
| `int v90m_ec_reset(v90m_ec_t *e, const v90m_ec_cfg *cfg)` | `V90M_OK`, or `V90M_EINVAL` for e or cfg NULL or a value outside the ranges, the endpoint left as it was |
| `void v90m_ec_destroy(v90m_ec_t *e)` | NULL is fine |
| `int v90m_ec_tx_bit(v90m_ec_t *e)` | from the modem's getbit |
| `void v90m_ec_rx_bit(v90m_ec_t *e, int bit)` | from the modem's putbit |
| `void v90m_ec_tick(v90m_ec_t *e, unsigned ms)` | timers |
| `void v90m_ec_line_stopped(v90m_ec_t *e)` | on `V90M_EV_DATA_STOPPED`: the falling edge of `v90m_state.moving` (1 to 0), within the sample it happens (from the event callback, or a `v90m_get_state` after each sample). Notes the frame in flight and begins no new frame until the line is back. Optional |
| `void v90m_ec_line_restored(v90m_ec_t *e)` | on `V90M_EV_DATA_MOVING`: the rising edge of `moving` (0 to 1), within the sample it happens. After `v90m_ec_line_stopped`: aborts the frame the stop cut if it is still going, and opens the next frame with two whole flags; in any case LAPM polls when frames are unacknowledged |
| `int v90m_ec_link_hst(v90m_ec_t *e, v90m_t *m)` | (added in 0.7.0) HST (`V90M_MOD_HST`): tie the endpoint to the answering modem m, before m decides whether to offer HST (the end of ANSam): link before the first sample. Linked, the endpoint follows m's decision: it holds T400 until m has connected (or ended) on another path, and is then the unlinked endpoint; on an HST call it takes the caller's bits from the start of the line, so an LR sent during our training is kept. `v90m_ec_reset` keeps the link (resetting at `V90M_EV_CONNECTED` is harmless). The link is two-way, and either side ends it: `v90m_destroy` or `v90m_reset` of m; `v90m_ec_destroy`, a reset with `mnp.enable` 0, or a link to another modem of e; a second endpoint linked to m unlinks the first. So either may be destroyed first, and e may serve a new modem per call: link again after `v90m_create` or `v90m_reset`, before the first sample. Linking starts e's call afresh, as `v90m_ec_reset` with its configuration does, so linking again between calls without a reset is enough. `V90M_OK`; `V90M_EINVAL` for e or m NULL, e with `mnp.enable` 0 or an originator, or m calling; `V90M_ESTATE` once m has decided. For either in memory of the caller's: its destroy (which frees nothing) still ends the link; call it before that memory is reused |
| `int v90m_ec_set_rates(v90m_ec_t *e, int tx_bps, int rx_bps)` | the line's rates (`v90m_state.tx_bps`, `rx_bps`), each > 0: after `v90m_ec_reset` at `V90M_EV_CONNECTED` (a reset forgets them) and at each `V90M_EV_RATES`. MNP computes T401 and T404 from them (2400 bit/s until given); LAPM's timers are the configured ones. `V90M_OK`, or `V90M_EINVAL` (e NULL, a rate <= 0), nothing changed |
| `size_t v90m_ec_write(v90m_ec_t *e, const uint8_t *data, size_t len)` | |
| `size_t v90m_ec_write_space(const v90m_ec_t *e)` | |
| `size_t v90m_ec_read(v90m_ec_t *e, uint8_t *data, size_t max)` | |
| `size_t v90m_ec_read_avail(const v90m_ec_t *e)` | |
| `int v90m_ec_tx_idle(const v90m_ec_t *e)` | everything written was acknowledged |
| `int v90m_ec_tx_busy(const v90m_ec_t *e)` | a frame is still leaving the transmitter (MNP: an LD owed too, either framing) |
| `void v90m_ec_release(v90m_ec_t *e)` | orderly: DISC, wait for UA. MNP: LD 255, `V90M_EC_DISCONNECTED` at once (wait for `v90m_ec_tx_busy` 0 before stopping the line) |
| `void v90m_ec_abort(v90m_ec_t *e)` | the line is gone: stop without a word, in every state; `v90m_ec_state` is `V90M_EC_DISCONNECTED` (reason `V90M_EC_R_LOCAL`) on return |
| `int v90m_ec_send_break(v90m_ec_t *e, int opt, int len10ms)` | V.42 12.3.1; `V90M_OK` when the break is on its way; `V90M_EINVAL` for an `opt` other than the three `V90M_BRK_*`; `V90M_ESTATE` when error control is not up (`V90M_EC_CONNECTED`) or a break of ours is still pending |
| `int v90m_ec_get_break(v90m_ec_t *e, int *opt, int *len10ms)` | 1 with `*opt` and `*len10ms` when a break came (in its place among the octets read), else 0 |
| `int v90m_ec_state(const v90m_ec_t *e)` | `V90M_EC_DETECT` ... |
| `void v90m_ec_get_status(const v90m_ec_t *e, v90m_ec_status *st)` | |

Break options: `V90M_BRK_NONDESTRUCTIVE_INSEQ` (0x00),
`V90M_BRK_NONDESTRUCTIVE_EXPEDITED` (0x40), `V90M_BRK_DESTRUCTIVE_EXPEDITED`
(0xC0); `len10ms` is the length in 10 ms units.

### Enums

- States: `V90M_EC_DETECT` (0), `V90M_EC_ESTABLISH`, `V90M_EC_CONNECTED`,
  `V90M_EC_ASYNC`, `V90M_EC_RELEASING`, `V90M_EC_DISCONNECTED`.
- Why an endpoint is `V90M_EC_DISCONNECTED` (`v90m_ec_status.reason`):
  `V90M_EC_R_NONE` (0), `V90M_EC_R_LOCAL`, `V90M_EC_R_PEER_DISC`,
  `V90M_EC_R_PEER_DM`, `V90M_EC_R_N400`, `V90M_EC_R_PROTOCOL`,
  `V90M_EC_R_NEGOTIATION`, `V90M_EC_R_NO_EC` (16), `V90M_EC_R_COMPRESSION`,
  `V90M_EC_R_NO_SABME`.
- Compression: `V90M_EC_COMP_NONE` (0), `V90M_EC_COMP_V42BIS`,
  `V90M_EC_COMP_V44`.
- The protocol the answerer's detection chose (`v90m_ec_status.protocol`,
  added in 0.7.0): `V90M_EC_PROTO_NONE` (0, not yet), `V90M_EC_PROTO_LAPM`,
  `V90M_EC_PROTO_MNP`, `V90M_EC_PROTO_ASYNC` (start-stop, no error
  control).

### `v90m_ec_status`

| field | |
|---|---|
| `state`, `reason` | as above |
| `k_tx`, `k_rx`, `n401_tx`, `n401_rx` | |
| `comp_alg` | `V90M_EC_COMP_*`, agreed |
| `comp_tx`, `comp_rx` | agreed, per direction |
| `enc_on`, `dec_on` | our encoder / decoder in use now |
| `alg_on` | `V90M_EC_COMP_*` of the encoder / decoder in use |
| `comp_n2`, `comp_n7`, `v44_n2_tx` ... `v44_n8_rx` | compression parameters |
| `fcs32` | |
| `tx_queued`, `rx_avail`, `unacked` | |
| `rx_frames`, `rx_frames_bad`, `tx_i`, `tx_i_retx`, `rx_i`, `rx_i_dropped`, `tx_rej`, `rx_rej`, `t401_expiries`, `violations` | counters |
| `comp_in`, `comp_out`, `decomp_in`, `decomp_out` | compression counters |
| `async_overruns`, `async_framing` | start-stop fallback counters |
| `protocol` | (appended in 0.7.0, as every field below) `V90M_EC_PROTO_*` |
| `mnp_framing`, `mnp_opt`, `mnp_streamlined` | MNP: framing mode 2 or 3, parameter 8 agreed, the HST (streamlined) layout |
| `hst`, `hst_rates`, `hst_back300` | the HST profile agreed (an HST call whose caller's 0xC0 has bit 0 and a rate in common): 1; the rate mask (bit n: 2400n bit/s); the 300 bit/s back channel |
| `attn_tx`, `attn_rx` | MNP: the type of the last LN sent, received (on an HST call the channel's attention codes: 0x1R a reversal, 0x2R a retrain, 0x3R a change by marks, R the rate code) |
| `rx_unknown`, `rx_unknown_id` | MNP: valid frames of an unknown type; the first octet of the last of them |
| `peer_ld_reason` | MNP: the reason in the far end's LD, 0 none |

`v90m_ec_status` grew in 0.7.0 (the fields from `protocol` on,
appended): rebuild against the new header.
