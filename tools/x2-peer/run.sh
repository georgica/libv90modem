#!/bin/sh
# Call our answerer from an emulated U.S. Robotics Courier (README.md).
#
#   tools/x2-peer/run.sh BRIDGE OUTDIR    (make test-x2-courier runs it)
#
# Environment: COURIER_EMU (courier-emu's checkout; required), PAD (0|3|6),
# MODS, COURIER_ROM (default: the 4.03 board image in COURIER_EMU),
# COURIER_AT (the Courier's settings before the dial), COURIER_INSTRUCTIONS
# (its budget, default 400M: the call ends there, at 116 s of line, some 35 s
# of wall time), TIMEOUT (wall seconds, default 3600). Prints the Courier's CONNECT line, its ATI11, and our
# state; exit 0 when both sides reached data mode, otherwise 1.
set -u
bridge=$1
out=$2
emu=${COURIER_EMU:?}
rom=${COURIER_ROM:-$emu/artifacts/courier-board-21210-capture-403/courier-board.rom}
at=${COURIER_AT:-ATX1S32=66S54=0S58=48&A3&B1Q0}
budget=${COURIER_INSTRUCTIONS:-400000000}
limit=${TIMEOUT:-3600}

case ${PAD:-0} in 0|3|6) ;; *) echo "x2-peer: PAD=${PAD} (0, 3 or 6)"; exit 1;; esac
case ${MODS:-} in ''|none|x2) ;; *) echo "x2-peer: MODS=${MODS} ('', none or x2)"; exit 1;; esac
[ -x "$emu/courier" ] || { echo "x2-peer: no courier-emu at $emu"; exit 1; }
[ -f "$rom" ] || { echo "x2-peer: no ROM image $rom"; exit 1; }
mkdir -p "$out"
sockdir=$(mktemp -d "${TMPDIR:-/tmp}/x2-peer.XXXXXX") || exit 1
sock=$sockdir/line.sock
courier=
trap '[ -n "$courier" ] && kill $courier 2>/dev/null; rm -rf "$sockdir"' EXIT
trap 'exit 1' INT TERM

echo "x2-peer: Courier $rom"
echo "x2-peer: $at, DT5551234; budget $budget instructions; log in $out"
# The Courier dials and answers its line on the socket; the digital line
# leaves the levels to the bridge. After CONNECT: +++ and ATI11.
COURIER_LINE_DIGITAL=1 COURIER_LINE_FRAME_MS=20 timeout "$limit" \
    "$emu/courier" run "$rom" --instructions "$budget" \
    --line-link "$sock" --line-listen --with-dsp \
    --nvram-fixture idsdl403 --board-id 7 --dip-preset default --tick-ms 5 \
    --at "${at}DT5551234" --serial-input-after 5000000 \
    --send-after-connect '+++' --send-after-connect "$(printf 'ATI11\r')" \
    --summary > "$out/courier.json" 2> "$out/courier.log" &
courier=$!
timeout "$limit" "$bridge" "$sock" -p "${PAD:-0}" -m "${MODS:-}" > "$out/bridge.log" 2>&1
ours=$?
# a bridge that stopped early leaves the Courier waiting on its line
[ $ours -eq 0 ] || kill $courier 2>/dev/null
wait $courier
theirs=$?
courier=
cat "$out/bridge.log"

# the Courier's terminal, from its report
python3 - "$out/courier.json" "$out/courier-serial.txt" <<'EOF'
import json, sys
try:
    text = json.load(open(sys.argv[1])).get("serial_text", "")
except (OSError, ValueError) as e:
    text = ""
    print("x2-peer: no report from the Courier (%s)" % e)
open(sys.argv[2], "w").write(text)
EOF
tr -d '\r' < "$out/courier-serial.txt" | sed -n 's/^/courier: /p' | grep -v '^courier: *$'
echo "x2-peer: Courier exit $theirs, bridge exit $ours"
# x2 not offered: the Courier must say V34; offered: x2 on its CONNECT line
# and in its ATI11
if [ "${MODS:-}" = x2 ]; then want='CONNECT.*/[Xx]2'; else want='CONNECT.*V34'; fi
if [ $ours -eq 0 ] && grep -q "$want" "$out/courier-serial.txt" \
   && { [ "${MODS:-}" != x2 ] || grep -q 'Modulation.*[Xx]2' "$out/courier-serial.txt"; }; then
    echo "x2-peer: PASS (both sides in data mode)"
    exit 0
fi
echo "x2-peer: FAIL"
exit 1
