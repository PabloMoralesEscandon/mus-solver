# J13 TXT v2 availability review (Claude)

Verdict: **APPROVE** (bounded, non-blocking nits).

This was a read-only review: I ran no builds, tests or benchmarks. Scope:
- the working-tree diff against f723699: `src/mus_strategy.c`, `src/mus_text_strategy.[ch]`,
  `include/mus_solver/mus_strategy.h`, README, `docs/adapter.md`, `docs/match-evaluation.md`,
  the Makefile, and the edits to `tests/test_adapter_cli.py`, `tests/test_cli.sh`,
  `tests/test_match_evaluation.py`, `tests/test_mus_adapter.c` (the fixture builder) and
  `tests/test_mus_solver.c`;
- the new `tests/test_text_availability.py`.

C-CFR and Simulador-Mus are clean, and neither submodule pointer changes.

## J13 action fulfilled
The audit asks to version the TXT so it keeps mass or availability, and to check that the
bot's final distribution is the same across backends for trained, empty and absent nodes.
- **Versioned format.** Every row carries `available=0/1` right after its key, and the header
  says `version=2`.
- **Same rule as binary.** The exporter's `policy_available` (`mus_strategy.c:174`) and the
  binary adapter's `lookup` (`mus_adapter.c:575-584`) apply the same rule.
  - Every accumulated strategy sum must be finite and at least 0, and so must their total.
  - The node is available exactly when that total is positive; since the total cannot be
    negative, "positive" and "nonzero" are the same test. Signed zero also behaves the same.
- **Probabilities round-trip.** They are printed with `%.17g`, so TXT and binary return
  bit-identical distributions for available nodes.
- **Equivalence tested for every node kind:**
  - trained uniform, trained skewed (0.25/0.75), zero-mass and absent nodes;
  - both players, the grande, chica, juego and punto lances, mus and discard;
  - the historical, public and hybrid 39:39 routes;
  - binary, zstd and TXT give byte-identical adapter output;
  - the J12 full-match raw versus TXT check covers nearest-neighbour and interpolated
    lookups, and the fallback route, under real play.

## Reader: format and numerical handling
- **Version detection.** The v2 prefix is recognised before the v1 prefix is checked, and
  other versions are rejected (tested with version=3).
- **Availability parsing** (`parse_policy`).
  - The token must be exactly `available=0` or `available=1`, followed by a space, directly
    after the key. `=2`, `=-1`, `=00`, an empty value and a missing token all give
    FORMAT_ERROR (`invalid_checkpoint`).
  - The probabilities are still validated (finite, within [0, 1], sum within 1e-9 of 1) before
    `available=0` gives NOT_FOUND. A corrupt unavailable row is therefore an error, not a
    silent fallback (tested with NaN).
  - **Memory safety:** `row_key` already guarantees the `infoset key=<digits> ` prefix.
    `strncmp` stops at the NUL, and `cursor[11]` is read only when `cursor[10]` is `0` or `1`.
- **v1 compatibility.** Without the flag, every valid row stays available, so the historical
  "exact uniform at zero mass" behaviour is kept. The tests cover the generic formats,
  public, score and empty stores. The documentation says that an old reader rejects v2 and
  that re-exporting is needed to recover availability.

## Footer
The footer is read by `read_footer` (`mus_text_strategy.c:85-109`).
- **Bounded read.**
  - It reads at most 128 bytes into `tail[129]` and NUL-terminates at `length`.
  - `length` is at least 1 (checked through `remaining > 0`).
  - It requires no embedded NUL and a final newline.
  - The last line must be exactly `mus-strategy-end information_sets=N` (the same overflow
    check as the header), and N must equal the header's count.
  - A last line with no preceding newline inside the window is accepted only when the
    window starts at `first_row` (an empty store).
  - The longest possible footer is 55 bytes, so a valid file is never refused.
- **Search stays inside the rows.** `rows_end` becomes `file_size`.
  - In the binary search, `high` starts at `rows_end`. Its existing
    `row_start >= high → FORMAT_ERROR` guard cannot trigger falsely: the midpoint is at
    least 4097 bytes below `high`, and a row is at most 4096 bytes.
  - The linear scan stops at `low < high`.
  - So the footer is never parsed as a row. The first-row and last-newline checks now use
    `rows_end`, and the first row is re-read after an explicit seek.
- **Detects:**
  - no footer, including a file that is only a header;
  - suffix truncation, whether through a row or of whole rows;
  - a footer count different from the header;
  - trailing junk;
  - a missing final newline.

  All are tested. Opening stays O(1). The documentation correctly says the footer does not
  certify the file against interior edits.

## Cache and limits
- **Cache.** Rows with `available=0` are cached as not found (`.found=false`), exactly like
  absent rows, so repeated queries are cheap and give the same result.
- **Line length.** A row grows by 12 bytes, far below the 4096-byte line limit (a worst-case
  row with 15 discard actions is under 1 KB).
- **Allocation.** No new allocations; the footer uses a stack buffer.

## Writer
- `write_strategy` covers all five formats: single-bet, raises, ordago, ordago-public and
  score. Both row writers (`write_node` and `write_ordago_node`) emit the flag, and the
  footer uses the same `stats.size` as the header.
- An export that fails midway now produces a file the v2 reader refuses (no footer), which
  is a real safety gain.
- The header comment requires writers to be stopped during export.

## Nits (non-blocking)
1. **The footer comes from `stats.size`, not from the number of rows actually written.**
   Counting rows in `write_node`/`write_ordago_node` and returning an error when the count
   differs from `stats.size` (before writing the footer) would make the footer confirm what
   was written, not just repeat the header. It costs one counter.
2. **Coverage of the non-ordago formats.**
   - The single-bet and raises exports are only checked for `version=2` and a few fields
     (`test_cli.sh:118-120`, `test_mus_solver.c:2273`).
   - Nothing reads these formats back, so this is low risk.
   - One `grep -q 'available=' … && tail -n1 | grep -q '^mus-strategy-end '` on
     `strategy.txt` and `raises-strategy.txt` would pin the writer contract.
3. **Cache and action count.** A cached not-found result for an `available=0` row skips the
   action-count check, the same as for absent rows. It cannot happen in practice because the
   key determines the actions. Noted only for completeness.
4. **Progress record.**
   - The J13 entry in `docs/review-progress.md` isn't written yet.
   - The J12 performance paragraph there ("pérdida de masa cero pendiente de J13") remains
     accurate as history, because its TXT fixtures were v1. The J13 entry could say that
     v2 exports close that gap, and that v1 files keep both the old behaviour and the
     weaker truncation detection.
