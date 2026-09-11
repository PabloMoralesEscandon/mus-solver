# J21: final Claude review

Task: `task_4d6531987f25`. Dispatch: `ctx_9b8264a49050`. Message: `msg_916592c8e64f`.

APPROVE the final J21 evidence, with one required factual correction: the non-LTO batch checks 12 executables, not 13. I reviewed J21.json, the J21 progress entry, the README limitation paragraph, the new changed-source dry-run assertion and J21-claude.md read-only, and every other timing and hash claim recomputes exactly. No production code changed since my approval.

EVIDENCE
1. Timings. Three alternating pairs on CPU0. In every batch, the stored wall time equals the sum of its child invocations and the stored memory equals the largest child peak.

   | Mode | J20 -> J21 per batch | Change | Per invocation (median) |
   |---|---|---:|---:|
   | Non-LTO, 20 all+debug builds | 5.96870 -> 1.00839 s | -83.105 % | 0.292 -> 0.050 s |
   | Default LTO, 3 all builds | 20.01219 -> 0.09212 s | -99.540 % | 6.670 -> 0.031 s |

   - Peak memory falls too (17,088 -> 6,940 KiB and 107,396 -> 6,940 KiB), because the old Makefile runs the linker on every invocation.
   - Nothing triggered the gate, and nothing is blocked.
2. Protocol and provenance.
   - The parent Makefile (72a0356) is embedded, and both versions build in the same directory for each mode.
   - The parent relinks every executable on each invocation, so its batches do contain the forced links that J21 removes, and the candidate measures a true no-op.
   - After every batch the script asserts that the executable hashes equal the reference, and the stored hashes form one identical set across all runs in each mode.
   - The top-level binary_hashes shows that J21's release mus-solver, mus-bot and mus-match-eval are byte-identical to J20's.
   - The setup note correctly explains the failed first launch: make was given without an absolute path, posix_spawn does not search PATH, and no sample was collected. The script now resolves make with shutil.which.
3. README. It now states the limitation accurately: after a C-CFR-only change, make -n shows the recompilation but can omit the later links, and make -q returns an error from the recursive call. The J20 sentence was also softened to "dependencias de los objetos".
4. Test. The new dry run after the copied info_store.c edit asserts three things: src/info_store.c appears in the output, the compiler is never called, and the artifact snapshot is unchanged. It comes before the real rebuild, and pins down the documented behaviour.
5. J21-claude.md is a faithful copy of my review.

REQUIRED CORRECTION (factual, documentation only)
- The progress entry says «Los hashes de los trece ejecutables del lote sin LTO», and validation.generated_code in J21.json says «All 13 non-LTO benchmark executables».
- The runs record 12 hash entries per batch:
  - 3 in release: mus-solver, mus-bot, mus-match-eval;
  - 9 in debug: mus-solver, mus_solver_tests, mus-bot, mus-match-eval, mus_match_tests, mus_hotpaths_tests, mus_adapter_tests, mus_adapter_configuration_tests, mus_ordago_public_tests.
- The debug goal does not build mus_adapter_candidates_tests, and all+debug does not build the three benchmarks.
- Change both to twelve (doce / 12). Optionally, note that the timing batches cover 12 (non-LTO) or 3 (LTO) of the 16 consumers, while the integration test covers all 16.

BEFORE COMMIT
- validation.incremental_test in J21.json still says «rerun pending» for the added dry-run assertion.
- Once the active rerun passes, update it, and make sure the progress entry's «Pasaron la prueba de integración» refers to that final run.
