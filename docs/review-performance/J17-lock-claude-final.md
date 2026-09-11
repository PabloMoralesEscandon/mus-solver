# J17 process-lock re-review (Claude)

Verdict: **APPROVE.** Diff reviewed against 0373fc1, including the untracked
tests/test_score_locks.py. R1 from /tmp/mus-review-j17-lock-claude.md is fixed. The final
logs show the full debug suite (including the CLI and lock suites), the sanitizer lock suite
and the release build passing.

## R1: fixed
- `acquire_score_lock` (app/mus_solver_cli.c:706-745) now locks the **directory inode**
  through `open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)` and `flock(LOCK_SH|LOCK_EX |
  LOCK_NB)`.
- Shared readers create and modify nothing. Opening a directory read-only works on
  read-only directories and mounts, so archived pre-J17 tables can be extended again without
  being changed.
- Test (tests/test_score_locks.py:126-138): a copy without `training.lock`, set to
  `chmod 0555`, is extended successfully. All of its files keep their hashes and no lock file
  appears. The check holds even when run as root, because readers never create files.
- Not falling back to "unlocked on EROFS" is the right call: a writer could reach the same
  directory through another, writable mount.

## Verified correct
- **Exclusive writers:** `training.lock` is rewritten with `fopen("w")` only after the
  exclusive directory lock succeeds. It truncates in place (same inode), is diagnostic only,
  and a refused writer never touches it (tested).
- **Error reporting:** EWOULDBLOCK/EAGAIN report the holder's last metadata. Any other
  `flock` error fails with its own `strerror` message, and there is no silent unlocked path.
- **Lifetime:** every path closes the lock descriptors.
  - `run_score` (writer or reader, depending on `iterations_set`) closes `lock` and
    `source_lock` at `done:`.
  - `run_score_checkpoint` takes a shared lock at line 895, with no `return` before `done:`
    (line 1003), and closes it there.
  - Nothing is ever unlinked. The kernel releases the locks on SIGINT, SIGTERM and SIGKILL;
    no handler is installed.
- **Coverage:** the source's shared lock spans the whole extension. Evaluate/export and the
  single-cell `--load` continuation now also take shared locks, which closes my earlier
  non-blocking note 5. Both reader routes are rejected while a writer holds the lock (tested).
- **Aliases and deadlocks:**
  - the realpath alias, same-directory and nested checks run before any lock is taken;
  - every lock is non-blocking, so crossed extensions fail instead of deadlocking;
  - a bind-mount alias of the same inode fails safely on the process's own shared lock.
- **Tests** (my earlier notes 1 and 2 are fixed):
  - `wait_locked` waits for `pid=<child pid> ` metadata, which the writer writes only after
    locking, before its single probe. The probe can no longer race the writer's acquisition;
  - `preexec_fn` restores the default SIGINT disposition in the children.

  The rest is unchanged: exclusion of a live writer, TERM/INT/KILL and resume on the same
  inode, stale metadata, coexisting shared readers and source writers refused, source
  immutability, and setup failures releasing both locks.
- **Documentation** (README:485-506): metadata versus the real lock, release on process
  death, no deletion needed, sources are never written, cooperative scope, the directory
  must stay in place, `flock` on directories must be supported by every participant (with
  an explicit error otherwise), old coordinators must not be mixed in, and persistence and
  fsync work is separate.

## Non-blocking
1. **Networked file systems.** On Linux, `flock` on a *directory* over NFS may be enforced
   only on the local client, and it can succeed silently. The per-file NLM emulation used for
   regular files may not apply to directories. The README requirement covers this in
   principle. Saying explicitly that the table should live on a local file system, or that
   sharing it across hosts is unsupported, would prevent a silent loss of exclusion.
2. **Old coordinators.** A table written by the new coordinator keeps `training.lock`
   permanently, so old coordinators, which check for the file's existence, refuse to write or
   extend from it. That fails safe and follows from the "don't mix" rule. A one-line remark
   could say that removing the file is safe only when no new-protocol process is using the
   directory.
3. **Performance.** Both `/tmp/mus-review-j17-lock-performance*.log` files are identical and
   predate the directory redesign. Their extension result is −1.57 % time and RSS within
   0.01 %. The design adds one `open` and one `flock` per run and does nothing per query or
   per iteration, so no regression is expected. Record the final benchmark when it finishes.
