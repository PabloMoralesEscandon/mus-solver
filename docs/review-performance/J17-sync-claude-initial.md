# J17 persistence review (Claude)

Verdict: **APPROVE** (non-blocking notes below). Diff reviewed against 1f1dec7, including
tests/persistence_trace.c, tests/test_persistence.py and the Makefile test CLI linked with
`--wrap=fsync --wrap=rename`. The full debug suite passes (including the CLI, lock and
persistence suites), and the persistence suite also passes under sanitizers and on Btrfs.
Checkpoint and schema formats are unchanged: only system calls were added.

## Publication protocol (app/mus_solver_cli.c)

`publish_file` (lines 491-505), in order:
1. `fflush`, then `fsync(fileno)`, only if the write succeeded;
2. `fclose` (always);
3. `rename(temporary, final)`;
4. `fsync` of the parent directory;
5. `remove(temporary)` on any failure.

**Failure before the rename** (write, flush, `fsync` or close) keeps the previously published
file and removes the temporary. Tested for a generic checkpoint (old digest kept, no
`.tmp.*`) and for `values.txt` (old table kept, no `values.txt.tmp`).

**Directory-sync failure after the rename** returns an error, and the new complete file stays
visible. This is documented (README:516-521) and tested: FILE, RENAME, FAIL, then a readback
showing `training_iterations=2`. The `remove(temporary)` that follows is then a harmless
ENOENT.

**Users:**
- **Checkpoints** (score cells, and generic `--save`/`--save-compressed`) go through
  `save_checkpoint` → `publish_file`. The unique `.tmp.N` temporaries created with `"wbx"`
  are unchanged.
- **`values.txt`** (save_score_diagonal) is published only after the diagonal's cells.
  mus_score_train calls save_cell for every cell, each fully synced and renamed, before
  save_diagonal. The trace confirms the order `score-02-02.cfr.zst` then `values.txt`. A
  failed values sync keeps the old table while the new cells are already published; a retry
  retrains and overwrites them (tested).
- **`extension.txt`** (lines 1139-1154) is written through `extension.txt.tmp` and an atomic
  rename, **only when no manifest exists**, so a retry never overwrites a valid manifest; an
  existing one is compared byte for byte. It runs after the exclusive lock (line 1276, after
  1240), so no other writer can race it. A stale temporary left by a crash is overwritten
  by `fopen("wb")`. An injected failure leaves no manifest, no temporary and no cells, and the
  retry publishes extension.txt, then the cell, then values.txt, with the source unchanged
  (tested).

**New continuation directory:** when `mkdir` returns 0, `synchronize_parent_directory` runs
before any use.

**Parent-path parsing** (`synchronize_parent_directory`, lines 474-489):

| Input | Directory synced |
|---|---|
| `dir/` (trailing slashes stripped) | `.` |
| `a/b/` | `a` |
| `/x` | `/` |
| `x` | `.` |
| `a//b` | `a/` (valid) |
| `/` | `/` |

Tested: a relative filename syncs `.`, and `"table with spaces/"` syncs its parent first.

**Out of scope, as documented:** TXT exports and `training.lock` metadata. The unchecked
`rewind` is J23.

## Tests

- **Trace shim** (persistence_trace.c): it is linked only into `mus_persistence_tests`; the
  production binaries call the real functions. It records FILE, DIR and RENAME with paths
  resolved through `/proc/self/fd`, and injects EIO per mode: file, values, extension, and
  directory-after-rename.
- **`ordered()`:** it asserts that every RENAME is immediately preceded by the FILE sync of
  its source and followed by the DIR sync of its destination's parent.
- **Scope:** the suite claims no physical power-loss behaviour, and the README says the same.

## Non-blocking

1. **Parent sync only on creation.** `synchronize_parent_directory(continuation_dir)` runs
   only when `mkdir` returns 0. If a run is killed between `mkdir` and that `fsync`, or the
   user created the directory, every later run sees EEXIST and never syncs the parent. A
   later power loss could then drop the directory entry even though later runs reported
   success. Syncing the parent on every writer run (one extra `fsync`), or whenever
   `values.txt` is still absent, closes this.
2. **Cleanup after rename.** `publish_file` calls `remove(temporary)` even after a successful
   rename followed by a directory-sync failure. That is harmless: every temporary name is
   either unique (`.tmp.N` created with `"wbx"`) or protected by the exclusive lock. Skipping
   the remove once the rename has succeeded would state the intent more clearly.
3. **Optional coverage.** `--save-compressed` and a stale `extension.txt.tmp` left by a crash
   use the same code paths but are not exercised directly.
4. **Performance.** Each cell now costs one file `fsync` and one directory `fsync`, and each
   diagonal adds two more for values.txt. On Btrfs this can be significant for large tables,
   so measuring on the workspace file system (build/review-j17-sync-io) rather than tmpfs is
   the right plan. Report per-cell overhead for a table with many small cells.
