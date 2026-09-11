# J23: Claude review

Task: `task_c757d829d768`. Dispatch: `ctx_59c5d50f5848`. Message: `msg_7803ddfd30d8`.

J23: code and tests APPROVED; one required CI correction, a submodule commit that is not on any remote. I reviewed read-only the diff against c79329b (CLI, preload, header, Makefile, workflow, the three tests, README and docs/adapter.md) and checked the remotes with git ls-remote. The observability changes, the fseeko check, the seed contract and test-core are correct and well tested, but the workflow cannot check out the pinned Simulador-Mus commit, and two sanitizer settings should be hardened.

REQUIRED (CI correctness and data safety)
1. The pinned Simulador-Mus commit 2fda65e and its parent c837741 (the J02 deferred counting) exist only locally.
   - git ls-remote on PabloMoralesEscandon/Simulador-Mus shows HEAD and master at cf52bbb, a branch 2-players at 736a34f, and no tags.
   - Both remote heads exist locally, and neither contains 2fda65e. cf52bbb is an ancestor of 2fda65e, so the pinned commits are unpushed descendants.
   - The submodule is on a detached HEAD, and no local branch contains 2fda65e.
   - So actions/checkout with submodules: recursive (and any fresh clone) will fail to fetch the recorded commit before any job runs.
   - Fix: create a branch at 2fda65e in the submodule now, so it is not garbage-collected once the reflog expires, and push it to the Simulador-Mus remote before pushing the root commits that record it.
   - C-CFR is fine: its pinned 91d7d39 is remote main.

RECOMMENDED (CI robustness)
2. Sanitizers on ubuntu-24.04 use GCC 13. Its libtsan and libasan are known to fail on runner kernels with high mmap randomization (ThreadSanitizer: unexpected memory mapping, or repeated DEADLYSIGNAL). Your local runs use a newer GCC and do not exercise this. Add before the sanitizer jobs a step such as: if [ "$INSTRUMENTATION" != normal ]; then sudo sysctl -w vm.mmap_rnd_bits=28; fi. It is harmless where not needed.
3. timeout-minutes is 30 on a 2-vCPU runner. Check it against your local wall time for the full make test under TSan and ASan/UBSan with make -j2. Raise it (for example to 60) if they come close.

VERIFIED CORRECT
- Workflow structure.
  - The matrix is muses {1, 2} × {normal, asan-ubsan, tsan}, with fail-fast off. One mus runs the full make test; two muses run test-core, since the bot still requires one mus.
  - Sanitizer CFLAGS and LDFLAGS reach C-CFR through the cfr-* rules, and the J20 receipts keep reused directories safe.
  - The second job builds the default LTO all target and runs the three build-integration tests.
  - permissions is contents: read and persist-credentials is false.
  - The pinned checkout SHA 3d3c42e5 is exactly the upstream v7 tag (v7.0.1), as the README says.
  - The packages (build-essential, pkg-config, libzstd-dev, libjson-c-dev, zstd, python3) cover pkg-config json-c and the zstd CLI used by test_adapter_cli.
  - The submodule URLs use HTTPS, and C-CFR is public.
- Makefile.
  - test-core builds and runs mus_solver_tests, the debug CLI binary with test_cli.sh (five formats), hot paths, the public properties and mus_adapter_configuration_tests (the two-mus guard).
  - test depends on test-core, which moves the configuration test there and runs it exactly once.
  - The recursive test-sanitize and test-tsan still run everything, and test-core is in .PHONY.
- uniform_after_filter_nodes.
  - In both the eager path (preload_node) and the lazy path (initialize_from_ordago), it is incremented only after a node is really imported, and only when mass, the prior summed over the destination menu's shared actions before the uniform mix, is zero. It is therefore a subset of imported_nodes.
  - Existing nodes return before the import; the lazy path uses an atomic increment; missing sources (no candidate with positive mass) are counted only in missing_source_nodes. Priors are unchanged.
  - Stats are reset at the start of both paths, including the new field through the designated initializer in attach.
  - The C tests pin the counter to 0, 0 and 1 in the eager cases and to 0, 1 and 0 in the lazy cases, covering a candidate with all its mass on actions missing from the destination menu, and a missing source.
- Mode nuance (optional to document). The eager path uses the source's average strategy, which is uniform for a zero-mass source node, so such a node with shared actions is not counted. The lazy path treats zero-sum candidates as missing. This is behaviour that predates J23, and the counter reflects each path's prior faithfully.
- score-workers.
  - cell_limit is the largest number of unready cells in any diagonal with a+b of at least score_a+score_b and inside the table or region, capped by --threads. Region resolution happens before the call, and the array of 80 entries covers every a+b up to 78.
  - This matches mus_score_train's batch concurrency, which is min(worker count, pending cells). Recovery and failed thread creation can only reduce it, as the README says.
  - The test values are right for target 3: 0:0 gives 3; 2:2 gives 1; region 1:1 gives 2 (diagonal 3 holds (1,2) and (2,1)); a rerun gives 0.
- Output compatibility. The new fields are inserted into the key=value lines score-preload and score-report. The existing regex in test_cli.sh (iterations=3 .*added_iterations=1) still matches, and the new score-workers line comes before score-resume.
- fseeko. The unchecked rewind is replaced by a checked fseeko(expected, 0, SEEK_SET) with IO_ERROR, after the fflush check, and no rewind remains in the CLI.
- --seed. The bot validates every occurrence and exits with invalid_seed on the first bad value; the last valid value wins. The test shows 999 then 123 gives output identical to 123 alone, and bad then 123 fails. docs/adapter.md states this contract.
- Scope. C-CFR is clean, and the algorithm files are unchanged except for the two counter lines in mus_preload.c. No J13, J17-sync or J19 code is present.
