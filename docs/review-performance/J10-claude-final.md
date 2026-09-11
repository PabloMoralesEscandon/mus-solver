# J10 follow-up: benchmark option recheck (Claude)

Verdict: **APPROVE.** My APPROVE in /tmp/mus-review-j10-claude.md stands. The production
diff is unchanged (the per-file diff stat for src, include and app matches what I reviewed:
147, 31, 108, 26 and 2 lines). This follow-up changes only tests/benchmark_score_table.py
and docs/performance.md. The /tmp-only terminal trial is not part of this diff and was not
reviewed.

## `--ignore-sampling-statistics` (tests/benchmark_score_table.py)
- **Default unchanged:** every file except `training.lock` and `recovery.txt` is hashed in
  full, so the stored statistics must also be equal across variants. Comparing against a
  pre-J10 baseline therefore fails loudly ("Table or checkpoint bytes differ"), as intended.
- **With the flag:** only `values.txt` is normalised:
  - the header has ` statistics=sample-variance-v1` removed;
  - each row keeps its first six tokens: `a b value iterations samples seed`;
  - the text is rebuilt with the writer's single-space and newline layout.

  All checkpoints are still compared byte for byte, and the file count check
  (`cells + 1`) is unchanged.
- **Soundness:**
  - the normalisation is applied to every variant, baseline included;
  - for a pre-J10 file it reproduces the original bytes exactly;
  - the value keeps its `%.17g` text.

  So equality still proves the same checkpoints and bit-identical fixed-mode values, iteration
  and sample counts and seeds. Only the two variance columns and the header suffix are
  ignored.
- **Reporting:** the result key is `policy_files_and_values_equal` instead of `files_equal`,
  and the summary adds `sampling_statistics_checked: false`, so a normalised comparison
  cannot be mistaken for full byte equality.
- **Documentation:** docs/performance.md:78-83 describes the option, what it compares and
  omits, the output keys, and the default full check.

## Results
- The final full suite passes, as does the two-mus core, statistics, regions and recovery
  run (per the coordinator).
- The ASan/UBSan recovery suite passes when run alone
  (/tmp/mus-review-j10-sanitize-recovery.log, 21:58:57). The core, statistics and region
  sanitizer runs are in j10-sanitize.log.
- Performance is still running and remains the gate before the commit.

## Non-blocking
1. **Load-sensitive timeouts.** The earlier sanitizer recovery failure, when run alongside
   the two-mus tests, was a 30 s timeout under load, not a logic failure. The
   subprocess timeout and the SIGKILL-case deadline in tests/test_score_recovery.py are fixed
   at 30 s. An optional scale factor (for example an environment variable for sanitizer
   runs) would prevent spurious failures without weakening the checks. Otherwise, keep
   sanitizer suites serial.
2. **Cosmetic.** In normalised mode the failure message still says "bytes differ". "Policy
   files or values differ" would match the flag.
3. **Tolerance of extra tokens.** Normalisation keeps the first six tokens of any row, so a
   row with extra tokens would compare equal in its first six fields. That is harmless,
   because the solver itself parses tables strictly, and the benchmark only compares output
   the solver produced.
