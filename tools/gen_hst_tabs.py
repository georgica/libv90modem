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
  hst_mf450      the receiver's matched filter: the transmit pulse over +-4
  hst_mf300      symbols, read at the receiver's 1600 Hz samples, one row a
                 strobe phase between two samples (15 at 450, 5 at 300), so
                 that a strobe is a plain dot product.
  hst_bbpf       the receivers' band-pass [OURS], evaluated only at the
                 receivers' 1600 Hz instants (a decimation filter): it rejects
                 the bands that fold into the back channel's 274-476 Hz there,
                 1124-1326, 1874-2076, 2724-2926 and 3474-3676 Hz, at least 60
                 dB, and is flat to +-0.5 dB over 250-500 Hz. Nothing else:
                 476-1124 Hz (the forward channel's lower edge, its echo) and
                 the low band (hum) are left to the matched filter. A Kaiser
                 low-pass, the fewest odd taps that meet it with Q15
                 coefficients; for that length, the cut-off (700-900 Hz, 10
                 Hz steps) and window (beta 3.0-8.0, 0.2 steps) with the
                 deepest alias bands (checked here, and in
                 test/test_hst_back.c).
  hst_dbpf       the detector's band [OURS], at 1600 Hz on hst_bbpf's output:
                 250-500 Hz within +-0.5 dB, at least 40 dB down at 150 Hz and
                 below and at 600-800 Hz. Kaiser band-pass, cut-offs 200 and
                 550 Hz, fewest odd taps, Q15.
  hst_hbpf       the detector's high band, for its ratio [OURS]: 600-3400 Hz
                 within +-0.5 dB, at least 40 dB down at 500 Hz and below.
                 Kaiser FIR, cut-offs 550 and 3700 Hz, fewest odd taps, Q15.

The forward channel (hst-facts.md "Forward channel"):

  hst_fpulse     the transmit pulse for v32tx_set_pulse [OURS, derived from
                 FW]: root-raised-cosine, beta 1/12, Kaiser window, on the
                 1800 Hz carrier, in v32_txh's layout ([10 phases a third of
                 a sample apart][symbol age][re, im], Q14, the carrier's
                 phase counted from the symbol's start). HST_FL, its length,
                 and the window's beta are the shortest and the first (from
                 0.5 up in quarters) that hold the firmware's points with
                 room for test/test_hst_fwd.c's measurement: flat within
                 +-0.2 dB to 1100 Hz either side of the carrier, -3 +- 0.2 dB
                 1200 Hz off, -40 dB 1400 Hz and more above it, -45 dB at 400
                 Hz and below. A window narrows the band at the -3 dB points,
                 so about 24 symbols is not enough: at 24 the -3 dB points
                 come out near -3.5 dB. Scaled so that its line RMS per unit
                 symbol gain is V.32's (v32_tx_rms_q12): v32tx's gain holds.
  hst_guard      the forward receiver's guard high-pass [OURS]: within
                 +-0.5 dB from 560 Hz up, at least 50 dB down at 480 Hz and
                 below (the spec's edges; the back channel's -20 dB band ends
                 at 476 Hz). Kaiser FIR, cut-off 520 Hz, the fewest odd taps,
                 Q15; run by hst_gf_tick (modem/hst_fwd.c), which uses its
                 symmetry: (HST_GUARD_N + 1) / 2 multiplies a sample.

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
        # the receiver's matched filter at 1600 Hz (k pulse phases a sample):
        # for a strobe `frac` phases (0 .. k-1) before the newest sample, the
        # taps on the newest-but-i0 sample and older, i0 = 1 when frac > 0;
        # the pulse over +-4 symbols, m = P .. 9P, zeros after the last
        k = P * baud // 1600
        rows_, n_ = [], []
        for frac in range(k):
            i0 = (frac + k - 1) // k
            taps = [qt[m % P][m // P] for m in range(P + i0 * k - frac, 9 * P + 1, k)]
            rows_.append(taps)
            n_.append(len(taps))
        width = max(n_)
        h.append(define("HST_MF%d_K" % bps, k, "matched filter: strobe phases a 1600 Hz sample"))
        h.append(define("HST_MF%d_N" % bps, width, "matched filter: taps at most"))
        h.append("/* [frac][t], Q14: the matched filter for a strobe frac pulse phases before")
        h.append(" * a sample, tap t on the t-th sample back from it; hst_mf%d_n[frac] taps */" % bps)
        h.append("extern const int16_t hst_mf%d[HST_MF%d_K][HST_MF%d_N];" % (bps, bps, bps))
        h.append("extern const uint8_t hst_mf%d_n[HST_MF%d_K];" % (bps, bps))
        c.append("const int16_t hst_mf%d[HST_MF%d_K][HST_MF%d_N] = {" % (bps, bps, bps))
        for r in rows_:
            r = r + [0] * (width - len(r))
            c += ["    {"] + rows(r, 12, 6) + ["    },"]
        c.append("};")
        c.append("const uint8_t hst_mf%d_n[HST_MF%d_K] = { %s };" % (bps, bps, ", ".join(str(v) for v in n_)))
        c.append("")
    return h, c


# ---------------- band-pass filters ----------------
def fir_db(h, f, fs=FS):
    re = im = 0.0
    for i, v in enumerate(h):
        re += v * math.cos(2 * math.pi * f * i / fs)
        im -= v * math.sin(2 * math.pi * f * i / fs)
    return 10 * math.log10((re * re + im * im) / (32768.0 * 32768.0) + 1e-30)


def kaiser_fir(f1, f2, n, beta, fs=FS):
    """Q15 Kaiser-windowed sinc, pass band f1..f2 (f1 = 0: a low-pass)."""
    m = (n - 1) / 2.0
    h = []
    for i in range(n):
        t = i - m
        if t == 0:
            v = 2 * (f2 - f1) / fs
        else:
            v = (math.sin(2 * math.pi * f2 * t / fs) - math.sin(2 * math.pi * f1 * t / fs)) / (math.pi * t)
        h.append(v * kaiser(t / (m + 1), beta))
    h = [q(v, 32768) for v in h]
    if any(h[i] != h[n - 1 - i] for i in range(n)):
        die("not symmetric")
    return h


def flat(h, passband, fs=FS):
    pb = [fir_db(h, f, fs) for f in range(passband[0], passband[1] + 1, 2)]
    return min(pb), max(pb)


def design(name, f1, f2, passband, stops, atten, fs=FS, trans=100.0):
    """The fewest odd taps whose Q15 Kaiser band-pass (cut-offs f1, f2; f2 =
    fs / 2 makes it a high-pass) is flat to +-0.5 dB over `passband` and
    `atten` dB down over each of `stops`; `trans`, the narrowest transition
    band in Hz, only sets where the search starts."""
    beta = 0.1102 * (atten + 2 - 8.7)
    est = int((atten + 2 - 7.95) / (2.285 * 2 * math.pi * trans / fs))     # Kaiser's estimate
    for n in range(max(est - est % 2 - 39, 11) | 1, 601, 2):
        h = kaiser_fir(f1, f2, n, beta, fs)
        lo, hi = flat(h, passband, fs)
        if lo < -0.5 or hi > 0.5:
            continue
        sb = max(fir_db(h, f, fs) for a, b in stops for f in range(a, b + 1, 2))
        if sb <= -atten - 1:
            sys.stderr.write("gen_hst_tabs: %s: %d taps, passband %.2f..%.2f dB, stop %.1f dB\n"
                             % (name, n, lo, hi, sb))
            return h
    die(name + ": no design up to 599 taps")


# the bands that fold into the back channel's 274-476 Hz at 1600 Hz
ALIAS = ((1124, 1326), (1874, 2076), (2724, 2926), (3474, 3676))


def design_decim(name, passband, stops, atten):
    """The fewest odd taps of a Q15 Kaiser low-pass flat to +-0.5 dB over
    `passband` and `atten` dB down over each of `stops`; for that length,
    the cut-off and window that put the stops deepest."""
    for n in range(11, 201, 2):
        best = None
        for fc in range(700, 901, 10):
            for b10 in range(30, 81, 2):
                h = kaiser_fir(0.0, float(fc), n, b10 / 10.0)
                lo, hi = flat(h, passband)
                if lo < -0.5 or hi > 0.5:
                    continue
                sb = max(fir_db(h, f) for a, b in stops for f in range(a, b + 1, 2))
                if best is None or sb < best[0]:
                    best = (sb, h, fc, b10 / 10.0, lo, hi)
        if best and best[0] <= -atten:
            sb, h, fc, beta, lo, hi = best
            sys.stderr.write("gen_hst_tabs: %s: %d taps (cut-off %d Hz, beta %.1f), passband %.2f..%.2f dB, "
                             "alias bands %.1f dB, 600 Hz %.1f dB, 1000 Hz %.1f dB\n"
                             % (name, n, fc, beta, lo, hi, sb, fir_db(h, 600), fir_db(h, 1000)))
            return h
    die(name + ": no design up to 199 taps")


def emit(name, macro, what, taps, h, c):
    h.append("#define %s %d" % (macro, len(taps)))
    h.append("/* %s; Q15, linear phase, gain 1 */" % what)
    h.append("extern const int16_t %s[%s];" % (name, macro))
    c.append("const int16_t %s[%s] = {" % (name, macro))
    c += rows(taps, 12)
    c.append("};")
    c.append("")


def tab_bpf():
    h, c = [], []
    emit("hst_bbpf", "HST_BBPF_N",
         "the receivers' band-pass, at their 1600 Hz instants: 250-500 Hz, >= 60 dB down "
         "at 1124-1326, 1874-2076, 2724-2926 and 3474-3676 Hz",
         design_decim("hst_bbpf", (250, 500), ALIAS, 60), h, c)
    emit("hst_dbpf", "HST_DBPF_N",
         "the detector's band, at 1600 Hz on hst_bbpf's output: 250-500 Hz, >= 40 dB down "
         "at 150 Hz and below and 600-800 Hz",
         design("hst_dbpf", 200.0, 550.0, (250, 500), ((0, 150), (600, 800)), 40, 1600.0), h, c)
    emit("hst_hbpf", "HST_HBPF_N", "600-3400 Hz, >= 40 dB down at 500 Hz and below",
         design("hst_hbpf", 550.0, 3700.0, (600, 3400), ((0, 500),), 40), h, c)
    return h, c


# ---------------- the forward channel's transmit pulse ----------------
F_BETA = 1.0 / 12             # spec decision 11 [OURS, derived from FW]
F_SPS = FS / 2400.0           # samples a symbol, 10/3
F_WC = 2 * math.pi * 1800.0 / FS
F_LENS = range(20, 49, 2)
F_WINS = [0.5 + 0.25 * k for k in range(15)]


def v32_rms_q12():
    """V.32's line RMS per unit symbol gain (v32/v32_tabs.c), which v32tx's
    gain is computed from"""
    import re
    with open("v32/v32_tabs.c") as f:
        m = re.search(r"v32_tx_rms_q12 = (\d+);", f.read())
    if not m:
        die("no v32_tx_rms_q12 in v32/v32_tabs.c")
    return int(m.group(1))


def fpulse_g(L, wb):          # the baseband pulse, three points a sample, in time order
    half = L * F_SPS / 2
    return [rrc((n / 3.0 - half) / F_SPS, F_BETA) * kaiser((n / 3.0 - half) / half, wb) for n in range(10 * L)]


def fpulse_db(g, f):          # power at f Hz from the carrier against the carrier, g complex or real
    re = im = re0 = im0 = 0.0
    for n, v in enumerate(g):
        a = 2 * math.pi * f * n / 24000.0
        vr, vi = (v.real, v.imag) if isinstance(v, complex) else (v, 0.0)
        re += vr * math.cos(a) + vi * math.sin(a)
        im += vi * math.cos(a) - vr * math.sin(a)
        re0 += vr
        im0 += vi
    return 10 * math.log10((re * re + im * im) / (re0 * re0 + im0 * im0) + 1e-30)


def fpulse_ok(g, step):
    """the design's margins inside test_hst_fwd's limits (+-0.5 dB flat,
    -3 +- 0.5 dB, -36 and -40 dB): the worst flatness, the -3 dB points,
    the most power 1400 Hz and more above, and 400 Hz and below"""
    e = [fpulse_db(g, f) for f in (-1200, 1200)]
    if max(abs(d + 3.0103) for d in e) > 0.2:
        return None
    low = max(fpulse_db(g, -f) for f in range(1400, 1801, step))
    if low > -45:
        return None
    high = max(fpulse_db(g, f) for f in range(1400, 2201, step))
    if high > -40:
        return None
    flat = [fpulse_db(g, f) for f in range(-1100, 1101, step)]
    if min(flat) < -0.2 or max(flat) > 0.2:
        return None
    return min(flat), max(flat), e, high, low


def tab_fpulse():
    for L in F_LENS:
        for wb in F_WINS:
            if fpulse_ok(fpulse_g(L, wb), 10):
                break
        else:
            continue
        break
    else:
        die("fpulse: no length up to %d symbols" % F_LENS[-1])
    g = fpulse_g(L, wb)
    # scaled so that its line RMS per unit symbol gain is V.32's: v32tx's
    # gain then holds for it (line power = sum |h|^2 / phases / 2 x mean |s|^2)
    rms_q12 = v32_rms_q12()
    k = (rms_q12 / 4096.0) / math.sqrt(sum(v * v for v in g) / 10 / 2 * 40)
    qt = [[(q(g[p + 10 * j] * k * math.cos(F_WC * (p + 10 * j) / 3.0), 16384),
            q(g[p + 10 * j] * k * math.sin(F_WC * (p + 10 * j) / 3.0), 16384)) for j in range(L)] for p in range(10)]
    got = round(math.sqrt(sum(a * a + b * b for r in qt for a, b in r) / 16384.0 ** 2 / 10 / 2 * 40) * 4096)
    if got != rms_q12:
        die("fpulse: line RMS %d, V.32's %d" % (got, rms_q12))
    # the quantised pulse, back at baseband
    gq = [complex(*qt[n % 10][n // 10]) * complex(math.cos(F_WC * n / 3.0), -math.sin(F_WC * n / 3.0))
          for n in range(10 * L)]
    r = fpulse_ok(gq, 2)
    if not r:
        die("fpulse: the quantised pulse misses")
    sys.stderr.write("gen_hst_tabs: fpulse: %d symbols, Kaiser %.2f; +-1100 Hz %.2f..%.2f dB, +-1200 Hz %.2f %.2f dB, "
                     ">= +1400 Hz %.1f dB, <= -1400 Hz %.1f dB\n" % (L, wb, r[0], r[1], r[2][0], r[2][1], r[3], r[4]))
    h = [define("HST_FL", L, "pulse length, symbols (v32.h's V32_TXL_MAX)"),
         "/* the forward pulse for v32tx_set_pulse: RRC beta 1/12, Kaiser window",
         " * (beta %g), on the 1800 Hz carrier, in v32_txh's layout: [phase, thirds" % wb,
         " * of a sample][symbol age][re, im], Q14; its line RMS is V.32's (v32_tx_rms_q12) */",
         "extern const int16_t hst_fpulse[10][HST_FL][2];"]
    c = ["const int16_t hst_fpulse[10][HST_FL][2] = {"]
    for row in qt:
        c.append("  {")
        c += ["    " + " ".join("{%6d,%6d}," % t for t in row[i:i + 6]) for i in range(0, L, 6)]
        c.append("  },")
    c.append("};")
    c.append("")
    return h, c


# ---------------- the forward receiver's guard high-pass ----------------
def tab_guard():
    taps = design("hst_guard", 520.0, FS / 2, (560, 4000), ((0, 480),), 50, trans=80.0)
    h = ["#define HST_GUARD_N %d" % len(taps),
         "/* high-pass before the forward receiver: within 0.5 dB from 560 Hz up, >= 50 dB",
         " * down at 480 Hz and below (the back channel); Q15, linear phase, gain 1 */",
         "extern const int16_t hst_guard[HST_GUARD_N];"]
    c = ["const int16_t hst_guard[HST_GUARD_N] = {"] + rows(taps, 12) + ["};", ""]
    return h, c


TABLES = (tab_bstep, tab_bpulse, tab_bpf, tab_fpulse, tab_guard)


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


if __name__ == "__main__":
    main()
