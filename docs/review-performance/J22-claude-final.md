# J22: final Claude review

Task: `task_35b07274b3de`. Dispatch: `ctx_a6038bee5198`. Message: `msg_20168e0f89f5`.

APPROVE the final J22 evidence. I reviewed read-only the final Makefile diff, the README, the J22 progress entry, J22.json, the completed tests/test_build_components.py and J22-claude.md, which reproduces my earlier review faithfully. Every figure recomputes exactly and the new test additions are correct; the only item left is updating the pending component-test note after the active rerun.

EVIDENCE
1. Performance. Three alternating pairs on CPU0, recomputed from the runs:

   | Workload | J21 -> J22 | Time | Game time | Peak memory |
   |---|---|---:|---:|---:|
   | Historical TXT, 1,000 matches | 0.922304 -> 0.913511 s | -0.953 % | -0.954 % | 2,856 -> 2,880 KiB (+0.840 %) |
   | Public TXT, 10,000 matches | 1.249458 -> 1.278671 s | +2.338 % | +2.339 % | 2,832 -> 2,856 KiB (+0.847 %) |

   - In both groups, wins, hands, decisions, checksum and the result hash each have a single value across all six runs.
   - Nothing triggered the gate, and nothing is blocked.
   - The protocol states O3 without LTO for both mus and C-CFR, and that no algorithm source changed.
2. Binaries. mus-solver and mus-bot have identical SHA-256 to J21. mus-match-eval differs (0b91… against ecd0…) because of link order, which is why only the match workloads were measured. That is consistent with the progress entry.
3. JSON receipt. mkdir, printf and cmp/mv now run as one + shell command joined with &&. The make environment, and so the lazy $(shell pkg-config …) expansions, is therefore built once rather than three times, and any failing step stops the chain. The other receipts keep parse-time := values, so their three separate lines cost nothing.
4. Test additions.
   - The stable-bot check replaces the stub with a logging pass-through, builds bot, clears the log, rebuilds, and requires exactly one --cflags json-c and one --libs json-c query. Nothing recompiles or relinks, so only the forced receipt expands.
     - This exact count also rules out nested queries through GNU make 4.4 passing exported variables to $(shell). Those would appear as duplicate lines, so the active rerun is the definitive check.
   - The custom cleanup check creates selected-root and C-CFR/selected-cache, runs clean-all with BUILD_DIR=selected-root and CFR_BUILD_DIR=selected-cache, asserts both are gone, and asserts the unrelated C-CFR/other-cache survives.
   - Neither cleanup run calls pkg-config.
5. README. It now tells users to run make clean-all after a C-CFR update that changes its source list, because C-CFR's own recipe updates libcfr.a in place. This addresses my earlier note. The rest of the J22 text is accurate.
6. Progress entry. It covers the component targets, lazy json-c with the single recipe, the eight-source archive with explicit simulator linking, temporary archive replacement, cleanup scope, -lm credited correctly to J10, the validation list, the byte-identity statement and the measurements. All of it matches the JSON and the code.

BEFORE COMMIT
- validation.components_test in J22.json still says the final rerun, with the query count and custom cleanup, is active. Update it once the rerun passes, so the progress entry's validation line refers to the final test.

OPTIONAL PRECISION
- For the public TXT workload, all three J22 runs (1.2677 to 1.2839 s) are slower than all three J21 runs (1.2463 to 1.2582 s). The +2.34 % is therefore a small but consistent link-order or layout effect in the match evaluator, not pure noise.
- This affects only that evaluation tool, not the solver or the bot. It is well below the gate, and the entry already keeps it as "variación desfavorable". A clause such as «consistente en los tres pares» would make that explicit.
