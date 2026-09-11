# J17 persistence: final bounded recheck (Claude)

Verdict: **APPROVE**, conditional on the coordinator's final build and checks passing. Every
log I can see (/tmp/mus-review-j17-sync-*.log, 18:52-18:54) predates this fix, so the new test
cases below have not yet been shown to pass in the full, sanitizer or Btrfs runs. My APPROVE in
task_00bc6da58ad8 stands for the rest of the diff. C-CFR is unchanged (`git -C C-CFR status`
is clean). The unchecked `rewind` remains J23.

## Parent synchronized on every writer startup (app/mus_solver_cli.c:1229-1239)
- For writers (`iterations_set`), `mkdir` may succeed or return EEXIST, and in both cases
  `synchronize_parent_directory(continuation_dir)` now runs, before the lock is taken and
  before any file is written. Other `mkdir` errors still fail.
- This closes my earlier note 1: a directory left by a run that died between `mkdir` and the
  `fsync`, or created by the user, is now confirmed at the start of every later write.
- Read-only runs (evaluate/export) do not sync, which is correct because they write nothing
  under this protocol.
- `publish_file` is unchanged. README:505-512 now says the parent is synced when a write
  starts, including when creating a continuation is retried.

## New tests (tests/test_persistence.py, tests/persistence_trace.c)
- **Failed-mkdir-sync retry** (lines 62-71): the `new-directory` mode fails any directory
  `fsync` before the first rename (persistence_trace.c:38). The first such call is the
  startup parent sync, so the failed run:
  - records exactly `[FAIL, <root>]`;
  - leaves an existing, empty directory (it stops before the lock, so there is not even a
    `training.lock`);
  - exits with an error.

  The retry uses the same trailing-slash path, which now reaches EEXIST. It asserts that the
  first event is the DIR sync of the root, that every rename is ordered, and that the
  publications are `score-02-02.cfr.zst` then `values.txt`.
- **Raw and compressed failures** (lines 44-60):
  - raw: FILE → RENAME → DIR ordering, and an injected file-sync failure keeps the old
    digest and leaves no `.tmp.*`;
  - `--save-compressed`: ordered publication, then an injected failure keeps the old `.zst`
    digest and leaves no `.zst.tmp.*`.
- **Stale `extension.txt.tmp`** (lines 86-94): after an injected extension-sync failure, the
  test plants an "incomplete previous attempt" temporary. The retry overwrites it
  (`fopen("wb")`) and publishes `extension.txt`, the cell and `values.txt` in order, with the
  source unchanged.

## Non-blocking
1. **Parent read permission.** A writer now needs read permission on the continuation's
   parent directory, because the directory is opened `O_RDONLY` in order to fsync it. A parent
   with execute permission only now fails every writer run, where it previously worked. This
   is unusual. A README sentence about the requirement, or a clearer error message, would be
   enough.
2. **Cosmetic.** The README paragraph has an uneven line break ("Esto solicita / al sistema
   de archivos que confirme los datos / y los nombres…").
3. **Record the final evidence.** Before committing, record the final full-suite,
   sanitizer and Btrfs passes that include the new `new-directory`, compressed and
   stale-temporary cases, and the Btrfs performance result (the performance log is still
   being written).
