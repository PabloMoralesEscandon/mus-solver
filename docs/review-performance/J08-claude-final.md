# J08 completed-cell recovery: final bounded recheck (Claude)

Verdict: **APPROVE.** This refines my APPROVE in task_743f471e0df6. The recheck covers the
diff against 14bbd70 and the identity refinement made since then. C-CFR is unchanged.

**Results:**
- final normal recovery suite (/tmp/mus-review-j08-final-recovery.log): passes;
- final ASan/UBSan recovery suite (/tmp/mus-review-j08-final-sanitize-recovery.log): passes;
- final release build: passes;
- full suite and two-mus core/hot-path runs: passed before this refinement; core production
  code is unchanged since then.

## Source table metadata in the record
- `ScoreRecoveryOptions.source_table` (app/score_recovery.h:9) is filled from `run_score`'s
  local `source_table`, only for extensions. That table lives until `done:` and outlives
  every save and recovery callback.
- `format_record` (app/score_recovery.c:87-102) now appends
  `source_iterations=%zu source_seed=%" PRIu64` from the source table's cell, or 0 without
  a source.
- Records are compared as whole strings, so if the source table's iterations or initial
  seed change while the source checkpoint stays byte-identical, the record no longer
  matches and the cell is retrained.
- This is defence in depth: the extension manifest already compares the whole serialized
  source table before training.
- The `mus-score-recovery/v1` label is unchanged, but no earlier record format was ever
  released, and a record in the old format simply fails to match and retrains.

## Prior-content regression (tests/test_score_recovery.py:72-91)
- The test asserts the raw v1 layout (`CFRCKPT\0`, version 1) and a valid trailing CRC32.
- It flips the byte at `16 + schema_length + 4`. In C-CFR's writer
  (C-CFR/src/checkpoint.c:453-462) the order is:
  1. magic (8 bytes);
  2. version (u32);
  3. schema length (u32);
  4. schema;
  5. variant (u32);
  6. for MCCFR external sampling, `mccfr_rng.state` (u64).

  So the flipped byte is the first byte of the RNG state, as claimed.
- It recomputes the CRC32, keeps the size and restores atime and mtime. The recovery run
  still succeeds, so the modified prior remains a loadable checkpoint, yet cell 1:2 is not
  recovered, and the prior's bytes are unchanged by the run. This proves recovery is bound
  to the prior's content, not its path, size or timestamps.

## Earlier notes, resolved or accepted
- README:550-551 now says unpublished checkpoints are reused only through a valid
  `recovery.txt` entry, and are otherwise ignored. **Fixed.**
- The core still rejects an invalid callback result as INCOMPATIBLE: a third-party callback
  has broken its contract. **Accepted.**
- `--threads` is recorded conservatively. **Accepted.**
- The SIGKILL race is guarded: the test asserts the recovered result, and the next cell has
  500 iterations of work. **Accepted.** Under a heavily loaded machine it could still
  flake, but it would fail loudly, not pass wrongly.

No new defects. The paired Btrfs benchmarks remain the coordinator's separate gate: fresh
training, tiny cells, and recovery of 8 finished cells in a 9-cell diagonal.
