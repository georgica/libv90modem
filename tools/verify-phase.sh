#!/bin/sh
# make verify-phase: the check at the end of each phase (docs/superpowers/
# specs/2026-11-22-audit-refactor-optimize-design.md 7.4), about an hour.
# The stages one after another, each printing one line:
#   armv5     make check-arm with the armv5 uClibc compiler (the box's ABI),
#             PAR=16, the whole short set
#   ports     tools/check-ports.sh build/ports, every target; m68k-uclibc is
#             reported only (README "Ports"), every other must pass
#   armv7hf   make check-arm with the armv7hf musl compiler, the whole short
#             set, compared with the host short set (tools/short-cmp.sh)
#   benches   bench-v90 bench-x2 bench-k56 bench-v34 bench-v90a bench-v90c
#             bench-v92c bench-bell (armv5), then each recording replayed on
#             the host and under qemu: the hashes must be the same
#   costs     cost-x2 cost-k56 cost-v34 cost-v90a cost-v90c cost-v92c
#             cost-bell (armv5, each in a build tree of its own, at once):
#             their lines in build/costs.txt
#   crd       make crd-scan RECS=...: 0 CRd detections
# then "VERIFY-PHASE PASS (<seconds> s)" or "VERIFY-PHASE FAILED". Logs:
# build/verify-phase/<stage>.log.
#
#   A5=<armv5 arm-linux-gcc>     default ../toolchains/armv5-eabi--uclibc--*
#   A7=<armv7hf arm-linux-gcc>   default ../toolchains/armv7-eabihf--musl--*
#   RECS=<recordings>            default /home/georgica/sync-build/live-050
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
unset V92_VERBOSE V32_DEBUG V92_TRACE RXBITS TXBITS TXOWN
unset MAKEFLAGS MFLAGS MAKELEVEL BUILD
TMPDIR=${TMPDIR:-$ROOT/build/tmp}; export TMPDIR
mkdir -p "$TMPDIR" || exit 1
tc() { ls -d "$ROOT"/../toolchains/$1/bin/arm-linux-gcc 2>/dev/null | head -1; }
A5=${A5:-$(tc 'armv5-eabi--uclibc--*')}
A7=${A7:-$(tc 'armv7-eabihf--musl--*')}
RECS=${RECS:-/home/georgica/sync-build/live-050}
[ -n "$A5" ] && [ -x "$A5" ] || { echo "verify-phase: no armv5 compiler (A5=$A5)"; exit 1; }
[ -n "$A7" ] && [ -x "$A7" ] || { echo "verify-phase: no armv7hf compiler (A7=$A7)"; exit 1; }
V=$ROOT/build/verify-phase
rm -rf "$V"; mkdir -p "$V" || exit 1
now() { date +%s; }
t0=$(now)
fail=0

# run NAME COMMAND: run it (log NAME.log), then print the stage's line,
# made by line_NAME from the log
run() {
    n=$1; shift
    s=$(now)
    ( eval "$*" ) > "$V/$n.log" 2>&1; rc=$?
    t=$(( $(now) - s ))
    l=$(line_$n 2>/dev/null)
    if [ $rc -eq 0 ]; then printf '%-8s ok    %5ss  %s\n' "$n" "$t" "$l"
    else printf '%-8s FAIL  %5ss  %s (build/verify-phase/%s.log)\n' "$n" "$t" "$l" "$n"; tail -15 "$V/$n.log" | sed 's/^/    /'; fail=1; fi
}

line_armv5() { grep -E '^(ALL ARM CHECKS PASS|ARM CHECKS FAILED)' "$V/armv5.log" | tail -1; }
run armv5 "make -s -j16 check-arm PAR=16 ARM_CC='$A5' BUILD=build/a5"

# every target; the gate is each target's line, m68k-uclibc's reported only
line_ports() { cat "$V/ports.sum"; }
ports() {
    tools/check-ports.sh build/ports > "$V/ports.out" 2>&1
    cat "$V/ports.out"
    tl=$(grep -E '^[a-z0-9_-]+( \(reference\))?: ' "$V/ports.out")
    bad=$(echo "$tl" | grep -v '^m68k-uclibc:' | grep -E 'FAILED|DIFFER|NOT RUN|SKIPPED|exit [1-9]|exit missing')
    # m68k: its counts, and how many programs differ (the list is in ports.out)
    m68k=$(echo "$tl" | grep '^m68k-uclibc:' | awk -F'; ' '{ d = 0; for (i = 2; i <= NF; i++) if ($i ~ /^DIFFER:/) d = split($i, x, " ") - 1
        print $1 (d ? ", " d " differ" : "") }')
    n=$(echo "$tl" | grep -v '^m68k-uclibc:' | grep -vE 'FAILED|DIFFER|NOT RUN|SKIPPED|exit [1-9]|exit missing' | grep -c .)
    echo "$n targets identical to host-gcc; m68k (report only): ${m68k#m68k-uclibc: }" > "$V/ports.sum"
    [ -z "$bad" ] || { echo "$bad"; echo "$(echo "$bad" | grep -c .) targets FAILED; $(cat "$V/ports.sum")" > "$V/ports.sum"; return 1; }
}
run ports ports

line_armv7hf() { grep -E '^short-cmp: [0-9]' "$V/armv7hf.log" | tail -1; }
run armv7hf "{ make -s -j16 test-all PAR=16 BUILD=build/short EXTRA_CPPFLAGS=-DTEST_FORCE_SHORT > '$V/short.log' 2>&1 || { tail '$V/short.log'; exit 1; }; } &&
    make -s -j16 check-arm PAR=16 ARM_CC='$A7' BUILD=build/a7 && tools/short-cmp.sh build/short build/a7"

# the benches: made (armv5), then each recording on the host and on qemu,
# whose hash lines ("tx hash", "rx hash", the receiver's "hash") must agree
B=build/vp-bench
line_benches() { cat "$V/benches.sum" 2>/dev/null; }
benches() {
    make -s -j16 ARM_CC="$A5" BUILD=$B bench-v90 bench-x2 bench-k56 bench-v34 bench-v90a bench-v90c bench-v92c bench-bell || return 1
    n=0; bad=
    while read -r name rec prog opts; do
        [ -n "$name" ] || continue
        # shellcheck disable=SC2086
        "$B/$prog" $opts "$B/$rec" > "$V/bench-$name.host" 2>&1
        # shellcheck disable=SC2086
        qemu-arm-static "$B/$prog.arm" $opts "$B/$rec" > "$V/bench-$name.arm" 2>&1
        h=$(grep -oE '(tx |rx )?hash [0-9a-f]{8}' "$V/bench-$name.host")
        a=$(grep -oE '(tx |rx )?hash [0-9a-f]{8}' "$V/bench-$name.arm")
        if [ -n "$h" ] && [ "$h" = "$a" ]; then n=$((n + 1)); else bad="$bad $name"; fi
    done <<EOT
v90     v90_call.rec bench_v90
api     v90_call.rec bench_api
x2      x2_call.rec  bench_v90  -x
k56     k56_call.rec bench_v90  -k
v34     v34_call.rec bench_v34
v90a    v90a.rec     bench_v90a
v90c    v90c.rec     bench_v90c
v92c    v92c.rec     bench_v90c
bell103 bell_103.rec bench_bell
bell212 bell_212.rec bench_bell
EOT
    if [ -z "$bad" ]; then echo "host = qemu hashes: $n benches" > "$V/benches.sum"; return 0; fi
    echo "host != qemu hashes:$bad ($n agree)" > "$V/benches.sum"; return 1
}
run benches benches

# the costs, each target in a build tree of its own, all at once
line_costs() { echo "$(grep -c . "$ROOT/build/costs.txt" 2>/dev/null || echo 0) lines in build/costs.txt"; }
costs() {
    for c in x2 k56 v34 v90a v90c v92c bell; do
        ( make -s -j4 ARM_CC="$A5" BUILD=build/vp-cost/$c cost-$c > "$V/cost-$c.log" 2>&1; echo $? > "$V/cost-$c.rc" ) &
    done
    wait
    st=0
    for c in x2 k56 v34 v90a v90c v92c bell; do
        [ "$(cat "$V/cost-$c.rc")" = 0 ] || { echo "cost-$c FAILED ($V/cost-$c.log)"; st=1; }
        echo "# cost-$c"
        grep -E 'ARM instructions|ratio' "$V/cost-$c.log"
    done > "$ROOT/build/costs.txt"
    cat "$ROOT/build/costs.txt"
    return $st
}
run costs costs

line_crd() { tail -1 "$V/crd.log"; }
run crd "make -s crd-scan RECS='$RECS' | tee /dev/stderr | tail -1 | grep -q ' 0 CRd detections'"

t=$(( $(now) - t0 ))
if [ $fail -eq 0 ]; then echo "VERIFY-PHASE PASS ($t s)"; exit 0; fi
echo "VERIFY-PHASE FAILED ($t s)"
exit 1
