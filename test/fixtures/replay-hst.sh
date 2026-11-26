#!/bin/sh
# replay-hst.sh REPLAY REC_HST HST_SCAN BENCH_HST: the HST tools on a
# simulated HST call that REC_HST (tools/rec_hst.c: sim/hstcall calling our
# answerer, MNP up, the retrain to 14400, 2 s of data) records. Exit 1 on
# any failure.
#
#   bench_hst -c  replays it into the answerer alone: our transmit is the
#                 recording's to the last sample, its hashes are rec_hst's
#   -H            v90m-replay's HST section: the detector heard 450 bit/s
#                 within W; our training's segments (S1, S2, S3, data) at
#                 code 4, then again at code 6 after the caller's
#                 confirming LA; connected at 9600, the rates 14400 / 450,
#                 MNP's agreed rates 0x7C, no reversal; our transmit is the
#                 recording's until the endpoint starts sending text (at
#                 data at 14400 or later)
#   without -H    HST not offered: no HST_WAIT, no HST section (V.22bis
#                 after ANSam, as for any caller without CM or AA)
#   hst-scan -b   the caller's back channel detected, 450 bit/s, on what
#                 the answerer heard
#   hst-scan -f   on what the answerer sent: two trainings, AB, CD and TRN
#                 within 10 symbols of 180, 72 and 3096 (the long one) and
#                 200 (the short one), and the data's constellation 32
#                 points (code 4, 9600) then 128 (code 6, 14400)
R=$1; H=${2:-}; S=${3:-}; B=${4:-}
[ $# -eq 4 ] || { echo "usage: test/fixtures/replay-hst.sh REPLAY REC_HST HST_SCAN BENCH_HST" >&2; exit 2; }
for v in R H S B; do eval "p=\$$v"; case $p in /*) ;; *) eval "$v=\$(pwd)/\$p" ;; esac; done
tmp=${TMPDIR:-/tmp}/v90m-replay-hst.$$
rm -rf "$tmp"; mkdir -p "$tmp" || exit 1
trap 'rm -rf "$tmp"' EXIT
cd "$tmp" || exit 1
fail=0
bad() { echo "  FAIL: $*"; fail=1; }

"$H" 2 hst.raw > rec.out || { bad "rec_hst"; cat rec.out; }
"$B" -c hst.raw > bench.out || bad "bench_hst -c"
grep -q 'our transmit differs from the recording in 0 (first at -1)' bench.out || bad "bench_hst -c: our transmit is not the recording's"
txr=$(sed -n 's/.*transmit hash \([0-9a-f]*\)$/\1/p' rec.out); txb=$(sed -n 's/^tx hash //p' bench.out)
[ -n "$txr" ] && [ "$txr" = "$txb" ] || bad "bench_hst -c: tx hash $txb, rec_hst's $txr"
echo "  bench_hst -c: the recording's transmit, hash $txb"

"$R" -H -b bits.txt hst.raw > h.txt || bad "-H"
want() { grep -q -- "$1" h.txt || bad "-H: no '$1'"; }
want '^HST: the wait began at [0-9.]* s; after [0-9]* ms, the detector heard a back channel at 450 bit/s$'
w=$(sed -n 's/^HST: the wait began .*; after \([0-9]*\) ms, the detector.*/\1/p' h.txt)
[ -n "$w" ] && [ "$w" -le 650 ] || bad "-H: detected after ${w:-?} ms, W is 650"
for c in 4 6; do
    for g in 'S1 (AB)' 'S2 (CD)' 'S3 (TRN)' 'data'; do want "^HST: *[0-9.]* s  our forward transmitter: $g, code $c\$"; done
done
want '^HST: *[0-9.]* s  connected: modulation 12, tx 9600$'
want '^HST: *[0-9.]* s  MNP agreed: rates 0x7C, back channel 450$'
want "^HST: *[0-9.]* s  the caller's confirming LA: MNP up$"
want '^HST: *[0-9.]* s  rates: 14400 bit/s sent, 450 received$'
want '^HST: reversals 0; the end 0$'
want '^modulation 12, 14400 bit/s down, 450 up,'
d14=$(sed -n 's/^HST: *\([0-9.]*\) s  rates: 14400 bit.*/\1/p' h.txt)
fd=$(sed -n 's/.*differs from the recording in [0-9]* (first at \([0-9.]*\) s).*/\1/p' h.txt)
if [ -n "$d14" ] && [ -n "$fd" ] && awk -v f="$fd" -v d="$d14" 'BEGIN { exit !(f >= d) }'; then
    echo "  -H: detected in $w ms; trainings at codes 4 and 6; our transmit the recording's to $fd s (14400 from $d14 s)"
else bad "-H: our transmit differs from $fd s, before data at 14400 ($d14 s)"; fi

"$R" -b bits0.txt hst.raw > n.txt || bad "no -H"
grep -q 'HST' n.txt && bad "without -H: an HST phase or section"
grep -q '^ *[0-9.]* s  modem phase V22 ' n.txt || bad "without -H: no V.22bis after ANSam"
echo "  without -H: HST not offered, V.22bis after ANSam"

"$S" -b hst.raw > b.txt; [ $? -eq 1 ] || bad "hst-scan -b: exit not 1"
grep -q '^hst.raw: back channel detected at [0-9.]* s, 450 bit/s' b.txt || bad "hst-scan -b: no back channel at 450"
"$S" -f hst.raw > f.txt; [ $? -eq 1 ] || bad "hst-scan -f: exit not 1"
grep '^hst.raw: sent:' f.txt > fs.txt
near() {    # near SEG WANT N: the Nth SEG segment's length within 10 symbols of WANT
    l=$(sed -n "s/^hst.raw: sent:   $1 from [0-9.]* s, \([0-9]*\) symbols$/\1/p" fs.txt | sed -n "$3p")
    [ -n "$l" ] && [ "$l" -ge $(($2 - 10)) ] && [ "$l" -le $(($2 + 10)) ] || bad "hst-scan -f: $1 number $3 is ${l:-none} symbols, want $2"
}
near AB 180 1; near CD 72 1; near TRN 3096 1; near AB 180 2; near CD 72 2; near TRN 200 2
grep -q '^hst.raw: sent: [0-9.]* s, 2 forward trainings heard$' fs.txt || bad "hst-scan -f: not 2 trainings on what we sent"
c=$(sed -n 's/^hst.raw: sent:   constellation: \([0-9]*\) points, code \([0-9]\), \([0-9]*\) bit\/s$/\1 \2 \3/p' fs.txt | tr '\n' ' ')
[ "$c" = "32 4 9600 128 6 14400 " ] || bad "hst-scan -f: constellations '$c'"
echo "  hst-scan: the back channel at 450 heard; sent: two trainings, 32 then 128 points"

[ $fail -eq 0 ] && echo "replay HST: pass"
[ $fail -eq 0 ] || { echo "REPLAY HST FAILED"; exit 1; }
