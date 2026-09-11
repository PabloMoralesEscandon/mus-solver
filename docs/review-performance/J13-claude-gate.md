# J13 performance-gate report review (Claude)

Verdict: **the arithmetic and provenance are correct; two precise documentation corrections are
needed** before these reports are committed. The report correctly separates changed play from
lookup cost.

This was read-only: I ran no builds, tests or benchmarks. I read these files:
- `ASTRA_BLOQUEO_RENDIMIENTO_J13_2026-09-11.md`;
- `docs/review-performance/J13-blocked.json`, `J13-blocked.patch` and `J13-claude.md`;
- the J13 row and section in `docs/review-progress.md`.

## Arithmetic: all correct
I recomputed every figure from the 46 stored runs.

**The public TXT confirmation (pairs 4–8).**

| Measure | Values | Median |
|---|---|---:|
| Old build, total time (s) | 1.3345, 2.4248, 1.3423, 1.3365, 1.3593 | 1.3423 |
| Candidate, total time (s) | 1.6917, 1.8412, 1.6281, 1.5928, 1.5923 | 1.6281 |
| Old build, game time (s) | | 1.34057 |
| Candidate, game time (s) | | 1.62624 |
| Old build, peak memory (KiB) | | 2,888 |
| Candidate, peak memory (KiB) | | 2,920 |

This gives **+21.285 %** total time, **+21.310 %** game time and **+1.108 %** memory. The first
three pairs give **+19.665 %**.

**Work counts:**
- hands: 19,094 → 20,777;
- decisions: 94,475 + 85,271 = 179,746 → 102,636 + 91,077 = 193,713;
- policy A: 19,385 → 29,509 fallback answers, 42,566 → 40,792 exact answers.

**Controls and training:**
- same TXT v1 on both builds: −0.881 % (public) and −0.548 % (historical);
- historical TXT v2: +11.608 %;
- public training: 9.4700 → 9.3421 s (−1.350 %);
- score table: 3.5475 → 3.6569 s (+3.085 %). The table really has 45 cells: 45 `.cfr.zst` files
  plus `values.txt`.
- The checkpoint and table hashes are identical across all six runs of each group.

## Provenance: sound
- The patch's SHA-256 equals the recorded `patch_sha256`, and `git apply --check` against
  `f723699` passes.
- The patch is the J13 diff I approved, plus the `test_cli.sh` checks I asked for (the
  `available=` flag and the end-of-file marker on the single-bet and raises exports) and the
  new availability test.
- The code is restored: only `docs/review-progress.md` is modified, with no source changes.
- The first-run script is embedded unchanged. The confirmation script reuses its exact `call`
  and `summary` functions (via `exec` of the stored text) and records itself separately. This
  is good practice.
- `J13-blocked.json` equals the confirmation output plus two added fields, `decision` and
  `patch_sha256`. Every other field is identical. Optional: note that those two fields were
  added by hand.
- Candidate checks agree: the v1 controls give identical result hashes on both builds.
- **Strong supporting evidence:** the historical TXT v2 candidate run reproduces the J12
  raw-binary result exactly (hands 2,028, wins 542/458, checksum 15805583173038379323).
  - So v2 changes TXT play to the binary backend's play, which is what J13 intended.
  - Citing this would show that the extra time comes from playing binary-equivalent
    decisions, not from a defect.

## Changed play versus lookup cost: correctly distinguished
The report correctly avoids calling this a CFR or training slowdown, or per-query latency. It
also says the joint measurement doesn't separate the causes. A useful, optional
quantification:
- decisions rise 7.77 % and hands rise 8.81 %;
- game time per decision rises **+12.56 %** (7.458 → 8.395 µs), mostly because the decision mix
  changes;
- policy A's fallback answers rise 52 %, and a fallback lookup keeps trying candidate nodes
  before giving up.

So about 1.078 × 1.126 ≈ 1.213: roughly a third of the increase is extra play, and two thirds
is costlier decisions under a different mix. This is still not an equal-work per-query cost.

## Required documentation corrections
**1. Say that the gate result is borderline and depends on the estimator.** The report states
the first-stage result and the confirmation, but not that:
- **The stated rule was not triggered.** The method says five extra pairs are run "if median
  wall time >=20%". The first stage gave 19.665 %, so by that rule the candidate passed
  (`summary` returned trigger=false). The confirmation was a discretionary extra measurement.
  This is disclosed in `confirmation_reason` in the JSON, but not in the report.
- **Other reasonable estimators land on both sides of 20 %:**

  | Estimator | Result |
  |---|---:|
  | All 8 pairs, ratio of medians | +20.03 % |
  | Median of per-pair ratios, all 8 | +19.32 % |
  | Median of per-pair ratios, confirmation only | +19.18 % |
  | Per-pair values | +22.1, +17.9, +19.5, +26.8, −24.1, +21.3, +19.2, +17.1 % |

Suggested sentence for the report and the progress entry:
> La regla previa (tres pares, ≥20 %) no se activó con +19,67 %; los cinco pares
> adicionales fueron una confirmación discrecional por proximidad al límite. El efecto está
> en el entorno del 20 %: la mediana de la confirmación da +21,29 %, las ocho parejas juntas
> +20,03 % y la mediana de las razones por pareja +19,3 %.

The user's decision should rest on "about 20 %, at the threshold", not on a clear 21 % breach.

**2. Mention the disturbance in pair 5.** The old-build run took 2.4248 s (+80 % over its
typical time), and the candidate in the same pair took 1.8412 s (+13 %).
- The medians are robust to it: both values are the maximum of their side.
- It is the reason the pair ratio is −24.1 %, and it shows that noise was present during the
  confirmation.
- One clause is enough, e.g. "la pareja 5 sufrió una perturbación (2,42 s en el ejecutable
  anterior); las medianas no dependen de ella."

## Other claims checked: accurate
- **"Export timing pending, not presented as done":** correct. No export group exists in the
  JSON.
- **"No altera C-CFR ni el simulador ni el esquema de checkpoints":** consistent with the patch
  stat.
- **Links:** they resolve from both locations (the repository root and `docs/`).
- **Scope of the +11.61 % historical result:** "three pairs, 1,000 matches" is correct, and the
  "not a universal penalty" disclaimer is appropriate.
