#!/bin/sh
# Call our answerer from Motorola's SM56 soft modem (README.md).
#
#   tools/k56-peer/run.sh BRIDGE OUTDIR    (make test-k56-sm56 runs it)
#
# Environment: DMODEM (a D-Modem checkout at c06a380d, built: its
# build/dsp/test/half_sm56; required), MODS ('', none or k56), RBS (robbed
# bits toward SM56, 0..3f), DOWN (our max_down_bps), STRICT_RENEG (1: a rate
# change and a second burst after it as well), KB (KiB each way, default 4),
# SM56_MS (SM56's modulation command; default AT+MS=K56FLEX,0), LOOP (the analogue loop's fractional delay to
# SM56, or none; default 0.45, none with RBS), LINE_SECONDS (default 120), TIMEOUT
# (wall seconds, default 600). Prints SM56's CONNECT line, its AT&V1 report
# and our state; exit 0 when both sides reached data mode and the octets
# each side sent arrived at the other byte for byte, otherwise 1.
set -u
bridge=$1
out=$2
dm=${DMODEM:?}
half=${HALF_SM56:-$dm/build/dsp/test/half_sm56}
kb=${KB:-4}
case ${MODS:-} in ''|none|k56) ;; *) echo "k56-peer: MODS=${MODS} ('', none or k56)"; exit 1;; esac
case ${STRICT_RENEG:-0} in 1) strict=-n;; 0) strict=;; *) echo "k56-peer: STRICT_RENEG=${STRICT_RENEG} (0 or 1)"; exit 1;; esac
[ -x "$half" ] || { echo "k56-peer: no $half (build D-Modem: README.md)"; exit 1; }
mkdir -p "$out"
rm -f "$out"/*.bin "$out"/bridge.log "$out"/sm56.log
set -- -m "${MODS:-none}" -r "${RBS:-0}" -d "${DOWN:-0}" -k "$kb" -s "${LINE_SECONDS:-120}"
[ -n "${SM56_MS:-}" ] && set -- "$@" -a "$SM56_MS"
[ -n "${LOOP:-}" ] && set -- "$@" -l "$LOOP"
[ -n "$strict" ] && set -- "$@" $strict
echo "k56-peer: $half; log in $out"
timeout "${TIMEOUT:-600}" "$bridge" "$half" "$out" "$@" > "$out/bridge.log" 2>&1
ours=$?
cat "$out/bridge.log"
n=$((kb * 1024))
[ -n "$strict" ] && n=$((2 * n))
same() {   # $1 sent, $2 received: n octets each, the same
    [ -f "$1" ] && [ -f "$2" ] && [ "$(wc -c < "$1")" -ge $n ] && [ "$(wc -c < "$2")" -ge $n ] \
        && cmp -s -n $n "$1" "$2"
}
ok=1
if same "$out/ours-sent.bin" "$out/sm56-recv.bin"; then echo "k56-peer: to SM56: $n octets, identical"
else echo "k56-peer: to SM56: NOT the $n octets sent"; ok=0; fi
if same "$out/sm56-sent.bin" "$out/ours-recv.bin"; then echo "k56-peer: to us: $n octets, identical"
else echo "k56-peer: to us: NOT the $n octets sent"; ok=0; fi
echo "k56-peer: bridge exit $ours"
if [ $ours -eq 0 ] && [ $ok -eq 1 ] && grep -q 'sm56: CONNECT' "$out/bridge.log" \
   && grep -q 'ours: CONNECT' "$out/bridge.log"; then
    echo "k56-peer: PASS (both sides in data mode, $n octets each way identical)"
    exit 0
fi
echo "k56-peer: FAIL"
exit 1
