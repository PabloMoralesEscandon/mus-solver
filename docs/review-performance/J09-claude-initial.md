# J09 closed score regions review (Claude)

Verdict: **REQUEST_CHANGES, documentation only.** The code is correct and I approve it. The
feature adds a user-visible CLI option, a new table and checkpoint schema family, and a new
`values.txt` header field, but README.md and docs/ have no mention of `--score-region`,
`mus.ordago-score-region/v1` or `region=` (`grep` finds nothing, and README.md is not in the
diff). Diff reviewed against 46af298, including the new tests/test_score_regions.py. C-CFR is
unchanged.

**Results:**
- core tests pass (/tmp/mus-review-j09-core.log);
- region CLI tests pass (/tmp/mus-review-j09-cli.log);
- ASan/UBSan core and region runs pass (sanitize.log);
- two-mus core, hot-path and region runs pass (two.log);
- the rest of the suite passes (tests.log).

## Required change
Document `--score-region A:B` in README, next to the `ordago-score` options and the
score-table schema section:
- **Coverage:** the union `x≥a, y≥b` or `x≥b, y≥a`, with bounds stored sorted. It is closed
  under score increases and the mano/postre swap, which is why its values match a full table.
- **Format:** the table header `mus.ordago-score-region/v1 … region=A:B`, and the
  `/region-A-B` checkpoint schema. These are incompatible with v4 checkpoints and TXT
  exports.
- **Defaults and resume:**
  - without `--score`, training starts at A:B;
  - when the option is omitted, the persisted bounds are inferred;
  - an explicit mismatch, or a score outside the region, fails.
- **Extensions:** the source needs only the destination's requested cells, and stays
  share-locked for the whole run.
- **Bot:** excluded starting scores are routed to the generic policy. Export TXT files only
  for ready cells.
- **Default unchanged:** without the option, the v4 format and behaviour are unchanged.

## Verified correct
- **Closure.** Take `(x, y)` with `x ≥ a, y ≥ b`, and any score `(x', y') ≥ (x, y)` at the
  end of a hand. The continuation is `cells[y'][x']`, where `y' ≥ b` and `x' ≥ a`, which is
  the other orientation. So every continuation of a region cell is in the region. Each
  orientation is an up-set, and their union is symmetric.
  - `mus_score_table_contains` excludes terminal coordinates and malformed bounds.
  - The core test checks symmetry, monotonicity and cardinality (100, 195 and 1600 cells)
    for three anchors.
- **Training equals the full table.** A cell's continuation values are identical to the
  full table's. The core test compares the complete CFR payload, excluding only the schema
  and its checksum, and the values against a full table, for four regions and with 1 or 2
  threads.
- **Schema.** `mus_score_game_set_schema` adds the `/region-A-B` coverage to the new schema
  ID. Its V-hash covers every higher cell, and excluded cells are always unready, so the hash
  is deterministic. `mus_score_game_init` now rejects excluded cells; its callers (training,
  the CLI after region resolution, the adapter for ready cells only, recovery and preload)
  never pass one.
- **Table validity and I/O.**
  - `valid_table` rejects ready cells outside the region and marks a diagonal as missing
    only when it has region cells.
  - Reading rejects rows outside the region.
  - The header is parsed and then re-formatted and compared exactly, which rejects swapped
    or out-of-range bounds, leading zeros, a region field on a v4 header (or the reverse),
    unknown versions and trailing text.
  - A table without a region writes the byte-identical v4 header (`fprintf` plus `fputc`).
- **Core training.** Excluded cells are skipped both in the extension pre-check and when
  collecting each diagonal. Diagonals with no pending region cell skip `save_diagonal`. J03
  rollback and J08 recovery operate on `scores[]`, the region cells, and J08's record
  includes the region schema.
- **CLI.**
  - `--score-region` parses with the J16 score grammar and is rejected for non-score formats
    or bounds at or above the target.
  - Without `--score`, the score defaults to the given bounds.
  - `resolve_score_region` runs after the table loads, in both `run_score` and
    `run_score_checkpoint`, on a local copy of the options. An explicit region must equal
    the persisted, sorted bounds, and a v4 table rejects any region. When the option is
    omitted, the persisted bounds supply the score. A score outside the region fails.
  - Single-checkpoint validation ignores excluded higher cells.
  - `print_score_resume` counts only ready cells, so nothing is misreported.
- **Extensions.** The coverage check now runs after the destination loads and requires only
  the destination's region cells at or above the requested sum. The source shared lock is
  still taken in `read_score_extension` and held until `done:`. The manifest still records
  the full source table. Tests cover full→region, region→region, region→smaller and a
  too-large destination, with the source unchanged.
- **Bot.** The adapter reads region tables unchanged and routes by `cells[x][y].ready`. The
  real-bot test routes the 25 cells of 35:35 to the score policy and excluded scores to the
  public generic, without modifying the files.

## Non-blocking
1. **Coverage exists only once the first diagonal is published.** Bounds are stored only in
   `values.txt`. If the first `--score-region` run dies before publishing the top diagonal,
   during the top cell or the extension's first cell, a retry without `--score-region` finds
   no table and silently creates a **full** table. It then retrains more, and overwrites the
   unpublished region checkpoint. The results remain valid, but the user's intent is lost.
   Either write an empty region header when the table is created (`valid_table` accepts a
   table with no ready cells), or document that the option must be repeated until the first
   diagonal is published.
2. **A 0:0 region.** `--score-region 0:0` creates a region table that covers everything but
   uses the region schema, not v4. That is harmless, but worth one sentence in the docs.
3. **Integration guide.** docs/adapter-integration.md could mention region tables when it
   says "TXT de todas las celdas publicadas". The current wording already implies only ready
   cells.
