#!/bin/sh
# The whole test suite (check-arm: every test, the API test, the
# subdirectories) built for each target and run there (qemu-user, wine, or
# the host itself), then each target's test output compared with the host's:
# the library is fixed point, so every target must print the same thing.
#
#   tools/check-ports.sh [OUT [target ...]]
#
# OUT (default build/ports) gets one directory and one log per target.
# TOOLCHAINS (default ../toolchains) holds the cross compilers (Bootlin
# "stable-2023.08-1", musl where there is one); a target whose compiler or
# runner is missing is reported and skipped, not failed.
#
# Targets: name, compiler, runner (- for none). The first is the reference.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/ports}; [ $# -gt 0 ] && shift
TC=${TOOLCHAINS:-$ROOT/../toolchains}
JOBS=${JOBS:-4}
mkdir -p "$OUT"; OUT=$(cd "$OUT" && pwd)

tc() { set -- "$TC"/$1/bin/$2; [ -x "$1" ] && echo "$1" || echo "$TC/missing/$2"; }

TARGETS="
host-gcc       gcc                                                     -
host-clang     clang                                                   -
x86_64-musl    $(tc 'x86-64--musl--*' x86_64-linux-gcc)                qemu-x86_64-static
i686-musl      $(tc 'x86-i686--musl--*' i686-linux-gcc)                qemu-i386-static
armv5-uclibc   $(tc 'armv5-eabi--uclibc--*' arm-linux-gcc) qemu-arm-static
armv7hf-musl   $(tc 'armv7-eabihf--musl--*' arm-linux-gcc)             qemu-arm-static
aarch64-musl   $(tc 'aarch64--musl--*' aarch64-linux-gcc)              qemu-aarch64-static
aarch64be-musl $(tc 'aarch64be--musl--*' aarch64_be-linux-gcc)         qemu-aarch64_be-static
mips-musl      $(tc 'mips32--musl--*' mips-linux-gcc)                  qemu-mips-static
mipsel-musl    $(tc 'mips32el--musl--*' mipsel-linux-gcc)              qemu-mipsel-static
ppc-musl       $(tc 'powerpc-e500mc--musl--*' powerpc-linux-gcc)       qemu-ppc-static:-cpu:e500mc
ppc64-musl     $(tc 'powerpc64-power8--musl--*' powerpc64-linux-gcc)   qemu-ppc64-static:-cpu:power8
riscv64-musl   $(tc 'riscv64-lp64d--musl--*' riscv64-linux-gcc)        qemu-riscv64-static
s390x-glibc    $(tc 's390x-z13--glibc--*' s390x-linux-gcc)             qemu-s390x-static
m68k-uclibc    $(tc 'm68k-68xxx--uclibc--*' m68k-linux-gcc)            qemu-m68k-static:-cpu:m68040
win32-mingw    i686-w64-mingw32-gcc                                    $ROOT/tools/winerun
"

# what a run prints that is the same on every target: the tests' own output,
# without the build commands, the runner's banners and line ends
norm() {
    tr -d '\r' < "$1" | grep -vE '^(\[arm\] |== |make|ALL .*PASS|[^ ]*(gcc|clang) |/[^ ]*/(ar|ld|objcopy) |rm -f |ln -s)' \
        | grep -vE 'of CPU here|% of real time'
}

want=$(echo ${*:-$(echo "$TARGETS" | awk 'NF {print $1}')})
while read -r name cc run; do
    [ -n "$name" ] || continue
    case " $want " in *" $name "*) ;; *) continue ;; esac
    q=$(echo "$run" | tr ':' ' '); [ "$run" = - ] && q=""
    if ! command -v "$cc" >/dev/null 2>&1; then echo "$name: SKIPPED (no compiler $cc)"; echo skip > "$OUT/$name.status"; continue; fi
    if [ -n "$q" ] && ! command -v $(echo "$q" | cut -d' ' -f1) >/dev/null 2>&1; then echo "$name: SKIPPED (no $q)"; echo skip > "$OUT/$name.status"; continue; fi
    extra=""; case "$cc" in *mingw*) extra="-D__USE_MINGW_ANSI_STDIO=1" ;; esac
    (
        make -C "$ROOT" -j"$JOBS" BUILD="$OUT/$name" ARM_CC="$cc" QEMU="$q" \
             CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror $extra" check-arm > "$OUT/$name.log" 2>&1
        echo $? > "$OUT/$name.status"
    ) &
done <<EOF
$TARGETS
EOF
wait

ref=""; fail=0
for name in $want; do
    st=$(cat "$OUT/$name.status" 2>/dev/null || echo missing)
    [ "$st" = skip ] && continue
    norm "$OUT/$name.log" > "$OUT/$name.out"
    n=$(grep -c 'checks, 0 failures' "$OUT/$name.out")
    if [ "$st" != 0 ]; then echo "$name: FAILED (exit $st; $OUT/$name.log)"; fail=1; continue; fi
    if [ -z "$ref" ]; then ref=$name; echo "$name: pass, $n test programs (the reference)"; continue; fi
    if cmp -s "$OUT/$ref.out" "$OUT/$name.out"; then echo "$name: pass, $n test programs, output identical to $ref"
    else echo "$name: pass but output DIFFERS from $ref: diff $OUT/$ref.out $OUT/$name.out"; fail=1; fi
done
[ $fail -eq 0 ] && echo "ALL PORTS PASS" || { echo "PORTS FAILED"; exit 1; }
