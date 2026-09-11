# J17 persistence: performance evidence review (Claude)

Conclusion: **the regression is confirmed and correctly attributed. The planned pause is
justified.** The small-cell workload exceeds the 20 % time gate by about 74 times
(+1,483 % median in the five confirmation pairs), and the cause is the new publication
`fsync`s, not CFR work. Under the stated rule, which J01's cost approval does not cover,
preserving the candidate as a patch, removing it, and waiting for the user's decision is
the right action. The production approval (task_00bc6da58ad8, task_e14b8a955c0d) was about
correctness only.

## Evidence

**Regular workloads** (/tmp/mus-review-j17-sync-performance.{py,log,json}): three
alternating pairs each, CPU0, workspace Btrfs, byte-identical outputs, gate not triggered.

| Group | Parent (s) | Candidate (s) | Time | RSS |
|---|---:|---:|---:|---:|
| public raw save, 500 iterations | 9.412 | 9.624 | +2.26 % | 0.00 % |
| public zstd save | 10.433 | 10.692 | +2.49 % | 0.00 % |
| score 35:35, 1000 iterations | 3.707 | 3.783 | +2.05 % | +0.11 % |
| extension | 3.942 | 4.107 | +4.18 % | −0.06 % |

**Small-cell batches** (/tmp/mus-review-j17-sync-small-performance.{py,log,json}):
- 100 fresh tables per batch, 45 cells each (4,500 cells), one iteration and one evaluation
  sample per cell;
- 3 alternating pairs, then 5 confirmation pairs; the run is finished (`DONE`,
  `blocked: true`).

| Pairs | Parent median (s) | Candidate median (s) | Time | RSS |
|---|---:|---:|---:|---:|
| initial 3 | 0.739 | 10.130 | +1,271 % | 0 % |
| confirmation 5 | 0.748 | 11.839 | **+1,483 %** | 0 % |

Parent batch range: 0.72-0.75 s. Candidate batch range: 7.8-12.5 s.

## Validity checks

1. **Equivalent work.**
   - Both builds get the same command, seed and cell set, each in a fresh directory.
   - Every table in every batch is hashed (46 files, `training.lock` excluded) and must match
     a single reference. The JSON records `all_policy_files_identical: true` for all 16
     batches.
   - The parent is the J17-lock build (`review-j17-lock-final`), so locking is not part of
     the difference.
2. **Attribution to publication, not traversal.**
   - The regular benchmark records identical `libmus_solver.a` SHA-256 values for parent
     and candidate (6e26b3cb…), so all game and CFR code is byte-identical. Only the CLI
     binary differs, and the persistence diff touches only `app/mus_solver_cli.c`.
   - The cost matches the fsync count. For one table:
     - 45 cells → 45 file `fsync`s and 45 directory `fsync`s;
     - 9 diagonals (sums 78→70) → 9 `values.txt` `fsync`s and 9 directory `fsync`s;
     - plus 1 startup parent sync.

     That is about 109 `fsync`s per table and about 10,900 per batch. The extra ≈11.1 s
     therefore works out to ≈1 ms per `fsync`, which is typical of Btrfs on SSD.
   - RSS is unchanged.
   - Optional, stronger proof: run the candidate once with `fsync` turned into a no-op
     (LD_PRELOAD of libeatmydata, or a no-op mode in the trace shim). It should match the
     parent.
3. **Amplification.** A 0.74 s parent batch (about 7.4 ms per table including process
   start-up) against 8-12 s candidate batches is far beyond timer and scheduling noise.
   Eight pairs, alternating order, CPU0, and `syncfs` of earlier dirty data outside each
   timed batch all hold. The candidate's variance (7.8-12.5 s) is what fsync latency on a
   shared Btrfs looks like and does not change the conclusion.
4. **No concurrent own jobs: one caveat.**
   `mus-review-j17-sync-small-performance.py` was written at 19:05:43, but the regular
   benchmark last wrote its log and JSON at 19:07:39. If the small run started before that,
   its first batches overlapped the regular run's extension group, both pinned to CPU0 and
   both writing to the same Btrfs. That could inflate early candidate `fsync` latency. The
   confirmation pairs (repeats 4-8, later in the run) reproduce the same magnitude, and
   parent timings stay stable throughout, so the verdict is unaffected. Record the start
   times in the JSON to close this point.
5. **Scope.** The small-cell workload is a deliberate worst case in which publication
   dominates. Regular tables pay 2-4 %. The rule applies to confirmed regressions on
   measured workloads regardless of how representative they are, so the pause stands.

## Options for the user's decision (no commitment implied)
- **Accept the cost:** full per-file durability, as the diff now implements it.
- **Treat `values.txt` as the only commit point:**
  - write and rename each cell without an `fsync`;
  - before each diagonal's `values.txt`, make the cells durable once, with a single
    `syncfs` or one directory `fsync` after the cell `fsync`s;
  - then publish `values.txt` with file and directory `fsync`s.

  Cells a crash leaves unsynced are not referenced by `values.txt`, and they are retrained
  and overwritten on restart. That keeps the recovery contract while cutting roughly 109
  `fsync`s per table to about 18 plus 9 `syncfs` calls. The README contract would need to
  change from "each checkpoint" to "each published diagonal". Generic `--save` (a single
  file) is unaffected.
- **Make strict per-file syncing opt-in** (for example a `--durable` flag), with the batched
  or unsynced behaviour as the default. This needs a documented default and its own review.

Any of these requires new measurements against the same parent under the original gate.
