#!/bin/sh
set -eu

solver=${1:?falta la ruta del solver}
tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT HUP INT TERM

"$solver" --help >"$tmp_dir/help.txt"
grep -q -- '--iterations N' "$tmp_dir/help.txt"
grep -q -- '--evaluate' "$tmp_dir/help.txt"

if "$solver" >"$tmp_dir/no-args.out" 2>"$tmp_dir/no-args.err"; then
    echo "el solver sin argumentos debio fallar" >&2
    exit 1
fi
if "$solver" --iterations 0 >"$tmp_dir/zero.out" 2>"$tmp_dir/zero.err"; then
    echo "--iterations 0 debio fallar" >&2
    exit 1
fi

"$solver" --iterations 10 --seed 7 --save "$tmp_dir/first.cfr" \
    >"$tmp_dir/first.out"
"$solver" --load "$tmp_dir/first.cfr" --iterations 10 \
    --save "$tmp_dir/resumed.cfr" >"$tmp_dir/resumed.out"
"$solver" --iterations 20 --seed 7 --save "$tmp_dir/continuous.cfr" \
    --export-strategy "$tmp_dir/strategy.txt" >"$tmp_dir/continuous.out"

cmp "$tmp_dir/resumed.cfr" "$tmp_dir/continuous.cfr"
grep -q 'report iterations=20' "$tmp_dir/continuous.out"
grep -q 'mus-strategy version=1' "$tmp_dir/strategy.txt"
grep -q 'posicion=' "$tmp_dir/strategy.txt"
grep -q 'pasar=' "$tmp_dir/strategy.txt"

if "$solver" --load "$tmp_dir/continuous.cfr" --iterations 1 \
    --export-strategy "$tmp_dir/strategy.txt" \
    >"$tmp_dir/overwrite.out" 2>"$tmp_dir/overwrite.err"; then
    echo "sobrescribir una exportacion debio fallar" >&2
    exit 1
fi

echo "Todas las pruebas de CLI pasaron."
