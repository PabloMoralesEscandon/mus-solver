#!/bin/sh
set -eu

solver=${1:?falta la ruta del solver}
tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT HUP INT TERM

"$solver" --help >"$tmp_dir/help.txt"
grep -q -- '--iterations N' "$tmp_dir/help.txt"
grep -q -- '--threads N' "$tmp_dir/help.txt"
grep -q -- '--threads-per-cell N' "$tmp_dir/help.txt"
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

# --help still parses each option, without starting oversized workloads.
for option in --iterations --report-every --threads --threads-per-cell \
              --reserve-information-sets --evaluation-samples --target --seed; do
    for value in '-1' ' -1' '+1' ' 1' '1 ' '1x' '184467440737095516160' ''; do
        if "$solver" --help "$option" "$value" >"$tmp_dir/unsigned.out" 2>"$tmp_dir/unsigned.err"; then
            echo "$option acepto un entero sin signo invalido: [$value]" >&2
            exit 1
        else
            [ "$?" -eq 2 ]
        fi
    done
    "$solver" --help "$option" 01 >"$tmp_dir/unsigned-valid.out"
    if [ "$option" != --seed ]; then
        if "$solver" --help "$option" 0 >"$tmp_dir/unsigned-zero.out" 2>"$tmp_dir/unsigned-zero.err"; then
            echo "$option acepto cero para una cantidad positiva" >&2
            exit 1
        else
            [ "$?" -eq 2 ]
        fi
    fi
done
"$solver" --help --seed 0 >"$tmp_dir/zero-seed.out"
"$solver" --help --seed 18446744073709551615 >"$tmp_dir/maximum-seed.out"
"$solver" --help --score 00:39 >"$tmp_dir/valid-score.out"
for score in ' 1:2' '1: 2' '+1:2' '1:+2' '40:0' '1:184467440737095516160'; do
    if "$solver" --help --score "$score" >"$tmp_dir/unsigned-score.out" 2>"$tmp_dir/unsigned-score.err"; then
        echo "--score acepto un marcador invalido: [$score]" >&2
        exit 1
    else
        [ "$?" -eq 2 ]
    fi
done

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
if "$solver" --format ordago-score --iterations 1 --threads-per-cell 0 \
    --continuation "$tmp_dir/zero-cell-threads" \
    >"$tmp_dir/zero-cell-threads.out" 2>"$tmp_dir/zero-cell-threads.err"; then
    echo "--threads-per-cell 0 debio fallar" >&2
    exit 1
fi
for format in single-bet raises-to-eight ordago ordago-public; do
    if "$solver" --format "$format" --iterations 1 --threads-per-cell 2 \
        >"$tmp_dir/legacy-cell-threads.out" 2>"$tmp_dir/legacy-cell-threads.err"; then
        echo "--threads-per-cell requiere ordago-score" >&2
        exit 1
    fi
done
if "$solver" --format ordago-score --evaluate --threads-per-cell 2 \
    --continuation "$tmp_dir/evaluate-cell-threads" \
    >"$tmp_dir/evaluate-cell-threads.out" 2>"$tmp_dir/evaluate-cell-threads.err"; then
    echo "--threads-per-cell sin entrenamiento debio fallar" >&2
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
grep -q 'schema=mus.ordago-score/v4' "$tmp_dir/score-strategy.txt"
grep -q 'marcador:0-0/objetivo:3' "$tmp_dir/score-strategy.txt"
cmp "$tmp_dir/preserved.cfr.zst" "$tmp_dir/score/score-01-01.cfr.zst"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 --threads 2 \
    --threads-per-cell 1 --evaluation-samples 50 \
    --continuation "$tmp_dir/score-continuous" \
    >"$tmp_dir/score-continuous.out"
cmp "$tmp_dir/score/values.txt" "$tmp_dir/score-continuous/values.txt"
for checkpoint in "$tmp_dir/score/"*.zst; do
    name=${checkpoint##*/}
    cmp "$checkpoint" "$tmp_dir/score-continuous/$name"
done
"$solver" --format ordago-score --target 3 --continuation "$tmp_dir/score" \
    --export-strategy "$tmp_dir/score-reloaded.txt" >"$tmp_dir/score-reloaded.out"
cmp "$tmp_dir/score-strategy.txt" "$tmp_dir/score-reloaded.txt"
# Several workers update each cell's shared strategy. Check the iteration
# totals and checkpoint loading without requiring identical parallel updates.
"$solver" --format ordago-score --target 3 --iterations 3 --seed 7 \
    --threads 2 --threads-per-cell 2 --evaluation-samples 20 \
    --continuation "$tmp_dir/score-shared" \
    --export-strategy "$tmp_dir/score-shared.txt" >"$tmp_dir/score-shared.out"
[ "$(grep -c '^score-cell .* iterations=3 ' "$tmp_dir/score-shared.out")" -eq 9 ]
grep -q 'training_iterations=3' "$tmp_dir/score-shared.txt"
"$solver" --format ordago-score --target 3 \
    --continuation "$tmp_dir/score-shared" \
    --export-strategy "$tmp_dir/score-shared-reloaded.txt" \
    >"$tmp_dir/score-shared-reloaded.out"
cmp "$tmp_dir/score-shared.txt" "$tmp_dir/score-shared-reloaded.txt"
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
"$solver" --format ordago --iterations 2 --seed 48 --reserve-information-sets 256 \
    --save "$tmp_dir/prior.cfr" >"$tmp_dir/prior.out"
"$solver" --format ordago --iterations 2 --seed 48 --reserve-information-sets 256 \
    --save-compressed "$tmp_dir/prior.cfr.zst" >"$tmp_dir/prior-zstd.out"
cp "$tmp_dir/prior.cfr" "$tmp_dir/prior-preserved.cfr"
"$solver" --format ordago-score --target 3 --score 1:1 --iterations 2 --seed 7 \
    --evaluation-samples 20 --continuation "$tmp_dir/preloaded-resume" \
    --preload-ordago "$tmp_dir/prior.cfr" --preload-weight 2 --report-every 1 \
    >"$tmp_dir/preloaded-partial.out"
grep -Eq 'score-preload score=1:1 source_nodes=[1-9][0-9]* imported_nodes=[1-9]' \
    "$tmp_dir/preloaded-partial.out"
grep -q 'score-report score=1:1 iterations=1' "$tmp_dir/preloaded-partial.out"
grep -q 'score-report score=1:1 iterations=2' "$tmp_dir/preloaded-partial.out"
cp "$tmp_dir/preloaded-resume/score-01-01.cfr.zst" "$tmp_dir/preloaded-preserved.cfr.zst"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 \
    --evaluation-samples 20 --continuation "$tmp_dir/preloaded-resume" \
    --preload-ordago "$tmp_dir/prior.cfr" --preload-weight 2 \
    --export-strategy "$tmp_dir/preloaded.txt" >"$tmp_dir/preloaded-resume.out"
grep -q 'training_iterations=2' "$tmp_dir/preloaded.txt"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 --threads 8 \
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
for version in 1 2 3; do
    mkdir "$tmp_dir/score-v$version"
    sed "s@mus.ordago-score/v4@mus.ordago-score/v$version@" "$tmp_dir/score/values.txt" \
        >"$tmp_dir/score-v$version/values.txt"
    cp "$tmp_dir/score-v$version/values.txt" "$tmp_dir/score-v$version-preserved.txt"
    if "$solver" --format ordago-score --target 3 --continuation "$tmp_dir/score-v$version" \
        --iterations 1 >"$tmp_dir/score-v$version.out" 2>"$tmp_dir/score-v$version.err"; then
        echo "la tabla con esquema v$version no debe reanudarse como v4" >&2
        exit 1
    fi
    cmp "$tmp_dir/score-v$version/values.txt" "$tmp_dir/score-v$version-preserved.txt"
    test -f "$tmp_dir/score-v$version/training.lock"
done
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

# Resume one score checkpoint without changing the published continuation table.
cp "$tmp_dir/score/values.txt" "$tmp_dir/score-values-preserved.txt"
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --iterations 3 --report-every 2 --save-compressed "$tmp_dir/score-plus-three.cfr.zst" \
    --export-strategy "$tmp_dir/score-plus-three.txt" >"$tmp_dir/score-plus-three.out"
grep -q 'initial_iterations=2 added_iterations=3 total_iterations=5 continuation=frozen' \
    "$tmp_dir/score-plus-three.out"
grep -q 'report iterations=4 threads=1' "$tmp_dir/score-plus-three.out"
grep -q 'report iterations=5 threads=1' "$tmp_dir/score-plus-three.out"
grep -q 'training_iterations=5' "$tmp_dir/score-plus-three.txt"
# Splitting the added work preserves RNG state, regrets, statistics and averages.
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --iterations 1 --save "$tmp_dir/score-plus-one.cfr" \
    --evaluate --evaluation-samples 20 >"$tmp_dir/score-plus-one.out"
grep -q 'evaluation scope=score-checkpoint-average-policy score=1:1 iterations=3' \
    "$tmp_dir/score-plus-one.out"
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" --load "$tmp_dir/score-plus-one.cfr" \
    --iterations 2 --report-every 1 --save-compressed "$tmp_dir/score-split.cfr.zst" \
    >"$tmp_dir/score-split.out"
cmp "$tmp_dir/score-plus-three.cfr.zst" "$tmp_dir/score-split.cfr.zst"
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" --load-compressed "$tmp_dir/score-split.cfr.zst" \
    --evaluate --seed 13 --evaluation-samples 20 \
    --export-strategy "$tmp_dir/score-split.txt" >"$tmp_dir/score-split-evaluate.out"
cmp "$tmp_dir/score-plus-three.txt" "$tmp_dir/score-split.txt"
# Explicit one-thread checkpoint training keeps the original exact result.
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --iterations 3 --threads 1 --report-every 1 \
    --save-compressed "$tmp_dir/score-explicit-serial.cfr.zst" \
    >"$tmp_dir/score-explicit-serial.out"
cmp "$tmp_dir/score-plus-three.cfr.zst" "$tmp_dir/score-explicit-serial.cfr.zst"
# Resume one shared strategy across report boundaries and a partial batch.
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --iterations 5 --threads 3 --report-every 2 \
    --save-compressed "$tmp_dir/score-shared-resume.cfr.zst" \
    --export-strategy "$tmp_dir/score-shared-resume.txt" \
    >"$tmp_dir/score-shared-resume.out"
grep -q 'initial_iterations=2 added_iterations=5 total_iterations=7 continuation=frozen' \
    "$tmp_dir/score-shared-resume.out"
for iterations in 4 6 7; do
    grep -q "report iterations=$iterations threads=3" "$tmp_dir/score-shared-resume.out"
done
[ "$(grep -c '^report ' "$tmp_dir/score-shared-resume.out")" -eq 3 ]
grep -q 'training_iterations=7' "$tmp_dir/score-shared-resume.txt"
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score-shared-resume.cfr.zst" \
    --iterations 3 --threads 8 --report-every 2 \
    --save "$tmp_dir/score-shared-resumed-again.cfr" \
    >"$tmp_dir/score-shared-resumed-again.out"
grep -q 'report iterations=9 threads=8' "$tmp_dir/score-shared-resumed-again.out"
grep -q 'report iterations=10 threads=8' "$tmp_dir/score-shared-resumed-again.out"
[ "$(grep -c '^report ' "$tmp_dir/score-shared-resumed-again.out")" -eq 2 ]
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load "$tmp_dir/score-shared-resumed-again.cfr" \
    --evaluate --evaluation-samples 20 \
    --export-strategy "$tmp_dir/score-shared-resumed-again.txt" \
    >"$tmp_dir/score-shared-resumed-evaluate.out"
grep -q 'training_iterations=10' "$tmp_dir/score-shared-resumed-again.txt"
grep -q 'evaluation scope=score-checkpoint-average-policy score=1:1 iterations=10' \
    "$tmp_dir/score-shared-resumed-evaluate.out"
cmp "$tmp_dir/score/values.txt" "$tmp_dir/score-values-preserved.txt"
for checkpoint in "$tmp_dir/score/"*.zst; do
    name=${checkpoint##*/}
    cmp "$checkpoint" "$tmp_dir/score-continuous/$name"
done
# Only higher values are needed, including when the selected cell is unpublished.
mkdir "$tmp_dir/score-values-only"
awk 'NR == 1 || $1 + $2 > 2' "$tmp_dir/score/values.txt" \
    >"$tmp_dir/score-values-only/values.txt"
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score-values-only" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --iterations 3 --save-compressed "$tmp_dir/score-values-only-result.cfr.zst" \
    >"$tmp_dir/score-values-only.out"
cmp "$tmp_dir/score-plus-three.cfr.zst" "$tmp_dir/score-values-only-result.cfr.zst"
test ! -f "$tmp_dir/score-values-only/training.lock"
# Reject changed continuation values and incomplete higher diagonals.
mkdir "$tmp_dir/score-changed-values" "$tmp_dir/score-missing-values"
awk '$1 == 2 && $2 == 2 {$3 = ($3 == 0 ? 0.5 : 0)} {print}' \
    "$tmp_dir/score/values.txt" >"$tmp_dir/score-changed-values/values.txt"
head -n 1 "$tmp_dir/score/values.txt" >"$tmp_dir/score-missing-values/values.txt"
for directory in score-changed-values score-missing-values; do
    if "$solver" --format ordago-score --target 3 --score 1:1 \
        --continuation "$tmp_dir/$directory" \
        --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
        --iterations 1 --save "$tmp_dir/score-invalid-result.cfr" \
        >"$tmp_dir/$directory.out" 2>"$tmp_dir/$directory.err"; then
        echo "reanudar con continuaciones incompatibles debio fallar" >&2
        exit 1
    fi
    test ! -f "$tmp_dir/score-invalid-result.cfr"
done
# Output paths cannot overwrite coordinator files, even through a symlink.
ln -s "$tmp_dir/score" "$tmp_dir/score-alias"
for output in "$tmp_dir/score/score-01-01.cfr.zst" \
    "$tmp_dir/score/extra.cfr.zst" "$tmp_dir/score-alias/values.txt"; do
    if "$solver" --format ordago-score --target 3 --score 1:1 \
        --continuation "$tmp_dir/score" \
        --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
        --iterations 1 --save-compressed "$output" \
        >"$tmp_dir/score-protected.out" 2>"$tmp_dir/score-protected.err"; then
        echo "sobrescribir archivos del coordinador debio fallar" >&2
        exit 1
    fi
done
if "$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --export-strategy "$tmp_dir/score/extra.txt" \
    >"$tmp_dir/score-protected-export.out" 2>"$tmp_dir/score-protected-export.err"; then
    echo "exportar dentro de la tabla del coordinador debio fallar" >&2
    exit 1
fi
cmp "$tmp_dir/score/values.txt" "$tmp_dir/score-values-preserved.txt"
cmp "$tmp_dir/score/score-01-01.cfr.zst" "$tmp_dir/preserved.cfr.zst"
# Resume with an immutable generic prior for newly reached nodes, including 14 workers.
"$solver" --format ordago-score --target 3 --score 1:1 \
    --continuation "$tmp_dir/score" \
    --load-compressed "$tmp_dir/score/score-01-01.cfr.zst" \
    --preload-ordago "$tmp_dir/prior.cfr" --threads 14 \
    --iterations 14 --save-compressed "$tmp_dir/resumed-with-prior.cfr.zst" \
    --export-strategy "$tmp_dir/resumed-with-prior.txt" >"$tmp_dir/resumed-with-prior.out"
grep -q 'added_iterations=14' "$tmp_dir/resumed-with-prior.out"
grep -q 'mode=lazy-all-lances' "$tmp_dir/resumed-with-prior.out"
grep -q 'training_iterations=' "$tmp_dir/resumed-with-prior.txt"
cmp "$tmp_dir/score/score-01-01.cfr.zst" "$tmp_dir/preserved.cfr.zst"
cmp "$tmp_dir/prior.cfr" "$tmp_dir/prior-preserved.cfr"
# A resume must save its work and must not reinitialize the training policy or RNG.
for mode in no-save seed prior-weight threads-per-cell overflow wrong-score wrong-target old-version; do
    set -- --save "$tmp_dir/score-invalid-result.cfr"
    target=3
    score=1:1
    source="$tmp_dir/score/score-01-01.cfr.zst"
    iterations=1
    directory="$tmp_dir/score"
    case "$mode" in
        no-save) set -- ;;
        seed) set -- "$@" --seed 7 ;;
        prior-weight) set -- "$@" --preload-weight 2 ;;
        threads-per-cell) set -- "$@" --threads-per-cell 2 ;;
        overflow) iterations=18446744073709551615 ;;
        wrong-score) score=0:0 ;;
        wrong-target) target=4 ;;
        old-version) directory="$tmp_dir/score-v3" ;;
    esac
    if "$solver" --format ordago-score --target "$target" --score "$score" \
        --continuation "$directory" --load-compressed "$source" \
        --iterations "$iterations" "$@" \
        >"$tmp_dir/score-invalid-$mode.out" 2>"$tmp_dir/score-invalid-$mode.err"; then
        echo "opciones incompatibles con la reanudacion debieron fallar: $mode" >&2
        exit 1
    fi
    test ! -f "$tmp_dir/score-invalid-result.cfr"
done

# Extend completed score checkpoints and recompute higher values before lower cells.
cp "$tmp_dir/score/values.txt" "$tmp_dir/extension-source-values.txt"
"$solver" --format ordago-score --target 3 --score 1:1 \
    --extend-continuation "$tmp_dir/score" --continuation "$tmp_dir/extended" \
    --iterations 3 --evaluation-samples 1 --report-every 1 >"$tmp_dir/extended-partial.out"
grep -q 'score-report score=1:1 iterations=3 .*added_iterations=1' "$tmp_dir/extended-partial.out"
grep -q 'score-cell score=1:1 target=3 iterations=5' "$tmp_dir/extended-partial.out"
cp "$tmp_dir/extended/score-01-01.cfr.zst" "$tmp_dir/extended-preserved.cfr.zst"
"$solver" --format ordago-score --target 3 \
    --extend-continuation "$tmp_dir/score" --continuation "$tmp_dir/extended" \
    --iterations 3 --evaluation-samples 1 --export-strategy "$tmp_dir/extended.txt" \
    >"$tmp_dir/extended-resume.out"
grep -q 'training_iterations=5' "$tmp_dir/extended.txt"
grep -q 'different_settings_cells=0' "$tmp_dir/extended-resume.out"
cmp "$tmp_dir/extended/score-01-01.cfr.zst" "$tmp_dir/extended-preserved.cfr.zst"
"$solver" --format ordago-score --target 3 --threads 2 \
    --extend-continuation "$tmp_dir/score" --continuation "$tmp_dir/extended-full" \
    --iterations 3 --evaluation-samples 1 >"$tmp_dir/extended-full.out"
cmp "$tmp_dir/extended/values.txt" "$tmp_dir/extended-full/values.txt"
for checkpoint in "$tmp_dir/extended/"*.zst; do
    name=${checkpoint##*/}
    cmp "$checkpoint" "$tmp_dir/extended-full/$name"
done
# The top score has no continuations: its checkpoint must equal an exact resume.
"$solver" --format ordago-score --target 3 --score 2:2 \
    --continuation "$tmp_dir/score" --load-compressed "$tmp_dir/score/score-02-02.cfr.zst" \
    --iterations 3 --save-compressed "$tmp_dir/extension-reference.cfr.zst" \
    >"$tmp_dir/extension-reference.out"
cmp "$tmp_dir/extension-reference.cfr.zst" "$tmp_dir/extended/score-02-02.cfr.zst"
# Shared workers can extend a checkpoint and keep the generic initializer for new nodes.
"$solver" --format ordago-score --target 3 --score 1:1 --threads-per-cell 14 \
    --extend-continuation "$tmp_dir/score" --continuation "$tmp_dir/extended-14" \
    --preload-ordago "$tmp_dir/prior.cfr" --iterations 14 --evaluation-samples 1 \
    --report-every 14 --export-strategy "$tmp_dir/extended-14.txt" >"$tmp_dir/extended-14.out"
grep -q 'score-cell score=1:1 target=3 iterations=16' "$tmp_dir/extended-14.out"
grep -q 'training_iterations=16' "$tmp_dir/extended-14.txt"
ln -s "$tmp_dir/score" "$tmp_dir/score-alias"
for mode in same alias nested different-settings no-extension seed overflow; do
    set -- --extend-continuation "$tmp_dir/score"
    destination="$tmp_dir/extended"
    iterations=3
    case "$mode" in
        same) destination="$tmp_dir/score" ;;
        alias) destination="$tmp_dir/score-alias" ;;
        nested) destination="$tmp_dir/score/new" ;;
        different-settings) iterations=4 ;;
        no-extension) set -- ;;
        seed) set -- "$@" --seed 7 ;;
        overflow) iterations=18446744073709551615 ;;
    esac
    if "$solver" --format ordago-score --target 3 --continuation "$destination" \
        --iterations "$iterations" --evaluation-samples 1 "$@" \
        >"$tmp_dir/extension-invalid-$mode.out" 2>"$tmp_dir/extension-invalid-$mode.err"; then
        echo "opciones incompatibles con la ampliacion debieron fallar: $mode" >&2
        exit 1
    fi
done
cmp "$tmp_dir/score/values.txt" "$tmp_dir/extension-source-values.txt"
cmp "$tmp_dir/score/score-01-01.cfr.zst" "$tmp_dir/preserved.cfr.zst"
cmp "$tmp_dir/extended/score-01-01.cfr.zst" "$tmp_dir/extended-preserved.cfr.zst"
test ! -d "$tmp_dir/score/new"

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

# The public isolated-hand schema must resume, export, and reject legacy data.
"$solver" --format ordago-public --iterations 10 --seed 7 \
    --save "$tmp_dir/public.cfr" >"$tmp_dir/public.out"
"$solver" --format ordago-public --load "$tmp_dir/public.cfr" --iterations 10 \
    --save-compressed "$tmp_dir/public-resumed.zst" >"$tmp_dir/public-resumed.out"
"$solver" --format ordago-public --iterations 20 --seed 7 \
    --save-compressed "$tmp_dir/public-continuous.zst" >"$tmp_dir/public-continuous.out"
cmp "$tmp_dir/public-resumed.zst" "$tmp_dir/public-continuous.zst"
"$solver" --format ordago-public --load-compressed "$tmp_dir/public-resumed.zst" \
    --evaluate --export-strategy "$tmp_dir/public.txt" >"$tmp_dir/public-eval.out"
grep -q 'scope=bounded-final-lances' "$tmp_dir/public-eval.out"
grep -q 'schema=mus.heads-up-single-hand.ordago-public/v1-muses-' "$tmp_dir/public.txt"
grep -q 'maximo_pagado=' "$tmp_dir/public.txt"
grep -q 'envites_pendientes=' "$tmp_dir/public.txt"
"$solver" --format ordago --iterations 10 --seed 7 --save "$tmp_dir/legacy.cfr" >"$tmp_dir/legacy.out"
if "$solver" --format ordago --load "$tmp_dir/public.cfr" --iterations 1 >"$tmp_dir/wrong.out" 2>"$tmp_dir/wrong.err"; then
    echo 'ordago accepted a public checkpoint' >&2; exit 1
fi
if "$solver" --format ordago-public --load "$tmp_dir/legacy.cfr" --iterations 1 >"$tmp_dir/wrong.out" 2>"$tmp_dir/wrong.err"; then
    echo 'ordago-public accepted a legacy checkpoint' >&2; exit 1
fi

"$solver" --format ordago-public --iterations 12 --threads 2 --seed 7 \
    --save "$tmp_dir/public-parallel.cfr" >"$tmp_dir/public-parallel.out"
grep -q 'report iterations=12 threads=2' "$tmp_dir/public-parallel.out"
"$solver" --format ordago-public --load "$tmp_dir/public-parallel.cfr" \
    --iterations 2 --threads 2 >"$tmp_dir/public-parallel-resumed.out"
grep -q 'report iterations=14 threads=2' "$tmp_dir/public-parallel-resumed.out"

# Both public checkpoint encodings seed all score lances without changing the source.
cp "$tmp_dir/public.cfr" "$tmp_dir/public-preserved.cfr"
"$solver" --format ordago-public --iterations 10 --seed 7 \
    --save-compressed "$tmp_dir/public-prior.zst" >"$tmp_dir/public-prior.out"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 \
    --evaluation-samples 20 --continuation "$tmp_dir/public-score" \
    --preload-ordago "$tmp_dir/public.cfr" >"$tmp_dir/public-score.out"
grep -q 'schema=mus.heads-up-single-hand.ordago-public/v1-muses-' "$tmp_dir/public-score.out"
grep -Eq 'imported_later_nodes=[1-9]' "$tmp_dir/public-score.out"
"$solver" --format ordago-score --target 3 --iterations 2 --seed 7 --threads 3 \
    --evaluation-samples 20 --continuation "$tmp_dir/public-score-zstd" \
    --preload-ordago-compressed "$tmp_dir/public-prior.zst" >"$tmp_dir/public-score-zstd.out"
cmp "$tmp_dir/public-score/values.txt" "$tmp_dir/public-score-zstd/values.txt"
for checkpoint in "$tmp_dir/public-score/"*.zst; do
    name=${checkpoint##*/}
    cmp "$checkpoint" "$tmp_dir/public-score-zstd/$name"
done
for encoding in raw zstd; do
    set -- --preload-ordago "$tmp_dir/public.cfr"
    if [ "$encoding" = zstd ]; then
        set -- --preload-ordago-compressed "$tmp_dir/public-prior.zst"
    fi
    "$solver" --format ordago-score --target 3 --score 2:2 \
        --continuation "$tmp_dir/public-score" \
        --load-compressed "$tmp_dir/public-score/score-02-02.cfr.zst" \
        --iterations 2 "$@" --save-compressed "$tmp_dir/public-score-resume-$encoding.zst" \
        >"$tmp_dir/public-score-resume-$encoding.out"
done
cmp "$tmp_dir/public-score-resume-raw.zst" "$tmp_dir/public-score-resume-zstd.zst"
cmp "$tmp_dir/public.cfr" "$tmp_dir/public-preserved.cfr"

echo "Todas las pruebas de CLI pasaron."
