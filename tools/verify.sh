#!/bin/sh
# make verify: the per-commit check (docs/superpowers/specs/
# 2026-11-22-audit-refactor-optimize-design.md 7.4). Six stages in
# parallel, each in a build tree of its own, each printing one line:
#   host      make -j16 check PAR=16, then tools/golden.sh check
#   replay    tools/replay-proof.sh check (RECS; V90M_NO_RECS=1 skips it)
#   bench     tools/bench-hashes.sh check
#   ubsan     make -j16 check-ubsan PAR=16 (the fixtures too)
#   armv7hf   make check-arm with the armv7hf musl compiler (TEST_SHORT),
#             less tools/ports-slow.txt, and the host short set
#             (-DTEST_FORCE_SHORT); then tools/short-cmp.sh compares them
#   fixed     make check-fixed and make check-symbols
# then "VERIFY PASS (<seconds> s)", or "VERIFY FAILED" and the end of the
# log of each stage that failed. Logs: build/verify/log/<stage>.log; the
# stages' build trees: build (host), build/ubsan, build/a7, build/short,
# build/verify/{replay,bench,fixed}.
#
#   A7=<armv7hf arm-linux-gcc>   default ../toolchains/armv7-eabihf--musl--*
#   RECS=<recordings>            default /home/georgica/sync-build/live-050
#   PAR=<jobs per stage>         default 16
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
unset V92_VERBOSE V32_DEBUG V92_TRACE RXBITS TXBITS TXOWN
TMPDIR=${TMPDIR:-$ROOT/build/tmp}; export TMPDIR
mkdir -p "$TMPDIR" || exit 1
P=${PAR:-16}
A7=${A7:-$(ls -d "$ROOT"/../toolchains/armv7-eabihf--musl--*/bin/arm-linux-gcc 2>/dev/null | head -1)}
RECS=${RECS:-/home/georgica/sync-build/live-050}; export RECS
[ -n "$A7" ] && [ -x "$A7" ] || { echo "verify: no armv7hf compiler (A7=$A7)"; exit 1; }
V=$ROOT/build/verify/log
rm -rf "$V"; mkdir -p "$V" || exit 1
# make's own variables would reach every stage
unset MAKEFLAGS MFLAGS MAKELEVEL BUILD

now() { date +%s; }
t0=$(now)

# stage NAME COMMAND: run in the background; NAME.rc, NAME.time, NAME.log
stage() {
    n=$1; shift
    ( s=$(now); sh -c "$*" > "$V/$n.log" 2>&1; echo $? > "$V/$n.rc"; echo $(( $(now) - s )) > "$V/$n.time" ) &
}

stage host "make -s -j$P check PAR=$P && tools/golden.sh check build"
stage replay "BUILD=build/verify/replay tools/replay-proof.sh check"
stage bench "BUILD=build/verify/bench tools/bench-hashes.sh check"
stage ubsan "make -s -j$P check-ubsan PAR=$P"
stage armv7hf "{ make -s -j$P check-arm PAR=$P ARM_CC='$A7' BUILD=build/a7 ARM_SKIP=tools/ports-slow.txt > build/verify/log/a7.log 2>&1; echo \$? > build/verify/log/a7.rc; } &
    { make -s -j$P test-all PAR=$P BUILD=build/short EXTRA_CPPFLAGS=-DTEST_FORCE_SHORT > build/verify/log/short.log 2>&1; echo \$? > build/verify/log/short.rc; } &
    wait
    [ \"\$(cat build/verify/log/a7.rc)\" = 0 ] || { tail -20 build/verify/log/a7.log; echo 'armv7hf: check-arm FAILED (build/verify/log/a7.log)'; exit 1; }
    [ \"\$(cat build/verify/log/short.rc)\" = 0 ] || { tail -20 build/verify/log/short.log; echo 'armv7hf: the host short set FAILED (build/verify/log/short.log)'; exit 1; }
    tools/short-cmp.sh build/short build/a7"
stage fixed "make -s BUILD=build/verify/fixed check-fixed && make -s -j$P BUILD=build/verify/fixed check-symbols"
wait

# one line per stage: the line of its log that says what it found
summary() {
    case $1 in
    host) grep -E '^golden: ' "$V/host.log" | tail -1 ;;
    replay) grep -E '^replay proof: ' "$V/replay.log" | tail -1 ;;
    bench) grep -E '^bench hashes: ' "$V/bench.log" | tail -1 ;;
    ubsan) grep -E '^ALL UBSAN CHECKS PASS' "$V/ubsan.log" | tail -1 ;;
    armv7hf) { grep -E '^par-run: .* skipped' "$V/a7.log" | tail -1 | sed 's/^par-run: //'; grep -E '^short-cmp: ' "$V/armv7hf.log" | tail -1; } | paste -sd ';' - | sed 's/;/; /' ;;
    fixed) grep -E '^(fixed|symbols): ' "$V/fixed.log" | paste -sd ';' - | sed 's/;/; /' ;;
    esac
}
fail=0
for n in host replay bench ubsan armv7hf fixed; do
    rc=$(cat "$V/$n.rc" 2>/dev/null || echo 1)
    s=$(summary "$n")
    if [ "$rc" = 0 ]; then
        printf '%-8s ok    %4ss  %s\n' "$n" "$(cat "$V/$n.time")" "$s"
    else
        printf '%-8s FAIL  %4ss  %s (build/verify/log/%s.log)\n' "$n" "$(cat "$V/$n.time" 2>/dev/null || echo ?)" "$s" "$n"
        fail=1
    fi
done
t=$(( $(now) - t0 ))
if [ $fail -eq 0 ]; then echo "VERIFY PASS ($t s)"; exit 0; fi
for n in host replay bench ubsan armv7hf fixed; do
    [ "$(cat "$V/$n.rc" 2>/dev/null || echo 1)" = 0 ] && continue
    echo "== $n (build/verify/log/$n.log, last 15 lines)"
    tail -15 "$V/$n.log"
done
echo "VERIFY FAILED ($t s)"
exit 1
