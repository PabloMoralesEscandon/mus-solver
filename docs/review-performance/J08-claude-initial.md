# J08 completed-cell recovery review (Claude)

Verdict: **APPROVE** (non-blocking notes below). The tracked diff was reviewed against
14bbd70, together with the new app/score_recovery.{c,h} and tests/test_score_recovery.py.
C-CFR is unchanged.

**Results:**
- final full suite (/tmp/mus-review-j08-final-tests.log): passes, including
  "All completed-cell recovery checks passed.";
- recovery test under ASan/UBSan (/tmp/mus-review-j08-sanitize-recovery.log): passes;
- two-mus run: the solver and hot-path suites pass.

The earlier sanitizer failure (/tmp/mus-review-j08-sanitize.log, 21:07:15, "cell did not
finish") came from the old 20,000-iteration SIGKILL case, which exceeded the 30 s deadline
under ASan. The test was changed to 500 iterations at 21:07:51 and now passes.

## Protocol
- **Write** (`score_recovery_save`, called from `save_score_cell` only after
  `save_checkpoint` succeeds):
  - it hashes the **published** checkpoint's bytes;
  - it formats one record with the cell result (value in `%a`, iterations, samples, seed),
    the checkpoint hash, the exact game schema (including the continuation hash), every
    setting, the prior's content hash and, for extensions, the source cell checkpoint's
    content hash;
  - it appends `"\n" record "\t" FNV-1a "\n"` and flushes.

  The leading newline isolates a partial tail left by a killed writer (tested).
- **Open** (after the exclusive lock at line 1216 and the manifest check at line 1252; the
  journal opens at line 1281):
  - it keeps only newline-terminated records with a valid checksum and cell indexes below
    40;
  - it keeps the **last** record per cell, skips overlong lines, and appends to the
    journal from then on.
- **Recover** (`score_recovery_load`):
  - it re-formats the expected record from the current settings and requires an exact
    string match, so any change to schema, settings, prior or source content means
    retraining;
  - it re-hashes the current checkpoint and requires the stored hash;
  - it loads the checkpoint through `cfr_checkpoint_read_zstd`, which checks schema, CRC
    and counters, and requires MCCFR external sampling with the expected iterations.

  FORMAT or INCOMPATIBLE problems become NOT_FOUND (retrain). I/O and memory errors abort.
- **Library** (src/mus_score.c:328-349): after a successful callback, it re-checks
  ready/valid, iterations (added plus source), samples and seed against the options, and
  skips training, evaluation and `save_cell` for recovered cells. Whole-diagonal publication
  and the J03 rollback are unchanged, because recovered cells go into the same batch.

## Crash windows (all safe)
| Interruption point | Next run |
|---|---|
| after the checkpoint rename, before its record | no record → retrain, which overwrites the checkpoint |
| during the record write | partial line skipped; the next append starts on a new line |
| the next run retrains the cell but dies before its record | the last record's hash no longer matches the new checkpoint → retrain |
| a `values.txt` failure (J03) | the next run recovers the saved cells and republishes the diagonal |
| power loss without fsync (J17 not applied) | the record may be on disk before the checkpoint's data, but the content hash then mismatches → retrain |

## Changed inputs
- **Prior contents:** hashed when the journal opens. Tested: same path, new contents,
  mtime restored.
- **Extension source cell:** its checkpoint is hashed both when the record is written and
  when recovering, and paths are never recorded, so moving the source doesn't matter.
  Changes to the source `values.txt` are already rejected earlier by the extension manifest.
- **Destination continuation values:** their schema hash (`V-…`) changes, so the cell
  retrains.

## Parsing, ownership, memory, concurrency
- **Parsing:** strict unsigned parsing; hexadecimal hashes of exactly 16 digits; values are
  finite and within [-1, 1]; `%a` round-trips exactly; records are about 400 bytes, well
  under the 2048-byte buffer.
- **Memory:** records are duplicated, a replaced record frees the old one, and
  `score_recovery_close` frees everything and is safe on a zeroed struct (it is always
  called at `done:`). Each recovery frees its temporary state and store.
- **Concurrency:** the record cache and options are immutable after opening, and the
  callback only reads immutable source files and cell checkpoints that belong to it. Only
  the coordinator thread writes the journal, so one cell is never both recovered and saved.
- **Old tables:** without a `recovery.txt`, behaviour is the same as before, and the first
  save creates the file. It is excluded from benchmark hashes and documented in
  README:485-532, including when it is safe to delete.

## Tests
- **Core:** the library test covers recovery with 1 and 2 threads, a post-callback mismatch
  becoming INCOMPATIBLE, an error from the callback aborting the run, success with exactly
  one save per cell, and final values equal to a run without recovery.
- **CLI:**
  - a deterministic interruption (an obstacle directory) followed by recovery that leaves
    the checkpoint untouched (same hash and mtime) and gives policies identical to a full
    run;
  - truncated record, bad checksum, bad checkpoint, and changed threads or reservation;
  - a partial tail, a changed prior, extension recovery with the source unchanged;
  - a real SIGKILL.

## Non-blocking
1. **Contradictory README sentence.** README:550-551 still says checkpoints of an
   interrupted diagonal that are not in `values.txt` "se ignoran; se reemplazan cuando se
   vuelve a entrenar esa celda". It should say they are reused only through a valid
   `recovery.txt` record, and are otherwise ignored and replaced.
2. **Kill-test race margin.** With threads = 1, the SIGKILL case kills after the
   `score-cell 1:2` line while cell 2:1 trains (500 iterations). If a loaded machine
   delayed the kill until 2:1 and `values.txt` finished, 1:2 would be published instead of
   recovered, and the assertion would fail. A generous `--evaluation-samples` for the
   second cell is the only cheap lever (both cells share settings); alternatively, check
   that `values.txt` is unchanged after the kill, so a failure gives a clear message.
3. **Conservative settings.** `--threads` (diagonal parallelism) does not change
   per-cell results, yet a change prevents recovery. That is safe but stricter than needed;
   `threads_per_cell` does affect determinism and is correctly included.
4. **Abort instead of retrain.** A post-callback mismatch in src/mus_score.c returns
   INCOMPATIBLE and stops the whole run instead of retraining that cell. Only a bug or a
   checksum-passing corruption could reach it, so aborting is acceptable; returning
   NOT_FOUND to retrain would be more forgiving.
5. **Performance.** Each save re-reads the checkpoint (the page cache should be warm) and,
   for extensions, the source cell. Each recovery hashes and fully loads the checkpoint,
   with peak memory similar to training that cell. The journal is append-only and never
   compacted (about 400 bytes per saved cell). The coordinator's benchmark should cover a
   table with many cells.
