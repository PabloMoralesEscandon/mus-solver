# J17 process-lock review (Claude)

Verdict: **REQUEST_CHANGES**, narrowly scoped to one compatibility regression affecting
extension sources (R1). The locking design is otherwise sound. Diff reviewed against
0373fc1, including the untracked tests/test_score_locks.py. The logs show the full debug
suite, the lock suite (normal and sanitizer) and the release build all passing.

## Required change

**R1. Extending from a table written before J17 now mutates the source, and fails if the
source directory is not writable.**
- `acquire_score_lock(lock, false, …)` opens the source lock with
  `O_RDONLY | O_CREAT | O_CLOEXEC` (app/mus_solver_cli.c:709).
- Every table produced before J17 lacks `training.lock`: the old coordinator ran
  `remove(lock_path)` on every exit path.
- So on the first extension from such a table:
  - if the source directory is writable, the coordinator **creates a new file in the
    source**. That contradicts the README's "shared sources open read-only" and the
    immutability claim. The test's source hashes cannot detect this, because its source was
    written by the J17 build and already has the lock file;
  - if the source directory is not writable (archived tables, a table owned by another user,
    a read-only mount), `open` fails with EACCES or EROFS, and the extension aborts with
    "no se pudo abrir el bloqueo". **Before J17 this succeeded.**
- Repro: produce a table, run `rm source/training.lock` (the state a pre-J17 coordinator
  leaves), then `chmod a-w source`, then run
  `mus-solver --format ordago-score … --extend-continuation source --continuation dest`.
  It exits 1.
- Suggested fix:
  - open without `O_CREAT` first;
  - on ENOENT, retry with `O_CREAT`;
  - if that fails with EROFS, proceed without a lock: no writer can exist on a read-only
    file system, because a writer must create `values.txt.tmp`;
  - on EACCES, fail with a specific message (the owner must create `training.lock`), or
    proceed with a warning and document the weaker guarantee;
  - document that the first extension of a pre-J17 writable table adds an empty
    `training.lock` to the source.
- Add a test: a source without `training.lock` in a directory without write permission
  (skip when euid is 0). It should succeed or give the documented error, and its files must
  stay unchanged.

## Verified correct

**Acquisition** (app/mus_solver_cli.c:706-738)
- `flock(LOCK_NB)` is exclusive for writers and shared for sources. Descriptors use
  O_CLOEXEC, and the process uses threads, not fork.
- Metadata is truncated and written (`ftruncate`, write, `fflush`) **only after** the
  exclusive lock succeeds, so a writer that is refused never touches the holder's inode or
  metadata (tested).
- Every failure closes the descriptor, which releases the lock. The inode is never unlinked,
  so two processes can never lock different files at the same path.

**Lifetime**
- Both `FILE*` locks live in `run_score` until `done:` (lines 1331-1334). The only returns
  in `run_score` are before the locks (line 1156) and at its end (line 1339), so every error
  path releases them.
- The source's shared lock is held for the whole destination run, which covers lazy loading
  of source cells.
- There is no signal handler: SIGINT, SIGTERM and SIGKILL end the process and the kernel
  drops the locks. The test resumes on the same inode after each signal.

**Deadlocks**
- Every lock is non-blocking. Crossed extensions (A→B and B→A) each fail immediately instead
  of waiting for each other.

**Alias rejection**
- `check_score_output_path` now runs before the shared lock (line 1007). It resolves both
  paths with `realpath`, so the same directory, a symlink alias and a nested output are
  rejected with the right message, not a confusing self-lock failure (test_cli.sh:557-564).

**Compatibility documentation**
- README:486-500 states the permanent inode, release on process death, not deleting the file,
  the shared source lock, not mixing with old coordinators, the stale metadata in the error
  message, and that persistence (J08) and fsync durability are separate work.
- test_cli.sh now expects `training.lock` to remain. The benchmark excludes it from the
  policy hashes.

**Tests** (tests/test_score_locks.py)
- a live writer excludes a second writer, which leaves inode and metadata unchanged;
- after TERM, INT or KILL the lock is released and the next run resumes on the same inode;
- orphan metadata with a fake PID is ignored;
- shared readers coexist with an extension, and source writers are refused during it;
- the source is unchanged by hash;
- an active source writer refuses new extensions;
- setup errors release both locks and keep the files.

## Non-blocking

1. **Test flakiness risk.** `held()` probes by taking `LOCK_EX|LOCK_NB` itself. If a probe
   lands between the writer's `open` and its `flock`, the writer is refused and exits, and
   `wait_locked` fails. In `wait_locked`, wait for the metadata `pid=<p.pid>` first (the
   writer writes it only after locking), then probe once.
2. **Inherited SIGINT disposition.** If the suite runs as a background job in a
   non-interactive shell (`make test &` in a script), SIGINT is ignored and the child
   inherits that. The SIGINT case would then time out. Pass
   `preexec_fn=lambda: signal.signal(signal.SIGINT, signal.SIG_DFL)` in `start()`.
3. **Advisory-lock assumptions.**
   - Document that only cooperating processes are excluded: `cp`, `rsync` and editors ignore
     the lock.
   - Document that the continuation directory should be on a local file system, or on NFS
     where `flock` is emulated with fcntl locks (Linux 2.6.12 and later). Elsewhere it may be
     node-local.
4. **Error message.** Any `flock` error (ENOLCK, EINVAL on some file systems) is reported as
   "lock held". Checking `errno == EWOULDBLOCK` would give a clearer message for unsupported
   file systems.
5. **Read-only paths are unlocked.** Evaluate/export without `--iterations`, and the
   single-cell `--load` path (`run_score_checkpoint`), take no shared lock, so they can read a
   table while a diagonal is being published. This predates J17. Files are replaced
   atomically, but values.txt and the cells could come from different diagonals. A shared
   lock there would make J17's guarantee uniform.
