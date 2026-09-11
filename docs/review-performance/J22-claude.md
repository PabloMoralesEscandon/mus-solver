# J22: Claude review

Task: `task_c36e99d369d7`. Dispatch: `ctx_9d54fc8e173e`. Message: `msg_2ec076dd77e7`.

APPROVE J22. I reviewed read-only the Makefile, make/build-config.mk, README and the new tests/test_build_components.py against f74e81d. Simulador-Mus and C-CFR are clean. Component separation, lazy json-c, the archive migration, failure preservation and cleanup scope are all correct, and I found no blocking issue; the notes below are non-blocking.

EVIDENCE
1. Separate targets. solver, bot and match-eval are phony aliases for the three release executables, and all is unchanged.
2. Lazy json-c.
   - JSON_C_FLAGS and JSON_C_LIBS are now recursive, and the tool comes from PKG_CONFIG, so its value can be overridden.
   - They are expanded only in three places:
     - the target-specific CPPFLAGS append on mus_bot_cli.o (CPPFLAGS is itself recursive, so the append stays deferred);
     - the bot link recipes;
     - the JSON receipt's own variables, now recursive too.
   - The shared compile receipt keeps its parse-time := and never contains JSON flags.
   - The test shows solver and debug/mus-solver build without calling pkg-config, while bot does call it and fails with the rejecting stub.
   - GNU make 4.4 passes exported variables to $(shell). A self-reference while expanding the JSON receipt's exported variables falls back to the original environment value, so this cannot recurse endlessly.
3. Simulator separation.
   - LIB_SOURCES is now MUS_SOURCES only, so libmus_solver.a holds 8 members (the test asserts this).
   - Only tests/test_mus_solver.c (mus.h, baraja_espanola.h) and the match-evaluation code (mus_sim.h, mus_log.h) include simulator headers.
     - The core test now links DEBUG_SIMULATOR_OBJECTS explicitly.
     - MATCH_SOURCES adds cartas, baraja_espanola and mus, so mus-match-eval and mus_match_tests (which uses the filtered-out object list) get them.
   - Header dependencies for the simulator objects are still included through the -include line for the match objects' .d files, which share those paths.
   - The test builds the solver with no Simulador-Mus directory at all. gcc ignores the missing include directory, and -Wmissing-include-dirs is not enabled.
4. Archive correctness.
   - MUS_ARCHIVE_CONFIG adds LIB_SOURCES, expanded at parse time after every assignment. Any change in membership therefore rewrites the archive receipt and rebuilds the archive.
   - The root archives are now built as: rm -f $@.tmp, then ar rcs $@.tmp with the objects, then mv -f $@.tmp $@. The archive is created fresh, so obsolete members disappear.
   - A failing archiver leaves the published archive untouched: make does not remove $@ because it was never modified, and the next build removes the leftover .tmp first.
   - mv within the same directory is atomic.
   - The test covers both cases: an old-style archive with simulator members migrates back to 8 members in the same directory, and a failing AR stub keeps the archive's SHA-256.
   - The first J22 build upgrades any existing archive automatically, because the receipt text changes.
5. Cleanup. clean-all runs clean and then $(MAKE) -C C-CFR BUILD_DIR=$(CFR_BUILD_DIR) clean, whose recipe is $(RM) -r -- $(BUILD_DIR).
   - Only the selected C-CFR directory is removed; the test keeps C-CFR/other-cache.
   - An empty value is a no-op, and GNU rm refuses . and .. and keeps its root protection.
   - With the defaults it also removes the LTO and review caches under C-CFR/build, which matches the README's "elimina además C-CFR/CFR_BUILD_DIR".
6. -lm is present in LDLIBS. The -fno-builtin solver build and run in the test verify real calls to libm.
7. README. The default build's requirements (zstd, json-c, pkg-config), the scope of solver/bot/match-eval, the contents of the core archive, full archive rebuilds on membership changes, and clean versus clean-all are all described accurately.

NON-BLOCKING NOTES
- One-time C-CFR recompile. build-config.mk is shared with the C-CFR overlay, whose objects depend on the archive receipt. Adding LIB_SOURCES therefore changes the C-CFR archive receipts, and the first J22 build recompiles every C-CFR object. This is harmless.
- C-CFR's own archive is still updated in place by its recipe (ar rcs $@ with all prerequisites). If a future submodule bump removes a C-CFR source, its receipt will recompile the objects but cannot delete the stale member from libcfr.a. The fix is to document running clean-all after such a bump, or have the overlay delete the archive when its receipt changes.
- pkg-config now runs once per expansion (several times per bot build). This is negligible.
- The cleanup test uses the default CFR_BUILD_DIR. One run of clean-all with a custom CFR_BUILD_DIR would pin down that it honours the variable.
- The link-order change in mus-match-eval is expected; your benchmark will cover it. Solver and bot hash-equality with J21 is consistent with LIB_SOURCES only dropping unused members.
