#!/bin/sh
# replay-opts.sh REPLAY: v90m-replay's -s (start sample) and -l (level, also
# decimal) on the public recordings of fixtures.txt. Exit 1 on any failure.
#
#   -s: the default run starts the modem a little before the answer tone; -s S
#       starts it at sample S, and the times are the recording's either way.
#       So a default run and "-s S" for the S where it starts give the same
#       trace, to the byte, but for the two lines at the top. And a recording
#       with N samples of its start cut off (N a multiple of 80, the block)
#       with "-s S-N" gives what the whole recording gives with "-s S", with
#       the times (all N/8000 s later in the whole one) taken out.
#   -l: whole numbers give what they always did, "-14.0" and "-14.00" are
#       "-14"; "-14.4" differs from "-14" only in how many samples differ
#       from the recording (the recording's modem sent at -12); bad values
#       exit 2 with the usage message.
R=$1
[ $# -eq 1 ] || { echo "usage: test/fixtures/replay-opts.sh REPLAY" >&2; exit 2; }
case $R in /*) ;; *) R=$(pwd)/$R ;; esac
cd "$(dirname "$0")" || exit 1
tmp=${TMPDIR:-/tmp}/v90m-replay-opts.$$
rm -rf "$tmp"; mkdir -p "$tmp" || exit 1
trap 'rm -rf "$tmp"' EXIT
fail=0
bad() { echo "  FAIL: $*"; fail=1; }
cd "$tmp" || exit 1
xz -dc "$OLDPWD/v34a.raw.xz" > v34a.raw || exit 1       # companded, V.34
xz -dc "$OLDPWD/v21a.raw.xz" > v21a.raw || exit 1

# the traces with their times taken out
norm() {
    sed -E '/^recording:/d; /^answer tone at/d; /^the modem starts at/d; /^  second [0-9]+:/d
            s/ *[0-9]+\.[0-9]+ s/ T s/g; s/^[0-9]+ samples;/N samples;/' "$1"
}

# -s: where the default run starts
"$R" v34a.raw > def.txt || bad "default run"
ans=$(sed -n 's/^answer tone at \([0-9.]*\) s.*/\1/p' def.txt)
lo=$(awk -v a="$ans" 'BEGIN { printf "%d", a * 8000 - 1600 - 5 }')
tail -n +3 def.txt > def.n
found=
s=$lo
while [ "$s" -le $((lo + 10)) ]; do
    "$R" -s "$s" v34a.raw > s.txt || bad "-s $s exit"
    tail -n +3 s.txt > s.n
    if cmp -s def.n s.n; then found=$s; break; fi
    s=$((s + 1))
done
if [ -z "$found" ]; then bad "no -s near the default start (about sample $lo) gives the default trace"
else echo "  -s $found: the default trace"; fi

# -s on a recording with its start cut off
if [ -n "$found" ]; then
    cut=100                                             # blocks = 8000 samples
    dd if=v34a.raw of=short.raw bs=160 skip=$cut 2>/dev/null
    "$R" -s "$found" v34a.raw > a.txt; "$R" -s $((found - cut * 80)) short.raw > b.txt
    # the first two lines have the recording's length and the start sample (norm drops them)
    if cmp -s a.txt b.txt; then bad "cut and uncut have the same header (the test is empty)"
    elif [ "$(norm a.txt)" = "$(norm b.txt)" ]; then
        echo "  -s $found, and -s $((found - cut * 80)) on the recording cut by $((cut * 80)): same trace, times apart"
    else bad "-s on the cut recording gives another trace"; diff a.txt b.txt | head -5; fi
fi

# -s at 0 and out of range
"$R" -s 0 v34a.raw > z.txt || bad "-s 0 exit"
grep -q '^the modem starts at sample 0 of the recording' z.txt || bad "-s 0: no start line"
"$R" -s 99999999 v34a.raw > /dev/null 2>&1; [ $? -eq 2 ] || bad "-s past the end: not exit 2"

# -l
"$R" v21a.raw > l.def; "$R" -l -12 v21a.raw > l.12
cmp -s l.def l.12 || bad "-l -12 is not the default"
"$R" -l -14 v21a.raw > l.14 || bad "-l -14"
"$R" -l -14.0 v21a.raw > l.140; "$R" -l -14.00 v21a.raw > l.1400
cmp -s l.14 l.140 && cmp -s l.14 l.1400 || bad "-l -14.0 or -14.00 is not -14"
"$R" -l -14.4 v21a.raw > l.144 || bad "-l -14.4"
cmp -s l.14 l.144 && bad "-l -14.4 is -14"
if diff l.14 l.144 | grep '^[<>]' | grep -vq -e '^[<>]   second [0-9]*: ' -e '^[<>] [0-9]* samples; after the answer'; then
    bad "-l -14.4 differs from -14 in more than the differing-sample counts"
    diff l.14 l.144 | head -5
else echo "  -l -14.4 differs from -14 in the counts of differing samples only"; fi
"$R" -l -14.4 v21a.raw | cmp -s - l.144 || bad "-l -14.4 not repeatable"
for v in -14.4.1 -14.456 abc "" - -. . 1e3 --5 -14,4 -1234 0x10; do
    "$R" -l "$v" v21a.raw > l.out 2> l.err; rc=$?
    if [ $rc -ne 2 ] || ! grep -q '^usage' l.err; then bad "-l '$v': exit $rc, want 2 and the usage message"; fi
done
for v in -1 abc "" 1.5 -0 1e3 " 5"; do
    "$R" -s "$v" v21a.raw > l.out 2> l.err; rc=$?
    if [ $rc -ne 2 ] || ! grep -q '^usage' l.err; then bad "-s '$v': exit $rc, want 2 and the usage message"; fi
done
[ $fail -eq 0 ] && echo "replay options: pass"
[ $fail -eq 0 ] || { echo "REPLAY OPTIONS FAILED"; exit 1; }
