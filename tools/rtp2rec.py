#!/usr/bin/env python3
"""rtp2rec: the two G.711 RTP directions of a call captured in a pcap, as a
recording in v90m-replay's format (tools/README.md, "Recordings"): per 10 ms
the 80 codewords the answering modem heard, then the 80 it sent. For the HST
evidence (docs/history/hst-facts.md, "Evidence": R1, R2, tapped at an ATA
with tcpdump), read with build/hst-scan and build/v90m-replay.

  tools/rtp2rec.py [--list] [--sent ADDR:PORT] [--red PT] [--from S] [--to S]
                   CAPTURE.pcap OUT.raw
  tools/rtp2rec.py --self-test

  --list        list the RTP streams found (source, destination, SSRC,
                payload type, packets, first and last arrival) and stop
  --sent A:P    the answering modem's stream: its RTP source address and port
                (what it sent; the other direction is what it heard). Without
                it, with two streams, the one with more answer tone (2100 or
                2225 Hz) in its first 6 s is taken for the answerer's; the
                choice is printed
  --red PT      the payload type of RFC 2198 redundancy (the ATA's "red"),
                when the capture's SDP is not at hand: its primary and
                redundant blocks are G.711 (0 or 8) and a lost packet's audio
                is taken from the next packet's redundant block. Without it, a
                dynamic payload type (96..127) that parses as RFC 2198 over
                G.711 is taken for it
  --from S, --to S  only from / up to S seconds of the timeline

Reads classic pcap (either byte order, micro- or nanosecond times) and pcapng
(enhanced and simple packet blocks), with Ethernet (802.1Q tags too), Linux
cooked (SLL, SLL2), raw IP or loopback link layers, IPv4 and IPv6, UDP. Each
direction is placed on one 8000 Hz timeline by its RTP timestamps (32-bit,
wrapping), the two lined up by the arrival of the first packet of each
(its primary block: RFC 2198's redundant blocks hold older audio); a sample no
packet gave is silence (mu-law 0xFF, A-law 0xD5), and how many there were is
printed. Both directions must carry the same law: PCMU (0) gives a mu-law
recording, PCMA (8) an A-law one (replay it with -a).

The capture is private: this tool writes the recording where it is told and
nothing else, and keeps no copy. Host tool, not installed; Python 3, standard
library only."""
import math
import struct
import sys

SILENCE = {0: 0xFF, 8: 0xD5}


# ---------------------------------------------------------------- capture
def packets(data):
    """(time in seconds, link type, frame) for each captured frame"""
    if len(data) < 24:
        raise ValueError("not a capture: too short")
    magic = data[:4]
    if magic in (b"\xd4\xc3\xb2\xa1", b"\xa1\xb2\xc3\xd4", b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\x3c\x4d"):
        le = magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1")
        nano = magic in (b"\x4d\x3c\xb2\xa1", b"\xa1\xb2\x3c\x4d")
        e = "<" if le else ">"
        link = struct.unpack(e + "I", data[20:24])[0] & 0x0FFFFFFF
        p = 24
        while p + 16 <= len(data):
            sec, frac, incl, _ = struct.unpack(e + "IIII", data[p:p + 16])
            p += 16
            yield sec + frac / (1e9 if nano else 1e6), link, data[p:p + incl]
            p += incl
        return
    if magic == b"\x0a\x0d\x0d\x0a":
        p, e, ifs = 0, "<", []
        while p + 12 <= len(data):
            if data[p:p + 4] == b"\x0a\x0d\x0d\x0a":
                e = "<" if data[p + 8:p + 12] == b"\x4d\x3c\x2b\x1a" else ">"
                ifs = []
            btype, blen = struct.unpack(e + "II", data[p:p + 8])
            if blen < 12 or p + blen > len(data):
                break
            body = data[p + 8:p + blen - 4]
            if btype == 1:                                  # interface description
                link = struct.unpack(e + "H", body[:2])[0]
                res = 6                                     # microseconds by default
                q = 8
                while q + 4 <= len(body):
                    code, olen = struct.unpack(e + "HH", body[q:q + 4])
                    if code == 0:
                        break
                    if code == 9 and olen >= 1:
                        r = body[q + 4]
                        res = -(r & 0x7F) if r & 0x80 else r     # base 2 or 10
                    q += 4 + ((olen + 3) & ~3)
                ifs.append((link, res))
            elif btype == 6 and ifs:                        # enhanced packet
                iface, hi, lo, cap = struct.unpack(e + "IIII", body[:16])
                link, res = ifs[iface] if iface < len(ifs) else ifs[0]
                ts = (hi << 32) | lo
                t = ts / (2.0 ** -res if res < 0 else 10.0 ** res)
                yield t, link, body[20:20 + cap]
            elif btype == 3 and ifs:                        # simple packet: no time
                cap = struct.unpack(e + "I", body[:4])[0]
                yield 0.0, ifs[0][0], body[4:4 + cap]
            p += blen
        return
    raise ValueError("not a pcap or pcapng capture")


def udp_of(link, f):
    """(src, sport, dst, dport, payload) of a UDP datagram, or None"""
    if link == 1:                                           # Ethernet
        if len(f) < 14:
            return None
        et, p = struct.unpack(">H", f[12:14])[0], 14
        while et in (0x8100, 0x88A8) and len(f) >= p + 4:
            et, p = struct.unpack(">H", f[p + 2:p + 4])[0], p + 4
        ip = f[p:]
    elif link == 113:                                       # Linux cooked
        if len(f) < 16:
            return None
        et, ip = struct.unpack(">H", f[14:16])[0], f[16:]
    elif link == 276:                                       # Linux cooked v2
        if len(f) < 20:
            return None
        et, ip = struct.unpack(">H", f[0:2])[0], f[20:]
    elif link in (101, 12, 14, 228, 229):                   # raw IP
        et, ip = 0, f
    elif link == 0:                                         # BSD loopback
        et, ip = 0, f[4:]
    else:
        return None
    if not ip:
        return None
    v = ip[0] >> 4
    if v == 4 and (et in (0, 0x0800)):
        ihl = (ip[0] & 15) * 4
        if len(ip) < ihl + 8 or ip[9] != 17 or struct.unpack(">H", ip[6:8])[0] & 0x3FFF:
            return None                                     # not UDP, or a fragment
        src, dst, u = ip[12:16], ip[16:20], ip[ihl:]
        sa, da = ".".join(str(b) for b in src), ".".join(str(b) for b in dst)
    elif v == 6 and (et in (0, 0x86DD)):
        if len(ip) < 48 or ip[6] != 17:
            return None
        u = ip[40:]
        sa = ":".join("%x" % x for x in struct.unpack(">8H", ip[8:24]))
        da = ":".join("%x" % x for x in struct.unpack(">8H", ip[24:40]))
    else:
        return None
    if len(u) < 8:
        return None
    sp, dp, ln = struct.unpack(">HHH", u[:6])
    return sa, sp, da, dp, u[8:ln] if 8 <= ln <= len(u) else u[8:]


def rtp_of(pl):
    """(payload type, sequence, timestamp, SSRC, payload) or None"""
    if len(pl) < 12 or pl[0] >> 6 != 2:
        return None
    cc, ext, pad = pl[0] & 15, pl[0] & 0x10, pl[0] & 0x20
    pt = pl[1] & 0x7F
    seq, ts, ssrc = struct.unpack(">HII", pl[2:12])
    p = 12 + 4 * cc
    if ext:
        if len(pl) < p + 4:
            return None
        p += 4 + 4 * struct.unpack(">H", pl[p + 2:p + 4])[0]
    end = len(pl) - (pl[-1] if pad and pl[-1] else 0)
    if p > end:
        return None
    return pt, seq, ts, ssrc, pl[p:end]


def red_blocks(pl):
    """RFC 2198: [(pt, timestamp offset, data)], the primary last; None if not"""
    hdr, p = [], 0
    while True:
        if p >= len(pl):
            return None
        b = pl[p]
        if b & 0x80:
            if p + 4 > len(pl):
                return None
            w = struct.unpack(">I", pl[p:p + 4])[0]
            hdr.append(((w >> 24) & 0x7F, (w >> 10) & 0x3FFF, w & 0x3FF))
            p += 4
        else:
            hdr.append((b & 0x7F, 0, None))
            p += 1
            break
    out = []
    for pt, off, ln in hdr:
        if ln is None:
            ln = len(pl) - p
        if p + ln > len(pl):
            return None
        out.append((pt, off, pl[p:p + ln]))
        p += ln
    return out


# ---------------------------------------------------------------- streams
class Stream:
    def __init__(self, key):
        self.key = key              # (src, sport, dst, dport, ssrc)
        self.pts = {}
        self.pkts = []              # (arrival, seq, ts, pt, payload)

    def name(self):
        s, sp, d, dp, ssrc = self.key
        return "%s:%d -> %s:%d SSRC %08x" % (s, sp, d, dp, ssrc)


def streams_of(data):
    st = {}
    for t, link, f in packets(data):
        u = udp_of(link, f)
        if not u:
            continue
        r = rtp_of(u[4])
        if not r:
            continue
        pt, seq, ts, ssrc, pl = r
        if pt in (72, 73, 74, 75, 76, 77, 78, 79):          # RTCP's range
            continue
        k = (u[0], u[1], u[2], u[3], ssrc)
        s = st.get(k)
        if s is None:
            s = st[k] = Stream(k)
        s.pts[pt] = s.pts.get(pt, 0) + 1
        s.pkts.append((t, seq, ts, pt, pl))
    return list(st.values())


def g711_blocks(s, red_pt):
    """[(rtp timestamp, law, codewords, redundant, arrival)] of a stream:
    G.711 packets, or RFC 2198 over G.711 (the redundant blocks too)"""
    out = []
    for t, seq, ts, pt, pl in s.pkts:
        if pt in (0, 8):
            out.append((ts, pt, pl, 0, t))
        elif (red_pt is not None and pt == red_pt) or (red_pt is None and 96 <= pt <= 127):
            b = red_blocks(pl)
            if not b or any(x[0] not in (0, 8) for x in b):
                continue
            for i, (bpt, off, d) in enumerate(b):
                out.append(((ts - off) & 0xFFFFFFFF, bpt, d, 1 if i < len(b) - 1 else 0, t))
    return out


def timeline(s, red_pt):
    """{sample: codeword}, sample 0 the first sample of the primary block
    of the packet that arrived first (a redundant block's audio is older
    than its packet: anchored on it, the stream would land late); its law,
    the redundant samples used, and that packet's arrival"""
    blocks = g711_blocks(s, red_pt)
    if not blocks:
        return None, None, 0, None
    laws = set(b[1] for b in blocks)
    if len(laws) != 1:
        raise ValueError("%s: both PCMU and PCMA" % s.name())
    anchor = min((b for b in blocks if not b[3]), key=lambda b: b[4])
    ts0, t0 = anchor[0], anchor[4]
    line, red_used = {}, 0
    # primaries first, then the redundant blocks fill what no primary gave
    for redundant in (0, 1):
        for ts, law, d, r, _ in blocks:
            if r != redundant:
                continue
            rel = (ts - ts0) & 0xFFFFFFFF
            if rel >= 0x80000000:
                rel -= 0x100000000                          # before the first one (reordered)
            for i, c in enumerate(d):
                if rel + i not in line:
                    line[rel + i] = c
                    red_used += redundant
    return line, laws.pop(), red_used, t0


def tone_score(line, law):
    """answer tone (2100 or 2225 Hz) in the first 6 s, against all power"""
    lin = [ulaw(c) if law == 0 else alaw(c) for c in (line.get(i, SILENCE[law]) for i in range(min(48000, max(line) + 1)))] if line else []
    tot, hit = 1e-9, 0.0
    for b in range(0, len(lin) - 400, 400):
        x = lin[b:b + 400]
        p = sum(v * v for v in x)
        tot += p
        best = 0
        for f in (2100, 2225):
            w = 2 * math.pi * f / 8000
            c = 2 * math.cos(w)
            a = q = 0.0
            for v in x:
                a, q = v + c * a - q, a
            best = max(best, (a * a + q * q - c * a * q) * 2 / len(x))
        if best > 0.5 * p:
            hit += p
    return hit / tot


def ulaw(u):
    u = ~u & 0xFF
    s, e, m = u & 0x80, (u >> 4) & 7, u & 15
    v = (((m << 3) + 0x84) << e) - 0x84
    return -v if s else v


def alaw(a):
    a ^= 0x55
    s, e, m = a & 0x80, (a >> 4) & 7, a & 15
    v = (m << 4) + 8 if e == 0 else ((m << 4) + 0x108) << (e - 1)
    return v if s else -v


# ---------------------------------------------------------------- the recording
def convert(data, sent=None, red_pt=None, t_from=None, t_to=None, log=sys.stderr):
    """the recording's octets from a capture's octets"""
    st = [s for s in streams_of(data) if g711_blocks(s, red_pt)]
    if len(st) < 2:
        raise ValueError("%d G.711 RTP streams found, two wanted (--list)" % len(st))
    lines = {}
    for s in st:
        lines[s.key] = timeline(s, red_pt)
    if sent is not None:
        a = [s for s in st if "%s:%d" % (s.key[0], s.key[1]) == sent]
        if len(a) != 1:
            raise ValueError("--sent %s: %d streams from there" % (sent, len(a)))
        a = a[0]
        b = [s for s in st if s is not a and s.key[0] == a.key[2] and s.key[1] == a.key[3]]
        if not b:
            b = [s for s in st if s is not a]
        if len(b) != 1:
            raise ValueError("--sent %s: the other direction is not one stream (--list)" % sent)
        b = b[0]
    else:
        if len(st) != 2:
            raise ValueError("%d G.711 RTP streams: name the answerer's with --sent (--list)" % len(st))
        sc = [tone_score(lines[s.key][0], lines[s.key][1]) for s in st]
        a, b = (st[0], st[1]) if sc[0] >= sc[1] else (st[1], st[0])
        log.write("rtp2rec: the answerer's (sent): %s (answer tone %.0f %% of its first 6 s; the other's %.0f %%)\n"
                  % (a.name(), 100 * max(sc), 100 * min(sc)))
    (la, law_a, ra, ta), (lb, law_b, rb, tb) = lines[a.key], lines[b.key]
    if law_a != law_b:
        raise ValueError("the two directions are not the same law")
    # the two lined up by their anchors' arrivals (each the primary block
    # of its stream's first packet to arrive)
    off_a = 0
    off_b = int(round((tb - ta) * 8000))
    lo = min(off_a + min(la), off_b + min(lb))
    hi = max(off_a + max(la), off_b + max(lb)) + 1
    if t_from is not None:
        lo = max(lo, int(t_from * 8000) + min(off_a, off_b))
    if t_to is not None:
        hi = min(hi, int(t_to * 8000) + min(off_a, off_b))
    n = max(0, (hi - lo + 79) // 80 * 80)
    sil = SILENCE[law_a]
    out = bytearray()
    miss = [0, 0]
    for blk in range(0, n, 80):
        for which, (line, off) in enumerate(((lb, off_b), (la, off_a))):      # heard, then sent
            for i in range(80):
                c = line.get(lo + blk + i - off)
                if c is None:
                    c = sil
                    miss[which] += 1
                out.append(c)
    log.write("rtp2rec: %s, %d samples (%.2f s); heard: %s, %d samples silent for want of a packet%s; "
              "sent: %s, %d%s\n" % ("mu-law" if law_a == 0 else "A-law (replay with -a)", n, n / 8000.0,
                                    b.name(), miss[0], ", %d from redundancy" % rb if rb else "",
                                    a.name(), miss[1], ", %d from redundancy" % ra if ra else ""))
    return bytes(out)


# ---------------------------------------------------------------- self-test
def self_test():
    """a capture made here (both directions, loss, reordering, RFC 2198,
    pcap and pcapng, IPv4 and IPv6) converted back to what was sent"""
    import random
    rnd = random.Random(2026)
    n = 8000 * 3
    heard = bytes(rnd.randrange(256) for _ in range(n))
    sent = bytearray()
    for i in range(n):                      # the answerer: 2100 Hz then noise
        v = int(8000 * math.sin(2 * math.pi * 2100 * i / 8000)) if i < 16000 else rnd.randrange(-3000, 3000)
        sent.append(lin2ulaw(v))
    sent = bytes(sent)

    def udp4(src, sp, dst, dp, pl):
        u = struct.pack(">HHHH", sp, dp, 8 + len(pl), 0) + pl
        ip = struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(u), 0, 0, 64, 17, 0,
                         bytes(int(x) for x in src.split(".")), bytes(int(x) for x in dst.split(".")))
        return b"\x00" * 12 + b"\x08\x00" + ip + u

    def udp6(src, sp, dst, dp, pl):
        u = struct.pack(">HHHH", sp, dp, 8 + len(pl), 0) + pl
        ip = struct.pack(">IHBB", 6 << 28, len(u), 17, 64) + src + dst
        return b"\x00" * 12 + b"\x86\xdd" + ip + u

    def rtp(pt, seq, ts, ssrc, pl):
        return struct.pack(">BBHII", 0x80, pt, seq & 0xFFFF, ts & 0xFFFFFFFF, ssrc) + pl

    def frames(v6, layers, first=False, empty=False):
        """layers: RFC 2198 blocks of the packets before, the oldest first
        (0: plain G.711); first: packet 0 carries them too (silence, from
        before the call); empty: a redundant block of no octets"""
        out = []
        ts_a, ts_b = 0xFFFFF000, 1234        # the answerer's wraps
        lost_b = {7, 8, 40}                  # heard: packets lost (with redundancy some come back)
        for k in range(n // 160):
            t = 1000.0 + k * 0.02
            pa = sent[160 * k:160 * k + 160]
            pb = heard[160 * k:160 * k + 160]
            if layers and (k > 0 or first):
                hdr, body = b"", b""
                for j in range(layers, 0, -1):
                    if empty:
                        hdr += struct.pack(">I", (1 << 31) | (160 * j << 10))
                        continue
                    prev = heard[160 * (k - j):160 * (k - j + 1)] if k >= j else b"\xff" * 160
                    hdr += struct.pack(">I", (1 << 31) | (0 << 24) | (160 * j << 10) | 160)
                    body += prev
                pb = hdr + b"\x00" + body + pb
            fa = rtp(0, k, ts_a + 160 * k, 0xA5A5, pa)
            fb = rtp(100 if layers else 0, k, ts_b + 160 * k, 0xB6B6, pb)
            if v6:
                a6, b6 = b"\x20\x01" + b"\x00" * 13 + b"\x01", b"\x20\x01" + b"\x00" * 13 + b"\x02"
                out.append((t, ("a", k), udp6(a6, 5004, b6, 6000, fa)))
                if k not in lost_b:
                    out.append((t + 0.0123, ("b", k), udp6(b6, 6000, a6, 5004, fb)))
            else:
                out.append((t, ("a", k), udp4("10.0.0.1", 5004, "10.0.0.2", 6000, fa)))
                if k not in lost_b:
                    out.append((t + 0.0123, ("b", k), udp4("10.0.0.2", 6000, "10.0.0.1", 5004, fb)))
        # two of each direction's packets reordered
        for tag in ("a", "b"):
            i = [x[1] for x in out].index((tag, 20))
            j = [x[1] for x in out].index((tag, 21))
            out[i], out[j] = (out[i][0], out[j][1], out[j][2]), (out[j][0], out[i][1], out[i][2])
        return [(t, f) for t, _, f in out]

    def pcap(fr):
        b = struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
        for t, f in fr:
            b += struct.pack("<IIII", int(t), int(round((t - int(t)) * 1e6)), len(f), len(f)) + f
        return b

    def pcapng(fr):
        def blk(bt, body):
            body += b"\x00" * (-len(body) % 4)
            return struct.pack("<II", bt, len(body) + 12) + body + struct.pack("<I", len(body) + 12)
        b = blk(0x0A0D0D0A, struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1))
        b += blk(1, struct.pack("<HHI", 1, 0, 65535) + struct.pack("<HHB3x", 9, 1, 9) + struct.pack("<HH", 0, 0))
        for t, f in fr:
            ts = int(round(t * 1e9))
            b += blk(6, struct.pack("<IIIII", 0, ts >> 32, ts & 0xFFFFFFFF, len(f), len(f)) + f)
        return b

    class Null:
        def write(self, s):
            pass

    ok = True
    pk = lambda *ks: set(i for k in ks for i in range(160 * k, 160 * k + 160))
    for name, cap, lost in (
            ("pcap, IPv4", pcap(frames(False, 0)), pk(7, 8, 40)),
            ("pcapng, IPv6, RFC 2198", pcapng(frames(True, 1)), pk(7)),
            ("RFC 2198 from the first packet", pcap(frames(False, 1, first=True)), pk(7)),
            ("RFC 2198, two layers, from the first packet", pcap(frames(False, 2, first=True)), set()),
            ("RFC 2198, an empty redundant block, from the first packet", pcap(frames(False, 1, first=True, empty=True)),
             pk(7, 8, 40))):
        r = convert(cap, log=Null())
        nb = len(r) // 160
        h = b"".join(r[160 * i:160 * i + 80] for i in range(nb))
        s = b"".join(r[160 * i + 80:160 * i + 160] for i in range(nb))
        # sent wherever the timeline puts it (redundancy from before the
        # call may start it earlier); heard 12.3 ms after it, 98 samples
        s0 = s.find(sent[:400])
        d = round(0.0123 * 8000)
        good = s0 >= 0 and s[s0:s0 + n] == sent and len(h) >= s0 + d + n
        good = good and all((h[s0 + d + i] == 0xFF) if i in lost else (h[s0 + d + i] == heard[i]) for i in range(n))
        good = good and all(c == 0xFF for c in h[:s0 + d])
        print("rtp2rec self-test: %s: %s" % (name, "pass" if good else "FAIL"))
        ok = ok and good
    # --sent naming the other stream: the two channels swapped (the
    # answerer's by its tone above)
    r = convert(pcap(frames(False, 0)), sent="10.0.0.2:6000", log=Null())
    h = b"".join(r[160 * i:160 * i + 80] for i in range(len(r) // 160))
    good = h[:n] == sent
    print("rtp2rec self-test: --sent: %s" % ("pass" if good else "FAIL"))
    ok = ok and good
    return ok


def lin2ulaw(v):
    """G.711 mu-law (the self-test's answer tone)"""
    s = 0x80 if v < 0 else 0
    v = min(abs(v), 32635) + 0x84
    e, mask = 7, 0x4000
    while e > 0 and not v & mask:
        e, mask = e - 1, mask >> 1
    return ~(s | (e << 4) | ((v >> (e + 3)) & 15)) & 0xFF


def main(argv):
    args, opts = [], {}
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ("--list", "--self-test"):
            opts[a] = True
        elif a in ("--sent", "--red", "--from", "--to") and i + 1 < len(argv):
            opts[a] = argv[i + 1]
            i += 1
        elif a.startswith("--"):
            return usage()
        else:
            args.append(a)
        i += 1
    if "--self-test" in opts:
        return 0 if self_test() else 1
    if not args or ("--list" not in opts and len(args) != 2) or ("--list" in opts and len(args) != 1):
        return usage()
    with open(args[0], "rb") as f:
        data = f.read()
    red = int(opts["--red"]) if "--red" in opts else None
    if "--list" in opts:
        for s in streams_of(data):
            print("%s: payload types %s, %d packets, %.3f .. %.3f s" % (
                s.name(), ",".join("%d" % p for p in sorted(s.pts)), len(s.pkts),
                min(p[0] for p in s.pkts), max(p[0] for p in s.pkts)))
        return 0
    try:
        out = convert(data, opts.get("--sent"), red,
                      float(opts["--from"]) if "--from" in opts else None,
                      float(opts["--to"]) if "--to" in opts else None)
    except ValueError as e:
        sys.stderr.write("rtp2rec: %s\n" % e)
        return 1
    with open(args[1], "wb") as f:
        f.write(out)
    return 0


def usage():
    sys.stderr.write("usage: tools/rtp2rec.py [--list] [--sent ADDR:PORT] [--red PT] [--from S] [--to S] "
                     "CAPTURE OUT.raw | --self-test\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
