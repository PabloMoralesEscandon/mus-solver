# J12 final recheck (Claude)

Verdict: **APPROVE, once one small provenance note is added** (see "Required correction").
The code, the tests and every number in the documentation are correct.

This recheck was read-only: I ran no builds, tests or benchmarks. I only read files and took
SHA-256 hashes of six small release binaries.

Scope:
- the new assertions in tests/test_match_evaluation.c;
- the J12 section of docs/review-progress.md;
- docs/review-performance/J12.json. It was rewritten at 22:42:13, while I was reviewing it,
  and this recheck covers the current 24 KB version.

## The new assertions (tests/test_match_evaluation.c:64-75, 167)

### `hand_scores_complete`
The test expects it true only in grande, and in chica after a rejected grande (the DEJE and
DEJE_ORDAGO scripts). That matches the host (app/match_evaluation.c:184-200):
- PASS, MUS and TWENTY: grande is passed or accepted, so a hidden winner remains and the
  flag is false from chica onwards;
- DEJE and DEJE_ORDAGO: the grande rejection is fully paid, so the flag stays true in chica,
  and the accepted chica makes it false from pares onwards;
- ACCEPT_ORDAGO: only grande is checked;
- a rejection makes the flag false only in pares (`previous == 2`), as specified.

### Skipped-pares declarations
The test checks `own_pairs == (declaration == own_declaration)` from each player's own
perspective. The host computes the declaration from `tipoPares` for mano and then postre,
relative to mano (`:194-199`).
- If both players had pares, pares would have been played, so the rule is exactly right.
- Because the rule is checked from both relative seats while the first mano alternates, it
  would catch a flipped orientation.
- Line 167 requires all three declarations (none, player 0, player 1) to appear in the
  12-match PASS run with seed 7.

### `prior_wager_total` after a rejection
I withdraw that part of my earlier nit 1: it was already tested directly at `:97` (for
DEJE, `prior == paid + pending`).

### Remaining (optional, not required)
The pares-specific transitions of `hand_scores_complete` are never tested in isolation:
- a rejected pares;
- a skipped pares where one player holds pairs.

Every script that reaches pares has already made the flag false earlier (a passed or
accepted grande or chica). A script that rejects grande, rejects chica and then plays or
skips pares would isolate them.

## Dispositions of my earlier nits
- **Binomial standard error:** accepted. A Wald error is zero at p=0 or p=1, and the
  documentation keeps the small-sample warning. A Wilson interval would be the natural
  choice if one is ever added.
- **Paired deal replay:** accepted as optional future work. It is not part of J12's action,
  and keeping the mus and discard trajectories coupled needs design work.

## docs/review-progress.md (J12 section)
- The description matches the reviewed code: the real `MusAdapter` path, a public
  observation, the 2–40 menu, one mus, a failure at the hand limit, marginal JSON versus the
  joint CSV, separate card and decision random streams, and no claims about policy
  strength or exploitability.
- The verification statements agree with the coordinator's report: the full suite, the
  simulator, ASan/UBSan for host and CLI, and the two-mus rejection.
- **Identical binaries.** I recomputed the hashes independently, and the parent J11 build and
  the J12 build match:

  | Binary | SHA-256 prefix |
  |---|---|
  | `mus-solver` | `b55fa9c1…` |
  | `mus-bot` | `8f260428…` |
  | `libmus_solver.a` | `d9d7a8fd…` |

  HEAD is `d80d01b`, so the "idénticos byte a byte" claim holds.

## docs/review-performance/J12.json
- **Table.** I recomputed every median from the 18 runs, and every entry in the table
  matches, including the KiB to MiB conversion:

  | Case | Total (s) | Game (s) | RSS (MiB) |
  |---|---:|---:|---:|
  | Historical binary | 1.0969 | 0.15882 | 476.36 |
  | Historical TXT | 1.0126 | 1.01113 | 2.77 |
  | Public binary | 3.1576 | 0.02474 | 1278.14 |
  | Public TXT | 0.1411 | 0.13970 | 2.74 |
  | Two public binaries | 6.2784 | 0.01397 | 2559.20 |
  | Hybrid | 0.0123 | 0.01101 | 3.05 |

  The spread is small: every case is within about 2 % of its median. Results are identical
  across repeats (the script asserts this, and the stored `result_sha256` values are equal
  per case).
- **Other claims.**
  - The generic cases exercise all five methods; the hybrid case at 39:39 uses exact,
    fallback and mus-limit decisions over 1,000 one-hand matches.
  - The binary RSS is about the size of the loaded tables (two instances in the self-play
    comparison, about twice the single public case).
  - TXT stays at about 3 MiB, consistent with on-demand lookup.
  - The fixture inputs are hashed in streaming mode with `hashlib.file_digest`.
- **Launcher.** The RSS method is sound, and the discarded first trial was diagnosed
  correctly.
  - The issue: `posix_spawn` uses `CLONE_VM`, and on Linux `exec_mmap` folds the old
    (parent) mm's peak RSS into the child's `signal->maxrss`. So a child spawned directly
    by a Python process that had read large files reports Python's peak.
  - The fix: the evaluator is spawned by a fresh 16 KB C launcher.
    - The launcher's own peak RSS is inherited from Python, but it is never reported.
    - The evaluator starts with fresh counters and inherits at most the launcher's tiny
      memory footprint, which is below the evaluator's own floor of about 2.7 MiB.
    - `ru_maxrss` from `wait4` is therefore the evaluator's own peak.

## Required correction (provenance, documentation only)
The embedded `script` (and `/tmp/mus-review-j12-performance.py`, rewritten at 22:42:13.418)
is not the code that produced the stored measurements.
- **The evidence:** at about 22:40 I read the earlier 6.27 MB version of this JSON. Its runs
  stored the full `result` object and had no `result_sha256`. The current file's runs carry
  bit-identical timings, for example:
  - run 1, historical binary: `wall_seconds` 1.0986832229973515, `game_seconds`
    0.155545113, `peak_rss_kib` 487792;
  - run 1, historical TXT: 1.0145724449976115, 1.01332099, 2840.

  A re-run cannot reproduce wall times to 16 digits, so the current file is a
  post-processed copy: the results were replaced by their SHA-256. The embedded script was
  edited to write `result_sha256`, so it presents itself as the source that produced these
  numbers.
- **Why it matters:** the measurement itself is unaffected (only how results are stored
  changed), but the file is labelled a reproducible measurement with its source embedded.
- **The fix, either of these:**
  1. add a field such as `"postprocessing": "per-run result objects replaced by canonical
     SHA-256 after measurement; the executed runner stored the full result under 'result'"`;
     or
  2. embed the executed runner plus the transformation.

  One line suffices; no re-measurement is needed.

## Minor (optional)
`existing_training_artifacts_unchanged: true` is a constant. It is meaningful only because
the earlier hash assertions must have passed for the script to reach that line. A name like
`release_hashes_asserted_equal` would state what was actually checked.
