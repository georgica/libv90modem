# awk -f tools/ports-cmp.awk REF GOT
# For a target whose printf rounds up where the correct rounding is down
# (tools/printf-probe.c; uClibc-ng): REF and GOT must have the same lines,
# and where a line differs, the only differences allowed are numbers with
# decimals printed one unit larger in magnitude in their last digit (same
# sign, same number of decimals: 0.2 -> 0.3, -5.83 -> -5.84, 9.9 -> 10.0).
# Prints the number of lines that differ that way; exits 1 on any other
# difference (and prints the first such line pair).
function mag(s,   t) { t = s; sub(/^-/, "", t); sub(/\./, "", t); return t + 0 }
function decs(s) { return index(s, ".") ? length(s) - index(s, ".") : 0 }
function same_but_rounding(a, b,   ta, tb, na, nb, i, x, y) {
    na = tok(a, ta); nb = tok(b, tb)
    if (na != nb) return 0
    for (i = 1; i <= na; i++) {
        x = ta[i]; y = tb[i]
        if (x == y) continue
        if (x !~ /^-?[0-9]+\.[0-9]+$/ || y !~ /^-?[0-9]+\.[0-9]+$/) return 0
        if (decs(x) != decs(y) || (x ~ /^-/) != (y ~ /^-/)) return 0
        if (mag(y) != mag(x) + 1) return 0
    }
    return 1
}
# split a line into numbers and the text between them
function tok(s, t,   n, m) {
    n = 0
    while (match(s, /-?[0-9]+(\.[0-9]+)?/)) {
        if (RSTART > 1) t[++n] = substr(s, 1, RSTART - 1)
        t[++n] = substr(s, RSTART, RLENGTH)
        s = substr(s, RSTART + RLENGTH)
    }
    if (s != "") t[++n] = s
    return n
}
FNR == NR { ref[FNR] = $0; nref = FNR; next }
{
    ngot = FNR
    if (FNR > nref) { bad = 1; badline = "extra line " FNR ": " $0; exit }
    if ($0 == ref[FNR]) next
    if (same_but_rounding(ref[FNR], $0)) { rounded++; next }
    bad = 1; badline = "line " FNR ":\n< " ref[FNR] "\n> " $0; exit
}
END {
    if (!bad && ngot < nref) { bad = 1; badline = "missing lines from " ngot + 1 }
    if (bad) { print badline; exit 1 }
    print rounded + 0
}
