# tools

| | |
|---|---|
| `v90m_replay.c` (`make replay`, installed as `v90m-replay`) | plays a recorded call into the answering modem and reports what it made of it |
| `v90m_fields.c` (`make build/v90m-fields`, not installed) | what the caller of a recorded call sent, field by field: the answering modem run as `v90m-replay` runs it, the library's V.21 and Phase 2 receivers on the caller's signal, the caller's level per state; each sequence's bits rebuilt with the caller's builders and compared (`docs/history/PROGRESS.md`, "realcheck") |
| `rec_orig.c` (`make build/rec_orig`, host only) | a simulated call of the library's caller recorded at the answerer in the format above, the answerer set up as `v90m-replay` sets it up: `v90m-fields` reads it as it reads a real one |
| `trace.c`, `trace.h` | the replay's report: the trace hooks of the modem (built with `-DP34D_TRACE`) and the lines printed per phase; v90d's server replay prints the same |
| `rec_v90.c`, `rec_v34.c` | record a simulated call (`make bench-v90`, `make bench-v34`) |
| `ber_v90.c` | bit error rate of V.90 over the simulated line |
| `qprof.c` (`make prof-v90c`, `make prof-v92c`, host only) | ARM instructions per tick and per function, from qemu's log of a bench run with a mark before every tick (`rig/bench_v90c -t`) |
| `capinfo.py`, `spec.py`, `hdlc.py` | a recording's level per 200 ms; its strongest spectral lines; the HDLC frames in a bit dump |
| `gen_*.py` | generators of the constant tables in `modem/` |
| `x2_pad.c`, `x2_pad.h` (host only, tested by `build/test_x2_pad`) | x2's pad octets: the octet the server sends so that the client receives a given level behind a 3 or 6 dB digital pad (`docs/history/x2-facts.md`, "Pads") |

## The call recording format

A recording holds, for every 10 ms (80 samples) of a call, what the codec
received from the line and what it sent, in that order:

- **companded**: 80 octets heard, then 80 octets sent: G.711 codewords
  (mu-law or A-law, as the line was). 160 octets per block.
- **linear receive**: the file starts with the 8 octets `V90DLIN1`; then per
  block 80 heard samples as little-endian signed 16-bit values on the scale
  of V.90 Table 1 (what `v90m_g711_decode` gives for a codeword), then 80
  codewords sent. 240 octets per block.

The recording starts before the call is answered (v90d records from
off-hook); the replay finds the answer tone in what was sent (the first
sound after 200 ms of silence) and starts the modem so that its own answer
tone begins at that moment.

For the replay to follow the call, the modem must be set up as the recording
modem was (level, modulations offered, rate ceilings, V.92): what it hears
contains the echo of what that modem sent. The replay's last line counts the
samples where its transmit differs from the recording's; 0 until data mode
(where the data were the recording modem's own) means it was set up right.
