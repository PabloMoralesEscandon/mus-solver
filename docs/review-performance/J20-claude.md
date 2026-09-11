# J20: Claude review

Task: `task_c949dbfa2003`. Dispatch: `ctx_9193122f493f`. Message: `msg_c6823b9ba5e2`.

APPROVE J20, with one strongly recommended fix. I reviewed the Makefile diff, make/build-config.mk, make/cfr-config.mk, tests/test_build_config.py, the C-CFR Makefile rules they overlay, the README text and the J20 progress entry read-only. Flag invalidation, one-to-two-to-one mus safety, GNU make prerequisite handling, bot flag isolation, the C-CFR overlay and shell handling are all correct; the remaining problem is that make -n/-q/-t now always report everything out of date, which matters for verifying J21.

EVIDENCE
1. Effective flags. The compile receipt records every variable used by the root compile recipe: CC, CPPFLAGS (including -DMUS_GAME_MAX_MUSES), CFLAGS, THREAD_FLAGS, C_STANDARD, WARNINGS and DEPENDENCY_FLAGS, plus the mode flags (RELEASE_FLAGS including RELEASE_LTO_FLAGS, or DEBUG_FLAGS). The archive receipt records AR. The link receipt records CC, LDFLAGS, THREAD_FLAGS, LDLIBS and CFR_BUILD_DIR, plus RELEASE_LINK_FLAGS. The bot has its own receipt for JSON_C_FLAGS and JSON_C_LIBS.
   - Every root object (library, app, tests, simulator, match evaluator, benchmarks) is built by the two pattern rules, which now depend on the receipt.
   - All 6 release and all 10 debug link targets have the link receipt as a prerequisite.
   - The C-CFR static pattern compile rules use CC, CPPFLAGS, CFLAGS, C_STANDARD, C_WARNINGS, RELEASE_FLAGS or DEBUG_FLAGS, and DEPENDENCY_FLAGS; all of these are recorded. C_WARNINGS is folded into the WARNINGS line.
2. One-to-two-to-one safety. Each receipt is written to a temporary file, compared with cmp, and moved into place only when different. So it keeps its timestamp when nothing changed and gets a fresh one on every change, including a return to an earlier value. There is no stale per-configuration marker, so going back from 2 to 1 rebuilds, and the test checks strictly increasing mus_game.o timestamps while running both test binaries at every step.
   - If make is interrupted after a receipt changes, the receipt stays newer than the objects, so the next build still recompiles.
   - If a compile fails, the compiler removes its output, so the next build retries.
   - C-CFR does not depend on the mus limit.
3. GNU make behaviour (installed version 4.4.1).
   - After running a recipe for a non-phony target in a normal build, make re-reads the file timestamp (notice_finished_file sets UNKNOWN_MTIME), so an unchanged receipt does not trigger any rebuild even though the phony mus-config-force always runs it.
   - The first prerequisite of the pattern and static pattern rules stays the .c file, because the extra receipt prerequisites are merged after the prerequisites of the rule that has the recipe.
   - The root archive recipes now use explicit object lists, so receipts are never archived.
   - The upstream C-CFR archive recipe uses the all-prerequisites variable, so cfr-config.mk attaches the archive receipt to the objects rather than the archive. The only cost is that changing AR recompiles the C-CFR objects; this is documented.
   - The private modifier needs GNU make 3.82 or later.
4. Bot flag isolation. Target-specific variables of mus_bot_cli.o are inherited by its prerequisites, but each receipt defines its own values with target-specific immediate assignments. These are expanded when the makefile is parsed, and build-config.mk is included after every global assignment, so the bot's inherited CPPFLAGS addition can never reach the shared receipt. The JSON flags use a separate receipt, and the test covers a parallel build that requests the bot first.
5. C-CFR overlay.
   - The submodule is invoked as make -C C-CFR -f Makefile -f (absolute path of cfr-config.mk), which leaves its sources and rules untouched.
   - The include of build-config.mk resolves correctly, because at that point the last entry of MAKEFILE_LIST is cfr-config.mk itself.
   - C-CFR receipts live under C-CFR/CFR_BUILD_DIR/release and /debug, separate from the root ones, and are covered by the submodule's build/ gitignore entry.
   - cfr-release and cfr-debug use disjoint receipts, so concurrent sub-makes cannot collide.
   - git -C C-CFR status is clean.
6. Shell handling. Values reach printf through exported environment variables and are printed with printf %s, so quotes, dollar signs and backticks inside flags are never re-parsed by the shell. Each update is written to a temporary file, then compared and moved atomically.
7. Tests. They use real compilers:
   - one-to-two-to-one with the test binaries executed at each step;
   - a stable no-op rebuild leaves every object, archive and receipt timestamp unchanged;
   - a CFLAGS change and a CC or AR wrapper both reach the C-CFR objects, and release objects stay untouched when only debug targets are requested;
   - JSON flags are kept out of the shared receipt.

RECOMMENDED FIX (strong; needed before J21 is verified by dry runs). make -n, make -q and make -t now always report every object out of date. Under -n, -q and -t, GNU make assumes that a target whose non-recursive recipe would have run was updated (NEW_MTIME) instead of re-reading its timestamp, and the receipts are forced by a phony prerequisite. So make -n debug will list every compile as well as every link, and make -q will never report up to date. That breaks the audit's own J21 evidence, which is make -n debug.
- Fix: prefix the three receipt recipe lines with + (both in build-config.mk and in the JSON receipt rule in the Makefile). They then run under -n, -q and -t, make re-reads the timestamps, and dry runs become accurate. The only side effect is writing a receipt whose content really changed, which is correct and harmless.
- Alternative: document that dry runs are unreliable and verify J21 only with real builds and timestamps.

MINOR (non-blocking)
- The receipts do not capture the compiler version behind an unchanged CC name, or environment variables such as CPATH. That could be documented, or the first line of the compiler's --version output could be added to the receipt.
- A JSON_C_LIBS change recompiles mus_bot_cli.o. This is harmless.
- test_build_config.py is not part of make test. The README documents running it manually, and it cleans up its temporary C-CFR directory.
- Paths containing spaces remain unsupported, as elsewhere in the repository.
- J21 is deferred as planned: the phony cfr-release and cfr-debug targets still relink every binary, and the link receipts become meaningful once J21 lands.
- The README warning about concurrent builds with different configurations in the same BUILD_DIR is accurate and appropriate.

ACKNOWLEDGEMENT. The LTO precision in the J20 progress entry is correct: C-CFR/build/review-baseline/release/libcfr.a contains 580 .gnu.lto sections, while the debug archive has none. My J18 final review inspected only libmus_solver.a when it said both sides were non-LTO, so that claim was incomplete: the mus objects were non-LTO and C-CFR was LTO. The relative comparisons are unaffected, provided both sides linked that same archive, as the entry states.
