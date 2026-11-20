#!/usr/bin/env python3
"""Generate modem/x2bank.c: x2's level banks, per pad mode (0, 1, 2) and
rate index (1..15), as data.

No rule we tried reproduces the published banks (docs/history/x2-facts.md,
"Banks"), and the Quad x2 firmware (QF060003) does not store them: its bank
constructor builds them at run time. So they are taken as D-Modem published
them, read back from that constructor run in courier-emu. The input is
D-Modem's dsp/x2_tables.cpp (github.com/D-Modem/D-Modem, AGPL-3.0), kept
outside this repository; this script reads its numbers only (the octets of
each list, and which list each position of each bank uses) and writes them
in our own layout. Nothing of its code is carried over.

Each bank's positions 1..5 share one list; position 0 may have its own. A
list is n entries, the largest level first: the level the client receives
(G.711 mu-law, positive) and the octet sent for it across the pad (mode 0:
the same). The octets go into one pool, each list where its run of octets
already is if it is there.

The checks here are the facts' (x2-facts.md "Banks"): B = 19, 22, 25, then
22 + n; MD = 5; mode 0's counts per position; index 1 is levels B
("Mapping") in modes 0 and 2; each list sorted and without repeats; the
lists hold enough digits for B bits.

Run: python3 tools/gen_x2_banks.py <D-Modem>/dsp/x2_tables.cpp > modem/x2bank.c
"""
import re
import sys

MODES, NIDX, MD = 3, 15, 5
# x2-facts.md "Banks": levels per position in mode 0 (first, then the others)
FACT_COUNTS = [(9, 9), (12, 13), (18, 18), (21, 20), (27, 22), (28, 25), (28, 29), (32, 32),
               (41, 35), (42, 40), (47, 45), (50, 51), (52, 62), (64, 64), (82, 70)]
LEVELS_B = [0xa5, 0xa8, 0xab, 0xae, 0xb3, 0xb9, 0xbf, 0xcb, 0xdf]


def die(msg):
    sys.stderr.write("gen_x2_banks: " + msg + "\n")
    sys.exit(1)


def read(path):
    text = open(path, encoding="ascii").read()
    arrays = {}
    for m in re.finditer(r"(\w+)\[(\d+)\]\s*=\s*\{([^}]*)\}", text):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", m.group(3))]
        if len(vals) != int(m.group(2)):
            die("%s: %d values, %s declared" % (m.group(1), len(vals), m.group(2)))
        arrays[m.group(1)] = vals
    # the rate table: per mode, 15 rows of B, MD, six counts, six level
    # lists and six sent lists (empty in mode 0)
    rows = re.findall(r"\{\s*(\d+),\s*(\d+),\s*\{([\d,\s]+)\},\s*\{([\w,\s]+)\},\s*\{([\w,\s]*)\}\s*\}", text)
    if len(rows) != MODES * NIDX:
        die("%d rate rows, %d expected" % (len(rows), MODES * NIDX))
    banks = []
    for k, r in enumerate(rows):
        mode, n = divmod(k, NIDX)
        counts = [int(x) for x in r[2].split(",")]
        recv = [arrays[x.strip()] for x in r[3].split(",")]
        sent = [arrays[x.strip()] for x in r[4].split(",") if x.strip()] or recv
        banks.append(dict(mode=mode, n=n + 1, b=int(r[0]), md=int(r[1]), counts=counts, recv=recv, sent=sent))
    return banks


def check(bk):
    where = "mode %d index %d" % (bk["mode"], bk["n"])
    n = bk["n"]
    if bk["b"] != ((19, 22, 25)[n - 1] if n <= 3 else 22 + n):
        die(where + ": B %d" % bk["b"])
    if bk["md"] != MD:
        die(where + ": MD %d" % bk["md"])
    for p in range(6):
        if len(bk["recv"][p]) != bk["counts"][p] or len(bk["sent"][p]) != bk["counts"][p]:
            die(where + ": counts")
    for p in range(2, 6):
        if bk["recv"][p] != bk["recv"][1] or bk["sent"][p] != bk["sent"][1]:
            die(where + ": positions 1..5 differ")
    for p in (0, 1):
        r, s = bk["recv"][p], bk["sent"][p]
        if r != sorted(set(r)) or any(o < 0x80 for o in r + s):
            die(where + ": a list not sorted, repeated or not positive")
    if bk["mode"] == 0:
        if (len(bk["recv"][0]), len(bk["recv"][1])) != FACT_COUNTS[n - 1]:
            die(where + ": counts other than the facts'")
        if bk["sent"] != bk["recv"]:
            die(where + ": mode 0 sends other octets than it receives")
    if n == 1 and bk["mode"] != 1 and bk["recv"][0] != LEVELS_B:
        die(where + ": not levels B")
    prod = 1
    for p in range(6):
        prod *= bk["counts"][p]
    if prod < 1 << bk["b"]:
        die(where + ": too few digits for B bits")


def main():
    if len(sys.argv) != 2:
        die("usage: gen_x2_banks.py <D-Modem>/dsp/x2_tables.cpp")
    banks = read(sys.argv[1])
    for bk in banks:
        check(bk)
    pool = []
    lists = []          # (recv offset, sent offset, n)

    def place(seq):
        for off in range(len(pool) - len(seq) + 1):
            if pool[off:off + len(seq)] == seq:
                return off
        pool.extend(seq)
        return len(pool) - len(seq)

    def list_id(recv, sent):
        ent = (place(recv), place(sent), len(recv))
        if ent not in lists:
            lists.append(ent)
        return lists.index(ent)

    ids = [(list_id(bk["recv"][0], bk["sent"][0]), list_id(bk["recv"][1], bk["sent"][1])) for bk in banks]
    if len(pool) > 0xffff or len(lists) > 0xff:
        die("pool or list table too large for its types")

    out = []
    out.append("/* GENERATED by tools/gen_x2_banks.py - do not edit.")
    out.append(" *")
    out.append(" * x2's level banks, per pad mode (0 none, 1 3 dB, 2 6 dB) and rate index")
    out.append(" * (1..15): read from the Quad x2 firmware QF060003, as published by")
    out.append(" * D-Modem (https://github.com/D-Modem/D-Modem, dsp/x2_tables.cpp at")
    out.append(" * bbeb8741e6c1: the firmware's bank constructor run in courier-emu). The")
    out.append(" * values only, in our own layout; see docs/history/x2-facts.md \"Banks\" and")
    out.append(" * docs/limits.md \"Included tables (x2)\". For mode 0 and indices 1, 8, 11,")
    out.append(" * 12, 13 and 14 they are the I-modem's lists as seen on the line. */")
    out.append('#include "x2tab.h"')
    out.append("")
    out.append("const uint8_t x2_bank_pool[%d] = {" % len(pool))
    for k in range(0, len(pool), 12):
        out.append("    " + " ".join("0x%02x," % v for v in pool[k:k + 12]))
    out.append("};")
    out.append("")
    out.append("/* level received at pool[recv + d], octet sent at pool[sent + d], d < n */")
    out.append("const x2_list_t x2_bank_lists[%d] = {" % len(lists))
    for k in range(0, len(lists), 4):
        out.append("    " + " ".join("{ %4d, %4d, %2d }," % e for e in lists[k:k + 4]))
    out.append("};")
    out.append("")
    out.append("/* B, then the list of position 0 and that of positions 1..5 */")
    out.append("const x2_bank_t x2_banks[X2_MODES][X2_NIDX] = {")
    for mode in range(MODES):
        out.append("    {")
        for n in range(NIDX):
            k = mode * NIDX + n
            out.append("        { %d, { %d, %d } },    /* index %d: %d, then %d levels */"
                       % (banks[k]["b"], ids[k][0], ids[k][1], n + 1, banks[k]["counts"][0], banks[k]["counts"][1]))
        out.append("    },")
    out.append("};")
    print("\n".join(out))


main()
