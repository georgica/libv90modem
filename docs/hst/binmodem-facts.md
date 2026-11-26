# USR HST: facts extracted from BinModem

Clean-room digest for libv90modem. These are facts in our own words, taken
from BinModem's HST implementation (Rust, GPL-3.0). No BinModem code or
prose is copied here. Each fact carries a `file:line` tag so we can trace it.

- Source tree: `/home/georgica/modem-research/ext/BinModem` at `2a1bfcc`
  (main, after merge `3b842cf` "Merge PR #74 … pr32-hst").
- HST commits: `6d5ae05` (channels), `783c810` (MNP, handover, fallback),
  `c6b67c1` (AT and call start), `d71f467` (front panel), `f582463` (HST 9600),
  `9efd437` and `ae0fe1b` (BBS and DOSBox front ends). All dated 2026-11-21.
- Path abbreviations used in the tags:
  - `D` = `docs/design/hst.md`
  - `H` = `crates/datapump/src/hst.rs`
  - `B` = `crates/datapump/src/hst/back.rs`
  - `C` = `crates/datapump/src/hst/call.rs`
  - `S` = `crates/datapump/src/hst/session.rs`
  - `M` = `crates/ec/src/mnp.rs`
  - `ML` = `crates/modem/src/lib.rs`
  - `AT` = `crates/at/src/lib.rs`
  - `V17` = `crates/datapump/src/v17.rs`, `V32` = `crates/datapump/src/v32.rs`
  - `HD` = `crates/ec/src/hdlc.rs`

**Confidence labels.** Every fact has one of these labels:

- **[FW]**: BinModem says the fact comes from the Courier firmware.
- **[MAN]**: from USR manuals.
- **[STD]**: borrowed from a published ITU standard, not measured on a Courier.
- **[OWN]**: BinModem's own choice, with no claim that a Courier does it.

BinModem is upfront that its HST talks only to itself. It has not been shown
to interoperate with a real Courier (`D:105-109`, `H:24-31`). Treat
**[OWN]** values as placeholders, not as HST.

---

## 0. Overview

- HST uses two channels on one two-wire line, both running at the same time
  (`D:9-15`, `H:8-22`) [FW]:
  - The **forward channel** is fast and goes one way. It uses 2400 baud on an
    1800 Hz carrier with V.32-family constellations.
  - The **back channel** is slow and goes the other way. It uses
    differential 8-PSK at 150 baud on 375 Hz (450 bit/s) or 100 baud on
    350 Hz (300 bit/s).
- Whichever end has more to send holds the forward channel. The ends swap
  with MNP attention frames (`H:21-22`, `D:21-22`) [FW].
- There is no echo cancellation in the scheme. The two channels are
  separated by frequency: the forward pulse is sharp enough to leave the low
  band clear (`H:14-16`) [FW].
- The firmware split [FW] (`D:16-18`, `H:4-5`):
  - The DSP is a TMS320C50. It hands one symbol's worth of bits per word to
    the 80188.
  - The 80188 does the HDLC framing in software.
- The DSP sample rate is **7200 Hz** (`H:9`, `B:7`) [FW].

## 1. Call start

### What the answerer does

- The answerer sends a plain **2100 Hz** answer tone (V.25 style). There are
  **no phase reversals and no V.8** (`C:3-9`) [FW for the sequence].
- The Courier's DSP entry for the answering side is at **0xC29D**. It sends
  the answer tone, then starts the forward transmitter and the back receiver
  together (`C:3-5`) [FW].
- BinModem's answerer timeline (`C:18-28`, `C:127-148`). All of these
  timings are **[STD]** (V.25) or **[OWN]**, not measured from a Courier
  (`C:11-13`, `D:101`):
  1. Silence for **2000 ms**. This is V.25's billing delay; V.25 allows
     1.8 to 2.5 s.
  2. The 2100 Hz tone for **3300 ms**; V.25 allows 2.6 to 4.0 s. Its peak
     level is 0.3 on the same scale as the data carriers (`C:30`) [OWN].
  3. Silence for **75 ms**.
  4. The session starts, with the answerer **holding the forward channel**.
- Before MNP agrees on rates, the forward channel trains at the opening rate
  of **7200** (`S:85-86`, `S:211-213`) [OWN].

### What the caller does

- The caller listens for the tone. Once the tone has stopped, it starts the
  back transmitter and the forward receiver together. The Courier's DSP
  entry for this is **0xC573** (`C:5-6`) [FW].
- BinModem's caller detection rules (`C:24-28`, `C:149-162`) [OWN]:
  - The tone must be present for at least **400 ms** to count.
  - It must then be absent for **30 ms** before the caller treats it as over.
  - A shorter burst resets the counter.
- The caller is the **MNP originator**. It sends the link request (LR) on the
  back channel at **450 bit/s**, whatever back-channel speed it wants in the
  end (`S:5-7`, `S:211-213`) [FW for the roles; the 450 start is OWN].

### Choosing HST against other modulations

- BinModem picks HST only when the user selects it explicitly with
  `AT+MS=HST` or `AT+MS=HST96`. Fax class does not run HST (`ML:3495-3497`).
- There is **no automode**. Neither direction falls back to
  V.32/V.32bis/V.22bis/V.21 (`D:112-113`, "Not yet").
- BinModem says the plain V.25 tone is "also what lets a V.22bis or V.32
  caller fall in behind it on a dual-standard modem" (`C:7-9`). This states
  the principle only. Nothing is implemented, and it gives no timings.
- After MNP is up, the terminal gets these result codes (`ML:4459-4470`):
  - `CONNECT <rate>/ARQ/HST`, for example `CONNECT 14400/ARQ/HST`. The format
    is described as "what a Courier said".
  - Plain `CONNECT` with X0.
  - `+ER: ALT` first, if `+ER=1`. MNP counts as V.42 Annex A's alternative
    protocol here.

### Call-start fallbacks and timeouts

- No MNP link **60 s** after going off hook ends the call (`ML:4560-4561`,
  `ML:4493-4496`, `D:98-99`) [OWN]. The result is:
  - `NO ANSWER` if no tone was ever heard.
  - `NO CARRIER` if a tone was heard.

## 2. Forward (fast) channel

### Modulation

- **Symbol rate:** 2400 baud (`H:49-50`) [FW].
- **Carrier:** 1800 Hz (`H:51-52`) [FW].
- **Rates and rate codes** (`H:54-72`, `M:21-23`, `M:50-61`) [FW]:
  - The rate code is the number of data bits per symbol, n = 2..7.
  - The rate is 2400·n bit/s.

  | code | rate  | coding (per firmware, `H:10-12`, `D:10-11`) |
  |------|-------|---------------------------------------------|
  | 2    | 4800  | uncoded (V.32's 4800)                       |
  | 3    | 7200  | trellis coded as V.32bis                    |
  | 4    | 9600  | trellis coded as V.32bis                    |
  | 5    | 12000 | trellis coded as V.32bis                    |
  | 6    | 14400 | trellis coded as V.32bis                    |
  | 7    | 16800 | 256-point set (the one V.32terbo published) |

- **Mapper** [FW]: it "reads the same constellation and trellis tables, with
  the same index arithmetic, as the firmware's V.32 transmitter" (`H:9-12`).
  - Read this as: the V.32bis signal sets, the V.32bis 8-state trellis
    encoder, and V.32bis differential encoding, unchanged.
  - BinModem does not spell out the trellis and differential state machines.
    It relies on its V.17 engine, which uses the same V.32bis modulation
    (`H:25-27`).
- **Scrambler** [FW]: **1 + x^-18 + x^-23**, which is V.32's
  calling-modem polynomial (`H:12-13`).
  - It is the same in both directions. Only one end transmits on the forward
    channel at a time, so no second polynomial is needed. BinModem reuses
    V.17's scrambler (`V17:44-55`).
  - In BinModem, the scrambler starts from all zeros at each training
    (`V17:51`) [STD, from V.17].
- **Bit order:** least significant bit first, everywhere on the Courier
  (`D:16`, `B:18`) [FW].

### Pulse shape

The Courier's pulse is much sharper than V.32's (`H:14-16`, `D:13-15`) [FW]:

- flat out to **1100 Hz** from the carrier;
- **3 dB** down at **1200 Hz** from the carrier;
- **36 dB** down at **1400 Hz** from the carrier.

So the forward channel fills about 600 to 3000 Hz, leaving the band below
about **450 Hz** clear for the back channel. The design doc's rough
equivalent is "36 dB down 1400 Hz off the carrier".

BinModem does not reproduce this pulse [OWN]:

- It uses V.17's **25% root-raised-cosine** pulse (`V32:60`).
- It adds a **10th-order Butterworth high-pass at 560 Hz** on the forward
  transmitter output and in front of the forward receiver (`H:74-84`,
  `D:32-34`).
  - On the transmitter side, the filter keeps our echo out of the far end's
    back-channel receiver.
  - On the receiver side, it keeps our own back-channel echo out.

### Training

- **Real HST training** [FW, and terse]: the design doc gives the start-up
  as "**180 + 72 + 200 or 3096 symbols**" (`D:106`).
  - BinModem does not say what each segment contains, and it does not say
    when the 3096-symbol form is used. See the Open questions.
- **What BinModem actually sends** [STD, not HST]: V.17's long train
  (`H:98-102`, `V17:204-217`):
  - 256 symbols of A/B alternation;
  - 2976 symbols of equaliser conditioning;
  - a 64-symbol bridge;
  - 48 symbols of scrambled ones;
  - 3344 symbols (about 1.39 s) in total.
- V.17's turn-off is 32 symbols of ones, then 48 quiet symbols
  (`V17:220-221`). This too is a stand-in.
- **Equaliser assumptions:** BinModem relies on V.17's receiver. Before the
  data starts, both ends have already agreed the rate through MNP
  (`H:154-159`). There is no in-band rate signal, unlike V.32's R-sequences.

### Rate selection and signalling

- Rates are agreed in MNP's LR parameter **0xC0** (see §5). The forward
  channel then retrains at the **fastest rate both ends allow** (`S:8-11`,
  `S:568-586`).
  - The firmware's connect routine at **8FBB:1C70** does the same: idle,
    then set the agreed rate, then start the forward transmitter (`S:9-11`)
    [FW].
- Once connected, rate changes travel as MNP attention code **0x3R** (§4,
  §6).
- The Courier changes bits per symbol **without retraining** (`D:67-68`)
  [FW]. BinModem's V.17 engine cannot do that, so BinModem retrains [OWN].

## 3. Back (reverse) channel

### Modulation and carrier

- **Modulation:** differential 8-PSK, 3 bits per symbol (`B:11-14`) [FW].
- **The two speeds**, chosen in the DSP by one flag bit (`B:5-10`) [FW]:

  | variant | NCO increment | polyphase step | tick rate | symbol rate | carrier | bit rate  |
  |---------|---------------|----------------|-----------|-------------|---------|-----------|
  | fast    | 0x6AAA        | 6 of 72 taps   | 300/s     | 150 baud    | 375 Hz  | 450 bit/s |
  | slow    | 0x638E        | 4 of 72 taps   | 200/s     | 100 baud    | 350 Hz  | 300 bit/s |

- How the ticks become symbols: "Every other tick carries a zero", so the
  real symbol rate is half the tick rate (`B:8-9`) [FW]. This looks like 2×
  zero-stuffed upsampling into the 72-tap shaping filter.
- Our own check of the increments: at fs = 7200, carrier/7200 × 2^19 gives
  375 → 27306 = 0x6AAA and 350 → 25486 = 0x638E. So the increment is the
  carrier frequency in units of 7200/2^19 Hz (a 16-bit accumulator with 3
  extra fraction bits, or similar). This is our arithmetic, not BinModem's
  statement.
- **Bit rates:** the 450 and 300 bit/s figures "the rates USR's manuals give"
  (`B:9-10`) [MAN+FW].

### Phase mapping

The mapping is differential [FW]:

- Three scrambled bits form a tribit `t`. `t` selects a phase step in
  eighths of a turn.
- The steps come from the constant **0x43705261**, read one nibble at a time
  from the low end (`B:11-14`, `B:28-30`). Step for each tribit:

  | t    | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |------|---|---|---|---|---|---|---|---|
  | step | 1 | 6 | 2 | 5 | 0 | 7 | 3 | 4 |

- The new phase is (phase + step) mod 8. The signal points sit at
  **22.5° + 45°·k** (`B:13-14`, `B:100-104`).
- **Bit order within the tribit:** the first bit sent is the tribit's least
  significant bit (`B:18-19`, `B:185-190`). BinModem marks this as its own
  reading of "LSB first", not a confirmed fact [OWN/inferred].
- At start, BinModem's transmitter resets the phase to 0 (`B:153`) [OWN].

### Scrambler

- **1 + x^-14 + x^-17**, self-synchronising (`B:15-16`, `B:67-90`) [FW].
- Firmware description: new bits enter at bit 17, and the feedback is the
  register XOR itself shifted three places.
- Equivalent form: output = input XOR out[n-14] XOR out[n-17]. The
  descrambler uses the received history.
- In BinModem the scrambler starts from zero at each start (`B:154`) [OWN].

### Pulse shape and preamble

- The Courier uses **72-tap shaping** and a **17-tick start**. BinModem
  reproduces neither (`B:20-21`) [FW for their existence; details unknown].
- BinModem's own choices [OWN]:
  - pulse: **35% RRC**, ±4 symbols (`B:92-95`);
  - preamble: **60 symbols of scrambled ones** before the data (`B:96-98`).
    That is 0.4 s at 450 bit/s and 0.6 s at 300 bit/s.
  - When no frame is queued, the transmitter sends scrambled ones
    (`B:176-188`).

### BinModem's receiver [OWN]

These are implementation notes, not HST facts:

- a 6th-order band-pass from 150 to 650 Hz (`B:220-224`);
- a mix down to baseband, then decimation to about 1600 Hz (`B:230-232`);
- an RRC matched filter and a Gardner timing loop;
- differential decisions at angle/45°, rounded;
- carrier detection with hysteresis on mean symbol power, on at 1e-4 and off
  at 3e-5 (`B:225-228`).

### Spectral coexistence

- The back channel's band, from the table above:
  - **450 variant:** 375 ± 101 Hz, about 274 to 476 Hz.
  - **300 variant:** 350 ± 68 Hz.
- The forward channel's lower edge is 1800 − 1200 = 600 Hz at −3 dB and
  400 Hz at −36 dB. The band below about 450 Hz is left to the back channel
  (`H:14-16`) [FW].
- The forward pulse alone does the job; no echo canceller is needed. In
  BinModem, the 560 Hz guard filter widens the gap further [OWN].

### Back-channel speed selection

- The LR always goes at **450** (`S:211-213`) [OWN].
- If either end set the 300 bit/s flag in 0xC0, the back channel drops to
  **300** after MNP connects (`S:573-581`, `M:246`).
- The change waits until the confirmation has been sent and then a further
  **250 ms** (`S:118-119`, `S:465-476`) [OWN].
- The end holding the forward channel listens for **both** back-channel
  speeds at once, through two receivers and two HDLC decoders. A change of
  speed therefore needs no coordination (`S:158-162`, `D:69-70`) [OWN].

## 4. Channel reversal (handover)

### Who holds the forward channel

- At the start, the answerer holds the forward channel and the caller holds
  the back channel (`S:5-7`, `S:233-234`) [FW].

### The request and the swap

1. The back-channel end asks for the forward channel with an MNP **attention
   (LN) of code 0x1R**. R is a rate code (`S:12-17`) [FW]:
   - BinModem sends R = the **current** forward rate code (`S:495`).
   - On receipt, the holder takes R as the rate for the new direction
     (`S:588-590`).
2. **The holder** acknowledges with LNA (`S:12-17`) [FW]:
   - Once the LNA has gone, it drops the forward carrier and moves to the
     back channel.
   - The firmware event for this is **0x73**.
   - BinModem waits until its HDLC encoder and modulator queue are empty,
     then a further **120 ms** "drain". Then it switches (`S:95-98`,
     `S:438-451`) [OWN timing].
3. **The requester**, on receiving the LNA (`S:12-17`) [FW]:
   - It drops the back channel and trains the forward channel.
   - The firmware event for this is **0x76**.
   - BinModem stops its back transmitter at once, waits **400 ms**, then
     starts the forward training at rate R (`S:120-123`, `S:605-609`,
     `S:478-486`) [OWN timing].
4. While a handover or rate change is pending, the ends send **control
   frames only**, never data (LA, LN, LNA, LD). After those, the modulator
   queue runs dry, and an empty modulator queue sends ones (`S:372-378`)
   [OWN].
5. On each switch, both ends reset their HDLC encoder and decoder. The new
   forward holder also restarts its back-channel receivers (`S:304-327`).

### When to ask [OWN]

The back-channel end asks only when all of these hold (`S:487-496`,
`S:62-71`):

- the link is up;
- it is holding the back channel;
- nothing else is pending;
- no attention is outstanding;
- at least **3000 ms** have passed since the last switch (`min_hold_ms`);
- its MNP backlog (queued octets plus unacknowledged octets) is over
  **256 octets** (`turnaround_backlog`).

### Watchdog [OWN]

- If an end hears no far carrier for **5000 ms**, with no handover pending,
  roles go back to the defaults: answerer forward, caller back (`S:87-90`,
  `S:498-516`).
- Which receiver counts as "hearing":
  - when holding the forward channel: any back receiver's carrier;
  - when holding the back channel: the forward receiver's carrier.

### Firmware status lamps

Firmware lamp behaviour as BinModem describes it (commit `d71f467`), from
the Courier manuals [MAN]:

- **AA** goes out at the answering end while the channels are reversed.
- **MR** blinks during training and handover.
- **HS** is out while the link is being set up.
- **ARQ** blinks when a frame is resent.

## 5. Error control and framing

### HDLC framing

The HDLC framing is synchronous and done in software on the 80188, the same
framing V.42 uses (`D:16-18`, `H:199-211`) [FW]:

- **0x7E flags**;
- a zero inserted after five consecutive ones (`HD:4`);
- bits sent LSB first;
- **CRC-16/X.25 (CCITT, reflected):**
  - preset 0xFFFF;
  - the ones' complement is transmitted;
  - the good-frame residue is **0xF0B8**;
  - the check value for "123456789" goes out as bytes `6E 90`.
  - The firmware CRC routine is at **8FBB:1874** (`H:199-211`) [FW].
- Between frames the line idles with flags (`HD:230-235`, `S:369`).
- MNP's own asynchronous framing is **not** used. HST runs MNP **class 3**,
  "the synchronous class" (`M:67`) [FW/MNP].

### MNP frame types

Frame type numbers follow MNP (`M:36-44`):

| type | name |
|------|------|
| 1    | LR   |
| 2    | LD   |
| 4    | LT   |
| 5    | LA   |
| 6    | LN   |
| 7    | LNA  |

### Standard headers (link setup)

In the standard layout, the frame starts with a length-indicator (LI)
octet, then the type octet, then parameters as type/length/value triples
(`M:10-15`).

**Link request (LR).** This is the Courier's template byte for byte,
taken from firmware address **0x856E7** (`M:11-13`, `M:702-711`) [FW]:

```
14  01  02  01 06 01 00 00 00 00 FF  02 01 03  03 01 08  04 02 40 00
LI  LR  ??  p1: len 6, 01 00..00 FF  p2: class 3  p3: window 8  p4: max data 0x0040=64 (LE)
```

- LI is the frame length minus 1.
- The octet after the type (0x02) is a constant the decoder skips
  (`M:157-159`).
- After the fixed part, BinModem appends (`M:104-117`):
  - parameter **8** (data-phase optimisation) = 3, when it wants optimised
    headers;
  - USR's parameter **0xC0**, with length 2: `C0 02 flags rates`.
- The template itself has **no** parameter 8 (see the Open questions).

**Other frames in the standard layout:**

- **LD:** `04 02 01 01 reason`. This layout is from the firmware (`M:13-14`,
  `M:118`) [FW].
- **LA:** `07 05 01 01 seq 02 01 credit` (`M:130`).
- **LN:** `07 06 01 01 seq 02 01 code` (`M:132`).
- **LNA:** `04 07 01 01 seq` (`M:134`).
- **LT:** `02 04 seq data…` (`M:120-124`).
- These four layouts follow MNP's published parameter form, **not** the
  Courier (`M:14-15`) [STD].

### Optimised headers (data phase)

Once both ends agree parameter 8, frames drop the LI octet: the first octet
is the type, then fixed fields (`M:16-20`, `M:119-135`). The layouts come
from the Courier's receive path at **8FBB:1892** and its frame builders at
**0x862B8, 0x861E1, 0x85F8C, 0x91DC2** [FW]:

| frame | optimised layout  |
|-------|-------------------|
| LT    | `04 seq data…`    |
| LA    | `05 seq credit`   |
| LN    | `06 seq code`     |
| LNA   | `07 seq`          |
| LD    | `02 reason`       |

- An LR always goes in the standard layout, even after optimisation is
  agreed. A decoder in optimised mode recognises an LR because its second
  octet is 0x01 (`M:193-194`, `M:568-572`).

### USR LR parameter 0xC0

The parameter has two value octets, `flags` and `rates` (`M:21-25`,
`M:46-62`). The firmware builds it at **0x85A82** and parses it at
**0x85422** [FW].

- **flags:**
  - bit 0 (0x01) = HST;
  - bit 3 (0x08) = 300 bit/s back channel.
  - Any other bits are unknown.
- **rates:** a bit mask with bit n set when rate code n (2400·n bit/s) is
  acceptable, for n = 2..7. Examples:
  - 7200 to 14400 is 0x78;
  - 9600 only is 0x10;
  - all six rates is 0xFC.
- **Merge rules** (`M:243-256`):
  - HST is agreed only if **both** ends set bit 0.
  - The agreed flags are the AND of both ends' flags, **plus** the OR of
    their 300 bit/s bits (if either end wants 300, the call gets 300).
  - The agreed rates are the AND of both masks, then AND 0xFC.
  - The best rate is the highest set bit from 7 down to 2.
  - "The responder keeps only the rates both ends allow" (`M:24-25`) [FW].
- **Other parameters** are merged too:
  - window = min of the two (at least 1);
  - max data = min of the two (at least 1);
  - optimised headers only if both ends sent a nonzero parameter 8.

### Link establishment

BinModem's state machine, partly [STD/OWN]:

1. The originator (the caller) sends an LR (`M:400-408`).
2. The responder computes the agreed values and replies with its own LR
   carrying them, in its own class (`M:599-607`). It then moves to the
   Confirming state.
3. The originator, on that LR, counts as connected. It sends **LA with seq 0
   and credit = window** as the confirmation, which is its last frame in
   the standard layout. After that it transmits in the agreed layout
   (`M:608-617`, `M:476-482`).
4. The responder counts as connected when it receives that LA, or when an LT
   arrives first (lost confirmation). In the second case it drops that LT
   and asks for a resend with an LA (`M:639-647`, `M:669-682`).
5. If the originator receives a repeated LR, it confirms again in the
   standard layout (`M:618-625`).

### Data transfer

All of this is [STD/OWN] (`M:474-513`, `M:639-691`):

- LT sequence numbers start at **1** and run mod 256.
- An LA acknowledges everything up to and including its seq, compared mod
  256 with a half window of 128.
- The receiver accepts **only** the next expected seq. It discards
  out-of-order frames but still sends an LA with the last in-order seq.
  That makes this go-back-N.
- The transmitter has `credit` frames outstanding, where credit is taken
  from the last LA.
- Defaults the HST session uses (`S:189-202`):
  - window **8**;
  - max data **64** octets;
  - optimisation 3;
  - class 3.

### Timers

- In an HST session, frames are resent after **3000 ms** with no progress,
  up to **15** tries. The MNP defaults are 2000 ms and 12 tries (`S:201-202`,
  `M:306-310`) [OWN].
- On a timeout (`M:537-553`):
  - every unacknowledged frame is marked to send again;
  - an LA goes out to tell the far end where this end has got to.

### Attention (LN/LNA)

- LN has its own sequence counter (`M:439-451`):
  - one attention is outstanding at a time;
  - a new one replaces the old;
  - it is resent every retransmit interval until an LNA arrives (`M:554-560`).
- The receiver always answers with an LNA and delivers each LN seq only once
  (`M:579-588`).
- Codes used:
  - **0x1R:** request the forward channel (§4).
  - **0x3R:** request forward rate R (§6).
- "Channel-control messages carry this number": the low nibble is the rate
  code (`H:62-64`) [FW].

### Not implemented

These are absent (`M:27-29`, `D:110-111`):

- USR's "acknowledgement variant";
- data frames with a piggybacked acknowledgement;
- **short data frames**;
- **MNP 5** compression.

V.42bis over HST is never mentioned.

### Data mode and EC requirement

- Data mode begins when MNP connects. `CONNECT …/ARQ/HST` is sent then
  (`ML:4452-4471`).
- BinModem has **no** HST mode without error control. HST always runs MNP
  (`ML:1325-1327`). Whether a Courier allows HST without EC is not covered.
- The terminal's backlog limit is **4096** octets, after which flow control
  stops accepting input (`ML:4562-4564`, `D:93-94`) [OWN].

## 6. Retrain, rate shifts, cleardown

### Online fallback and fall-forward

The general approach is [FW] (`D:65-68`): the receiving end watches the
signal-to-noise ratio (SNR) of the forward channel and asks for a new rate
with **0x3R**, and the holder then changes rate.

BinModem's version [OWN] (`S:519-564`):

- **When it runs:** only at the back-channel end (the forward receiver),
  only while linked, and only after **1000 ms** in data since the last
  training (`S:99-101`).
- **Working SNR per rate,** borrowed from its V.17 test figures, "3 dB above
  the 1e-5 point" (`S:106-116`):

  | rate  | working SNR |
  |-------|-------------|
  | 14400 | 27 dB       |
  | 12000 | 24 dB       |
  | 9600  | 21 dB       |
  | 7200  | 18 dB       |

- **Going down:** if SNR < working(current) − 2 dB, ask for the highest
  lower rate that is agreed and has working − 1 dB ≤ SNR. If none qualifies,
  ask for the lowest rate on the ladder.
- **Going up:** if the next rate up is agreed and SNR > working(next) + 1 dB,
  ask for one step up.
- **Holder:** on 0x3R, it waits for its LNA to drain plus **120 ms**, then
  retrains at R (`S:592-597`, `S:452-464`).
- **Asker:** on the LNA, it retrains its receiver at R. If no LNA arrives
  within **2500 ms**, it assumes the change happened and retrains anyway,
  because the LNA may be arriving at the new rate (`S:102-104`, `S:530-543`,
  `S:598-604`).
- The `fallback` setting can turn this off (`S:57-59`).
- **In a turnaround it asked for** [OWN] (`S:478-486`, `S:497`,
  `S:521-564`, `S:605-609`): after the LNA to its own 0x1R it is still the
  back-channel end for the **400 ms** wait before its training, and the
  fallback check runs on every tick of that wait (it does not look at the
  pending take). Its forward receiver counts as in data until **150 ms**
  of quiet has passed (`crates/datapump/src/v17/receiver.rs:86`, `:384-388`),
  so it can read an SNR off our forward carrier as that carrier stops and
  ask **0x3R** at a lower rate. That LN goes out once it holds the forward
  channel. Seen live (2026-11-25, rev3 debug): on its second take of the
  forward channel, 0x35 at 14400, our receiver in data at -35 dB.
- **As holder,** it does nothing when its own 0x3R is acknowledged (the
  LNA is acted on only at the back-channel end, `S:598-604`), so the rate
  it asked for is never trained.
- **0x2R:** no handling at all. It gets the LNA every LN gets, and nothing
  else happens (`S:566-611`, the catch-all arm at `S:611`).
- **What this leaves for us** (our note, not a BinModem fact): we drop a
  stale 0x3R only when our receiver stays in data for the whole 4 s bound
  (spec "Rate changes"). If the receiver unlocks even briefly in those 4 s
  (a marginal line), the follow counts as a failed retrain. Our 0x2R
  step-down then starts, and against BinModem, which never trains for an
  0x2R, it runs down to `RETRAIN_FAILED`. The same holds for any retrain we
  ask BinModem for while it holds forward (`v90m_retrain`, frame errors).
  This comes from BinModem having no 0x2R; we keep the step-down as
  specified.

### Cleardown

- **ATH:** sends MNP **LD** with reason **0xFF** (user). The line is held
  **1000 ms** for the LD to get out, then goes on hook (`ML:1757-1766`,
  `ML:4565-4567`, `M:290-295`) [OWN timing].
- **The far end,** on receiving LD, ends the call and reports `NO CARRIER`
  (`M:576`, `S:610`, `D:95-96`).
- **Carrier loss:** if no far carrier is heard for **12 000 ms** after the
  link is up, the call ends. This limit is set longer than the 5 s watchdog
  on purpose (`S:91-94`, `S:504-508`) [OWN].
- **MNP retries exhausted:** the link sends LD with reason **0x04**
  (`M:543-545`).
- **No LR answer** within the retry limit ends the call with reason
  **0x01**; no LD is sent (`M:517-525`).
- The reason codes 0xFF, 0x04 and 0x01 are "the reasons this end gives"
  [OWN/MNP]. Only the LD frame layout is from the firmware.

## 7. HST 9600 (1986) versus 14400/16800

- **HST96** in BinModem is the 1986 Courier HST: **9600 forward, 300 back,
  and nothing else** (`AT:407-408`, `ML:4412-4416`, commit `f582463`):
  - the rate mask has only code 4 (0x10);
  - the 0x08 back-300 flag is set.
- **HST** in BinModem means 7200 to 14400 with the 450 back channel
  (`ML:4558-4559`, `S:62-71`). `+MS`'s minimum and maximum rates trim the
  offered mask; for example `AT+MS=HST,0,0,9600` (`ML:4378-4391`, `D:83-84`).
- **Negotiation:** there is no separate version field. Version is implied
  by 0xC0's rate mask and back-300 flag. An HST96 end meets a later HST at
  **9600/300** through the merge rules above (`ML tests: hst_call.rs:105-107`,
  `M:246`).
- 16800 (code 7) appears in the mask format but is **not implemented**.
  4800 (code 2) is not implemented either (`H:54-59`, `D:107-108`).
- BinModem gives no model-by-model history (when 12000 or 16800 arrived,
  ASL). Its firmware evidence comes from one 1995 V.Everything only. Whether
  the 1986 unit uses the same LR, 0xC0, optimised headers or training is
  **not established**. HST96 is modelled as the same protocol with a
  narrower offer.

## 8. Sources and uncertainty as BinModem states them

### Primary source

The primary source is disassembly of a **Courier V.Everything flash build
dated 06/09/95**: the 80188 supervisor and the TMS320C50 DSP code (`D:3-5`,
`H:3-6`). The working notes "with every address" are **outside** the
BinModem repository (`D:4-5`).

Addresses BinModem cites:

| address    | what it is                                                     |
|------------|----------------------------------------------------------------|
| 8FBB:1874  | CRC                                                            |
| 8FBB:1892  | receive path                                                   |
| 8FBB:1C70  | connect                                                        |
| 0x856E7    | LR template                                                    |
| 0x85A82    | builds 0xC0                                                    |
| 0x85422    | parses 0xC0                                                    |
| 0x862B8, 0x861E1, 0x85F8C, 0x91DC2 | frame builders                         |
| DSP 0xC29D | answer start                                                   |
| DSP 0xC573 | caller start                                                   |
| —          | firmware events 0x73 and 0x76 (handover)                       |

Secondary sources:

- the USR manuals: 450 and 300 bit/s rates, lamp meanings;
- ITU V.25: tone and timing values, labelled as such.

### Marked as not reproduced, unknown or BinModem's own

- **Forward start-up:** the real one is "180 + 72 + 200 or 3096 symbols";
  BinModem sends V.17's 1.4 s train (`D:105-106`, `H:27-30`).
- **Forward pulse:** BinModem uses V.17's 25% RRC and a 560 Hz guard, not
  the Courier's pulse (`D:107`, `H:27-29`).
- **Back channel:** the pulse (35% RRC) and the 60-symbol preamble are
  guesses. The Courier's 72-tap shaping and 17-tick start are not reproduced
  (`B:18-21`, `D:36-38`, `D:107-108`).
- **Timings:** handover and fallback timings are BinModem's own (`D:108-109`).
- **Call start:** the tone and silence lengths are V.25's. The Courier's
  timers are "known only in units the firmware does not name" (`C:11-13`,
  `D:101`).
- **Policy:** these are BinModem's own (`S:19-22`):
  - when to request a turnaround;
  - the 7200 opening rate;
  - the watchdog.
- **Not implemented:**
  - 4800 and 16800;
  - USR's extra frame types;
  - MNP 5;
  - automode;
  - USR S-registers and `&N` (`D:103-114`).
- **Interop:** BinModem has not been tested against a real Courier
  (`H:28-29`). Commit `7ec6218` mentions a courier-emu peer for V.34 only,
  not HST.

## 9. Running BinModem as an HST peer

### Build

- `cargo build --release` at the workspace root **fails** with the installed
  rustc 1.93.1:
  - the GUI crate's egui/eframe 0.36.1 needs **rustc 1.95**;
  - the `datapump` crate also uses an API that is unstable on 1.93,
    `u32::isolate_lowest_one`, at `crates/datapump/src/v92/digital.rs:2526`.
  - The build downloaded crates without trouble, so network access works.
    Installing a toolchain (`rustup toolchain install 1.95`) was **not** done.
- **Workaround that worked:**
  1. Export the tree to a scratch copy with `git archive`.
  2. Replace that one call with the equivalent `x & x.wrapping_neg()`.
  3. Build only the headless crate: `cargo build --release -p modem --bins
     --examples`.
  - This builds `modem-sip`, `modem-loop`, `modem-call`, `modem-dial`,
    `line-check` and the `courier_peer` example. The GUI `binmodem` binary
    needs rustc ≥ 1.95.
  - Result, 2026-11-24: the build succeeded in about 1 minute. The binaries
    are in
    `/tmp/claude-1000/-home-georgica-v90d/46f3ac5e-1d0f-4251-ab22-7454a81c8f02/scratchpad/bm/target/release/`.
    That scratch directory may be cleared; rebuild if it is.
  - `cargo test --release -p modem --test hst_call` passed all 6 tests there.
    No SIP or audio call was placed.

### HST over SIP, headless

Use `crates/modem/src/bin/modem-sip.rs`.

1. Write an accounts file in INI style: one `[section]` per account, with
   the keys `registrar`, `domain`, `username`, `password` and optionally
   `register = no`, `local_port`, `rtp_port`, `laws`, `transport`
   (`crates/sip/src/account.rs:245-270`).
2. To call:

   ```
   modem-sip --accounts sip.txt --account test \
       --type "AT+MS=HST" --type "ATD sip:modem@127.0.0.1:5070" \
       --send "hello" --hold 20 --seconds 120
   ```

3. Notes on calling:
   - `ATD` takes a number or a full **SIP URI**
     (`crates/sip/src/line.rs:173-176`), so a local test server works.
   - Use `AT+MS=HST96` for the 9600/300 variant.
   - Use `AT+MS=HST,0,0,9600` to cap the rate.
4. To answer, add `--answer` and type `AT+MS=HST`. The program types `ATA`
   itself when the call rings (`modem-sip.rs:370-383`).
5. Output and exit code:
   - stdout gets the terminal output;
   - stderr gets notes such as "HST: MNP up, 14400 forward, 450 back",
     handovers and retrains (`ML:4437-4486`);
   - the exit code is 0 if the call reached data mode.
6. Line settings:
   - sample rate 16 kHz;
   - `--drive` sets the line level, default 0.1, to avoid G.711 clipping;
   - `--record <wav>` records the line.

### Local audio loop

- `modem-loop --in <dev> --out <dev> --carrier HST --seconds N` runs two
  modems through one sound-card cable (`modem-loop.rs:53-56`).
- The GUI equivalents are `binmodem --answer --carrier HST …` and the
  DOSBox `--com` mode (`docs/usage.md:109-127`). Both need the GUI build.

### In-process tests

These cover the whole HST stack without any audio:

- `cargo test -p modem --test hst_call`
- `cargo test -p datapump --test hst_session --test hst_duplex`

### Netstalgia

Not used.

---

## Open questions

These are things BinModem does not settle, roughly in order of how badly we
need them.

1. **Forward training contents.** "180 + 72 + 200 or 3096 symbols"
   (`D:106`). What is each segment: tone, reversals, equaliser sequence,
   scrambled ones? Which is the short form and which the long, and what
   picks between them (first train, turnaround, retrain)? Is there any
   in-band rate indication?
2. **Courier forward pulse.** We have only three points: flat to 1100 Hz,
   −3 dB at 1200 Hz, −36 dB at 1400 Hz from the carrier. We do not know the
   roll-off figure, whether it is root-split, or the tap count. These
   numbers suggest a very low excess bandwidth: the −3 dB point at the
   Nyquist edge of 1200 Hz implies β ≈ 0.08–0.15 in total.
3. **Back-channel pulse and start.** The 72-tap filter's coefficients are
   unknown, and so is the "17-tick start". The preamble pattern, its
   length, and the starting phase and scrambler state are unknown. The bit
   order within a tribit is inferred, not confirmed.
4. **Turnaround timing on the wire.** Courier timers are in unnamed units.
   We do not know how long the holder keeps carrier after the LNA, the gap
   before the new forward training, or what firmware events 0x73 and 0x76
   wait for. We also do not know the Courier's policy for asking: is it a
   backlog threshold? a minimum hold?
5. **Rate change without retraining.** How does the Courier switch bits per
   symbol on the fly? Is it a sync point after the LNA, a marker symbol or
   a frame boundary? Do 0x3R semantics differ between the Courier and
   BinModem, and what are the Courier's SNR thresholds?
6. **Other attention codes.** Only 0x1R and 0x3R are named. Is there a 0x2R
   or 0x4R+? Does R in 0x1R mean the requester's preferred rate or the
   current one? BinModem sends the current one.
7. **0xC0 flags.** Only bits 0 and 3 are known. Bits 1, 2 and 4–7 might
   mean ASL, a V.32 capability, or something else.
8. **Parameter 8 and optimised headers.** The Courier's LR template has no
   parameter 8. Does the Courier send it in other cases, or does it switch
   to optimised headers on some other trigger? The standard layouts of
   LA/LN/LNA come from the MNP books, not the firmware.
9. **USR frame types.** The layouts are not given for the acknowledgement
   variant, data with piggybacked acknowledgement, and short data frames.
   Does a real Courier send them to a peer that did not ask for them?
10. **MNP 5 / V.42bis over HST.** Does a Courier negotiate either, and how
    (parameter 3 class bits? the LR's class octet)? Is there an HST mode
    without error control?
11. **Call start on a real Courier.** We need the tone length, the silence
    before forward training, the caller's detection time, and whether the
    caller sends anything before the LR. We also need the automode logic: how
    a dual Courier recognises a V.32/V.22bis caller versus an HST caller
    behind the 2100 Hz tone, and how long it waits.
12. **4800 and 16800 details.** Is 4800 uncoded QPSK exactly as V.32? Which
    256-point set and which trellis does 16800 use: V.32terbo's? Why is the
    mask's top bit (code 7) present while code 8 is absent?
13. **Line levels.** Nothing gives the forward/back transmit power ratio or
    absolute levels.
14. **1986 HST.** It is unverified that it shares the 1995 firmware's LR,
    0xC0 and framing. BinModem simply assumes so.
