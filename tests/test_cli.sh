#!/bin/sh
set -eu

solver=${1:?falta la ruta del solver}
tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT HUP INT TERM

"$solver" --help >"$tmp_dir/help.txt"
grep -q -- '--iterations N' "$tmp_dir/help.txt"
grep -q -- '--threads N' "$tmp_dir/help.txt"
grep -q -- '--evaluate' "$tmp_dir/help.txt"
grep -q -- '--format FORMAT' "$tmp_dir/help.txt"
grep -q -- '--reserve-information-sets N' "$tmp_dir/help.txt"
grep -q -- '--load-compressed FILE' "$tmp_dir/help.txt"
grep -q -- '--save-compressed FILE' "$tmp_dir/help.txt"
grep -q -- '--preload-ordago-compressed FILE' "$tmp_dir/help.txt"
grep -q -- 'raises-to-eight' "$tmp_dir/help.txt"
grep -q -- 'ordago' "$tmp_dir/help.txt"
"$solver" --help --format ordago --evaluate \
    >"$tmp_dir/combined-help.txt"
grep -q -- '--evaluate' "$tmp_dir/combined-help.txt"

if "$solver" >"$tmp_dir/no-args.out" 2>"$tmp_dir/no-args.err"; then
    echo "el solver sin argumentos debio fallar" >&2
    exit 1
fi
if "$solver" --iterations 0 >"$tmp_dir/zero.out" 2>"$tmp_dir/zero.err"; then
    echo "--iterations 0 debio fallar" >&2
    exit 1
fi
if "$solver" --iterations 1 --threads 0 \
    >"$tmp_dir/zero-threads.out" 2>"$tmp_dir/zero-threads.err"; then
    echo "--threads 0 debio fallar" >&2
    exit 1
fi
if "$solver" --evaluate --threads 2 \
    >"$tmp_dir/evaluate-threads.out" 2>"$tmp_dir/evaluate-threads.err"; then
    echo "--threads sin entrenamiento debio fallar" >&2
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
"$solver" --iterations 20 --threads 1 --seed 7 \
    --save "$tmp_dir/one-thread.cfr" >"$tmp_dir/one-thread.out"
"$solver" --iterations 20 --seed 7 \
    --save-compressed "$tmp_dir/continuous.cfr.zst" \
    >"$tmp_dir/compressed.out"

cmp "$tmp_dir/resumed.cfr" "$tmp_dir/continuous.cfr"
cmp "$tmp_dir/reserved.cfr" "$tmp_dir/continuous.cfr"
cmp "$tmp_dir/one-thread.cfr" "$tmp_dir/continuous.cfr"
grep -q 'report iterations=20' "$tmp_dir/continuous.out"
grep -q 'mus-strategy version=1' "$tmp_dir/strategy.txt"
grep -q 'posicion=' "$tmp_dir/strategy.txt"
grep -q 'pasar=' "$tmp_dir/strategy.txt"

"$solver" --iterations 12 --threads 3 --report-every 4 --seed 17 \
    --save "$tmp_dir/parallel.cfr" >"$tmp_dir/parallel.out"
grep -q 'report iterations=12 threads=3' "$tmp_dir/parallel.out"
"$solver" --load "$tmp_dir/parallel.cfr" --iterations 5 --threads 2 \
    --save "$tmp_dir/parallel-resumed.cfr" \
    >"$tmp_dir/parallel-resumed.out"
grep -q 'report iterations=17 threads=2' "$tmp_dir/parallel-resumed.out"
"$solver" --load "$tmp_dir/parallel-resumed.cfr" \
    --export-strategy "$tmp_dir/parallel-strategy.txt"
grep -q 'training_iterations=17' "$tmp_dir/parallel-strategy.txt"

"$solver" --iterations 12 --threads 3 --report-every 5 --seed 17 \
    >"$tmp_dir/partial-workers.out"
grep -q 'report iterations=5 threads=3' "$tmp_dir/partial-workers.out"
grep -q 'report iterations=10 threads=3' "$tmp_dir/partial-workers.out"
grep -q 'report iterations=12 threads=3' "$tmp_dir/partial-workers.out"
[ "$(grep -c '^report ' "$tmp_dir/partial-workers.out")" -eq 3 ]
"$solver" --iterations 5 --threads 8 --report-every 2 --seed 17 \
    >"$tmp_dir/fewer-active-workers.out"
grep -q 'report iterations=5 threads=8' "$tmp_dir/fewer-active-workers.out"
[ "$(grep -c '^report ' "$tmp_dir/fewer-active-workers.out")" -eq 3 ]
"$solver" --iterations 3 --threads 8 --report-every 100 --seed 17 \
    >"$tmp_dir/large-report-interval.out"
grep -q 'report iterations=3 threads=8' "$tmp_dir/large-report-interval.out"
[ "$(grep -c '^report ' "$tmp_dir/large-report-interval.out")" -eq 1 ]
"$solver" --iterations 20 --threads 1 --report-every 1 --seed 7 \
    --save "$tmp_dir/frequent-single-thread.cfr" \
    >"$tmp_dir/frequent-single-thread.out"
cmp "$tmp_dir/frequent-single-thread.cfr" "$tmp_dir/continuous.cfr"

"$solver" --load "$tmp_dir/continuous.cfr" \
    --export-strategy "$tmp_dir/loaded-strategy.txt"
cmp "$tmp_dir/strategy.txt" "$tmp_dir/loaded-strategy.txt"
"$solver" --load-compressed "$tmp_dir/continuous.cfr.zst" \
    --export-strategy "$tmp_dir/compressed-strategy.txt"
cmp "$tmp_dir/strategy.txt" "$tmp_dir/compressed-strategy.txt"

"$solver" --format raises-to-eight --iterations 20 --seed 11 \
    --save "$tmp_dir/raises.cfr" \
    --export-strategy "$tmp_dir/raises-strategy.txt" \
    >"$tmp_dir/raises.out"
grep -q 'schema=mus.heads-up-single-hand.raises-to-eight/v1' \
    "$tmp_dir/raises-strategy.txt"
grep -Eq 'subir_a_(4|8)=' "$tmp_dir/raises-strategy.txt"

"$solver" --format ordago --iterations 2 --threads 1 --seed 13 \
    --save "$tmp_dir/ordago-auto-reserve.cfr" >"$tmp_dir/ordago-auto-reserve.out"
"$solver" --format ordago --iterations 2 --threads 1 --seed 13 \
    --reserve-information-sets 100000 \
    --save "$tmp_dir/ordago-explicit-reserve.cfr" >"$tmp_dir/ordago-explicit-reserve.out"
cmp "$tmp_dir/ordago-auto-reserve.cfr" "$tmp_dir/ordago-explicit-reserve.cfr"

"$solver" --format ordago --iterations 2 --threads 2 --seed 13 \
    --save "$tmp_dir/ordago.cfr" \
    --export-strategy "$tmp_dir/ordago-strategy.txt" \
    >"$tmp_dir/ordago.out"
grep -q 'report iterations=2' "$tmp_dir/ordago.out"
grep -Eq 'schema=mus.heads-up-single-hand.ordago-with-mus/v9-muses-[12]' \
    "$tmp_dir/ordago-strategy.txt"
grep -q 'envidar_' "$tmp_dir/ordago-strategy.txt"
grep -q 'ordago=' "$tmp_dir/ordago-strategy.txt"
grep -q 'envites=' "$tmp_dir/ordago-strategy.txt"
grep -q 'usados_j0=' "$tmp_dir/ordago-strategy.txt"
grep -q 'usados_j1=' "$tmp_dir/ordago-strategy.txt"
grep -q 'cambiar_' "$tmp_dir/ordago-strategy.txt"

# Exercises repeated hidden histories while another worker updates their
# shared information sets. C-CFR must retain one strategy snapshot per
# traversal instead of mixing a cached action with a later distribution.
"$solver" --format ordago --iterations 10 --threads 2 --seed 0 \
    --report-every 10 >"$tmp_dir/ordago-concurrent-snapshot.out"
grep -q 'report iterations=10 threads=2' \
    "$tmp_dir/ordago-concurrent-snapshot.out"

"$solver" --format ordago --load "$tmp_dir/ordago.cfr" --evaluate \
    >"$tmp_dir/ordago-evaluate.out"
grep -q 'evaluation scope=bounded-final-lances iterations=2' \
    "$tmp_dir/ordago-evaluate.out"

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

# Train consecutive diagonals, then resume the remaining score cells.
"$solver" --format ordago-score --target 3 --score 1:1 --iterations 2 \
    --seed 7 --evaluation-samples 50 --continuation "$tmp_dir/score" \
    >"$tmp_dir/score-partial.out"
grep -q 'score-cell score=1:1 target=3 iterations=2' "$tmp_dir/score-partial.out"
test -f "$tmp_dir/score/score-01-01.cfr.zst"
test ! -f "$tmp_dir/score/score-00-00.cfr.zst"
cp "$tmp_dir/score/score-01-01.cfr.zst" "$tmp_dir/preserved.cfr.zst"
"$solver" --format ordago-score --target 3 --score 0:0 --iterations 2 \
    --seed 7 --evaluation-samples 50 --continuation "$tmp_dir/score" \
    --evaluate --export-strategy "$tmp_dir/score-strategy.txt" \
    >"$tmp_dir/score-resumed.out"
grep -q 'evaluation scope=score-average-policy score=0:0 samples=50' \
    "$tmp_dir/score-resumed.out"
grep -q 'schema=mus.ordago-score/v3' "$tmp_dir/score-strategy.txt"
grep -q 'marcador:0-0/objetivo:3' "$tmp_dir/score-strategy.txt"
cmp "$tmp_dir/preserved.cfr.zst" "$tmp_dir/score/score-01-01.cfr.zst"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 \
    --evaluation-samples 50 --continuation "$tmp_dir/score-continuous" \
    >"$tmp_dir/score-continuous.out"
cmp "$tmp_dir/score/values.txt" "$tmp_dir/score-continuous/values.txt"
for checkpoint in "$tmp_dir/score/"*.zst; do
    name=${checkpoint##*/}
    cmp "$checkpoint" "$tmp_dir/score-continuous/$name"
done
"$solver" --format ordago-score --target 3 --continuation "$tmp_dir/score" \
    --export-strategy "$tmp_dir/score-reloaded.txt" >"$tmp_dir/score-reloaded.out"
cmp "$tmp_dir/score-strategy.txt" "$tmp_dir/score-reloaded.txt"
# The normal target uses the same continuation and checkpoint workflow.
"$solver" --format ordago-score --score 37:36 --iterations 2 --seed 7 \
    --evaluation-samples 50 --continuation "$tmp_dir/score-40" \
    --export-strategy "$tmp_dir/score-40.txt" >"$tmp_dir/score-40.out"
grep -q 'marcador:37-36/objetivo:40' "$tmp_dir/score-40.txt"

if "$solver" --format ordago-score --target 5 --continuation "$tmp_dir/score" \
    --evaluate >"$tmp_dir/score-wrong-target.out" 2>"$tmp_dir/score-wrong-target.err"; then
    echo "cargar una tabla con otro objetivo debio fallar" >&2
    exit 1
fi
if "$solver" --format ordago-score --target 3 --score 3:0 \
    --continuation "$tmp_dir/score" --evaluate \
    >"$tmp_dir/score-terminal.out" 2>"$tmp_dir/score-terminal.err"; then
    echo "un marcador inicial terminal debio fallar" >&2
    exit 1
fi
if "$solver" --format ordago --score 0:0 --iterations 1 \
    >"$tmp_dir/score-old-format.out" 2>"$tmp_dir/score-old-format.err"; then
    echo "las opciones de marcador requieren ordago-score" >&2
    exit 1
fi
# Transfer the same source policy from plain and compressed checkpoints.
"$solver" --format ordago --iterations 2 --seed 17 --reserve-information-sets 256 \
    --save "$tmp_dir/prior.cfr" >"$tmp_dir/prior.out"
"$solver" --format ordago --iterations 2 --seed 17 --reserve-information-sets 256 \
    --save-compressed "$tmp_dir/prior.cfr.zst" >"$tmp_dir/prior-zstd.out"
cp "$tmp_dir/prior.cfr" "$tmp_dir/prior-preserved.cfr"
"$solver" --format ordago-score --target 3 --score 1:1 --iterations 2 --seed 7 \
    --evaluation-samples 20 --continuation "$tmp_dir/preloaded-resume" \
    --preload-ordago "$tmp_dir/prior.cfr" --preload-weight 2 \
    >"$tmp_dir/preloaded-partial.out"
grep -Eq 'score-preload score=1:1 source_nodes=[1-9][0-9]* imported_nodes=[1-9]' \
    "$tmp_dir/preloaded-partial.out"
cp "$tmp_dir/preloaded-resume/score-01-01.cfr.zst" "$tmp_dir/preloaded-preserved.cfr.zst"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 \
    --evaluation-samples 20 --continuation "$tmp_dir/preloaded-resume" \
    --preload-ordago "$tmp_dir/prior.cfr" --preload-weight 2 \
    --export-strategy "$tmp_dir/preloaded.txt" >"$tmp_dir/preloaded-resume.out"
grep -q 'training_iterations=2' "$tmp_dir/preloaded.txt"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 \
    --evaluation-samples 20 --continuation "$tmp_dir/preloaded-zstd" \
    --preload-ordago-compressed "$tmp_dir/prior.cfr.zst" --preload-weight 2 \
    --evaluate >"$tmp_dir/preloaded-zstd.out"
cmp "$tmp_dir/prior.cfr" "$tmp_dir/prior-preserved.cfr"
cmp "$tmp_dir/preloaded-resume/score-01-01.cfr.zst" "$tmp_dir/preloaded-preserved.cfr.zst"
cmp "$tmp_dir/preloaded-resume/values.txt" "$tmp_dir/preloaded-zstd/values.txt"
for checkpoint in "$tmp_dir/preloaded-resume/"*.zst; do
    name=${checkpoint##*/}
    cmp "$checkpoint" "$tmp_dir/preloaded-zstd/$name"
done
# Saved target checkpoints no longer need the source checkpoint.
"$solver" --format ordago-score --target 3 --continuation "$tmp_dir/preloaded-resume" \
    --export-strategy "$tmp_dir/preloaded-reloaded.txt" >"$tmp_dir/preloaded-reloaded.out"
cmp "$tmp_dir/preloaded.txt" "$tmp_dir/preloaded-reloaded.txt"
mkdir "$tmp_dir/score-v2"
sed 's/mus\.ordago-score\/v3/mus.ordago-score\/v2/' "$tmp_dir/score/values.txt" \
    >"$tmp_dir/score-v2/values.txt"
cp "$tmp_dir/score-v2/values.txt" "$tmp_dir/score-v2-preserved.txt"
if "$solver" --format ordago-score --target 3 --continuation "$tmp_dir/score-v2" \
    --iterations 1 >"$tmp_dir/score-v2.out" 2>"$tmp_dir/score-v2.err"; then
    echo "la tabla con las reglas incorrectas v2 no debe reanudarse" >&2
    exit 1
fi
cmp "$tmp_dir/score-v2/values.txt" "$tmp_dir/score-v2-preserved.txt"
test ! -f "$tmp_dir/score-v2/training.lock"
for source in "$tmp_dir/raises.cfr" "$tmp_dir/continuous.cfr"; do
    if "$solver" --format ordago-score --target 3 --iterations 1 \
        --continuation "$tmp_dir/preload-invalid" --preload-ordago "$source" \
        >"$tmp_dir/preload-invalid.out" 2>"$tmp_dir/preload-invalid.err"; then
        echo "precargar un formato distinto de ordago debio fallar" >&2
        exit 1
    fi
done
test ! -d "$tmp_dir/preload-invalid"
for weight in 0 -1 nan inf 1e999; do
    if "$solver" --format ordago-score --target 3 --iterations 1 \
        --continuation "$tmp_dir/preload-invalid" --preload-ordago "$tmp_dir/prior.cfr" \
        --preload-weight "$weight" >"$tmp_dir/preload-weight.out" 2>"$tmp_dir/preload-weight.err"; then
        echo "un peso de precarga invalido debio fallar" >&2
        exit 1
    fi
done
if "$solver" --format ordago --iterations 1 --preload-ordago "$tmp_dir/prior.cfr" \
    >"$tmp_dir/preload-old.out" 2>"$tmp_dir/preload-old.err"; then
    echo "la precarga requiere ordago-score" >&2
    exit 1
fi
if "$solver" --format ordago-score --target 3 --continuation "$tmp_dir/preloaded-resume" \
    --evaluate --preload-ordago "$tmp_dir/prior.cfr" \
    >"$tmp_dir/preload-eval.out" 2>"$tmp_dir/preload-eval.err"; then
    echo "la precarga requiere entrenamiento" >&2
    exit 1
fi

# A checkpoint from a different score must not load in the selected cell.
cp "$tmp_dir/score/score-01-01.cfr.zst" "$tmp_dir/score/score-00-00.cfr.zst"
if "$solver" --format ordago-score --target 3 --continuation "$tmp_dir/score" \
    --evaluate >"$tmp_dir/score-mixed.out" 2>"$tmp_dir/score-mixed.err"; then
    echo "cargar un checkpoint de otro marcador debio fallar" >&2
    exit 1
fi
rm "$tmp_dir/score/score-01-01.cfr.zst"
if "$solver" --format ordago-score --target 3 --continuation "$tmp_dir/score" \
    --iterations 1 >"$tmp_dir/score-missing.out" 2>"$tmp_dir/score-missing.err"; then
    echo "una tabla sin sus estrategias debio fallar" >&2
    exit 1
fi

echo "Todas las pruebas de CLI pasaron."
