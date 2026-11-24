# USR HST and MNP: public-source digest

Clean-room input for the HST implementation in libv90modem. Every fact below
is taken from a public document listed in the Sources section. Nothing here
comes from D-Modem, courier-emu, BinModem, or `/home/georgica/modem-research`
(search-engine snippets that pointed at D-Modem pull requests were ignored and
not opened).

Local copies of the documents (not committed) are under
`/home/georgica/hst-wt/build/hst-sources/` (see Sources for exact files).
Citation tags in square brackets, e.g. [P1], [M2], refer to the Sources list.
"Verbatim" quotes are exact.

Confidence tags: **PRIMARY** = vendor manual or patent text; **SECONDARY** =
hobbyist/press text; **GAP** = not found.

---

## 0. Executive summary

* HST is described only at block-diagram level in public documents. No public
  source gives the HST forward-channel **symbol rate, constellation, trellis
  code, scrambler polynomial, or training sequence** for the 9600/14400/16800
  modes (GAP, see section 7).
* What is public and precise: the **band plan** (forward carrier 1800 Hz; 450
  bit/s back channel carrier 375 Hz, 300 bit/s fallback carrier 350 Hz; the
  originate side transmits the low carrier, the answerer the 1800 Hz carrier),
  the **rate ladder and negotiation order** (handshake at 2400 bit/s V.22bis
  with ARQ negotiation, then both shift to 9600, then 12000/14400/16800), the
  **channel-reversal rule** (US 5,008,901), the **rate-change rule** (US
  4,890,316: thresholds and timings), the **AT/S-register/result-code
  behaviour**, and an **HST link layer that is a streamlined MNP** with
  specific new frame types (US 5,008,901).
* MNP classes 2-4 link layer is fully specified in ITU-T V.42 (03/93) Annex A,
  which I downloaded (our 2002 copy in `refs/itu` has Annex A deleted).
* No public HST audio recordings or spectrograms were found.

---

## 1. HST physical layer as published

### 1.1 Rates and history

* Courier HST: 9600 bit/s asymmetric, 1986; manual title "9600/2400/1200/300
  BPS ... WITH USR HST ERROR CONTROL" [M1 cover; App. F]. Back channel
  **300 bit/s** in the 1986 manual: "At top speed, data flows in one
  direction at 9600 bits per second and at 300 bits per second in the other"
  [M1 ch.1 "Asymmetric Modulation", p.1-1].
* Later: **450 bit/s** back channel, 14400 (1989), 16800 (March 1992)
  [S4 sec II.2.c; M2]. Rate set for HST in Rev B manual: "16.8K, 14.4K, 12K,
  9600, 7200 bps, synchronous/asynchronous, asymmetrical, 450 bps back channel
  with automatic handshake adjustment to 300 bps, Trellis Coded Modulation
  (TCM), Quadrature Amplitude Modulation (QAM)" and "4800 bps ... asymmetrical,
  450 bps back channel ... QAM" (no TCM at 4800) [M2 App. H, p.H-1].
* Modulation text [M2 glossary, "HST"]: "High Speed Technology, USRobotics'
  proprietary signaling scheme, design and error control protocol for
  high-speed modems. HST incorporates trellis-coded modulation, for greater
  immunity from variable phone line conditions, and asymmetrical modulation
  for more efficient use of the phone channel at speeds of 4800 bps and above.
  HST also incorporates MNP-compatible error control procedures adapted to
  asymmetrical modulation."
* Link rates selectable (`&Nn`): 300, 1200, 2400, 4800, 7200 (&N5), 9600 (&N6),
  12K (&N7), 14.4K (&N8), 16.8K (&N9, "HST-to-HST only") [M2 ch.4 "&Nn"].
  Tech spec "ADAPTIVE SPEED LEVELING: 16.8K (HST only), 14.4K, 12K, 9600,
  7200, 4800 bps" [M2 App. H p.H-2].
* The "1986" 9600 manual lists only &N0..&N3, &N6 (&N4, &N5 reserved) [M1
  ch.4 p.4-6 and App. B].

### 1.2 Carriers and band plan (PRIMARY)

Manual "TRANSMITTER CARRIER FREQUENCIES" [M2 App. H p.H-5; also M3 App. K]:

| Mode | Originate-mode transmitter | Answer-mode transmitter |
|---|---|---|
| USR-HST, 450 bps back channel | 375 Hz | 1800 Hz |
| USR-HST, 300 bps back channel | 350 Hz | 1800 Hz |
| V.32 bis / V.32 | 1800 Hz | 1800 Hz |
| V.22 bis / V.22 / Bell 212A | 1200 Hz | 2400 Hz |
| Bell 103 | 1270/1070 mark/space | 2225/2025 |
| V.21 | 980/1180 | 1650/1850 |

[M3 App. K lists the same table plus a "Receiver Carrier Frequencies" table
that is the mirror: HST 450 bps back channel originate-receiver 1800 Hz,
answer-receiver 375 Hz; HST 300 bps originate-receiver 1800 Hz, answer-receiver
350 Hz.]

Reading: with the default allocation (answerer = high-speed transmitter), the
answerer sends the wideband forward channel centred on **1800 Hz** and the
originator sends the narrowband back channel centred on **375 Hz** (or 350 Hz
when the 300 bit/s back channel is negotiated). After a channel reversal the
roles swap, i.e. the originating modem would send on 1800 Hz. The tables are
tied to the originate/answer *role*, so I infer the carriers swap with the
direction of the fast channel; the manual does not state this explicitly
(inference, flagged).

Back-channel centre frequency is consistent with the analog filter design in
the patent: back-channel bandpass zeros at 550 and 750 Hz, poles 463/463/302
Hz [P2 "Analog Architecture", H2(S)]; forward-channel filter is a notch with
zeros 445/396/320 Hz, poles 568/562/534 Hz (so the forward spectrum has a notch
under the back channel), plus a low-pass pole at 3100 Hz, Q=0.87 [P2 H1(S),
H3(S)]. (The filter transfer-function equations are images in Google Patents,
only the parameter values survive as text; the exact biquad topology is a GAP.)
The carrier-detect low-pass corner is 1591 Hz on the forward channel and 30 Hz
on the back channel [P1/P2 H4(S)].

Other analog facts [P1 "Analog Architecture"]: AGC dynamic range 45 dB, 600 mV
RMS output, time constant 15 ms for the forward channel and 1.5 s for the back
channel; the 9600 forward channel uses TCM "the trellis encoding function
specified in the V.32 standard" (Transmitter routine TR section) and the
Receiver does Viterbi decoding "the inverse trellis function defined in the
V.32 standard" [P1 "Transmitter 102 routines", "Receiver 103 routines"]. The
receiver DSP is a TMS32020 at 19.8144 MHz; "decode a 9600 bps trellis-coded
signal" [P1]. The patent describes only the 9600 design; "7200 bit/s and
higher" is where the Viterbi metric is used, and "At 4800 bit/s, the incoming
signal is not trellis coded" [P2, Receiver 103 routines].

Low-speed fallback filters: "comply with the CCITT specifications for V.22 ...
employed for transmission at 300, 1200 and 2400 bps, with the lower band being
assigned as the transmission channel to the modem which originates the call"
[P1 Analog Architecture].

SECONDARY (unverified; page was behind a Cloudflare challenge, only a search
snippet was seen): a vintage-computing wiki ("Baud does not equal BPS",
wiki.preterhuman.net) says the original HST was 1800 Hz forward at **2400
baud** TCM and a 350 Hz, **300 baud** back channel; the 450 variant used 375 Hz
at 450 baud; 12000/14400 used "6 bits per Baud / 7 bits per Baud". These match
the carrier table above but are not otherwise verifiable here. The later
HST.bis patent (below) states a 3200 baud 16-point eye for 9600 bit/s, which
is **not** consistent with 2400 baud, so treat symbol rates as GAP.

### 1.3 HST.bis (US 5,579,305, filed 1994-02-09; inventors Norrell, Hansen; USR)

A later asymmetric design, "HST.bis", with echo cancellation of the narrow band
overlapping the forward channel. Different from classic HST (classic HST
is band-split with no echo cancellation: [P3] "These inventions use band
splitting techniques ... an echo cancellation mechanism is not necessary").
Facts useful as background only:

* Preferred embodiment: forward 28,800 bit/s, back channel 1,067 bit/s; main
  channel carrier 1920 Hz; sample rate 9600 S/s; DAC at 18x symbol rate for the
  forward modulator and 3x for the back-channel modulator; second-stage
  demodulator 920 Hz.
* Training from a V.22bis ARQ connection: answerer sends `wink 64 Tf`, `AB 72
  Tf`, `CD 8 Tf`, `TRN1 N1 Tf` (N1 = 1600 symbols, "train to a level sufficient
  to support a 16 point eye at 3200 baud, or 9600 bits per second"), then
  quiets for the back-channel training (`AB 20 Tb`, `CD 4 Tb`, `TRN2 60 Tb`),
  then `AB 72 Tf`, `CD 8 Tf`, `TRN3 192 Tf` (N3), `SCRB1 32 Tf` (scrambled
  binary ones at 9600). Quick retrain (`AB`,`CD`,`TRN1`, return to 9600),
  Full retrain, Full Turnaround, Quick Turnaround (stored echo canceller and
  equalizer states) described in FIGS. 11-16. [P3 "Modem Training"]
* This is a different (and later) modem generation than the 9600/14400/16800
  band-split HST of the manuals; the AB/CD names are the V.32-style
  alternating-state segments.

### 1.4 Start-up / handshake (PRIMARY, behavioural)

From the Rev B manual Appendix A "HST Mode" [M2 pp.A-1..A-3]:

1. "The modems begin handshaking by training (synchronizing) at **2400 bps**
   and exchanging information on their error control, data compression, and
   speed capabilities."
2. "Once the modems enable error control ... If the remote modem is operating
   at 2400 bps or lower, the Courier stays at 2400 bps or falls back to match
   the remote modem's slower rate. If the remote modem is HST-compatible and
   operating at a higher speed, both modems shift up and train at **9600 bps**.
   From there, depending on the speed of the remote modem, they shift up again
   to 12K bps, 14.4K bps, and again to 16.8K bps; or they fall back to 7200 bps
   and again to 4800 bps, if the remote modem is operating at one of those
   lower speeds."
3. "Initially, the high-speed channel is allocated to the **answering modem**,
   the 450-bps channel to the calling modem. This allocation reverses if the
   calling modem has more data to transmit than the answering modem."
4. Error control is negotiated *during the 2400 bps handshake*. "HST modems, if
   unable to establish an error control connection, drop to 2400 bps" [M2 ch.4
   &Mn]; with `&N0` and no error control: "it only connects at 2400 bps or
   lower. This ensures that there will not be data transfer at high speeds
   unless the call is protected by error control" [M2 App. A].
5. Direct high-speed connect (no 2400 handshake): both ends `B1 &B1 &M0 &N6/7/8/9`
   with identical fixed rate: "there can be no error control because HST modems
   negotiate error control during 2400 bps handshaking" and "Without error
   control, there are no high speed line reversals. The high speed channel is
   always allocated to the answering modem. Also, there is no online fallback"
   [M2 App. A "Specialized High-Speed Connect"].
6. 1986 manual (9600 only): "the modems negotiate the link connection ('shake
   hands') at 2400 bps and automatically resume operation at 9600 bps. The
   lower handshaking rate maintains compatibility with the existing telephone
   network" [M1 ch.3, p.3-2]; "It's possible for two HST-compatible modems to
   connect directly at 9600 ... both modems must be set ... Fixed link rate of
   9600 bps (&N6) ... Error control, either &M4 or &M5" (1986 manual says
   &M4/&M5 here; Rev B says &M0; this is a discrepancy between manual
   editions).
7. Front panel: "HS ... OFF during HST-mode link negotiations at 2400 bps, then
   ON during connection"; "AA ... in HST mode, goes OFF if the channel is
   reversed and your answering modem transmits at 450 or 300 bps" [M2 ch.5
   front panel indicators].

Answer tones / fallback ([M2], [M3]):

* `B1` (HST default): HST handshake with **Bell 2225 Hz answer tone** (US/
  Canada), no V.32/V.32bis answer tones. `B0`: CCITT V.25 2100 Hz answer
  sequence plus V.32 tones ("required to answer V.32/V.32 bis"; also required
  for answering overseas calls at 1200 bit/s and up) [M2 ch.6 "Handshaking
  Options (Bn)"; App. C "Bn"]. "Set Courier HST modems to B0 to answer
  HST-mode international calls. The B0 setting does not affect the modem's
  ability to call or answer domestic modems. It takes slightly longer."
* A Dual Standard modem at B1 "does not send out any V.32 bis or V.32 answer
  tones"; at B0 "will switch to HST mode for that call only if the other modem
  is operating in HST mode" [M2 App. A "Dual Standard Handshaking"]. Dual
  Standard modems "always connect with each other, either in HST mode or in V.32
  bis mode".
* HST-to-V.32bis modems: "A Courier HST modem cannot establish a high-speed
  connection with a Courier V.32bis modem. They can only connect at 2400 bps"
  [S4 II.3].
* Compatibility list (what an HST Courier falls back to): V.22bis 2400, V.22
  1200, Bell 212A 1200, Bell 103 300/110, V.21 300, V.23 1200/75 [M2 App. H].
  Troubleshooting text: HST needs the remote to be "HST compatible, V.32
  terbo compatible at 14.4 Kbps, V.32 compatible at 9600 bps, V.22 bis
  compatible at 2400 bps, Bell 212A-compatible at 1200 bps, or Bell
  103-compatible at 300 bps" [M4 ch.17 "Troubleshooting"].
* Unknown/non-MNP callers: "If the remote modem doesn't recognize an MNP Link
  Request, error control isn't possible. (In HST asymmetrical mode, USRobotics
  modems use a proprietary scheme similar to MNP.)" [M2 App. A "MNP
  Handshaking"]. `S27 = 16` (bits 4/5) turns off MNP handshaking, since "A non-MNP
  modem may misinterpret the MNP link request" (see section 2) [M1 ch.5; M4].
* The Courier "first attempts a connection using V.42 (LAPM) error control
  and, if that doesn't succeed, it attempts an MNP connection. If that doesn't
  succeed, the Courier tries to connect without error control ... The exception
  ... HST modems, at high speeds, do not use V.42 error control. They use
  USRobotics' proprietary HST technique" [M2 ch.4 `&Mn`].

### 1.5 Channel reversal and rate-change timing (patents, PRIMARY)

US 5,008,901 [P1]:

* Rule: "direction of the high-speed channel is dynamically reversed whenever
  the modem currently transmitting over the backchannel accumulates more than a
  predetermined maximum backlog of untransmitted data." Backlog threshold:
  "largest frame that can be sent [on the back channel] contains **64 bytes**.
  In the event that more than 64 bytes of data is ready to be sent over the
  backchannel, the modem ... verifies that the far modem has had the benefit
  of the high speed channel for **at least a predetermined minimum period of
  time (e.g. 2 seconds)**, and if it has, the backlogged modem issues a link
  command (a type 4 LN Link Protocol Data Unit) to initiate a direction
  reversal, which is essentially transparent to the remainder of the link
  management protocol."
* "A LN type 4 indicates a channel reversal at the current speed. It is sent by
  the back channel. A LNA is sent at the current speed, and then the reversal
  takes place."
* Claims 1-3: reversal command sent over the low-speed channel when queued data
  exceeds a predetermined amount; inhibited if the modem has been transmitting
  on the low-speed channel for less than a minimum interval; the remote modem
  "responds with a command acknowledgement signal, whereupon said modem which
  sent said channel reversal command begins transmitting over said high speed
  channel and said remote modem begins transmitting over said low speed
  channel." Claim 4: fixed-length back-channel frames with a frame-id naming
  the data length so the CRC can be checked without waiting for the closing flag.
  Claim 5: a repeat command for repetitious data groups (LTR/LTDR).
* High-speed transmit buffer flow control to the DTE: "if the forward channel
  buffer becomes filled to more than 90% of its capacity, it signals the
  connected DTE (by lowering CTS ... or XOFF) ... When the transmit buffer is
  emptied to less than half-full, the modem ... signals the DTE to resume."
* Also, when the patent lists AT-command data-rate behaviour, `&Nn`: 0 normal,
  1 300, 2 1200, 3 2400, 6 9600 [P1 "AT EXTENDED COMMANDS"].

US 4,890,316 [P2] (adaptive speed; adds 450 bps back channel: "at 300 (or 450
bps) in the opposite direction"):

* Receiver monitors Viterbi error metric; the routine RENORM sums sequential
  metric values and compares to a threshold, setting boolean `RSCATTER` for the
  supervisor "(performed ... during high-speed operation at 7200 bit/s and
  higher)". At 4800 bit/s (not trellis coded) the equalizer eye-scatter value
  (ERRR/ERRI) is used instead.
* **Upshift**: "Decisions about upshifts are made at **10 second** intervals. If
  the signal quality, as indicated by RSCATTER is good for **90%** of that 10
  second interval, a speedup is requested." (procedure `CHECK_BLER` in module
  HS). Speed-up request inhibited for an interval after a speed-down request.
* **Downshift triggers**: (a) failed retrain: "`DETERMINE_FALLBACK` ... counts
  failed attempts to retrain, and initiates a speed reduction whenever
  **three failed attempts** are made in a **ten second** interval." (b) frame
  errors: "Speed reductions are requested whenever **15 frame errors** occur
  within a given **10 second** interval. These ten second intervals commence
  upon the first occurrence of an error". Frame errors are CRC errors on MNP
  frames *plus* "**50 millisecond** long blocks of fill characters which are
  treated as frames when no MNP frames are being received".
* **Signalling**: the request is an MNP **LN** frame on the back channel whose
  TYPE field is the action; attention codes (equates in module HS):
  `atn_reverse_9600 equ 14h` (high-speed channel reversal at 9600 baud),
  `atn_retrain_7200 equ 23h` (fall back to 7200 with retrain),
  `atn_upshift_9600 equ 34h` (speed increase to 9600). (Only these three
  examples are given; the encoding of the full code space is a GAP. The high
  nibble appears to be the action class and the low nibble the rate index, but
  that is my reading, not stated.)
* **Execution on the forward channel**: "A request for a high speed transmission
  rate change is a mandatory command which is executed as follows: at the
  conclusion of the frame currently being transmitted, a **15 millisecond
  sustained mark signal** is sent over the high speed channel at the current
  speed, followed by a **15 millisecond constant mark signal at the newly
  requested speed**. Note that, under the MNP protocol, the constant mark signal
  is not otherwise sent, since fill characters having unique value are sent
  when no data frames are being transmitted." (Claim 6: "a new rate command"
  sent over the high speed channel.)
* The ARQ stream therefore has idle "fill characters having unique value"
  between frames, and receivers treat 50 ms of fill as a "block" for error
  accounting.

Other facts from the same patents:

* Inter-frame error accounting uses MNP-style CRC-16 on every frame.
* Call-progress detection thresholds the supervisor uses (answer tone 2150-
  2500 Hz; voice 1130-2000 Hz for 0.4 s; dial tone 300-800 Hz >2.7 s; ring
  back 0.7-2.7 s; busy 0.2-0.6 s pulses) [P1 "Call progress detection"].
* Data compression in the 1988 design: "the routine ENCODE executed by the
  Supervisor ... provides such data compression using the standard MNP Level 5
  compression technique" [P2 "Data Compression"].
* LPDU names/classes: standard MNP "six types of frames" LR, LD, LT, LA, LN,
  LNA [P1].

---

## 2. User-visible behaviour

### 2.1 AT commands and registers that control HST

(all from [M2] Rev B manual unless otherwise stated; [M1] = 1986 9600 manual)

* **`Bn`**: "used primarily to select HST or V.32 bis operation". `B1` required
  for HST; selects the Bell 2225 Hz answer tone; default for the Courier HST and
  for the HST Dual Standard in some documents (the Rev B glossary lists Dual
  Standard factory default as B0 in ch.4 and "Courier HST Dual Standard: Default"
  under B1 in App. C; the two statements conflict; treat as unresolved). `B0`
  is V.32/V.32bis + CCITT V.25 2100 Hz answer sequence; for overseas calls.
* **`&Nn`**: link rate (see 1.1). `&N0` normal, "negotiate highest available
  speed with remote modem". `&N6..&N9` = fixed 9600/12K/14.4K/16.8K. If the
  link rate is locked and the remote differs, the modem hangs up.
* **`&Bn`** (DTE rate fixed): required `&B1` and DTE 19.2K/38.4K/57.6K for
  14.4K/16.8K connects.
* **`&Mn`**: `&M0` no error control; `&M4` normal/ARQ (default); `&M5` ARQ only
  (hang up if no error control). Note for HST: "HST modems, if unable to
  establish an error control connection, drop to 2400 bps."
* **`&An`** result-code level: `&A0` plain; `&A1` adds `/ARQ`; `&A2` adds
  modulation `/HST` or `/V32`; `&A3` adds protocol `/HST`, `/LAPM`, `/MNP`,
  `/NONE`, `/SYNC` and compression `/V42BIS` or `/MNPS`.
* **`&Kn`, `&Hn`, `&In`, `&Yn`**: compression/flow/break (HST compression:
  "Courier modems under V.42 or HST error control use V.42 bis compression"
  [M2 ch.1, p.1-2]; the 1988-patent embodiment used MNP5.)
* **`S13`** (bit-mapped) bit 5 (value 32): "Disable HST (used for testing V.32 bis
  in Dual Standard modems)"; bit 6 (64): "Disable MNP Level 3 (used for testing
  Level 2)"; bit 0: reset on DTR drop; bit 1: reverse normal auto-answer; bit 2:
  disable 250 ms pause before result code; bit 3/4: auto dial on DTR / power on.
  [M2 App. B]. (1986: bit 6 "Disable MNP Service Class 3" [M1 App. B].)
* **`S15`** (bit-mapped, Rev B) [M2 App. B]:
  bit 0 (1) disable extra high-frequency equalization (HST only; "short" vs
  "long" equalization shown in `ATI6`); bit 1 (2) **disable online fallback**;
  bit 2 (4) **disable 450 bps back channel**; bit 3 (8) non-ARQ transmit buffer
  1.5K to 128; bit 4 (16) disable MNP Level 4; bit 5 (32) backspace key for
  delete; bit 6 (64) workaround for non-compliant 2400 bps MNP modems; bit 7
  (128) custom. Early Courier HST (stason.org settings page for the 9600 unit
  [S6]) lists the same bits with an enable/disable polarity table: "0: HST
  high frequency equalization enabled/disabled; 1: online fallback; 2: 450bps
  back channel; 3: non-error-correcting transmit buffer 1.5KB or 128; 4: MNP 4;
  5: backspace/delete; 6: MNP adjusted for non compliant modems; 7: proprietary
  network compatibility". The 1986 manual lists S15 as "Reserved" [M1].
* **`S27`** (Rev B) [M2 App. B]: bit 0 (1) V.21 300 for overseas; bit 1 (2)
  unencoded V.32; bit 2 (4) disable V.32 (HST-testing on Dual Standard); bit 3
  (8) disable 2100 Hz answer tone ("to allow two V.42 modems to connect more
  quickly"); bits 4+5 "Error control handshaking options: select the total
  values of bits 4 and 5": 0 = complete sequence (V.42 detection, LAPM, MNP);
  16 = disable MNP; 32 = disable V.42 detection and LAPM; 48 = disable the
  detection phase only ("if you know that the remote modem does LAPM, but not
  the Detection phase"); bit 6 reserved; bit 7 (128) "Some software may not accept 7200,
  12000, 14400 and 16800 bps result codes. This setting disables the codes and
  displays the 9600 code instead."
* **`S34`** bit 0 (1) disable V.32 bis; bit 1 (2) disable proprietary V.32bis
  modulation; bit 2 (4) faster retrains (V.32 bis) [M2].
* **`S17`** (disconnect reason during ARQ), values 1-6, 254, 255 ("Received
  data different than requested", "Incompatible MNP protocol", "Connection
  request parameters unknown", "Remote modem retry timeout", "Inactivity
  timeout", "Destination user not found", 254 "Peer protocol error", 255
  "User initiated disconnect") and **`S20`** (reason for NO CARRIER 0-7) are
  documented for the 9600 Courier HST [S6]. These S17 values correspond to MNP
  LD reason codes (254 and 255 match V.42 Annex A reason 255 for user
  disconnect).

### 2.2 Result codes (X-level and numeric, V.0)

From [M2] ch.5 "Result Codes" (page 5-5/5-6), numeric (V0) / verbal:

* Plain: 15 CONNECT 1200/ARQ, 16 CONNECT 2400/ARQ, 17 CONNECT 9600/ARQ,
  19 CONNECT 4800/ARQ, 22 CONNECT 12000/ARQ, 24 CONNECT 7200/ARQ,
  26 CONNECT 14400/ARQ, 47 CONNECT 16800/ARQ; 14 CONNECT/ARQ with X0.
* `&A2` modulation codes: 23 CONNECT 9600/HST, 27 CONNECT 9600/ARQ/HST; 28/29
  4800 HST (no/ARQ), 30/34 7200, 31/32 12000, 35/36 14400, 53 CONNECT 16800/HST,
  and ".../ARQ/HST" 57 (printed `57/CONNECT16800/ ARQ/HST` in the OCR) vs the V.32
  twin codes 33/37 9600, 38/39 4800, 40/44 7200, 41/42 12000, 45/46 14400.
* `&A3`: e.g. `CONNECT 16800/ARQ/HST/HST/V42BIS` (modulation/ARQ then protocol then
  compression); `CONNECT 14400/ARQ/V32/LAPM/V42BIS`; `CONNECT 9600/SYNC`;
  `CONNECT 2400/NONE`. "Although these codes will return numeric identifiers,
  they are the same numeric identifiers used for &A2 result codes."
* 1986 manual: only (15) CONNECT 1200/ARQ, (16) CONNECT 2400/ARQ, (17) CONNECT
  9600/ARQ [M1 ch.5 "/ARQ Result Codes"].
* `ATI6` link diagnostics (14400 HST Dual Standard example) [M2 ch.8 Fig 8.3]:
  fields "Chars sent / received / lost, Octets sent / received, Blocks sent /
  received / resent, Retrains Requested / Granted, Line Reversals, Blers, Link
  Timeouts, Link Naks, Data Compression (V42BIS 2048/32), Equalization Long/
  Short, Fallback Enabled/Disabled, Protocol (LAPM/MNP/HST?/NONE/SYNC), Speed
  (rx/tx), Disconnect reason". Definitions: "Line Reversals: The number of
  times HST-mode modems switched the high and low speed channels. ... Blers:
  Errors in data and protocol blocks ... Link Timeouts: Protocol detection
  problems ... Link Naks: Negative acknowledgments".
* `ATI7`: "Display product configuration" (1993+ V.Everything shows
  `Options  HST,V32bis,Terbo,VFC,V34+,x2,V90`) [M4 ch.? "Checking Your
  Courier V.Everything Modem's Software Version"]. No HST-era `ATI7` sample was
  found. `ATI0` product code (3 digits) mapping table: not extracted.
* Front-panel LEDs: HS (>2400 bps), ARQ (HST/MNP established), MR blinking when
  retraining [M1 App. F; M2 ch.5].

### 2.3 How to force HST / what an answering Courier does

* Force HST: `B1`, `&N0` (or `&N6..&N9` for fixed), `&M4`/`&M5`, DTE fixed
  19.2K+ (`&B1`). To refuse HST on a Dual Standard (test): `S13` bit 5 or `S27`
  bit 2 disable V.32.
* How a Courier (HST) answers an unknown caller: with `B1` it sends the Bell
  2225 Hz answer tone (the 2400/1200/300 compatible tone); HST-aware peers then
  do the 2400 bit/s training, negotiate error control (MNP-compatible Link
  Request exchange), and shift to 9600 and above; if the caller is a plain
  2400/1200/300 modem it stays there. With `B0` it sends V.25 2100 Hz + V.32
  tones ("takes slightly longer"). A non-MNP modem at 2400 receiving MNP link
  requests may misbehave, hence `S27=16` (disable MNP) and the
  `&M0` no-error-control mode. With `&M5` and an unrecognised error control
  request the Courier "hangs up". Answering at high speed is never done
  without error control (&N0): "it only connects at 2400 bps or lower".
* Modem-to-modem direction: the answerer owns the fast channel at connect.

---

## 3. MNP / error control

(V.42 Annex A, the MNP-compatible "alternative procedure", is **fully
available** in the 1993 and 1996 editions of V.42, which I downloaded from
itu.int (the "-S" PDFs are the full recommendation, 80 and 81 pages; the copy in
`refs/itu/T-REC-V.42-200203-I` has Annex A deleted). MNP class background and
class 5 compression are in section 4.)

### 3.1 V.42 (03/93) Annex A: alternative procedure ("MNP classes 2-4")

All values below are verbatim from [I1] Annex A, pages 47-68.

**Framing (A.3, A.4)** two modes, negotiated by the LR:
* *Start-stop octet-oriented* (framing mode 2; always used for the
  protocol-establishment phase): `SYN(0x16) DLE(0x10) STX(0x02)` start flag,
  header, information, `DLE(0x10) ETX(0x03)` stop flag, then 16-bit FCS.
  Transparency: a DLE in the body (header + info) is doubled (DLE-DLE);
  the flag DLEs are not doubled. FCS is "generated by the cyclic redundancy
  check (CRC) polynomial **x^16 + x^15 + x^2 + 1**" (CRC-16/ARC; V.42 notes
  "the CRC polynomial used in this frame mode differs from that specified in
  8.1.1.6.1" which is the CCITT one). "The frame body and ETX octet of the stop
  flag are included in the FCS calculation. The start flag and all DLE octets
  used to maintain data transparency are excluded." Each octet carries start
  and stop bits (10 bit per octet).
* *Bit-oriented* (framing mode 3): HDLC flag 0x7E, header, info, FCS (8.1.1.6.1,
  i.e. CRC-CCITT), flag; zero-insertion per 8.1.1.2. "An error-correcting entity
  that supports framing mode 3 must also support framing mode 2." Responder
  answers with the lesser mode.

**Header field (A.6.2)**: octet 0 = length indication (number of header octets
after itself; 255 = next two octets are a 16-bit extended length); octet 1 =
type; then fixed parameters; then variable parameters, each `type(1..254),
length(1), value(length)`.

Type values (Table A.1): **LR=1, LD=2, LT=4, LA=5, LN=6, LNA=7**.
(Type 3 is unused.)

**LR (Table A.2)**, no information field:

```
len, 01(LR), 02 (constant parameter 1),
  01 06 01 00 00 00 00 FF      (constant parameter 2: type1,len6,"1,0,0,0,0,255")
  02 01 <framing mode 2|3>
  03 01 <k>                    (max outstanding LT frames, <=255)
  04 02 <N401 hi?, lo>         (max info length; "two octets")
  08 01 <data phase optimization bitmap>
```
(The constant parameter 2 octet string is given in A.6.4.1.4 as
"(1,6,1,0,0,0,0,255)".) Optimisation bitmap: bit 1 = 1 means max information
field of 256 octets; bit 2 = 1 means "fixed field LT and LA frames" (the
streamlined class 4 headers); bits 3-8 reserved, zero. Negotiation (A.7.1.5):
framing mode lower of two, k lower, N401 smaller, unknown parameters ignored
(response includes only parameters both received and understood); constant
parameter 1 must be 2 or negotiation fails.

**LA (Tables A.3a/b)**: non-optimised: length **7**, type 5, `01 01 N(R)`,
`02 01 N(k)`; optimised (fixed): length **3**, type 5, N(R), N(k) as plain
octets. N(R) in the confirming LA of establishment is **0**; N(k) there equals
the credit in the response LR.

**LT (Tables A.6a/b)**: non-optimised: length **4**, type 4, `01 01 N(S)`;
optimised: length **2**, type 4, N(S) octet. Information field 1..N401 octets,
null information not allowed. V(S) starts at **1**; modulus **256**.

**LN (Table A.7)**: length **7**, type 6, `01 01 N(SA)`, `02 01 <1|2|3>`:
1 = destructive and expedited break, 2 = non-destructive expedited (non-D, E),
3 = non-destructive non-expedited. **LNA (Table A.8)**: length **4**, type 7,
`01 01 N(RA)`.

**LD (Table A.4)**: length 4 (or 7 with user code), type 2, `01 01 <reason>`
[`02 01 <user code>`]. Reasons (Table A.5): 1 "LR expected but not received",
2 "LR constant parameter 1 contains an unexpected value", 3 "LR received with
incompatible or unknown parameter value", 4-254 reserved, 255 user-initiated.
Recommendation: do not send LD on user disconnect "to promote proper
interworking with the installed base".

**Procedures**: initiator (originator) sends LR, starts T401, on reply LR
negotiates, sends confirming LA, enters data phase; responder replies LR and
waits for LA, resend LR if another LR arrives (A.7.1). Data phase: LT with
N(S)=V(S); LA when invalid frame/out-of-seq/no credit/T404 expiry/(k>1 rules);
for k=1 LA after every good LT. Selective rule: an LA whose N(R) equals the
previous LA N(R) triggers retransmission from the first unacknowledged LT;
also T401 expiry; "The first reception of an LT frame with N(S) = V(R) - 1,
however, is ignored and does not cause transmission of an LA frame." Credit:
N(k); zero credit halts the sender. LN/LNA: one outstanding LN at a time,
sequence N(SA) independent from LT sequence, V(SA) init 1.

**Timers/parameters (A.7.5)**: T401 in establishment 0.5-9 s; T401 for LT/LN:
`T401 >= 2*((k/2)*Lb*(Lf+Llt+N401) + Lb*(Lf+Lla))/bps + Trt` with Lb=10 (mode 2) or
8 (mode 3), Lf = 7 (octet) or 4 (bit-oriented) frame overhead, Llt = 5 (non-opt)
or 3 (opt), Lla = 8 (non-opt) or 4 (opt); typical 6 s at 1200 and 4 s at 2400
(N401=64), 16 s / 19 s for N401=256 (Table A.9). T402 = 0.5 T401 (LA timer);
T403 inactivity optional >=59 s; T404 flow-control timer 7 s at 1200 and 3 s at
>=2400 (Table A.10); **N400 = 12**; "Applications should support N401 = 64";
"Applications should support k = 8"; k never exceeds 255.

**Detection** (V.42 clause 7 / Appendix VI): the V.42 detection phase (ODP/ADP)
is how LAPM and the alternative procedure are discovered; an originator that
does not do detection starts with an LR; the V.42 text says "there are DCEs
currently in operation that implement the protocol defined in Annex A" and
that "it is not intended to further enhance the protocol defined in Annex A".
V.42 (03/93) clauses 1 and A.7: unsupported/mismatch handling; see the text in
`build/hst-sources/itu/v42-93.txt`.

### 3.2 What HST adds to MNP (US 5,008,901 [P1], section "Extended Link Protocol")

HST error control "is compatible with, and represents an extension to" MNP,
and uses the **same frame vocabulary**. Differences (verbatim summary):

1. "Frame formats are streamlined to eliminate redundant bytes. This is similar
   to standard MNP level 4, except that the process applies to **all frame
   types**, and the **first Length indicator byte** found in standard MNP has
   been **removed**."
2. New frame types: **NAK**, **LTA**, and (back channel only) **LT1, LT2, LT3,
   LTR, LTDR**. Various new LN types "to handle the line reversal and data rate
   switching".
3. **NAK** "functions as a double ACK frame. It implies that the particular LT
   was the last correctly received, and that later incorrectly sequenced LTs
   were detected."
4. **LTA** "is a combined Data frame (LT) and Ack frame (LA). Its purpose is to
   provide extra bandwidth for the back channel".
5. **LT1/LT2/LT3**: back channel only, fixed-size data frames with exactly 1, 2
   or 3 data bytes; "the lower 4 bits of the LT sequence number is sent in the
   **upper nybble of the frame id byte**" (FIG. 5; the figure itself is an image
   in Google Patents and was not retrievable as text).
6. **LTR / LTDR**: back channel only, fixed-size frames meaning "repeat (or double
   repeat for the LTDR) the last data frame", "sent only if the last data frame
   was <5 bytes". "In order to increase the Hamming distance of the FCS, **a
   phantom 0FFH is shifted into the CRC** before transmitting it." Rationale: auto-
   repeat of a held key and arrow-key escape sequences (the transmitter waits
   "a few ms" after an ESC to see whether a sequence follows).
7. Receivers "check the first byte in a frame for one of the above types. If it
   is, then [verify] the CRC without waiting for the closing flag." Whenever one
   of these is retransmitted "it is reformatted into a standard LT frame".
8. Frames begin and end "with unique flag codes and ... include CRC ... 16-bit
   check value" [P1].
9. The back-channel largest data frame is **64 bytes** [P1].
10. LN is used for reversal and rate switching (codes in 1.5).

Known gaps (the FIG. 5 byte layouts, the numeric frame-id values for NAK/LTA/
LT1..LT3/LTR/LTDR, the flag/escape bytes in HST mode, the HST CRC polynomial
and init value, and the exact LN type numbering) are not in any public text I
could find. The patent says the HST frames use "standard MNP rules and formats"
except for these differences, so the V.42 Annex A framing (SYN DLE STX ... DLE
ETX CRC) with the length byte dropped is the best public hypothesis, and
needs live confirmation against a real Courier.

---

## 4. MNP classes 1-5: other public descriptions

### 4.1 Class summary (Wikipedia [S7b], 1986 USR manual [M1], V.42)

* Class 1: half-duplex block mode; Class 2: full-duplex stream mode, ~84% of
  line throughput ("approximately 202 characters per second at 2400 bps");
  Class 3: Class 2 plus start/stop bit stripping and synchronous (bit-oriented
  HDLC-like) transfer, ~108% of a plain modem, ~259 cps at 2400 [M1 App. A]
  (Wikipedia: "the framing bits were turned off, improving overall efficiency by
  about 20%"). Class 4: Adaptive Packet Assembly (packets "between 64 and 256
  bytes"; the modems "constantly monitor the line for dropped packets, and if a
  certain threshold is crossed (selected by the user), the modem drops back to a
  smaller packet size") and Data Phase Optimization (some framing dropped after
  link setup) [S7b]. Class 5: data compression (typically about 1.6:1), feeding
  the MNP4 packetizer. Classes 6 (statistical duplexing, universal link
  negotiation), 7 (enhanced compression), 9, 10 (adaptive size, 10EC cellular):
  names only, no wire detail.
* Rev B manual: Courier implements MNP Levels 2-4 plus 5 (and V.42/V.42bis);
  `S15` bit 4 (16) "Disable MNP Level 4; retransmitting the larger Level 4 data
  blocks may be a problem if you expect a great number of errors" [M2];
  `S13` bit 6 (64) "Disable MNP Level 3 (used for testing Level 2)".
* Class mapping to V.42 Annex A: LR parameter 8 bit 1 (256-octet N401) and bit 2
  (fixed-format LT/LA) are what MNP calls class 4 optimisation (data phase /
  "administrative" optimisation); framing mode 3 (bit oriented) is class 3;
  mode 2 (start-stop octet) is class 2. N401 = 64 is the class 2/3 data size
  ("Applications should support N401 = 64"), 256 for class 4. (This mapping is my
  reading; V.42 does not use the class numbers.)

### 4.2 Detection phase (Wikipedia "Microcom Networking Protocol" [S7b], V.42)

* Originator Detection Pattern (ODP): DC1 with even parity (0x88), one or two
  0xFF, DC1 with odd parity (0x89), then the same count of 0xFF. Detection-phase
  timer T400. Answerer Detection Pattern (ADP): `E $FF [$FF] C $FF [$FF]` for
  error correction plus compression, or `E $FF [$FF] NUL $FF [$FF]` for error
  correction only, sent at least ten times; then L-ESTABLISH (LR/LR/LA) within T401.
  (V.42's own ODP/ADP bit patterns and the "alternative answerer detection
  patterns" are in V.42 clause 7 and Appendix VI, which are in our refs copy
  `T-REC-V.42-200203-I.txt` lines ~700 and 3058-3090.)
* US 5,802,483 (a derived implementation, not the standard) says LR and LA are
  sent asynchronously, then it "switches to synchronous mode ... SDLC frame
  structure" at class 3 or above; link-connect retry count 6, data retransmission
  count 18, window 4. Treat as implementation-specific.

### 4.3 MNP class 5 compression (Microcom patents US 5,027,376 [P4], US 4,748,638 [P5], UNB course page [S9])

Algorithm facts, quoted from [P4] (priority 1988-05-31) except where noted:

* "Each compressed character code includes three header bits and one to seven
  data bits ... made up of 4 to 10 bits with the nth shortest compressed
  character code representing the nth most frequently occurring data
  character." Each compressed code = 3-bit header + data part; "each data part of
  more than 1 bit ... will begin with a 1. ... the leading 1 ... is removed" so up
  to 8 bits fit in 7-bit locations; the decompressor restores the 1 "except if
  the header is 0".
* Tables: a 256-entry frequency (count) table, a pointer table (character ->
  rank) and inverse pointer table (rank -> character), and a code table ordered
  by code length. After each character its count is incremented; "its code is
  swapped with the next more frequent character's while its count is greater
  than or equal to" that neighbour's (done identically at both ends).
* Counter overflow: when a count "exceeds a fixed limit value ... all of the
  frequencies in the table are divided by two" (limit value not stated;
  [P4] ll. ~334, 551). Initial state: counters zeroed and loaded from a
  permanent table of "global counts of character frequencies" (table values not
  given).
* Run-length (Figs 5a/5b): "the first three of the characters are processed in
  the normal compressed manner... The characters after the first three ... are
  counted without incrementing the frequency table." When the run ends or the
  counter reaches the number of codes available "the compressor sends the
  compressed character code corresponding to the length of the run of the
  characters minus three"; the count goes through the same code table as a
  numeric value (the decompressor reads the next code "for its actual numeric
  value"), so the run count is 0 to 255 beyond the first three.
* Tokens: "11111111111 is the most infrequent character and 11111111110 is a
  token"; further tokens are created by appending bits to the 256th code.
  A bit-flush token is sent when input runs out: "The compressor appends the
  flush token to the leftover bits and pads with zeros to a whole byte"; on the
  flush token the decompressor discards the pad and continues; an exit token ends
  compressed mode (data then passes uncompressed). A 16-bit buffer is emitted in
  whole bytes, leftover bits queued.
* UNB page [S9] (secondary, cites Held's *The Complete Modem Reference*):
  codeword lengths 4-10 bits (plus 11 for the last two); the 257 codewords are
  4 at 4 bits, 4 at 5, 8 at 6, 16 at 7, 32 at 8, 64 at 9, 127 at 10, 2 at 11;
  initial table: rank n carries value n (0-3 = `0000`..`0011`, 4-7 =
  `01000`..`01011`, 8-15 = `011000`..`011111`, 16-31 start `1000000`, 32-63
  start `10100000`, 255 = `11111111110`, 256 = `11111111111` "RESET"); worked
  decode example `0100010110111000110001100011` = `01000 10110111 0001 1000110
  0011`. (The agent cross-checked this against the patent: header 000/001 -> 1
  data bit, 010 -> 2 ... 111 -> 7.) It is unconfirmed that real modems start
  from the identity table; the patent says a table of global character
  frequencies is loaded.
* Courier throughput table with MNP5 vs V.42bis (e.g. text 2325-2625 cps vs
  3400-5760 at 14.4K; random 8-bit 1460-1575 cps vs 1700) is in [M2 App. A]
  (shows MNP5 expands incompressible data, so `&K3` disables MNP5).

### 4.4 Not public (MNP)

How MNP5-compressed data is carried in LT frames (compressor state per frame vs
stream; flush/exit token numbers; the exact initial frequency table; class 5
parameters in LR beyond V.42 Annex A parameters 1-4 and 8); the CRC-16
initial value and FCS byte/bit order (A.3.6 gives only the polynomial
x^16+x^15+x^2+1; the usual MNP convention is init 0 reflected, but that is not
in any source I found); class 4 adaptive packet assembly thresholds and step
sizes; "administrative optimization" beyond the Annex A bit; classes 6-10 wire
formats. The Microcom "MNP Network Specification, Version 1.0 (15 Aug 1983)" is
cited in [P1] but not online.


---

## 5. Public recordings / spectrograms (item 4)

**Not found.** Searches for HST handshake audio or spectrograms returned only
generic dial-up material:

* windytan, "The sound of the dialup, pictured" (V.32bis-era handshake
  spectrogram, not HST): https://www.windytan.com/2012/11/the-sound-of-dialup-pictured.html
  (a BoingBoing thread on it mentions in passing that an HST handshake on a
  Courier V.Everything "was very quick"; no audio).
* Gough Lui's V.90/V.92 sound collection and modemsite.com V.90/V.92 handshakes
  (3Com Courier/Sportster V.90): not HST.
* The Internet Archive item "USR Courier 9600 HST Users Manual"
  (https://archive.org/details/usr-courier-9600-hst-users-manual/) is a manual
  scan, not audio.

Suggested next step if audio is wanted: search archive.org audio, YouTube and
BBS-era collections for "Courier HST", "USR HST 9600/14400 handshake", and ask
on Vintage Computer Federation / Gough's Tech Zone forums. Note that the
2400 bit/s V.22bis training that precedes the HST shift-up would be audible and
identifiable (Bell 2225 Hz answer tone then V.22bis), so a recording of a
2400->9600/14400 HST connect, if found, would show the shift-up and the
back-channel carrier near 375 Hz.

---

## 6. Sources

Local copies live under `/home/georgica/hst-wt/build/hst-sources/` (not committed).

### Patents
* **[P1] US 5,008,901** "Asymmetrical duplex error-controlled modem", Wallach,
  Suffern, Walsh, assigned U.S. Robotics; priority 1987-10-29 (App. 07/115,354),
  issued 1991-04-16. https://patents.google.com/patent/US5008901
  Local: `patents/US5008901.html`, extracted text `patents/US5008901.txt`.
  Used for: asymmetric architecture, AT command set, reversal rule, extended MNP
  frame types, call-progress thresholds, filters/AGC. (Assembly listings are
  omitted from Google's text: `##SPC1## ##SPC2##`. FIG. 5 frame formats and the
  filter equations are images, not in the text.)
* **[P2] US 4,890,316** "Modem for communicating at high speed over voice-grade
  telephone circuits", Walsh, Wallach (continuation-in-part of [P1]); filed
  1988-10-28, issued 1989-12-26. https://patents.google.com/patent/US4890316
  Local: `patents/US4890316.html`, `.txt`. Used for: adaptive rate change
  (10 s/90%/15 errors/3 retrains), LN attention codes, 15 ms mark signalling,
  450 bps back channel, filter parameter values, MNP5 compression statement.
* **[P3] US 5,579,305** "Asymmetric duplex modem utilizing narrow band echo
  cancellation" (HST.bis), Norrell, Hansen; filed 1994-02-09, issued 1996-11-26.
  https://patents.google.com/patent/US5579305 ; continuation US 5,892,757
  https://patents.google.com/patent/US5892757 (same description). Local:
  `patents/US5579305.*`, `patents/US5892757.*`.

### Manuals and press
* **[M1] Courier HST User's Manual, 1986**, USRobotics M610-0101-00, 129 pp.
  https://vtda.org/docs/computing/USRobotics/M610-0101-00_CourierHST_1986.pdf
  Local: `manuals/courierHST-1986-M610.pdf` and OCR text `manuals/hst1986.txt`.
  (Also on archive.org as "USR Courier 9600 HST Users Manual".) ch.1 p.1-1
  (asymmetric, MNP classes 1-3), ch.3 p.3-2 (2400 handshake), App. A (error
  control, MNP classes 1-3, frame/CRC), App. B (S13, S15 reserved), App. F.
* **[M2] Courier High Speed Modems User's Manual 1.015.544 Rev B**
  (Courier HST, HST Dual Standard, V.32 bis; ASL, 16.8K). 
  https://vtda.org/docs/computing/USRobotics/1.015.544_USRCourierHighSpeedModemsUsersManualRevB.pdf
  Local: `manuals/courier-hs-manual-revB.pdf`, OCR `manuals/hs.txt` (OCR noise:
  "SOO" = 4800, "S" for 8 etc.; cross-checked with M3/M4).
  Sections: ch.4 (B, &N, &M, high speed modulation), ch.5 (result codes,
  front-panel), ch.8 (ATI6), App. A (link negotiation, error control,
  throughput), App. B (S registers), App. C (command summary), App. H (specs).
* **[M2b] USR Courier quick reference card 1992** (1.015.871B).
  https://vtda.org/docs/computing/USRobotics/1.015.871B_USR_Courier_QRC_1992.pdf
  Local `manuals/courier-qrc-1992.pdf`, `manuals/qrc.txt`. Confirms S13 bit 32
  "Disable HST", S15 equalization/fallback/back channel bits, `&N` 0-9.
* **[M3] Courier V.34 Manual (1994)** USR, text version.
  http://www.textfiles.com/computers/courierv34man.txt  Local
  `manuals/courierv34man.txt`. App. K "Transmitter Carrier Frequencies" and
  "Receiver Carrier Frequencies" (lines ~8246-8365), glossary "Asymmetrical
  Modulation" (450 bps back channel, dynamic reversal), S15 bit 2 "Disable 450
  bps back channel--HST only".
* **[M4] Courier V.Everything Command Reference (3Com, ~2000)**
  https://unavco.knowledgebase.co/assets/108/USR_modem_manual.pdf Local
  `manuals/usr_modem_manual.pdf`, `manuals/ve.txt`. ATI7 sample; HST/B setting
  advice; S15 and S13/S27 bit tables; ch.17 troubleshooting; HST glossary.
  Online equivalents: https://support.usr.com/support/3453b/3453b-crg/appd%201-s%20registers.html
  and https://support.usr.com/support/3453c/3453c-ug/s_registers.html
* **[S4] Patrick Chen, "What You Need To Know About Modems" v1.0, 25 Dec 1991** /
  hylafax "Modems" book II.2.c, II.3.
  https://legacy.hylafax.org/site2/Modems-PeterChen/02protocols.html Local
  `manuals/hylafax-protocols.html`. HST history dates, the HST/V.32bis
  incompatibility (2400 only). Secondary.
* **[S5] Mike Ehlert, "A Comparison of High Speed Modems" 9600DATA.TXT v1.4/1.5
  (Feb/Mar 1991)**. http://cd.textfiles.com/nfbfiles/modems/9600DATA.TXT
  (mirror http://databarn.cow.net/bbs/9600data.txt). Local `manuals/9600DATA.TXT`,
  `web/9600data-cow.txt`. Secondary; popular description of "high speed
  channel with 300 (later 450) bps back channel for ACK/NAK". (Date claim "1984"
  conflicts with the 1986 manual.)
* **[S6] stason.org "COURIER HST modem Settings and Configuration"**
  https://stason.org/TULARC/pc/modems-telephone-ISDN/U-Z/U-S-ROBOTICS-INC-Modem-COURIER-HST.html
  Local `web/stason-hst.html`. Early 9600 Courier HST's S13/S15/S17/S20/&N/&M
  tables. Secondary compilation (appears derived from a USR sheet).
* **[S7] Wikipedia "USRobotics"** https://en.wikipedia.org/wiki/USRobotics
  (HST 1986, 14.4k 1989, 16.8k 1992). **[S7b]** Wikipedia "Microcom Networking
  Protocol" https://en.wikipedia.org/wiki/Microcom_Networking_Protocol (class
  summaries, ODP/ADP); local `mnp/wiki.txt`.
* **[P4] US 5,027,376** (Microcom, Friedman/Melhorn, MNP5 compression)
  https://patents.google.com/patent/US5027376 local `mnp/US5027376.txt`;
  **[P5] US 4,748,638** https://patentimages.storage.googleapis.com/38/c1/e6/4f464c2bbfd716/US4748638.pdf
  local `mnp/US4748638.pdf` (OCR is garbled two-column); US 5,802,483
  https://patents.google.com/patent/US5802483 local `mnp/US5802483.txt`;
  EP0283735 in `mnp/` (not used).
* **[S9]** UNB ECE course page "MNP5 Data Compression"
  https://www.ece.unb.ca/tervo/ee4253/mnp5.shtml local `mnp/unb_mnp5.txt`.
* **[S8] vintage wiki "Baud does not equal BPS"** (Higher Intellect/preterhuman):
  claims 2400 baud forward, 300/450 baud back; **not fetched** (Cloudflare);
  unverified. https://wiki.preterhuman.net/Baud_does_not_equal_BPS

### ITU
* **[I1] ITU-T Rec. V.42 (03/93)** full text (80 pp), Annex A alternative
  procedure on pp. 47-68.
  https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.42-199303-S!!PDF-E&type=items
  Local `itu/V42-199303.pdf`, text `itu/v42-93.txt` (Annex A at lines 3100-3600).
* **[I2] ITU-T Rec. V.42 (10/96)** full text (81 pp) with Annex A and
  Appendix V. https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.42-199610-S!!PDF-E&type=items
  Local `itu/V42-199610-S.pdf`, `itu/v42-96.txt`.
* `/home/georgica/v90d/refs/itu/T-REC-V.42-200203-I.pdf` (read only): Annex A and
  Appendix V "deleted ... in the 2002 revision"; abbreviations list still names
  LA/LD/LN/LNA/LR/LT; Appendix VI lists alternative answerer detection patterns.
  Also has V.22bis (11/88), V.32bis (02/91), V.42bis (01/90), V.25, V.21, V.22
  that cover HST's fallback modes.

---

## 7. Biggest gaps

1. **No public HST forward-channel modem spec**: symbol rate, constellation
   mapping and bits per symbol for 7200/9600/12000/14400/16800, trellis code
   (the patent only says "the trellis encoding function specified in the V.32
   standard" for the 9600 TCM design), scrambler polynomial, pulse shaping,
   carrier/phase tracking, and the training/retraining sequences and the tones
   that signal start of the HST (the "shift up to 9600" signalling) are not in
   any patent text, manual or press item found.
2. **Back-channel modulation**: only carrier (350/375 Hz) and rate (300/450
   bit/s) are public; symbol rate, constellation (the secondary snippet says
   differential, unverifiable) and scrambler are not.
3. **Exact HST link-layer bytes**: FIG. 5 of US 5,008,901 (frame id values for
   NAK/LTA/LT1-3/LTR/LTDR, sync/flag bytes, CRC init) is an image; the text
   gives only the rules above. The numeric LN attention type encoding beyond
   `14h/23h/34h` is not public.
4. **Handshake signalling at 2400 bps**: manuals describe the order but not the
   exact tones/bits used to announce HST capability on top of V.22bis.
5. **Channel-reversal on-line training**: only timing rules (2 s minimum, 64
   byte backlog, LN type 4/LNA) are public; the on-line retrain sequence for
   classic HST is not (HST.bis has one in US 5,579,305).
6. **No audio/spectrograms** of HST calls.
7. Primary OCR quality in the Rev B manual is imperfect (digit/letter
   confusions); I cross-checked critical values against the V.34 text manual
   (M3) and V.Everything manual (M4).
