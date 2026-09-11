# J19 rolling score-cell slots: review (Claude)

Verdict: **CHANGES REQUESTED — one small, concrete fix, plus a clarification in the header
contract.** The scheduler itself is correct and I approve it.

This was read-only: I ran no builds, tests or benchmarks. Scope:
- the working-tree diff against HEAD `142e203`: `src/mus_score.c`,
  `include/mus_solver/mus_score.h` and the Makefile;
- the new `tests/test_score_scheduler.c`;
- the callers in `app/mus_solver_cli.c` and `app/score_recovery.c`, and the store
  destruction in C-CFR.

C-CFR is unchanged, and J13 is still absent.

## Scheduler correctness: verified
- **Rolling mapping.** The first `active` cells start in slots 0..active−1. Cells are
  consumed strictly in score order: at position *o*, slot `o % N` is joined; then that slot
  alone is refilled with `scores[o + N]`, which is consumed at position *o + N*, in the same
  slot. Every cell is trained exactly once and consumed in order.
- **At most N live stores.** A slot is reused only after its result has been read, saved
  and its store destroyed. Finished results waiting for their turn stay within the N slots.
  The arrays are sized `MUS_ORDAGO_WAGER`, and `worker_count ≤ target`.
- **Serial behaviour is identical.** With `worker_count == 1`, or a single cell, `parallel`
  is false, so `start_score_cell` trains on the caller. The order is train 0, save 0,
  train 1, save 1, and so on, exactly as before. When `count < N`, every cell starts at once
  and none is refilled.
- **Saves stay on the caller, in order.** `save_cell` runs only on the caller, after
  `pthread_join` of that slot (which establishes happens-before), in score order.
  - `*options->preload_stats` is written only by the caller.
  - Workers write only their own `worker->preload_stats`, and the progress callback
    receives that pointer.
- **Continuations stay immutable.** Workers read `table` (the higher diagonals) and the
  extension's source table.
  - Results go into the local `batch[]`, and `table->cells` is written only after every cell
    in the diagonal has been consumed.
  - `save_cell` receives `const` data. Rolling back after a `save_diagonal` failure is
    unchanged.
- **Thread-creation fallback.** If `pthread_create` fails, `start_score_cell` trains that
  cell synchronously on the caller and records `started = false`, so the consume step skips
  the join. The results match the serial reference; the test checks this bit for bit.
- **Failure cleanup.** On an error from a worker or from `save_cell`:
  - no further cells are assigned;
  - every started thread is joined;
  - every slot's store is destroyed before `free(workers)` and returning.
  - The current slot's store is destroyed twice (lines 647 and 654), and a consumed slot
    that was not refilled is destroyed a second time too. This is safe:
    `cfr_info_store_destroy` resets every field, including `concurrent_state`, so a second
    call frees NULL pointers.
- **Recovery races: none.**
  - `score_recovery_load` runs on worker threads. It only reads `records[][]`, the options
    and `prior_hash`, which are filled at open and never changed afterwards.
    `score_recovery_save` only appends to `journal`, and only on the caller.
  - Workers read their own cell's `score-AA-BB.cfr.zst`, which is never the file the caller
    is saving (saving happens only after that slot is consumed).
  - The source directory is read-only, `errno` is thread-local, and extension loads were
    already concurrent.

## Required fix: output lines can interleave (`app/mus_solver_cli.c`)
`save_score_cell` builds each `score-cell` line from several stdio calls:
- `printf("score-cell … win_probability=%.17g")`, with no newline;
- then, in `print_score_statistics`, `printf(" uncertainty=…")`;
- an optional `printf(" sampling=…")`;
- and finally `putchar('\n')`.

Before J19 this ran only after every worker in the batch had been joined. **Now it runs
while other cells train.** Worker threads print whole lines with their own `printf` calls:
- `score-report …` from `report_score_progress` with `--report-every`;
- `score-extend …` from `load_score_extension_cell`;
- `score-recovered …` from `score_recovery_load`.

stdio locks each call, not the whole line. So a worker's line can land between the caller's
pieces, and a worker's `fflush` can flush a half-written line. For example:

```
score-cell score=1:2 target=5 … win_probability=0.5score-report score=0:3 … added_iterations=4
 uncertainty=local-sampling sample_variance=… standard_error=…
```

This breaks machine-readable output with supported combinations: `--threads > 1` together
with `--report-every`, `--extend-continuation`, or a partial recovery. Tests that match
`score-cell ` lines could become flaky (for example `test_score_statistics.py:43` uses
`line.startswith('score-cell ')`).

**Fix, either of:**
- wrap the output section of `save_score_cell` (the preload report, the `score-cell` line
  and the statistics) in `flockfile(stdout)` … `funlockfile(stdout)`. POSIX stdio locks
  are recursive, and `_POSIX_C_SOURCE 200809L` is already defined;
- or format the whole line into a buffer and emit it with a single `fputs`.

The fix should also cover `(void)fflush(stdout)`, although that alone is harmless.

**Header contract.** `mus_score.h` now says that save callbacks "can overlap training of
other cells". Please state the consequence explicitly: `save_cell` can run concurrently
with the `progress`, `recover_cell` and extension `load_cell` callbacks of other cells, so
any state these callbacks share (stdout included) must be synchronized.

## Non-blocking notes
1. **Contention with training threads.**
   - The caller no longer trains a cell itself.
   - With N × M training threads, the caller's save (zstd compression, plus a full re-read
     of the checkpoint for the recovery hash in `score_recovery_save`) now competes with
     them.
   - `docs/performance.md` says "`--threads 2 --threads-per-cell 4` puede entrenar dos
     estrategias con hasta ocho hilos en total". That should mention the caller's
     concurrent save.
   - The pending measurements should cover N equal to the number of cores. If needed,
     N − 1 is a useful comparison.
2. **Head-of-line blocking.** Because results are consumed in order, a slow cell keeps
   finished slots idle until its turn. This is inherent to a bounded in-order scheduler,
   and the docs already warn that a much costlier cell limits the speed-up.
3. **Test gaps.** The test is well designed:
   - its dependency chain in `recover_cell` would deadlock (time out) under the old batch
     barrier, so it proves the rolling behaviour;
   - it checks that saves run on the caller, in order;
   - after a save failure it checks that only 3 cells started, that nothing is still
     running, and that nothing was published;
   - its thread-creation fallback matches the serial reference bit for bit.

   Optional additions:
   - a worker failure (for example `recover_cell` returning an I/O error for cell 1) while
     cell 2 is running;
   - a transient creation failure (one failure, then success). Today, once injection
     starts, every later creation fails.
4. **Makefile.** The target is added to `test`, so it also runs under `test-sanitize` and
   `test-tsan`. Linking is safe: the test object defines every symbol of `mus_score.c`, so
   the archive's `mus_score.o` is never pulled in.
