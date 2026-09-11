# J10 evaluation statistics review (Claude)

Verdict: **APPROVE** (non-blocking notes below). Diff reviewed against 8f88bcb, including
tests/test_score_statistics.py. C-CFR is unchanged; `-lm` is added for `sqrt`.

**Results:**
- core tests pass (/tmp/mus-review-j10-core.log);
- the statistics CLI tests pass (j10-cli.log);
- the full suite passes (j10-tests.log);
- ASan/UBSan: core, statistics and region runs pass (j10-sanitize.log);
- two muses: core, hot paths, statistics and regions pass (j10-two.log).

## Estimator (src/mus_score.c, mus_score_evaluate_with_options)
- **Welford:** `delta = u − mean; mean += delta/n; m2 += delta·(u − mean_new)`.
  `u − mean_new = delta·(n−1)/n` has the same sign as `delta`, so `m2` never goes negative
  under rounding. The sample variance is `m2/(n−1)`, and the standard error is
  `sqrt(var/n)`.
- **Fixed means are bit-identical.** `mean += delta / (double)(sample + 1)` performs exactly
  the old operations (`delta` is `utility − mean`). This is tested against
  `mus_score_evaluate` and against a prefix run, and a capped-target run gives the same rows
  and checkpoints as fixed sampling.
- **Stopping:**
  - `valid_evaluation_options` requires 0 < min ≤ max, a finite target ≥ 0, min ≥ 2 when
    the target is positive, and min == max when the target is 0;
  - the check runs only once `samples ≥ min`, using `m2/sample`, which is `m2/(n−1)`
    (never divided by zero);
  - `target_reached` is reported.

  Tested: stopping at more than 32 and fewer than 1,000 samples with SE ≤ 0.08, where the
  prefix one sample shorter has SE > 0.08; an unreachable target that uses the full budget.
- **Output preservation:** `*result` and `*value` are written only on success. Tested with
  seven invalid option sets against a byte-for-byte sentinel.
- **Edge cases:**
  - n = 1 gives an unknown variance (0);
  - identical utilities give variance 0, so stopping happens at the minimum, which the
    README calls out ("un mínimo pequeño puede observar varianza cero");
  - the stored variance bound of 2 (+1e−12) is the true maximum of an unbiased variance of
    values in [−1, 1] (n = 2, values ±1).
  - The utilities are ±1 or −(continuation value) ∈ [−1, 1].

## Persistence
- **Writing:** the header gets ` statistics=sample-variance-v1` when any ready cell's
  variance is known. Every row then carries `known variance` (%.17g); legacy cells are
  written as `0 0`.
- **Reading:**
  - the suffix is detected and stripped, then the header is compared strictly as before;
  - statistics rows need exactly 8 tokens, legacy rows exactly 6;
  - `known` must be 0 or 1;
  - the variance must be finite, ≥ 0 and ≤ 2, must be 0 when unknown, and needs ≥ 2
    samples when known.

  Tested: v9 suffix, known = 2, known = 1 with 1 sample, −1, NaN, 3, a missing suffix, and
  a missing column.
- **Identity:** `mus_score_game_set_schema` hashes only the ready flag and the value, so the
  game, checkpoint and TXT schemas are unchanged (tested: the schema is unaffected by the
  variance fields).
- **Recorded samples:** `cell.evaluation_samples` is now the number actually used.

## Integration
- **CLI:**
  - both options are required together; min ≥ 2 and ≤ `--evaluation-samples`;
  - they are allowed only for training without `--load`, or with `--evaluate`, and
    rejected for other formats; the target must be finite and positive;
  - output reports `uncertainty=local-sampling`, the variance, SE and `win_probability_se`
    (SE/2), and `unknown` for legacy cells or n < 2. With adaptive sampling it also reports
    `stop=estimated-target|sample-limit`;
  - a fresh `--evaluate` reports its own statistics without changing the stored table
    (tested with hashes).
- **Extension manifest:** it adds `evaluation_min_samples` and `evaluation_target_se` only
  when adaptive sampling is on, so existing fixed-sampling manifests (J09) stay
  byte-compatible. Changing the target makes the extension incompatible even when the
  sample counts happen to coincide (tested).
- **Resume and extension checks:** the core's `cell_evaluation_matches`, used for recovered
  cells and existing extension destinations, and the CLI's `score_sampling_matches`, used
  for the resume count, accept either the maximum sample count or a stored variance that
  reaches the target within [min, max].
- **J08 recovery v2:** the record adds the cell's known flag and variance (%a) and the
  min/target options, under the `mus-score-recovery/v2` label. A v1 record has 9 tokens, and
  its `mus-score-recovery/v1` token fails the 0/1 check, so it is retrained. Changing the
  target prevents recovery (tested). The core then re-checks the sample count against the
  options.
- **Bot:** it reads tables through `mus_score_table_read` and ignores the variance. The
  fixture was updated to designated initializers.
- **README** (lines 375-430): local, conditional uncertainty that excludes policy error and
  error propagated from continuations; not a confidence interval for the table or for the
  bot's results; a practical stopping rule, not a guarantee; possible bias from optional
  stopping; fixed mode is the default and preserves means and checkpoints; the format and
  its readers; the manifest and recovery changes.

## Non-blocking
1. **Format change for default runs.** Fixed sampling also records the variance, so every
   table written or resumed by J10 gets the statistics header and extra columns, and pre-J10
   readers (older bots and coordinators) reject it. It fails safely and the README documents
   it, but deployment must upgrade readers together. If older readers must keep working,
   write the statistics only on request.
2. **Benchmarks against the parent.** Byte comparisons of `values.txt` will now always
   differ. Compare the first six columns, or strip the statistics, when checking fixed-mode
   equivalence (tests/benchmark_score_table.py still hashes whole files).
3. **Duplicated check.** `score_sampling_matches` (CLI) and `cell_evaluation_matches` (core)
   implement the same rule. A shared helper in the public API would stop them diverging.
4. **Lenient parsing.** `--evaluation-target-se` uses `parse_positive_double` (`strtod`),
   which still accepts leading whitespace, a `+` sign and hex floats (as noted in J16).
