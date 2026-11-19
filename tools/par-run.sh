#!/bin/sh
# Run test programs in parallel, then print their output in the given order.
#
#   tools/par-run.sh LOGDIR "RUNNER" PREFIX PROG...
#
# Each PROG runs as `RUNNER PROG` (RUNNER may be empty, e.g. qemu-arm-static),
# PAR at a time (default 8), its stdout and stderr to LOGDIR/<name>.log.
# Afterwards, for each PROG in order: "PREFIX <name>" (when PREFIX is not
# empty) and its log. Exit status 1 when any PROG failed.
logdir=$1; runner=$2; prefix=$3; shift 3
par=${PAR:-8}
mkdir -p "$logdir" || exit 1
for p in "$@"; do
	n=$(basename "$p")
	rm -f "$logdir/$n.log" "$logdir/$n.rc"
done
printf '%s\n' "$@" | xargs -P "$par" -I{} sh -c \
	'n=$(basename "$1"); $2 "$1" > "$3/$n.log" 2>&1; echo $? > "$3/$n.rc"' sh {} "$runner" "$logdir"
fail=0
for p in "$@"; do
	n=$(basename "$p")
	[ -n "$prefix" ] && echo "$prefix ${n#arm_}"
	cat "$logdir/$n.log"
	rc=$(cat "$logdir/$n.rc" 2>/dev/null || echo 1)
	[ "$rc" = 0 ] || fail=1
done
exit $fail
