# J01 public preload integration: final recheck (Claude)

Verdict: **APPROVE.** I re-read the current files against eb6bd55, not the earlier
snapshots. This supersedes my REQUEST_CHANGES in /tmp/mus-review-j01-preload-claude.md.
The public-model cost was accepted by the user, and the J23 useful-mass statistics are out of
scope. Adapter support is the next separate block.

## Blocking finding B1: fixed
- tests/test_cli.sh:628-629 now creates the compressed source with
  `--format ordago-public --iterations 10 --seed 7 --save-compressed public-prior.zst`, a
  valid command. It matches public.cfr, which was trained with the same seed and iterations,
  and the raw-vs-zstd `cmp` checks depend on that match.
- /tmp/mus-review-j01-preload-tests.log (one mus) passes the full suite: solver, hot paths,
  public ordago (75,900 comparisons), **CLI** ("Todas las pruebas de CLI pasaron."),
  adapter, adapter configuration, adapter CLI and hybrid CLI.
- /tmp/mus-review-j01-preload-cli-two.log (two muses) ends with "Todas las pruebas de CLI
  pasaron." (18:16:38), with no usage or failure output.
- The previously unrun checks now pass in both builds:
  - raw and compressed public preload;
  - values.txt and every cell checkpoint identical between the two encodings;
  - resume with preload identical for raw and zstd;
  - the source checkpoint unchanged.

## Test bounds fix
- tests/test_ordago_public.c: the phase array is now `bool phases[MUS_PHASE_REDRAW_PLAYER_1 + 1]`.
- The old `MUS_PHASE_TERMINAL + 1` was indexed with mus and discard phases (values 8-11,
  above TERMINAL = 7), which wrote out of bounds. I missed this in my first review; it is now
  correct.
- The sanitizer run (/tmp/mus-review-j01-preload-sanitize.log) and the two-mus unit run
  (141,358 comparisons) pass.

## Production code: unchanged apart from documentation
- app/mus_solver_cli.c, src/mus_game.c and src/mus_preload.c are unchanged from what I
  reviewed.
- include/mus_solver/mus_score.h differs only in re-wrapped doc comments (the lazy-prior
  sentence now reads "…and apply the configured uniform mixture…"). There is no API change.
- My earlier analysis therefore still holds:
  - key translation: an injective initial-score offset; mus and discard keys unchanged;
    NOT_FOUND for an ended match or a response outside the budget; INVALID for a score below
    the initial one; output preserved;
  - public eager and lazy priors cover all lances, with identity-based renormalization over
    narrower menus;
  - the historical prior paths are unchanged;
  - descriptor validation checks the exact schema;
  - the CLI legacy-then-public fallback is safe: cfr_checkpoint_read preserves its outputs on
    error and returns INCOMPATIBLE_GAME on a schema mismatch.

## Performance (/tmp/mus-review-j01-preload-performance.json; three alternating pairs, CPU0)

| Workload | Parent | Candidate | Time | RSS |
|---|---:|---:|---:|---:|
| ordago-public, 500 iterations | 5.793 s | 5.497 s | −5.11 % | −0.12 % |
| score 35:35 | 3.706 s | 3.629 s | −2.06 % | 0 % |
| score with legacy prior | 5.046 s | 4.946 s | −1.97 % | 0 % |

- Counters are identical, all 46 score files are identical, and both sources are preserved.
  No regression.
- The public prior is new, with no earlier implementation to compare against: 4.93 s and
  109,444 KiB with the 20-iteration source (41.9 MB). Its size follows from the
  already-accepted public model.

## Non-blocking (carried over)
1. Score tables do not record which source seeded them (`ordago` or `ordago-public`). This
   predates the diff, and a README note would help.
2. When both schemas fail, the fallback error reports only the public attempt.
3. Keep the next adapter block under the original performance thresholds, measured against
   its own parent.
