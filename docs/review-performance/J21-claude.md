# J21: Claude review

Task: `task_95eb6a39d836`. Dispatch: `ctx_d051577337e4`. Message: `msg_fa672829479d`.

APPROVE J21. I reviewed read-only the Makefile diff against 72a0356, the new tests/test_build_incremental.py and the README paragraph. Consumers now correctly depend on the real C-CFR archives, and every behaviour you asked about is correct in real builds. The only point left is a documentation precision about dry runs after a change to C-CFR alone.

EVIDENCE
1. Rule design. The archive targets (C-CFR/CFR_BUILD_DIR/release/libcfr.a and the debug one) keep the phony cfr-release and cfr-debug as prerequisites and have a +@: recipe.
   - Because the file target has a recipe, GNU make re-reads the archive's real timestamp after the recursive check (notice_finished_file sets UNKNOWN_MTIME). A rule with no recipe would be marked NEW_MTIME and force every consumer to relink, so the no-op recipe is essential.
   - The + makes the same happen under -n, -q and -t.
   - As a result, an unchanged sub-build keeps the archive's timestamp and nothing relinks, while a real archive update relinks exactly the consumers.
2. All 16 consumers are converted. The 6 release ones are mus-solver, mus-bot, mus-match-eval and the three benchmarks. The 10 debug ones are mus-solver, mus_solver_tests, mus-bot, mus-match-eval, mus_match_tests, mus_hotpaths_tests, mus_adapter_tests, mus_adapter_candidates_tests, mus_adapter_configuration_tests and mus_ordago_public_tests.
   - cfr-release and cfr-debug now appear only in .PHONY, in their own rules, and as prerequisites of the archive targets.
   - Every link recipe names the archive explicitly and uses either the first-prerequisite variable (always the object) or explicit lists. None uses the all-prerequisites variable, so adding the archive as a prerequisite changes no command line.
   - The archive path is the one already used on the link lines.
3. Cases.
   - First build: the archive does not exist, the sub-make creates it, and the re-read timestamp then orders every consumer after it.
   - Stale objects: C-CFR's own .d files and rules rebuild the archive, and its new timestamp relinks the consumers.
   - Configuration changes: J20's receipts invalidate the C-CFR objects, so the archive is rebuilt and the consumers relink. Link-only flags relink without compiling.
   - No-op builds: the sub-make runs, the archive is unchanged, so nothing compiles or links.
   - Parallel builds: many consumers share one file target, which depends on one phony target, so each recursive check runs once per invocation and every consumer waits for it. The release and debug sub-builds use separate directories, as before.
   - C-CFR sources are untouched: the test works on a copy.
4. Test (tests/test_build_incremental.py).
   - It copies the root sources, include, tests and make directories, C-CFR/src and include, and the Simulador-Mus sources, plus both Makefiles, into a temporary directory under build/ that is removed afterwards. The submodule is never edited.
   - A Python compiler wrapper appends every compile and link command to a JSON log.
   - It checks:
     - a fresh dry run makes no compiler calls;
     - a real build followed by an unchanged build makes no compiler or linker calls and leaves every artifact timestamp identical, which is the core J21 requirement;
     - an unchanged dry run prints no compiler command and changes nothing;
     - make -q on mus-solver succeeds;
     - an LDFLAGS-only change relinks without compiling and keeps every object and archive;
     - appending a function to the copied C-CFR/src/info_store.c compiles exactly two objects (the debug and release info_store.c), relinks all 16 or more executables (every timestamp increases), leaves every other object untouched, and settles on the next build with no calls;
     - both test binaries still run.
   - The goal list (all, debug, three benchmarks, the candidates test) covers all 16 consumers.

DOCUMENTATION PRECISION (recommended, non-blocking)
After a change to C-CFR sources alone, make -n under-reports.
- The recursive sub-make runs in dry-run mode and prints C-CFR's compile and ar commands, but it does not really update libcfr.a. The root then re-reads the archive's old timestamp and prints no relinks for the consumers.
- make -q, in the same situation, fails through the sub-make's non-zero status. The result is non-zero, but reported as an error rather than a clean "not up to date".
- This is inherent to a real-file dependency behind a recursive make, and real builds are correct (the test proves it).
- However, the README sentence from J20, "la simulación refleja las dependencias reales", overstates the case now. Suggested addition:
  «Tras cambiar solo C-CFR, `make -n` muestra su recompilación, pero no los enlaces posteriores, porque la simulación no actualiza `libcfr.a`; `make -q` termina con error desde el submake.»

MINOR
- The new paragraph in the README is accurate for real builds.
- test_build_incremental.py, like test_build_config.py, is a standalone check outside make test. That is consistent with the README.
- Optionally, the test could also assert that a dry run after the copied C-CFR edit prints the sub-make's compile lines. That would pin down the documented limitation.
