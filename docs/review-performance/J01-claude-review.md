# J01 core/CLI implementation review (Claude)

Verdict: **APPROVE for the core/CLI portion.** Diff reviewed against HEAD 83c0a28.
J01 stays open until the adapter and preload integration are reviewed. The performance gate
is independent.

Correction to my design review (task_01b746042ef5): the bound I proposed there (paid <= 39,
max(paid) + prior <= 39) was wrong. I ignored that rejecting an **uncovered ordago** pays 1
point outside the numeric budget. The new tests reproduce both counterexamples:
- accepted 27 + 12, then an uncovered ordago rejection: max(paid) + prior = 40
  (tests/test_ordago_public.c:196-212);
- dejes of 17 and 22, then an uncovered rejection: paid 40 before punto
  (tests/test_ordago_public.c:214-235).

The public codec admitting 0..40 is correct. The score codec keeps its `< target` bound
(line 228 of the test checks that a public key with paid 40 is rejected by the v4 decoder).

## 1. Historical v9 and score-v4 identities are preserved

- The schema macros are unchanged. MUS_ORDAGO_PUBLIC_STRATEGY_SCHEMA_ID is new
  (include/mus_solver/mus_game.h:26-36). MUS_SCORE_SCHEMA_ID and its `base=` v9 are
  untouched, so v4 table headers and cell schemas do not change.
- The mode mapping is exact for every existing format:
  - legacy / single-bet / raises: HISTORICAL with target 0;
  - score: PUBLIC_DEFERRED with a nonzero target.

  Every replaced proxy (`score_target != 0` became `== PUBLIC_DEFERRED`,
  `score_enabled` became `== PUBLIC_DEFERRED`) therefore takes the same branch as before for
  every pre-existing state:
  - src/mus_game.c:793, 823, 853, 867, 897 (settlement dispatch);
  - src/mus_game.c:1082 (prior recomputation), 1113 (budget), 1766 (validation);
  - src/mus_game.c:3348 (key dispatch).
- The new `score_target != 0` guards (src/mus_game.c:719, 737, 765) have no effect for score
  games.
- The new accepted-ordago terminal check (src/mus_game.c:874-879) is redundant for v4: the
  ordago settles 40 to the winner, which is always >= the target, so score_match_finished
  already ends the hand.
- validate_model (src/mus_game.c:1457-1471) requires the model mode to equal the format mode.
  HISTORICAL still requires paid = pending = 0. Score keeps its target and initial-score
  checks.
- Every score-state construction sets the mode:
  - mus_score_game_state_init (src/mus_game.c:4223);
  - mus_score_information_set_from_ordago (src/mus_game.c:4089).

  The encoder builds keys by hand and never dispatches on the model mode
  (src/mus_game.c:4297-4390). mus_game_state_init zeroes the whole state
  (src/mus_game.c:1788), so legacy defaults to HISTORICAL.
- Inserting `accounting_mode` at the start of MusGameModel is safe for persistence: the C-CFR
  checkpoint writes trainer data (RNG, counters, store), not game-state bytes. It stays
  outside the trusted-undo betting suffix, as the header comment says.
- The v9 golden keys (511864500 / 511854660, and the two-mus values) are asserted in
  tests/test_ordago_public.c:78-82.

## 2. ordago-public semantics

- **Keys.** Betting phases use the v4 public layout, with per-player paid points, accepted
  pending prior and response masks (src/mus_game.c:3282-3296 via 3348). Mus and discard
  phases keep the v9 layout.
  - Previous-lance strengths are public: `current_strength` changes only with betting
    actions (`PLAYER_1` when player 1 bets, `BOTH` on raise or accept;
    src/mus_game.c:2560-2600). No card-derived value enters a key.
  - At 0:0, target 40, the public keys equal the v4 keys; the random-walk test checks this
    (tests/test_ordago_public.c:253).
- **Budget.** max(paid) + accepted pending (src/mus_game.c:1113-1117). Rejected stakes are
  excluded from the prior recomputation (1082). Menus from decoded information match the
  state menus (check_codec).
- **Accounting.**
  - Dejes are paid immediately.
  - Pending contracts and intrinsic points are paid at the final count, including the
    intrinsic points of rejected pares/juego/punto.
  - With target 0 there is no early stop (update_score_accounting 755-781, and
    compute_score_accounting 734-741).
  - Each total saturates at 40 (add_ordago_score, src/mus_game.c:602-608).
- **Accepted ordago.** update_score_accounting settles (40, 0) and clears pending. The new
  check at src/mus_game.c:874-879 stops advance_lance **before** the next lance's accounting
  can run. That premature accounting (prepare_final_lance -> update_score_accounting) was the
  source of the stale pending bits. The apply-level block (src/mus_game.c:2612-2630) then
  marks later lances SKIPPED and fixes (40, 0). Cached and recomputed accounting agree:
  compute_score_accounting returns at the ordago lance with the same earlier paid points.
- **Utility.** At terminal, settled[p] - settled[1-p] (the existing ordago branch in
  mus_trusted_terminal_utility). The test checks 40:1 -> +39, and an accepted ordago erasing
  an earlier deje.
- **Codec.** The public decode passes a limit of 41 to decode_score_betting_payload; target
  is used only for the `scores >= target` check (src/mus_game.c:3903).
  `score_enabled = (target != 0)`, so it is false for public keys (src/mus_game.c:3893).
  - The public encoder admits 0..40 and requires max == max(scores)
    (src/mus_game.c:4311-4317).
  - Error paths preserve the output; the test checks the sentinel in decode and encode.

## 3. Integration

- **Training.** supported_game accepts the public descriptor (src/mus_training.c:43-47).
  The shared MUS_ORDAGO_TRUSTED_OPERATIONS is safe: every model-only helper now reads the
  mode, and the context distinguishes the formats.
- **Evaluation.** The bounded diagnostic and from-state evaluators are parametrized by
  descriptor (src/mus_evaluation.c:634-778). State initialization is chosen by the
  descriptor.
- **Export.**
  - The writer selects the public decoder explicitly (src/mus_strategy.c:181-186).
  - Rows read `mano=pagados:X-Y cartas=<hand>`, mirroring the score row's
    `marcador:.../objetivo:... cartas=` (src/mus_strategy.c:203-222).
  - The field labels are `maximo_pagado` / `envites_pendientes`.
  - The header schema comes from the game.
- **CLI.** Every CLI_FORMAT_ORDAGO site also handles ORDAGO_PUBLIC:
  - option parsing, reservation, export, evaluation scope;
  - descriptor, state init, evaluate (app/mus_solver_cli.c:259-265, 438, 528-531, 573,
    1318-1335, 1415-1418);
  - `--threads-per-cell` stays score-only (test_cli.sh loop).
- **Adapter and preload.** They still use the legacy descriptor and schema, so they reject
  public data, as the README states. Proper integration is deferred.

## 4. Tests

- **Regressions.** Both audit reproductions (key split in v9, equal keys in public and v4
  at 0:0), and equal menus across rival hands.
- **Hidden-hand invariance.** Both roles, 0..MAX muses, four betting scenarios, every
  compatible rival hand. Observations group the model with hidden cards and accounting
  caches removed. There are more than 1000 comparisons, and every decision phase is seen.
- **Random legal walk (1000 samples).**
  - public vs trusted menus;
  - public vs trusted child models, validation of every child, undo restoring model and
    undo count;
  - codec round-trip and menu equality;
  - v4 key equality at 0:0.
- **Terminal utility by hand; validation.** Mode mismatch, target/initial rejection, and a
  decode/encode sentinel.
- **CLI.**
  - resume equals continuous (cmp of compressed checkpoints);
  - bounded evaluation, export fields, schema cross-rejection in both directions;
  - parallel training and resume.
- **Hot paths.** J15 phase and trusted/public key tests extended to the public descriptor.

## 5. Non-blocking findings

1. tests/test_cli.sh:597: the new ordago-public block comes after
   `echo "Todas las pruebas de CLI pasaron."`. The success message now prints before tests
   that can still fail. Move the echo to the end.
2. tests/test_ordago_public.c:179-185: the hidden-hand scenarios never raise or play an
   ordago. They only open with 2 or 10 and accept or reject. Response-mask and ordago keys
   are covered by the random walk (codec and v4 equality) but not by the invariance grouping.
   Add one scenario that raises once and one that rejects an ordago.
3. src/mus_game.c:1457-1471: public validation has no explicit invariant "an accepted ordago
   outcome implies TERMINAL with later lances SKIPPED". The rules cannot reach such a state.
   But a crafted non-terminal state with an accepted-ordago lance and matching (40, 0) caches
   would pass validate_state and could be continued. A one-line check would make the contract
   explicit.
4. The v9 lock is two golden keys at one pares state. The mode mapping makes legacy
   unchanged by construction. Still, a golden digest of legacy keys and menus along fixed
   random playouts, computed at 83c0a28, would guard all phases against future edits of the
   shared paths.
5. tests/test_ordago_public.c:114-129: note in the comment why outcome_strengths and
   current_strength are deliberately kept in the observation: they come from betting
   actions, so they are public.
6. docs/review-progress.md (J01): replace "Falta aprobación de implementación de Claude"
   once the adapter and preload parts are reviewed. This core approval does not close J01.
