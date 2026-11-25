#!/usr/bin/env python3
"""Generate modem/hst_tabs.h and modem/hst_tabs.c: the tables of the HST
pump (docs/history/hst-facts.md; the spec's "HST pump").

The back channel (hst-facts.md "Back channel"):

  hst_bstep      the phase step, in eighths of a turn, of each tribit: the
                 firmware's constant 0x43705261 read a nibble at a time from
                 the low end (BF §3) [FW].
  hst_bpulse450  the transmit pulse [OURS]: root-raised-cosine, beta 0.35,
  hst_bpulse300  over HST_BL = 10 symbols, Kaiser window (beta 3): about 68 dB
                 down at 600 Hz, and a worst-case ISI of 1.4 degrees through
                 an ideal matched filter. Polyphase
                 for a phase accumulator at 8000 Hz: a 150 baud symbol is
                 53 1/3 samples, so the 450 table has 160 phases a symbol
                 (thirds of a sample, 3 a sample); a 100 baud symbol is 80
                 samples, 80 phases (1 a sample). Row `ph`, column j: the
                 pulse at ph / P + j - HST_BL / 2 symbols from its centre;
                 column 0 weights the newest symbol. Q14, peak 1.
                 HST_B450_RMS_Q12, HST_B300_RMS_Q12: the line signal's RMS
                 per unit symbol amplitude (random 8-PSK symbols), Q12.
                 Checked here: power at 600 Hz and above at least 60 dB
                 below the pulse's peak (1.5 symbol rates from the 450
                 carrier), and its -20 dB band.
  hst_bbpf       the receivers' band-pass [OURS]: 250-500 Hz within +-0.5
                 dB, at least 50 dB down at 600 Hz and above and at 150 Hz
                 and below (50 and 60 Hz hum, not its harmonics at 180-250 Hz,
                 which the detector's 8-PSK lock rejects instead). Meant to be
                 evaluated at the receiver's 1600 Hz instants: its stop band
                 at 600 Hz and above is what folds into 250-500 Hz there.
                 Windowed-sinc FIR (Kaiser), cut-offs
                 200 and 550 Hz, the fewest odd taps that meet it with Q15
                 coefficients (checked here, and in test/test_hst_back.c).
  hst_hbpf       the detector's high band, for its ratio [OURS]: 600-3400 Hz
                 within +-0.5 dB, at least 40 dB down at 500 Hz and below.
                 Kaiser FIR, cut-offs 550 and 3700 Hz, fewest odd taps, Q15.

To add a table: write a function that returns (header lines, source
lines) and append it to TABLES.

Run: python3 tools/gen_hst_tabs.py (from the top of the tree; writes both)
"""
import math
import sys

FS = 8000.0


def die(msg):
    sys.stderr.write("gen_hst_tabs: " + msg + "\n")
    sys.exit(1)


def i0(x):
    s, t, k = 1.0, 1.0, 1
    while t > 1e-14 * s:
        t *= (x / (2 * k)) ** 2
        s += t
        k += 1
    return s


def kaiser(u, beta):          # u in [-1, 1]
    return i0(beta * math.sqrt(1 - u * u)) / i0(beta) if abs(u) < 1 else 0.0


def rows(vals, per, width=6):
    return ["    " + ", ".join("%*d" % (width, v) for v in vals[i:i + per]) + ","
            for i in range(0, len(vals), per)]


def define(name, value, comment):
    return "%-32s /* %s */" % ("#define %s %d" % (name, value), comment)


def q(v, one):
    r = int(round(v * one))
    if r > 32767 or r < -32768:
        die("value %g out of range at scale %d" % (v, one))
    return r


# ---------------- the back channel's phase steps ----------------
STEP_WORD = 0x43705261        # BF §3 [FW]


def tab_bstep():
    steps = [(STEP_WORD >> (4 * t)) & 15 for t in range(8)]
    if sorted(steps) != list(range(8)):
        die("0x%08X is not a permutation of 0..7" % STEP_WORD)
    h = ["/* phase step of tribit t, in eighths of a turn: 0x%08X a nibble at a" % STEP_WORD,
         " * time from the low end (BF §3) */",
         "extern const uint8_t hst_bstep[8];"]
    c = ["const uint8_t hst_bstep[8] = { " + ", ".join(str(s) for s in steps) + " };"]
    return h, c


# ---------------- the back channel's transmit pulse ----------------
RRC_BETA, BL, WIN_BETA = 0.35, 10, 3.0
# speed: (baud, carrier, phases a symbol, phase step a sample)
BSPEEDS = ((450, 150, 375.0, 160, 3), (300, 100, 350.0, 80, 1))


def rrc(t, a=RRC_BETA):
    if abs(t) < 1e-9:
        return 1 - a + 4 * a / math.pi
    if abs(abs(t) - 1 / (4 * a)) < 1e-9:
        return (a / math.sqrt(2)) * ((1 + 2 / math.pi) * math.sin(math.pi / (4 * a))
                                     + (1 - 2 / math.pi) * math.cos(math.pi / (4 * a)))
    return (math.sin(math.pi * t * (1 - a)) + 4 * a * t * math.cos(math.pi * t * (1 + a))) / \
           (math.pi * t * (1 - (4 * a * t) ** 2))


def pulse(t):                 # t in symbols from the centre
    return rrc(t) * kaiser(t / (BL / 2.0), WIN_BETA)


def pulse_dtft(h, per, f):    # |H| at f symbol rates, h sampled `per` a symbol
    re = im = 0.0
    for i, v in enumerate(h):
        re += v * math.cos(2 * math.pi * f * i / per)
        im -= v * math.sin(2 * math.pi * f * i / per)
    return math.hypot(re, im)


def tab_bpulse():
    h = [define("HST_BL", BL, "pulse length, symbols")]
    c = []
    for bps, baud, fc, P, step in BSPEEDS:
        if abs(P / step - FS / baud) > 1e-9:
            die("%d: %d phases at %d a sample is not a symbol" % (bps, P, step))
        tab = [[pulse(ph / float(P) + j - BL / 2.0) for j in range(BL)] for ph in range(P)]
        pk = max(max(r) for r in tab)
        qt = [[q(v / pk, 16384) for v in r] for r in tab]
        # line RMS per unit symbol amplitude: E|baseband|^2 / 2, baseband
        # power the mean over phases of the sum of squares
        ms = sum(sum((v / 16384.0) ** 2 for v in r) for r in qt) / P
        rms = math.sqrt(ms / 2.0)
        # the quantised pulse's spectrum, in symbol rates from the carrier
        seq = [qt[ph][j] for j in range(BL) for ph in range(P)]       # in time order
        h0 = pulse_dtft(seq, P, 0.0)
        far = (600.0 - fc) / baud
        # from 600 Hz up 6 symbol rates (1500 Hz at 450, 1200 at 300), on the
        # pulse at its table rate; test_hst_back's tx_spectrum measures the
        # 8000 Hz output, aliases included, up to 4000 Hz
        worst = max(pulse_dtft(seq, P, far + k / 50.0) for k in range(0, 50 * 6))
        if 20 * math.log10(worst / h0) > -60:
            die("%d: pulse %.1f dB at 600 Hz and above" % (bps, 20 * math.log10(worst / h0)))
        edge = 0.5
        while 20 * math.log10(pulse_dtft(seq, P, edge + 0.001) / h0) >= -20:
            edge += 0.001
        sys.stderr.write("gen_hst_tabs: %d: >= 600 Hz %.1f dB, -20 dB band %.1f-%.1f Hz, rms %.4f\n"
                         % (bps, 20 * math.log10(worst / h0), fc - edge * baud, fc + edge * baud, rms))
        h.append(define("HST_B%d_P" % bps, P, "pulse phases a symbol"))
        h.append(define("HST_B%d_STEP" % bps, step, "phases a sample at 8000 Hz"))
        h.append(define("HST_B%d_RMS_Q12" % bps, round(rms * 4096), "line RMS per unit symbol amplitude, Q12"))
        h.append("/* [phase][j], Q14: %d baud, %g Hz */" % (baud, fc))
        h.append("extern const int16_t hst_bpulse%d[HST_B%d_P][HST_BL];" % (bps, bps))
        c.append("const int16_t hst_bpulse%d[HST_B%d_P][HST_BL] = {" % (bps, bps))
        c += ["    { " + ", ".join("%6d" % v for v in r) + " }," for r in qt]
        c.append("};")
        c.append("")
    return h, c


# ---------------- band-pass filters ----------------
def fir_db(h, f):
    re = im = 0.0
    for i, v in enumerate(h):
        re += v * math.cos(2 * math.pi * f * i / FS)
        im -= v * math.sin(2 * math.pi * f * i / FS)
    return 10 * math.log10((re * re + im * im) / (32768.0 * 32768.0) + 1e-30)


def bandpass(f1, f2, n, beta):
    m = (n - 1) / 2.0
    h = []
    for i in range(n):
        t = i - m
        if t == 0:
            v = 2 * (f2 - f1) / FS
        else:
            v = (math.sin(2 * math.pi * f2 * t / FS) - math.sin(2 * math.pi * f1 * t / FS)) / (math.pi * t)
        h.append(v * kaiser(t / (m + 1), beta))
    return [q(v, 32768) for v in h]


def design(name, f1, f2, passband, stops, atten):
    """The fewest odd taps whose Q15 Kaiser band-pass (cut-offs f1, f2) is
    flat to +-0.5 dB over `passband` and `atten` dB down over each of `stops`."""
    beta = 0.1102 * (atten + 2 - 8.7)
    est = int((atten + 2 - 7.95) / (2.285 * 2 * math.pi * 100.0 / FS))     # Kaiser's, 100 Hz transitions
    for n in range(est - est % 2 - 39, 601, 2):
        h = bandpass(f1, f2, n, beta)
        if any(h[i] != h[n - 1 - i] for i in range(n)):
            die(name + ": not symmetric")
        pb = [fir_db(h, f) for f in range(passband[0], passband[1] + 1, 2)]
        if min(pb) < -0.5 or max(pb) > 0.5:
            continue
        sb = max(fir_db(h, f) for lo, hi in stops for f in range(lo, hi + 1, 2))
        if sb <= -atten - 1:
            sys.stderr.write("gen_hst_tabs: %s: %d taps, passband %.2f..%.2f dB, stop %.1f dB\n"
                             % (name, n, min(pb), max(pb), sb))
            return h
    die(name + ": no design up to 599 taps")


def tab_bpf():
    h, c = [], []
    for name, macro, what, f1, f2, pb, stops, atten in (
            ("hst_bbpf", "HST_BBPF_N", "250-500 Hz, >= 50 dB down at 600 Hz and above and 150 Hz and below",
             200.0, 550.0, (250, 500), ((0, 150), (600, 4000)), 50),
            ("hst_hbpf", "HST_HBPF_N", "600-3400 Hz, >= 40 dB down at 500 Hz and below",
             550.0, 3700.0, (600, 3400), ((0, 500),), 40)):
        taps = design(name, f1, f2, pb, stops, atten)
        h.append("#define %s %d" % (macro, len(taps)))
        h.append("/* %s; Q15, linear phase, gain 1 */" % what)
        h.append("extern const int16_t %s[%s];" % (name, macro))
        c.append("const int16_t %s[%s] = {" % (name, macro))
        c += rows(taps, 12)
        c.append("};")
        c.append("")
    return h, c


TABLES = (tab_bstep, tab_bpulse, tab_bpf)


def main():
    head = "/* GENERATED by tools/gen_hst_tabs.py - do not edit. The design is in its header. */"
    hdr = [head, "#ifndef V90D_MODEM_HST_TABS_H", "#define V90D_MODEM_HST_TABS_H",
           "#include <stdint.h>", ""]
    src = [head, '#include "hst_tabs.h"', ""]
    for fn in TABLES:
        h, c = fn()
        hdr += h + [""]
        src += c + ([""] if c and c[-1] != "" else [])
    hdr.append("#endif")
    while src[-1] == "":
        src.pop()
    with open("modem/hst_tabs.h", "w") as f:
        f.write("\n".join(hdr) + "\n")
    with open("modem/hst_tabs.c", "w") as f:
        f.write("\n".join(src) + "\n")


main()
