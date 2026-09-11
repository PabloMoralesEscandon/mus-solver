# J23: final Claude review

Task: `task_48e1c218c721`. Dispatch: `ctx_e70d8b39a036`. Message: `msg_73b93f6abdc8`.

APPROVE J23 for commit; no corrections required. I reviewed read-only the final diff, the workflow, the J23 progress entry, J23.json and the strengthened seed test, recomputing every performance and counter claim from the stored runs. Everything agrees; the only step left is setting the J23 status row after this review.

EVIDENCE
1. Performance. 36 runs: three alternating pairs for each of six workloads, against c79329b. Recomputed medians:

   | Workload | J22 -> J23 | Time | Peak memory |
   |---|---|---:|---:|
   | Public training | 9.6838 -> 10.2908 s | +6.268 % | +0.001 % |
   | Score table | 3.8409 -> 3.9565 s | +3.010 % | -0.143 % |
   | Historical preload | 6.8810 -> 7.0723 s | +2.780 % | +0.003 % |
   | Public preload | 9.1973 -> 9.6254 s | +4.654 % | +0.010 % |
   | Historical TXT | 0.93338 -> 0.93524 s | +0.199 % | +2.315 % (largest) |
   | Public TXT | 1.28598 -> 1.28488 s | -0.086 % | +0.141 % |

   - Nothing triggered the gate, and nothing is blocked.
   - The table in the progress entry matches, and the build commands record O3 without LTO for both trees.
2. Determinism. In every workload there is a single identical_results_sha256 value across all six runs, covering checkpoint bytes, every cell's checkpoint and values.txt, or the full match result without elapsed time. For the matches, wins, hands, decisions and checksum each have one value.
3. Preload counters, recomputed from the stdout of each candidate repetition across the 45 score-preload lines:
   - historical: imported 934,055, uniform_after_filter_nodes 7,323;
   - public: imported 819,575, uniform_after_filter_nodes 7,332;
   - identical in all three repetitions, and equal to preload_diagnostic_totals.
   - The parent runs report the same imported_nodes and missing_source_nodes (2,292,431 and 2,439,702) but not the new field, which shows directly that the counter does not change import behaviour.
4. Seed test.
   - command is [bot, --load, checkpoint, --seed, 123], and the input is 64 copies of the base observation, whose policy is mixed (it includes raise probabilities).
   - The test requires that 999 then 123 gives output byte-identical to 123 alone, that command[:-1] plus 999 (seed 999 alone) gives different output, which proves the comparison is not vacuous, and that bad then 123 fails.
   - Validation records its final pass in normal, ASan/UBSan and TSan after the benchmarks.
5. Validation and provenance.
   - All six local combinations are listed with log hashes and their passing lines: one mus test with 75,900 hidden-hand comparisons; two-mus test-core with 141,358.
   - The three build-contract scripts and the default LTO build are listed as passing.
   - The notes record GCC 16.1.1 locally, the fact that hosted Ubuntu 24.04 / GCC 13 CI has not run, the two initial 30-second sanitizer timeouts before the targeted 180-second bound, and the post-processing done after sampling.
   - ci_publication states that nothing was executed remotely and that c837741 and 2fda65e must be published from the local review/mus-audit-fixes branch before the root branch. No push was done.
6. Scope. The C-CFR and Simulador-Mus trees are clean, the submodule commits recorded in the JSON match, and there is no algorithm or scheduler change. J13 remains rejected, J17 sync and J19 remain withdrawn. The workflow is unchanged since my follow-up approval.

OPTIONAL PRECISION
For public training, all three J23 runs (10.14 to 10.35 s) are slower than all three J22 runs (9.60 to 9.99 s), even though J23 does not touch that code path. The binary changed only through CLI and preload edits, so this is most likely a layout effect. The entry already records the increases without attributing a cause, which is appropriate; a clause saying the ranges do not overlap would make that explicit.
