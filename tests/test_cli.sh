#!/bin/sh
set -eu

solver=${1:?falta la ruta del solver}
tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT HUP INT TERM

"$solver" --help >"$tmp_dir/help.txt"
grep -q -- '--iterations N' "$tmp_dir/help.txt"
grep -q -- '--evaluate' "$tmp_dir/help.txt"
grep -q -- '--format FORMAT' "$tmp_dir/help.txt"
grep -q -- '--reserve-information-sets N' "$tmp_dir/help.txt"
grep -q -- 'raises-to-eight' "$tmp_dir/help.txt"
grep -q -- 'ordago' "$tmp_dir/help.txt"

if "$solver" >"$tmp_dir/no-args.out" 2>"$tmp_dir/no-args.err"; then
    echo "el solver sin argumentos debio fallar" >&2
    exit 1
fi
if "$solver" --iterations 0 >"$tmp_dir/zero.out" 2>"$tmp_dir/zero.err"; then
    echo "--iterations 0 debio fallar" >&2
    exit 1
fi
if "$solver" --iterations 1 --reserve-information-sets 0 \
    >"$tmp_dir/zero-reserve.out" 2>"$tmp_dir/zero-reserve.err"; then
    echo "--reserve-information-sets 0 debio fallar" >&2
    exit 1
fi

"$solver" --iterations 10 --seed 7 --save "$tmp_dir/first.cfr" \
    >"$tmp_dir/first.out"
"$solver" --load "$tmp_dir/first.cfr" --iterations 10 \
    --save "$tmp_dir/resumed.cfr" >"$tmp_dir/resumed.out"
"$solver" --iterations 20 --seed 7 --save "$tmp_dir/continuous.cfr" \
    --export-strategy "$tmp_dir/strategy.txt" >"$tmp_dir/continuous.out"
"$solver" --iterations 20 --seed 7 --reserve-information-sets 64 \
    --save "$tmp_dir/reserved.cfr" >"$tmp_dir/reserved.out"

cmp "$tmp_dir/resumed.cfr" "$tmp_dir/continuous.cfr"
cmp "$tmp_dir/reserved.cfr" "$tmp_dir/continuous.cfr"
grep -q 'report iterations=20' "$tmp_dir/continuous.out"
grep -q 'mus-strategy version=1' "$tmp_dir/strategy.txt"
grep -q 'posicion=' "$tmp_dir/strategy.txt"
grep -q 'pasar=' "$tmp_dir/strategy.txt"

"$solver" --load "$tmp_dir/continuous.cfr" \
    --export-strategy "$tmp_dir/loaded-strategy.txt"
cmp "$tmp_dir/strategy.txt" "$tmp_dir/loaded-strategy.txt"

"$solver" --format raises-to-eight --iterations 20 --seed 11 \
    --save "$tmp_dir/raises.cfr" \
    --export-strategy "$tmp_dir/raises-strategy.txt" \
    >"$tmp_dir/raises.out"
grep -q 'schema=mus.heads-up-single-hand.raises-to-eight/v1' \
    "$tmp_dir/raises-strategy.txt"
grep -Eq 'subir_a_(4|8)=' "$tmp_dir/raises-strategy.txt"

"$solver" --format ordago --iterations 1 --seed 13 \
    >"$tmp_dir/ordago.out"
grep -q 'report iterations=1' "$tmp_dir/ordago.out"

if "$solver" --format ordago --evaluate \
    >"$tmp_dir/ordago-evaluate.out" 2>"$tmp_dir/ordago-evaluate.err"; then
    echo "evaluar ordago desde la raiz debio fallar" >&2
    exit 1
fi
grep -q 'evaluacion desde la raiz no esta disponible' \
    "$tmp_dir/ordago-evaluate.err"

if "$solver" --load "$tmp_dir/raises.cfr" --iterations 1 \
    >"$tmp_dir/wrong-format.out" 2>"$tmp_dir/wrong-format.err"; then
    echo "cargar raises-to-eight como single-bet debio fallar" >&2
    exit 1
fi

if "$solver" --load "$tmp_dir/continuous.cfr" --iterations 1 \
    --export-strategy "$tmp_dir/strategy.txt" \
    >"$tmp_dir/overwrite.out" 2>"$tmp_dir/overwrite.err"; then
    echo "sobrescribir una exportacion debio fallar" >&2
    exit 1
fi

echo "Todas las pruebas de CLI pasaron."
