# J01 adapter integration: final bounded recheck (Claude)

Verdict: **APPROVE.** The production review in task_0bef531f41f9 stands. The diff stat
against f62ad27 for src/include/app/Makefile is unchanged from what I approved: 4 files, 74
insertions, 28 deletions. The solver binaries are identical (SHA-256 cbf3e6d1…df424), so this
block does not affect training. As instructed, I accept keeping `score_table_disabled` as the
no-table reason (the `strategy` field already says ordago-public) and keeping the explicit
`create_public`.

## Score-bias test addition (tests/test_public_adapter_cli.py:109-117)

The test removes `--no-score-adjustment` and uses the default adapter configuration
`{score_adjustment=true, offset=8.0, factor_min=0.5, factor_max=2.0}`
(src/mus_adapter.c:95).

- **Public generic at 38:10, player 0:**
  - raw factor = (40−38+8)/(40−10+8) = 10/38 ≈ 0.263, clamped to factor_min = 0.5;
  - the fixture node has ordago 0.3 and pass 0.7, so ordago = 0.15/(0.15+0.7) = **3/17**.

  The test checks both values on all three backends (raw, zstd, TXT).
- **Trained 39:30 cell (hybrid):** the adjustment is skipped
  (`!d->score_strategy`, line 965), so the factor stays 1 and ordago stays 0.25 from the
  score fixture.

Both expected values follow exactly from the code. This closes my earlier note that the
retained heuristic was never exercised with a public generic. The JSON suite passes normally
and under ASan/UBSan (/tmp/mus-review-j01-adapter-final-cli.log: two passes).

## Performance (/tmp/mus-review-j01-adapter-performance.py, .log, .json)

**Method:**
- 64,000 sequential JSON queries per run, CPU0 affinity, three alternating parent/candidate
  pairs;
- the corpus covers all betting lances, mus, discard and off-abstraction responses;
- the script asserts identical output hashes in each group and identical solver SHA-256
  values, and applies the 20 % time / 25 % RSS gate;
- RSS is VmHWM sampled during the queries.

| Group | Parent (s) | Candidate (s) | Time | Mean query | p95 | RSS |
|---|---:|---:|---:|---:|---:|---:|
| legacy raw | 1.6909 | 1.6908 | −0.01 % | +0.55 % | +1.55 % | +1.80 % (44 KiB) |
| legacy TXT | 2.9185 | 2.9512 | +1.12 % | +0.94 % | +0.21 % | −1.18 % |
| hybrid TXT | 2.3238 | 2.3330 | +0.40 % | +0.46 % | −0.23 % | −1.05 % |

Outputs are byte-identical across every run in each group (same sha256 and method counts).
All deltas are within noise and far below the thresholds.

**New public paths (absolute):**
- raw: 1.392 s, 0.0158 ms mean, 0.0220 ms p95, 2312 KiB;
- TXT: 1.400 s, 0.0159 ms mean, 0.0220 ms p95, 2340 KiB.

Both are deterministic across repeats and comparable to the legacy paths. The log ends with
ALL_CHECKS_PASSED.

**Minor, non-blocking:** the benchmark runs with `--no-score-adjustment` and does not time
the public zstd backend. The adjustment is a few arithmetic operations per betting decision,
and zstd affects only load time. Its query path is the same store lookup as raw.
