# J18 review (Claude)

Verdict: **APPROVE**. No blockers.

This was read-only: I ran no builds, tests or benchmarks. I reviewed the working-tree diff
against HEAD `06a671d`:
- `src/mus_game.c` and `src/mus_adapter.c`;
- the new `tests/test_adapter_candidates.c` and its Makefile integration;
- `docs/review-progress.md` and the J13 blocking report (which now records the user's decision).

## J13 absent: confirmed
- There is no `available=`, `mus-strategy-end`, `version=2` or `has_availability` anywhere
  in `src`, `include`, `tests` or the Makefile.
- `tests/test_text_availability.py` does not exist.
- `test_adapter_cli.py` still requires a v2 header to be rejected.
- The progress row and the blocking report record "No se aplica", and the saved patch and
  measurements are kept.

## Direct observed-hand encoding (`observed_ordago_hand_code`)
I checked each branch against `decode_ordago_hand` (`mus_game.c:3820`) and
`ordago_observed_hand_equal` (`:4290`). The card classes are PITOS=0, FOURS=1 … KINGS=7.

| Hand kind | Formula | Inverse of decode? |
|---|---|---|
| Exact | `compact_hand_code` checks the hand totals 4 cards (so it cannot go out of bounds); otherwise −1 | Yes: a bijection with `compact_hand_from_code` |
| Grande | 28 when there are 4 kings; otherwise `kings*7 + highest`, with highest in 0..6 | Yes: decode gives `code/7`, `code%7` |
| Chica | 28 when there are 4 pitos; otherwise `pitos*7 + lowest − 1`, with lowest in 1..7 | Yes: decode adds FOURS=1 |
| Pares | pair = `high`; medias = `8+high`; duples = `16 + high(high+1)/2 + low`, with low ≤ high | Yes: decode's triangular walk uses the same offsets; the maximum is 51, below the limit of 52 |
| Juego | `game_key` (index into `GAME_VALUES`, or −1) | Yes |
| Punto | `point_value − 4` | Yes: decode gives `code + 4` over a range of 27 |

**Key values are preserved.**
- Every formula reads only fields that the equality check compares for that branch:
  - Grande and chica ignore `highest`/`lowest` when all four cards are kings (or pitos);
  - pares ignores the second pair value for pair and medias.
- So if some code *c* decodes to a hand equal to the input, the formula returns exactly *c*.
- Every observed hand kind is injective, so the old loop's first match was that same unique
  *c*.
- `decoded` starts from the same state the old loop copied (`candidate = decoded`), so
  `has_second_pairs_value` compares the same way. After the hand check, `decoded` is
  overwritten by the full-key decode (`:4490`), so mutating it has no later effect.

**Rejected inputs are preserved.** The new code accepts an input only if the formula's code
decodes and compares equal. That is precisely when the old loop would have found a match.
- −1 and out-of-range codes are rejected by the range check `code < 0 || code >= radix`
  before any table access (`GAME_VALUES[code]` is safe).
- Bad counts, classes, pair types, duples with low > high, and points outside 4..30 are all
  rejected, as before.

**Ignored and stale fields are unchanged.** The existing `test_encoder` round-trips all
330 hands in all 9 phases, and checks stale `hand`/`highest`/`lowest`/`second` values for the
historical, public and score encoders. Those three encoders all go through
`encode_ordago_information_set` (`:4505-4517`), and no other hand-radix search remains.

## Manhattan-ring candidate enumeration (`nearest_candidates`)
- **The public and score path is untouched.** It still keeps scores and pending totals fixed,
  and returns early when the pending total is above 40.
- **Distance.** `prior_wager_total` is validated as non-negative (`mus_adapter.c:460`), and
  `max_settled_score` is an unsigned byte. So the centre is the clamped projection onto the
  grid [0, upper]², and for every grid point the distance equals `minimum_distance` plus the
  Manhattan distance to the centre: above the bound, |x − p| = (x − upper) + (upper − p).
- **The rings enumerate each grid point exactly once.**
  - The prior range is clamped to [0, upper].
  - The score is centre ± delta, and only one side is used when delta = 0.
  - Radii up to 2·upper cover the whole grid.
  - The `active` bound and the encoding-failure skips are the same as before.
  - With `upper = 0` there is a single point, as before.
- **The stopping rule is correct.** Ring distances only increase, and the worst candidate
  can only improve.
  - The loop breaks only when the list is full and the ring distance is strictly worse, so
    equal-distance rings are always finished.
  - Ties are then decided by mismatch, numeric wager count and key, as before.
  - `break` leaves only the radius loop; templates are still visited in the same order.
- **Order does not depend on enumeration order.**
  - `compare_candidates` ends with the key, and the historical key encodes the prior and the
    score, so there are no equal duplicates within a template.
  - `insert_candidate` keeps the 64 smallest under that total order (anything tying with the
    worst is dropped), whatever order they arrive in.
  - Templates are processed in the same order as before.
  - `encode_information` has no side effects.
- **Overflow and headers.** The four distances are each at most `MAX_AMOUNT` = INT64_MAX/8, so
  their sum cannot overflow (as before). `abs` comes from the already included `<stdlib.h>`.

## Test and Makefile
- `test_adapter_candidates.c` includes the private `mus_adapter.c` and compares the full
  shortlist entry by entry against an exhaustive copy of the old grid, without the pruning
  step. It covers:
  - 3 formats × 5 lances × 90 samples = 1,350 cases;
  - response templates, including the `active` constraint;
  - prior totals at 0, 1, 19, 20, 38, 39, 40, 41 and `MAX_AMOUNT`;
  - settled scores 0..40;
  - `MAX_AMOUNT` totals;
  - the early return for pending totals above 40;
  - many ties at the 64-candidate boundary.
- **Linking is safe.** The archive's `mus_adapter.o` is never pulled in, because the test
  object defines all of its symbols, and every static function in the included file is used
  there, so `-Werror` stays clean.
- The new target is in `test`, so it also runs under `test-sanitize` and `test-tsan`.

## Nits (non-blocking)
1. **A direct test of the hand formula.**
   - Loop over every phase and every code *c* in range, and assert
     `observed_ordago_hand_code(decode(c)) == c`.
   - Add a few invalid inputs: 5 kings; highest 7 with fewer than 4 kings; duples with
     low > high; point values 3 and 31; a juego value outside the table.
   - The existing round-trip covers this indirectly. A direct test would pin the inverse if
     the layout of any hand kind ever changes.
2. **Drifting reference.** `reference_candidates` duplicates the old loop inside the test.
   A one-line comment saying it is the pre-J18 algorithm, kept as the oracle, would stop it
   being "optimized" along with the real code later.
3. **Progress entry.** The J18 section in `docs/review-progress.md` is still pending
   (measurements are in progress).
