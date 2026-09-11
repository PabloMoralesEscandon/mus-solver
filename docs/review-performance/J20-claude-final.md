# J20: final Claude review

Task: `task_84989db627c5`. Dispatch: `ctx_9d590849f2e7`. Message: `msg_094625db13fa`.

APPROVE the final J20. I reviewed read-only the + receipt recipes in make/build-config.mk and the Makefile JSON rule, the expanded tests/test_build_config.py, the README, J20.json, the J20 progress entry and J20-claude.md, which reproduces my earlier review faithfully. The recommended fix is correctly applied and tested, and every performance figure recomputes exactly; one documentation update remains once the expanded test finishes.

EVIDENCE
1. Metadata recipes. All three receipt lines now start with + in both the shared rule and the JSON rule. GNU make runs + lines even under -n, -q and -t, and then re-reads the target timestamp instead of assuming it was updated. So an unchanged configuration leaves every receipt timestamp alone, and the dry run shows no compiles; a changed configuration rewrites only the receipt, so the dry run correctly lists the pending compiles without building anything.
   - The @ still suppresses the echo, so dry-run output stays clean.
   - The C-CFR sub-make is a recursive line, so it runs under -n and applies the same logic.
   - In normal builds, + changes nothing apart from passing the jobserver, which is harmless for printf and cmp. The earlier measurements and the byte-identical claim therefore remain valid.
2. Tests.
   - An unchanged dry run prints no compile command and leaves the timestamps of every object, archive and receipt identical.
   - A dry run with MUS_MAX_MUSES=2 prints the compiles but does not touch mus_game.o. The following real build recompiles it, both test binaries run, and the default configuration is then restored.
   - Changing only AR back while keeping the CC wrapper rebuilds the root archive but not the root objects, which confirms the archive receipt is separate.
   - LTO switched on and then off without changing directory adds and then removes the .gnu.lto_ sections in both mccfr.o and the root mus_game.o. That directly covers the stale-LTO reuse found in the historical baseline.
   - The earlier one-to-two-to-one, CFLAGS, CC and AR, bot-isolation and stable-rebuild checks remain.
3. README. It says correctly that make -n and make -q check, and if needed update, the receipts without compiling. It also covers the limits I raised: a compiler replaced under the same name, or external variables such as CPATH, need a new directory or a clean build.
4. J20.json. I recomputed all four groups from the stored runs (three alternating pairs on CPU0), and every group has a single set of result hashes (checkpoints, table, match results):

   | Workload | J18 reference -> J20 | Time | Peak memory |
   |---|---|---:|---:|
   | Public training | 10.6847 -> 10.5310 s | -1.44 % | +0.006 % |
   | Score table | 4.2575 -> 4.1004 s | -3.69 % | 0.00 % |
   | Historical TXT matches | 0.93885 -> 0.93336 s | -0.58 % (game time also -0.58 %) | -1.15 % |
   | Public TXT matches | 1.33251 -> 1.33189 s | -0.05 % | -0.29 % |

   - The gate is not triggered, and nothing is blocked.
   - binary_hashes shows that mus-solver, mus-bot and mus-match-eval from J20 are byte-identical to a clean build with the parent Makefile under the same options. So J20 changes no generated code.
   - The protocol states correctly that the measured differences reflect moving away from the historical mixed build (mus objects without LTO, C-CFR with LTO), not an algorithm change.
   - The historical C-CFR LTO options are stored, and the post-processing note lists what was added afterwards.
5. Progress entry. The table matches the recomputed figures, and the LTO correction and the byte-identity statement are accurate.

REQUIRED BEFORE COMMIT (documentation only, once the expanded integration test passes):
- Add one sentence to the J20 progress entry saying that the receipt recipes use + so make -n and make -q reflect the real state, and that the integration test now covers unchanged and changed dry runs.
- Update validation.config_test in J20.json. It currently describes the test run from before this refinement.

OPTIONAL: add a one-line README caution that make -t marks objects up to date without compiling them, so it must not be used after a configuration change. This is inherent to make -t, and J20 does not make it worse.
