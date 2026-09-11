# J09 closed score regions: final review (Claude)

Verdict: **APPROVE.** The required documentation is now in place, and the early publication
of the empty region header is correct. Diff reviewed against 46af298. The code I approved
earlier (/tmp/mus-review-j09-claude.md) is unchanged apart from the header publication
below.

**Results on this revision** (the CLI and test changed at 21:40:28, the docs at 21:41:11):
- final build: "All closed-region checks passed." (/tmp/mus-review-j09-final-build.log,
  21:41:14);
- ASan/UBSan: passes (final-sanitize.log, 21:42:06);
- two muses: passes (final-two.log, 21:42:06).

Performance belongs to the coordinator. The score group shows +3.59 % time and +0.04 % RSS,
with the gate not triggered, and the extension group was still running.

## Documentation (resolves the earlier required change)
README.md, lines 282-316:
- the `--score-region A:B` command and the coverage rule;
- the reason it is closed (scores rise, mano and postre swap);
- cell counts: 35:18 is 195 cells versus 351 for the full diagonals, 35:35 is 25 versus
  45, and `0:0` is 1,600. These match the core test anchors and the arithmetic
  Σk for k = 1..26 and k = 1..9;
- a warning that cell counts are not time savings;
- identical policies for common cells, with the same seeds and one thread per cell;
- defaults: without `--score`, training starts at A:B; bounds are saved sorted before the
  first cell;
- resume inference, with an error for a mismatch or an out-of-region score, and "use
  another directory" to change coverage;
- extension sources need the destination's cells and stay share-locked;
- the bot routes excluded scores to the generic policy, and only published cells need TXT
  exports;
- `0:0` uses the region schema, and v4 stays the default.

README.md, lines 577-582 (schema section): the `mus.ordago-score-region/v1` header with
`region=A:B`, and `/region-A-B` in the checkpoint and TXT schemas. These files are not
interchangeable with v4, even with identical policies.

docs/adapter-integration.md, lines 14-21: region tables are accepted, only the region's TXT
files are needed, excluded scores are generic, and exports from a full table must not be
mixed with a region table's.

## Early empty-header publication (app/mus_solver_cli.c, run_score)
- `new_table` is set only when `values.txt` is absent (`ENOENT`) during a training run
  (`iterations_set`). Every other open failure, or a read-only run on a missing table,
  still fails, so evaluate and export never create files.
- **Order:** directory lock, then table read or creation, `resolve_score_region`, the
  extension coverage check, the manifest check (which writes `extension.txt` for new
  extensions), then **the header**, then `print_score_resume`, recovery and training. So
  the bounds are persisted only after validation, and before any cell trains.
- **Scope:** only new region tables (`new_table && table.region_enabled`). A new v4 table
  is unchanged, with no early `values.txt`.
- **Write path:** `save_score_diagonal`, the same temporary file and rename as every
  diagonal. There is no fsync, consistent with J17 being paused.
- **Empty tables are valid.** `valid_table` accepts a region table with no ready cells, and
  `mus_score_table_read` accepts zero rows. Other readers handle it:
  - the bot routes everything to the generic policy;
  - a single-checkpoint load reports missing continuations;
  - an extension from it fails its coverage check;
  - `print_score_resume` prints nothing when the count is 0.
- **Test** (tests/test_score_regions.py:66-74): an obstacle at the first cell's checkpoint
  makes the first `--score-region 2:1` run fail. `values.txt` is left with the `region=1:2`
  header and no rows. The retry without the option infers 1:2, reports
  `score-table score=1:2`, and its rows equal the reference region table. This closes my
  earlier non-blocking note 1.

## Non-blocking
1. Because the bounds are now persisted before training, a typo in `--score-region`
   followed by any failure leaves the directory fixed to that region, and a corrected
   retry fails as a mismatch. That is the intended, documented behaviour ("utilice otro
   directorio"), but a sentence saying that deleting an empty `values.txt` (one with no
   rows) is also safe could spare users a new directory.
2. The final logs shown cover the region suite in the normal, ASan and two-mus builds. The
   coordinator should also record a full-suite pass on this exact revision and the finished
   performance groups before committing.
