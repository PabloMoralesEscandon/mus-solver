# J18 final evidence and documentation review (Claude)

Verdict: **APPROVE**. Two small documentation precisions are suggested (non-blocking).

This was read-only: I ran no builds, tests or benchmarks. I checked:
- `docs/review-performance/J18.json`;
- the J18 section and row in `docs/review-progress.md`;
- the J13 blocking report's user-decision update;
- the test-only follow-ups to my implementation approval (`task_0bb2fdb88071`).

## J13 is still absent
- `src`, `include`, `tests` and the Makefile contain no `available=`, `mus-strategy-end` or
  `has_availability`.
- `tests/test_text_availability.py` does not exist.
- The J13 report now records «No se aplica. Siguiente»; its "Quedan…" paragraph is replaced
  and it links to the progress log.
- The J13 row says "No aplicado por decisión del usuario", and the J18 text repeats
  "No se ha aplicado J13".

## Follow-ups to my earlier nits: done
- **Oracle comment.** `test_adapter_candidates.c` now labels `reference_candidates` as the
  "Frozen pre-J18 enumeration … independent oracle".
- **Invalid inputs.** Seven explicit cases, each checked against the historical, public and
  score encoders, and each asserting that the output key stays 42 after an error.
  Every case hits the rejection it is meant to:
  - grande: highest card = kings with 3 kings;
  - chica: lowest card = pitos with 3 pitos;
  - pares: duples with low > high, where the base hand of four kings gives duples, so this
    is a real low > high case;
  - punto: 3, and 31 (out of range);
  - juego: value 38 (not in the table);
  - mus: a hand of all-255 counts.

  `info_base` gives consistent public scores (0/0), so the public and score encoders also
  reject because of the hand, not some other field.
- **A direct per-code inverse test** was not added. The codec differential below is stronger
  evidence, although it lives outside the test suite.

## Medians and gate outcome: all recomputed and correct

**End-to-end runs, three alternating pairs each:**

| Load | Parent | J18 | Change |
|---|---:|---:|---:|
| Public training | 9.6052 s | 9.8536 s | +2.586 % |
| Score table | 3.7336 s | 3.7344 s | +0.020 % |
| Historical TXT matches | 1.0266 s | 0.9280 s | −9.598 % |
| Public TXT matches | 1.4050 s | 1.2873 s | −8.375 % |

- **Internal game time:** −9.610 % and −8.418 %.
- **Peak memory:** +0.002 %, +0.175 %, +4.125 % and +1.286 %.
- **Identical results:** in each group, all six runs share one hash (checkpoint, 45-cell
  table, or full match JSON), and hands, decisions and checksums are equal.

**Query benchmark (76 cases):**
- I recomputed every per-case median, and each equals the stored summary.
- Iterations follow the parent-calibrated plan (3,596 to 1,000,000) in all six runs.
- Checksums are identical across both versions and all repeats, so each case does fixed,
  equal work.
- The range is −91.92 % to +12.42 %; no case reaches 20 %.
- Query-harness memory: +5.85 %.
- `blocked` is null, and nothing triggered a confirmation. The documentation's statement
  "Ninguna carga activa el límite" is correct.

**Build parity.** Neither the parent (`build/review-j12`) nor the candidate
(`build/review-j18`) `libmus_solver.a` contains `.gnu.lto` sections, and both
`mus-match-eval` binaries are 272,832 bytes. So both sides are non-LTO release builds with
matching flags, and the comparison is not confounded by build settings.
- The query harness compiles the parent's `mus_adapter.c` (`git show 06a671d`) against the
  parent library, and the working-tree file against the J18 library.
- Both use the same fixture helpers and the same `libcfr` build.

## Profile attribution and its limits
- The profiles store, per case, the generation, encoding and lookup times and call counts.
  The script asserts the same keys, checksums and lookup counts for parent and candidate.
- **Claim confirmed:** for historical *absent* chica, pares, juego and punto, both memory
  and TXT go from 149.0 to 67.0 encodings per decision, with 64.0 lookups in both.
  - Grande stays at 2, because it has no earlier lances and so no grid.
  - The public variant stays at 2, because it never searches the grid.
  - Exact and interpolated routes are unchanged (1 and 2).
- The uninstrumented pares/absent example is correct: −91.92 % with memory, −89.90 % with TXT.
  The timing binaries record zero instrumentation counts, which confirms they are
  uninstrumented.
- The protocol and the documentation both say that `lookup_seconds` includes validating
  actions and probabilities, that clock instrumentation adds cost, and that the profile
  attributes work but is not a measure of speed-up. That wording is appropriate.

## Codec differential
- Parent and candidate results (`/tmp/mus-j18-codec-{parent,candidate}.txt`) are identical:
  `calls=1113900 valid=988980 hash=1928429011480121827 failures=0`.
- The count checks out: 1,580 valid (hand, phase) pairs × 3 formats × (1 + 18 values × 13
  fields) = 1,113,900.
  - The 400 excluded pairs are the 70 hands without pairs in pares, plus exactly one of
    juego or punto for each of the 330 hands.
- The perturbed values cover the extremes of `int` and `uint8`, negatives, and the boundaries
  (3, 4, 7, 8, 30–33, 37, 38, 40, 41, 255).

## Documentation precisions (suggested, non-blocking)
1. **Say how many query cases worsened, and where.**
   - 26 of the 76 cases got slower, by up to +12.42 %: 12 exact, 10 absent and 4
     interpolated.
   - These are all cheap paths where J18 removes no work: encoding and lookup counts are
     identical there (1 per decision; no grid for public, grande, mus or discard).
   - In several of them the candidate's repeats are consistently above the parent's; for
     example, historical TXT punto exact goes from 0.086–0.088 s to 0.091–0.101 s. That is
     most likely code layout or noise, not algorithmic.
   - Suggested clause: «26 de 76 casos empeoran (máx. +12,42 %), en rutas baratas sin
     búsqueda donde J18 no cambia el número de codificaciones ni lecturas; ninguno llega al
     límite.»
2. **Provenance details for the JSON.**
   - Embed the parent codec-differential result next to the candidate's, since the JSON
     claims they are identical but stores only the candidate's.
   - Optionally, state in the documentation that both sides are release builds without LTO.
     The build command sets `RELEASE_LTO_FLAGS=`, while the Makefile default is `-flto`.

The +2.59 % public-training difference is, as the documentation says, not evidence of a
per-node cost change. The ranges nearly overlap (9.47–9.63 s against 9.56–9.98 s), and J18
does not touch the training path.
