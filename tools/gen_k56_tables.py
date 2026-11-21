#!/usr/bin/env python3
"""Generate modem/k56tab.c: K56flex's data-mode tables, per rate code
(1..13, 32000 to 56000 bit/s) and pad (0 none, 1 3 dB, 2 6 dB), as data.

K, u and r of each rate and M (the rings) per pad are the facts'
(docs/history/k56-facts.md, "Tables") and are checked here. The ring lists
(which constellation group each shell-mapper ring is) and the
constellations (the mu-law magnitude index of each point) follow no rule
we found (k56-facts.md "Tables": a minimum-distance ladder reproduces 25 of
the 39 constellations only with its first point and spacing given per
table, and none of the ring lists is in order of group energy). So they
are taken as D-Modem published them: its reading of the tables of
Motorola's SM56 driver (sm56.ko). The input is D-Modem's
dsp/k56flex_tables.cpp (github.com/D-Modem/D-Modem, AGPL-3.0, at
c06a380d), kept outside this repository; this script reads its numbers
only (the rate rows, each ring_<pad>_<kbit/s> and bios_<pad>_<kbit/s> list)
and writes them in our own layout. Nothing of its code is carried over.

The checks: K, u, r and M per pad against the facts; the frame identity
K + 8 (u - 1) + 8 - 8 / r = R / 1000; M^8 >= 2^K; each ring list a
permutation of 0 .. M - 1; each constellation M << (u - 1) magnitude
indices, each 0..127, strictly increasing.

Run: python3 tools/gen_k56_tables.py <D-Modem>/dsp/k56flex_tables.cpp > modem/k56tab.c
"""
import re
import sys

NRATES, PADS = 13, 3
# k56-facts.md "Tables", per rate code r = 1..13
FACT_K = [28, 30, 24, 26, 26, 28, 30, 24, 25, 27, 29, 31, 25]
FACT_U = [1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4]
FACT_R = [2, 2, 2, 2, 4, 4, 4, 4, 8, 8, 8, 8, 8]
FACT_M = [[13, 15, 9, 11, 11, 13, 15, 9, 10, 11, 13, 16, 9],
          [13, 15, 9, 11, 11, 13, 15, 9, 10, 11, 14, 16, 10],
          [13, 15, 9, 11, 11, 13, 15, 9, 10, 11, 13, 16, 10]]


def die(msg):
    sys.stderr.write("gen_k56_tables: " + msg + "\n")
    sys.exit(1)


def read(path):
    text = open(path, encoding="ascii").read()
    lists = {}
    for m in re.finditer(r"\b(ring|bios)_(\d)_(\d\d)\[\]\s*=\s*\{([^}]*)\}", text):
        key = (m.group(1), int(m.group(2)), (int(m.group(3)) - 30) // 2)
        if key in lists:
            die("%s_%s_%s twice" % m.group(1, 2, 3))
        lists[key] = [int(v) for v in re.findall(r"\d+", m.group(4))]
    # the rate rows: K, the shaping period r, u, then M per pad (D-Modem's order)
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*\{\s*(\d+),\s*(\d+),\s*(\d+)\s*\}\s*\}", text)
    if len(rows) != NRATES:
        die("%d rate rows, %d expected" % (len(rows), NRATES))
    rates = [dict(K=int(w[0]), r=int(w[1]), u=int(w[2]), M=[int(x) for x in w[3:6]]) for w in rows]
    for kind in ("ring", "bios"):
        for pad in range(PADS):
            for rc in range(1, NRATES + 1):
                if (kind, pad, rc) not in lists:
                    die("no %s_%d_%d" % (kind, pad, 30 + 2 * rc))
    if len(lists) != 2 * PADS * NRATES:
        die("%d lists, %d expected" % (len(lists), 2 * PADS * NRATES))
    return rates, lists


def check(rates, lists):
    for i, t in enumerate(rates):
        rc = i + 1
        where = "%d bit/s" % (30000 + 2000 * rc)
        if (t["K"], t["u"], t["r"]) != (FACT_K[i], FACT_U[i], FACT_R[i]):
            die(where + ": K, u, r other than the facts'")
        if t["M"] != [FACT_M[p][i] for p in range(PADS)]:
            die(where + ": M other than the facts'")
        if t["K"] + 8 * (t["u"] - 1) + 8 - 8 // t["r"] != 30 + 2 * rc:
            die(where + ": the frame identity fails")
        for pad in range(PADS):
            M = t["M"][pad]
            if M ** 8 < 1 << t["K"]:
                die(where + " pad %d: M^8 < 2^K" % pad)
            ring = lists[("ring", pad, rc)]
            if sorted(ring) != list(range(M)):
                die(where + " pad %d: the ring list is not a permutation of 0..M-1" % pad)
            bios = lists[("bios", pad, rc)]
            if len(bios) != M << (t["u"] - 1):
                die(where + " pad %d: %d points, M << (u - 1) expected" % (pad, len(bios)))
            if any(b > 127 for b in bios) or any(bios[k + 1] <= bios[k] for k in range(len(bios) - 1)):
                die(where + " pad %d: a magnitude above 127, or not increasing" % pad)


def pool_out(out, name, kind, rates, lists):
    pool, at = [], []
    for pad in range(PADS):
        row = []
        for rc in range(1, NRATES + 1):
            row.append(len(pool))
            pool.extend(lists[(kind, pad, rc)])
        at.append(row)
    if len(pool) > 0xffff:
        die("pool too large for its offsets")
    out.append("static const uint8_t %s_pool[%d] = {" % (name, len(pool)))
    for pad in range(PADS):
        for rc in range(1, NRATES + 1):
            vals = lists[(kind, pad, rc)]
            out.append("    /* pad %d, %d bit/s */" % (pad, 30000 + 2000 * rc))
            for k in range(0, len(vals), 16):
                out.append("    " + " ".join("%3d," % v for v in vals[k:k + 16]))
    out.append("};")
    out.append("static const uint16_t %s_at[3][K56_NRATES] = {" % name)
    for row in at:
        out.append("    { " + ", ".join("%4d" % v for v in row) + " },")
    out.append("};")
    out.append("")
    return len(pool)


def main():
    if len(sys.argv) != 2:
        die("usage: gen_k56_tables.py <D-Modem>/dsp/k56flex_tables.cpp")
    rates, lists = read(sys.argv[1])
    check(rates, lists)

    out = []
    out.append("/* GENERATED by tools/gen_k56_tables.py - do not edit.")
    out.append(" *")
    out.append(" * K56flex's data-mode tables per rate code (1..13) and pad (0 none, 1 3 dB,")
    out.append(" * 2 6 dB). The ring lists and the constellations are as published by")
    out.append(" * D-Modem, github.com/D-Modem/D-Modem dsp/k56flex_tables.cpp at c06a380d,")
    out.append(" * read from Motorola's SM56 driver: the values only, in our own layout.")
    out.append(" * K, u, r and M are the facts' and checked against them. See")
    out.append(" * docs/history/k56-facts.md \"Tables\" and docs/limits.md \"Included tables")
    out.append(" * (K56flex)\". */")
    out.append('#include <stddef.h>')
    out.append('#include "k56tab.h"')
    out.append("")
    out.append("/* K, u, r, M per pad (0, 3, 6 dB) */")
    out.append("const k56_rate_t k56_rates[K56_NRATES] = {")
    for i, t in enumerate(rates):
        out.append("    { %2d, %d, %d, { %2d, %2d, %2d } },    /* %d bit/s */"
                   % (t["K"], t["u"], t["r"], t["M"][0], t["M"][1], t["M"][2], 32000 + 2000 * i))
    out.append("};")
    out.append("")
    out.append("/* the group of each ring, M[pad] octets at ring_pool[ring_at[pad][rate - 1]] */")
    n_ring = pool_out(out, "ring", "ring", rates, lists)
    out.append("/* the magnitude index of each point, M[pad] << (u - 1) octets at")
    out.append(" * bios_pool[bios_at[pad][rate - 1]] */")
    n_bios = pool_out(out, "bios", "bios", rates, lists)
    out.append("const uint8_t *k56_ring(int rate, int pad)")
    out.append("{")
    out.append("    if (rate < 1 || rate > K56_NRATES || pad < 0 || pad > 2)")
    out.append("        return NULL;")
    out.append("    return ring_pool + ring_at[pad][rate - 1];")
    out.append("}")
    out.append("")
    out.append("const uint8_t *k56_bios(int rate, int pad)")
    out.append("{")
    out.append("    if (rate < 1 || rate > K56_NRATES || pad < 0 || pad > 2)")
    out.append("        return NULL;")
    out.append("    return bios_pool + bios_at[pad][rate - 1];")
    out.append("}")
    print("\n".join(out))
    sys.stderr.write("gen_k56_tables: %d ring octets, %d constellation octets\n" % (n_ring, n_bios))


main()
