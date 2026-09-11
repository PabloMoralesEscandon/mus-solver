# J01 final core/CLI review after restoring the change (Claude)

Verdict: **APPROVE** for the restored core/CLI change. The working tree was reviewed against
HEAD 83c0a28 and against the approved snapshot /tmp/mus-review-j01-blocked-candidate. The
earlier approval (task_664c5e0eef1e, msg_77f6aee17f98) still holds. J01 stays open until the
adapter and preload integration are reviewed.

The user accepted the model cost (+195.42 % time, +125.04 % RSS at 500 iterations, CPU0,
seed 7), and docs/review-progress.md and docs/review-performance/J01.json record that. As
instructed, this review does not revisit it. The exception covers only that model cost.

## Scope of the changes since the approved snapshot
`cmp` against the snapshot shows exactly five changed files:
- src/mus_game.c
- tests/test_cli.sh
- tests/test_mus_hotpaths.c
- tests/test_ordago_public.c
- docs/review-progress.md

Makefile, README, the CLI, evaluation, strategy, training and the headers are byte-identical
to the approved candidate. The new untracked docs (J01.json, J01-blocked.patch,
J01-claude-review.md) are records only.

## 1. The CLI success message now runs last (tests/test_cli.sh:626-627)
The echo was moved after the ordago-public block, so the message now appears only after every
check has passed. Cosmetic: a double blank line is left where the echo was removed (around
line 596).

## 2. Accepted-ordago validation guard (src/mus_game.c, validate_model, lance loop ~1486-1495)
- For PUBLIC_DEFERRED formats it rejects:
  - an accepted-ordago outcome while the phase is not TERMINAL;
  - any non-SKIPPED outcome after the accepted-ordago lance.

  Both checks happen before any other outcome validation. The ordago lance itself is not
  compared with itself: the "later lance" check runs before that lance is marked.
- The guard also applies to **score v4**, which is PUBLIC_DEFERRED too. It accepts every
  reachable v4 state:
  - v4 reaches TERMINAL in advance_lance through score_match_finished;
  - the shared apply-level accepted-ordago block (src/mus_game.c:2612-2630) marks every
    later lance SKIPPED (no wager, no ordago flag, strength NONE) and sets TERMINAL, for
    every ordago format.

  So the guard only removes crafted, unreachable states. The existing v4 accounting and path
  tests validate v4 states through the public operations and would catch a false rejection.
- Legacy HISTORICAL validation is untouched, which is conservative scoping.
- Trusted operations skip whole-state validation, so the training hot path gains no cost.
- Test (tests/test_ordago_public.c:251-264): an accepted ordago in grande is TERMINAL and
  valid. Setting phase = CHICA is rejected. TERMINAL with a later outcome of ALL_PASS is
  rejected. The progress entry says the regression failed before the guard was added.

## 3. Hidden-hand groups with a raise and an uncovered ordago rejection
(tests/test_ordago_public.c:156-199)
- Scenario 4: the opener bets 2. If the first responder sees one numeric wager (read from
  the public decoder), it raises 4 when the menu offers it; the second response accepts.
  This covers response masks and accepted raised contracts in the grouping.
- Scenario 5: the opener plays ordago, and it is rejected. This covers uncovered payments
  (1 point each) and the paid/prior public keys that follow them.
- The observation comment now says that the strength fields derive from public betting. This
  is correct: current_strength changes only with betting actions (src/mus_game.c:2560-2600).
- Not covered (optional): a rejected **covered** raise (deje > 1) in the grouping. The random
  walk and the paid-40 test cover those codec paths.

## 4. Legacy playout digests (tests/test_mus_hotpaths.c:207-274)
- 256 fixed-seed playouts for each historical format (single-bet, raises, ordago) through
  the public operations, so every step is validated.
- The FNV-1a digest takes explicit little-endian bytes of: the phase, the menu size, every
  legal action, the key at player nodes, and the terminal utility (range-checked to [-40, 40]
  and offset to non-negative).
- Odd samples avoid ordago, so playouts reach deeper lances. Separate ordago constants for one
  and two muses. single-bet and raises are independent of the mus limit, so one constant each
  is correct.
- The constants cannot be recomputed in a read-only review. The progress entry says they were
  computed with mus_game.c and mus_game.h at 83c0a28. Recommendation: record in J01.json the
  exact build and command used to generate them, so they can be reproduced.

## New defects or regression concerns
No new functional defect was found. Non-blocking:
1. The guard extends to score v4. This is behaviourally neutral for reachable states (see
   §2). J01.json says no complete v4 checkpoint byte comparison was run for this candidate.
   A v4 playout digest (one or two cells, fixed seed), or a byte comparison of v4 checkpoints
   from the pre-change version, would lock v4 the way the new digests lock v9.
2. Record how the digest references were generated (see §4).
3. Cosmetic double blank line in tests/test_cli.sh.
4. J01 remains open: the adapter and preload integration are separate reviewed commits. The
   accepted-cost exception must not cover them. Measure them against their own parent, using
   the original thresholds.
