# J01 adapter integration review (Claude)

Verdict: **APPROVE** (non-blocking notes below). Diff reviewed against f62ad27. Core CFR and
the mus rules are unchanged; the diff touches only the adapter, the bot CLI, the docs, the
tests and one Makefile line.

## Loading and schema identification
- `mus_adapter_load` (src/mus_adapter.c:190-240): reads with the legacy descriptor first. Only
  on INCOMPATIBLE_GAME does it `fseek` to 0 and read again with the public state and
  descriptor, for both raw and zstd files.
  - cfr_checkpoint_read preserves `owned_store` and the trainer on error, so the retry starts
    clean.
  - `public_generic` is set only when the public read succeeds.
  - A truncated or trailing-byte public checkpoint fails on the public read and exits 1; the
    JSON test covers both cases.
  - Any other legacy error (FORMAT, IO) is returned without a retry.
- `mus_adapter_open_text` (lines 244-266): falls back to
  `mus_text_strategy_open_schema(path, MUS_ORDAGO_PUBLIC_STRATEGY_SCHEMA_ID, 0, …)` only on
  INCOMPATIBLE_GAME. The J04 reader returns that status only for a schema mismatch, so a
  header-only or wrong-version public TXT file still fails (tested). The output pointer stays
  NULL on failure.
- `mus_adapter_create_public` wraps `mus_adapter_create` and sets the flag only on success.
  With MAX_MUSES = 2 it returns INCOMPATIBLE_GAME and leaves the sentinel output unchanged
  (tests/test_adapter_configuration.c). The historical `mus_adapter_create` keeps the legacy
  schema, and its doc explains that a bare store cannot identify its format.

## Routing (select_strategy, lines 350-398)
- **Legacy generic without a table:** it returns immediately, as before.
- **Legacy generic with a table:** an untrained cell returns with `untrained_start_score` and
  does not require the pending total, as before.
- **Trained cell (precedence):** same as before — score encoding with absolute
  `match_scores`, `strategy_scores` set, `score_strategy` true. The only change is that
  `strategy_reason` is now set before the score TXT opens; on an error the decision is
  discarded anyway.
- **Public generic (no table, or an untrained cell):**
  - requires `hand_start_scores`; the pending total is required in betting phases and must
    be 0 in mus, discard and grande;
  - the start-score checks (≥ 0, no more than the current score, equal to it in
    mus/discard/grande; lines 357-362) guarantee paid = match − start ≥ 0;
  - it sets score_enabled = false, scores = paid, max = max(paid), and
    prior = min(pending, 40);
  - `public_strategy` true, `score_strategy` false; the diagnostic strategy is
    "ordago-public" and strategy_scores is null.
- **Score TXT cache:** it opens or switches only for trained cells. Routing to public
  generic leaves the cached cell untouched. The JSON test covers the switches
  public → score (30,30) → score (30,39) → public, in betting and mus phases.

## Lookup
- `encode_information` chooses the score, public or legacy encoder from the flags.
- **Exact lookup:** it is attempted only when prior_total ≤ 40. For score and public,
  prior_total is `pending_wager_total`; legacy keeps `prior_wager_total`.
- **Nearest lookup** (lines 724-756): for score and public, payments and pending stay fixed,
  only the current betting sequence is approximated, and pending > 40 yields no candidates.
  The legacy branch still varies prior/score.
- **Interpolation:** uses the flag-aware encoder.
- **`score_proxy`:** false for public and for score.
- **Score heuristic** (line 965, `score_adjustment && betting && !d->score_strategy`): still
  applies to public generic, as documented, and trained cells still skip it.
- **Errors:** `decide_impl` (lines 791-812 onwards) returns on every new validation error
  before the only random draw (line 997). The C test checks that the decision stays
  byte-identical after an invalid public observation. The JSON test checks, for each
  backend, that a valid query after invalid ones gives the same result, so the random stream
  is preserved.

## Historical compatibility
With `public_generic` false, every changed expression reduces to its old form: the encoder
choice, the early return in select_strategy, prior_total, score_proxy and the nearest-lookup
branch. The existing adapter, adapter-CLI and hybrid suites pass
(/tmp/mus-review-j01-adapter-tests.log).

## Tests
- **C (test_public_adapter):** both players; every betting, mus and discard phase exact,
  with probabilities checked; source key equals the public encoding; two adapters with the
  same seed agree; missing start scores rejected with the decision preserved; interpolated
  and nearest responses; fallback, with no sources, when either paid points or pending
  differ; missing pending rejected.
- **JSON (test_public_adapter_cli.py):**
  - identical results across raw, zstd and TXT;
  - fallback for other paid points, other pending, or pending 41;
  - interpolated and nearest results, the same across backends;
  - an ordago response and `mus_limit`;
  - six invalid observations, with the random stream preserved on every backend;
  - hybrid routing both ways;
  - corrupt TXT and checkpoint rejected with exit 1 and empty stdout;
  - source files unchanged (sha256).
- The full debug suite passes, including the new JSON test.

## Non-blocking
1. Both new tests turn off score adjustment (`--no-score-adjustment`,
   `config.score_adjustment = false`), so the retained heuristic is never exercised with a
   public generic. One case with adjustment on (the expected `ordago_factor`, and 1 for a
   trained cell) would pin it.
2. A public generic without a table reports `strategy_reason = score_table_disabled`. That is
   accurate but uninformative. A distinct reason (e.g. `public_generic`) would help, and the
   docs' list of reasons would need updating.
3. If a public store is passed to the historical `mus_adapter_create`, every lookup misses
   and the adapter falls back silently. A bare store cannot be identified, so the doc note is
   the right mitigation. A caller-side assertion in the integration guide could help.
4. When the retried load or open fails, the reported status comes from the public attempt,
   which is still INCOMPATIBLE_GAME for unrelated schemas. The load message could say that
   both schemas were tried.
