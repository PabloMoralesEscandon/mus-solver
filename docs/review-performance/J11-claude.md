# J11 bounded-diagnostic scope review (Claude)

Verdict: **APPROVE** (non-blocking notes below). Diff reviewed against d293c57: README.md,
app/mus_solver_cli.c and include/mus_solver/mus_evaluation.h only. Numerical evaluation and
the rules are unchanged; validation, binary comparisons and timing belong to the
coordinator.

## Wording against the code (src/mus_evaluation.c)
`ordago_diagnostic_metrics`:
1. builds two states with `prepare_ordago_final_lance_diagnostic`, which applies seven
   card-class counts for **each** player (both hands fixed), then NO_MUS, then pass/pass in
   grande and chica. It requires the phase to be PUNTO for the "point" deal and JUEGO for
   the "game" deal;
2. evaluates each state separately with `ordago_evaluation_from_state`, the generic C-CFR
   evaluator from that subgame root, so each best response is computed within one deal;
3. averages the eight metrics with `average_evaluation_metrics`.

Every claim therefore holds:
- **`deals=2`:** exactly two fixed deals.
- **`opponent_cards=fixed`:** both hands are fixed, so the responder never faces uncertainty
  about the opponent's cards. The README says "las cartas de ambos jugadores".
- **`best_responses=per-deal`:** best responses are optimised independently in each subgame
  and then averaged. The README says "por separado en juego y punto; después se promedian".
- **`root_bound=none`:** the README and header disclaim root exploitability and any proven
  upper or lower bound, which is exactly the J11 scope correction.
- **Header comments** (mus_evaluation.h): they state the same facts for `ordago` and
  `ordago-public`, and describe the correct extension: weight compatible opponent hands and
  accumulate at shared information sets before choosing the best-response action, because
  averaging independent best responses still responds to each hidden hand.
- **README:** `mus_ordago_evaluation_metrics_from_state` and its public equivalent evaluate
  whatever root they are given ("el alcance depende del estado inicial"). This is correct
  and matters, because those APIs are not limited to fixed hands.

## Output compatibility
- **Changed line:** only `--format ordago|ordago-public --evaluate` output changes. Four
  `key=value` fields are appended after `exploitability` on the same line; the root formats
  get an empty suffix, so their output is byte-identical.
- **Existing consumers still match:** `test_cli.sh:198`
  (`evaluation scope=bounded-final-lances iterations=2`) and `:607`
  (`scope=bounded-final-lances`).

## Does this fulfil J11?
The joint audit's J11 "Acción" asks for three things, all done:
1. keep `bounded-final-lances` — done;
2. explain which uncertainty was removed (both hands fixed, per-deal best responses,
   averaging), and not claim a single deal or a proven bound — done;
3. describe the correct way to broaden it (weighted compatible hands, aggregated at shared
   information sets before maximising) — documented, not implemented.

The audit frames the third point as guidance for any future extension, not as a
requirement for this item, so documenting it is appropriate.

## Non-blocking
1. **Pin the contract in tests.** Add `grep -q 'root_bound=none'` (or the whole suffix) to
   the ordago and ordago-public evaluation checks in tests/test_cli.sh, so the scope fields
   cannot silently disappear.
2. **Fixed public history.** Each deal also fixes the public prefix (no mus; grande and
   chica passed by both players). "Último lance" implies it, but a short clause in the
   header comment or README ("con historial previo fijo de pases") would make the scope
   fully explicit.
3. **Parsers.** Any external parser that treats `exploitability` as the final field of the
   line must now read `key=value` tokens. Worth one sentence in the release notes; all
   in-repo consumers match substrings.
4. **Cosmetic.** In README the inline code span for the field list breaks across a line.
   CommonMark renders the newline as a space, which is fine, but keeping it on one line would
   be tidier.
