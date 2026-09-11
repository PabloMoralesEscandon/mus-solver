# J12 deployed-policy match evaluation review (Claude)

Verdict: **APPROVE** (bounded, non-blocking nits). Reviewed:
- the tracked diff against d80d01b (Makefile, README);
- the new app/match_evaluation.[ch], app/mus_match_eval_cli.c,
  tests/test_match_evaluation.{c,py} and docs/match-evaluation.md;
- the Simulador-Mus change (one explicit `(uint64_t)` cast in `combinaciones`, needed for
  root's strict flags; no behaviour change).

The training and library code is unchanged. Results so far: the host checks pass, the CLI
checks pass (initial and final), and the solver, hot-path and public suites pass; final
full, sanitizer and performance runs are with the coordinator.

## The J12 action is met
The audit asks for a match evaluation that uses the same selector, translation and fallback
as the bot, and records coverage by decision type, phase and score, on top of the J02
simulator. Every decision goes through `mus_adapter_decide` (or the declared baseline), on
top of the real `simularRondaMusConEstrategias` with J02's deferred counting. Coverage is
recorded by phase, starting score and action, broken down by method, and by route
(historical generic, public generic, trained cell, baseline). The CSV trace keeps these
jointly, without cards.

## Correctness checks
- **Orientation.**
  - `relative_player` maps the absolute seat to 0 = mano, 1 = postre. Scores are oriented
    the same way per hand.
  - Policy A is seat 0 and B is seat 1, and `game.mano = match % 2` alternates the first
    mano (the CLI test checks the first trace row per match).
  - The simulator's winner (team 1 or 2) maps to the seat.
- **Public observation** (`observation`, `decide_wager`):
  - its own cards, from `valorMus` class counts;
  - `hand_start_scores` = the scores at the start of the hand;
  - `match_scores` = the callback's public scores (dejes only, per J02);
  - `known_hand_scores` = the dejes paid this hand;
  - `pending_wager_total` = accepted numeric totals;
  - `prior_wager_total` = accepted totals plus rejected stakes (`cantidadAnterior`), which
    is the legacy key's semantics;
  - `hand_scores_complete` drops once a lance was passed or accepted, pares were rejected,
    or pares were skipped with a player holding pairs;
  - the history is the current lance only.

  No showdown winner or hidden intrinsic points reach a query. Skipped-pares strength
  comes from `tipoPares` of both hands, which is the public pares declaration
  (PARES_SKIPPED_NONE / PLAYER_0 / PLAYER_1).
- **Strengths.** `active_strength` sets bit `1 << player` on a raise, accept or ordago:
  opener only gives PLAYER_x, raise or accept gives BOTH, and all passes give NONE. This
  matches the enum values and `current_strength` in mus_game.c. `finish_previous_lances`
  closes negotiated lances from `after_action`, and handles skipped pares. Grande and chica
  are always negotiated with two players, and a skipped final lance needs no closing.
- **Legal menu.**
  - At pass: PASS. Facing a bet: REJECT and ACCEPT. Unless an ordago is pending: ORDAGO
    and RAISE for every total from 2 to 40 above the current one.
  - Each candidate is validated with `aplicarAccionEnviteMus` on a copy, so the menu is
    exactly what the engine accepts (maximum 42 entries, capacity 44).
  - A RAISE amount is an increment, as in the adapter protocol. `engine_action` converts it
    to a total.
  - The chosen action is re-validated before it is returned.
- **Mus and discards.**
  - The mus decision offers MUS and NO_MUS; NO_MUS records `mus_stop_player`.
  - A second MUS after one mus is rejected. The adapter answers the second round with
    MUS_LIMIT, which the `three` CLI runs exercise.
  - Discards offer only canonical non-empty masks
    (`mus_ordago_game_discard_mask_is_canonical`). The canonical bit order (class ascending,
    copies in slot order) is mapped back to physical slots; equal-class cards are
    interchangeable.
  - `discard_counts` and `mus_count` are updated in engine order: mano, then postre.
- **Two muses:** `MUS_GAME_MAX_MUSES != 1` returns INCOMPATIBLE, and adapter creation
  already rejects it.
- **Failure semantics.**
  - Callbacks record `hand->status` and return the engine's error value (−1, a nonzero
    discard result, or an invalid action), so the round returns −1 and the first status is
    propagated.
  - An illegal or out-of-range decision gives INVALID_ARGUMENT.
  - Reaching `max_hands` gives NOT_FOUND with no win rate.
  - The result is written only on success (tested byte for byte). The log level is restored
    (tested). The trace may be partial, as documented. Overflow of the counters is guarded.
- **Reproducibility.** Only the simulator uses `rand()`, seeded once. Each adapter has its
  own stream derived from the seed. Reruns are identical (tested with and without a trace),
  raw and zstd give identical results (tested), and the documentation promises no
  cross-platform identity or card coupling between different policies.
- **Routing.** The adapter's route flags feed `strategy_counts`. Tested:
  - historical and public generic;
  - a 39:39 trained hybrid gives 100 % score-cell decisions;
  - 38:39 gives 100 % public generic;
  - two hybrid adapters.

## Tests
- **Host** (test_match_evaluation.c):
  - scripted policies assert observation invariants for the scripts PASS, TWENTY, DEJE,
    DEJE_ORDAGO, MUS and ACCEPT_ORDAGO: pending totals 10 and 20, strength BOTH, deje
    scores 2 and 4, discard counts, cards kept after the redraw, one-hand ordago matches;
  - the counts across dimensions add up;
  - **the PASS run equals the simulator's own passive strategy exactly** (wins, hands,
    checksum), which proves the host neither perturbs the engine nor its `rand()` stream;
  - an invalid action and the max-hands limit both preserve the result.
- **CLI** (test_match_evaluation.py): raw, zstd and TXT for both generic formats; opponent
  `three` (including the off-abstraction raise of 3); the trace (`wx`, cannot truncate an
  input); hybrid routing; the strict grammar; a foreign checkpoint rejected; input files
  unchanged.

## Non-blocking nits
1. **No direct test** of the legacy-only observation fields (`prior_wager_total` after
   rejections, `hand_scores_complete` transitions, the skipped-pares strength values). They
   are validated only indirectly, by the adapter accepting the observations. One scripted
   case with a rejected grande and skipped pares, asserting these fields, would lock the
   legacy mapping.
2. **Statistical reporting.** `win_rate_a` is a raw frequency. Adding its binomial standard
   error (sqrt(p(1−p)/n)) to the JSON would discourage over-reading small runs; the
   documentation already warns about this.
3. **Paired comparisons (optional).** Comparing two policies uses independent card
   streams. A mode that replays each deal with the seats swapped would sharply reduce
   variance for A/B comparisons.
4. **Process.** Commit the Simulador-Mus cast in the submodule first, then the root
   gitlink, as in J02.
