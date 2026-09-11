# J19 withdrawn candidate and performance gate: review (Claude)

Verdict: **APPROVE**. The withdrawal follows the pre-registered rule; the arithmetic and
provenance are correct; and the saved patch fixes the problem I raised. Three precisions
are suggested (non-blocking).

This was read-only: I ran no builds, tests or benchmarks. I checked:
- `docs/review-performance/J19-blocked.patch`, `J19-blocked.json` and
  `J19-claude-initial.md` (byte-identical to my earlier report);
- the J19 row and section in `docs/review-progress.md`;
- `ASTRA_BLOQUEO_RENDIMIENTO_J19_2026-09-12.md`.

## Withdrawal and state
- Production code is at HEAD `142e203` (J18): only `docs/review-progress.md` is modified,
  and `tests/test_score_scheduler.c` is absent.
- J18 is retained (`tests/test_adapter_candidates.c` exists).
- J13 is absent (no availability code).
- The patch SHA-256 is `1a9ddb94…`, matching `patch_sha256`, and `git apply --check`
  against HEAD passes.
- Both submodules are unchanged, according to the validation notes.

## The saved patch fixes my finding
- **stdout.** `save_score_cell` now holds `flockfile(stdout)` from the `score-cell`
  prefix through `print_score_statistics` (including the adaptive suffix and the newline),
  then calls `funlockfile`. `fflush` runs afterwards, outside the lock.
  - Worker lines (`score-report`, `score-extend`, `score-recovered`) are single `printf`
    calls, so they now block until the record is complete.
  - stdio locks are recursive, and no other lock is taken inside, so this cannot deadlock.
  - The `score-preload` line stays outside the lock, which is fine because it is already
    one call.
- **New test.** `tests/test_score_scheduler_cli.py` runs 4 workers with `--report-every 1`
  on three fresh tables and one extension. It requires every stdout line to fully match one
  of four record patterns, and checks the exact counts:

  | Line type | Expected count |
  |---|---:|
  | `score-cell` | 45 |
  | `score-report` | 1,800 |
  | `score-table` | 1 |
  | `score-extend` | 45, or 0 without the extension |

  This would catch a split record.
- **Contract.** `mus_score.h` now says that `save_cell` can overlap other cells'
  `progress`, `recover_cell` and extension `load_cell` callbacks, and that shared state must
  be synchronized.

## Startup refinement and my earlier thread-count note
- **Final startup.** Cells 1..active−1 start on threads; the caller then trains cell 0
  synchronously (`started[0] = false`). Consuming cell 0 is immediate, and only then is
  slot 0 refilled with a thread.
- **During any save, at most N−1 cells are training.** The slot being saved is refilled
  only after `save_cell` returns. So at most (N−1)·M training threads run alongside one
  single-threaded save: no more than the N·M budget.
  - In steady state there can be N worker threads plus the caller, but the caller is idle
    in `pthread_join` while they all train.
  - The documentation patch states this correctly.
- **Correction to my note.** My note said the caller's save competed with N·M training
  threads. That was overstated for the intermediate version too, not only the final one:
  the same refill-after-save rule already capped training at N−1 cells during a save. The
  only real difference in the intermediate version was the startup, where N threads trained
  while the caller sat idle. The report's corrected description is right, so no change to
  it is required.

## Medians and gate: all recomputed and correct

| Group | Pairs | Time | Peak memory |
|---|---|---|---|
| Serial, 35:35 | 3 | 3.4890 → 3.6619 s (+4.95 %) | 38,972 → 39,032 KiB (+0.15 %) |
| Parallel, 120 cells | 3 | 14.0327 → 10.1897 s (**−27.39 %**) | 269,668 → 330,256 KiB = 263.35 → 322.52 MiB (**+22.47 %**, +59.17 MiB): no trigger |
| Tiny batch | 3 | 1.0538 → 0.8639 s (−18.02 %) | 6,236 → 8,968 KiB (+43.81 %): **trigger** |
| Tiny batch, confirmation | 5 | 1.0301 → 0.8642 s (**−16.10 %**) | 6,252 → 8,848 KiB = 6.11 → 8.64 MiB (**+41.52 %, +2.54 MiB**): confirmed, `blocked = tiny-parallel-batch` |

- **How a batch is measured.** For every batch, the stored wall time equals the sum of its
  100 children's times, and the stored memory equals the largest child peak. This matches
  the protocol, which defined it before the run.
- **Identical outputs.** Within each group, all runs share one artifact set and one first
  stdout hash, so checkpoints and values are identical.
- **The confirmation was triggered by the rule itself** (≥25 % memory in the first three
  pairs), not by discretion. The decision follows the protocol.

**Peak of the batch versus the typical child.**
- Across the 500 children per version in the confirmation, the median child peak goes
  5,852 → 6,754 KiB (**+15.41 %**); the first three pairs give +15.37 %.
- The documentation keeps the peak-of-batch rule and the typical child clearly apart, and
  does not present +41.52 % as the typical increase.

**The profile is separate from the paired measurements.**
- Each version's profile is a single instrumented run:
  - span 13.6870 → 9.8828 s;
  - summed save time 7.4898 → 8.1622 s;
  - median wait before a save 76.95 → 43.07 ms;
  - peak memory 269,780 → 383,508 KiB = 263.46 → 374.52 MiB (+42.16 %).
- The report says these are single runs, that the overlapping sums must not be added to
  wall time, and that the gate comparison uses the uninstrumented executables. Correct.

## Suggested precisions (non-blocking; they help the user's time/memory decision)
1. **Spread of the parallel memory results.** The three candidate runs were 322,196,
   330,256 and 362,156 KiB (314.6 / 322.5 / 353.7 MiB), against a tight parent spread of
   267,956–269,724 KiB. One uninstrumented run was +34 % over the parent's median. The
   median +22.47 % stays under the rule, but the candidate's memory is variable — as is the
   +42 % profile. A clause such as «una de las tres ejecuciones del candidato alcanzó
   353,7 MiB» would make that visible.
2. **Distribution of the tiny-batch increase.** In the confirmation, the child 90th
   percentile goes 6,076 → 8,208 KiB (about +35 %), while the median rises +15.41 %. The
   increase is concentrated in the upper tail, not in a single batch. Citing the 90th
   percentile next to the median would describe the distribution better than the median
   alone.
3. **Where the parent profile came from.** It comes from the preliminary session: the
   stored stderr and RSS files, and generated sources that predate pinning to `142e203`.
   So the two profiles are not a pair measured in the same session. The postprocessing
   note implies this; one sentence in the report would make it explicit.
