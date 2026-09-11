# J23: follow-up Claude review

Task: `task_c0e0328f0871`. Dispatch: `ctx_3bc3cfb1c9e2`. Message: `msg_101f04f91ad4`.

APPROVE the J23 follow-up for local completion; no code or documentation corrections remain. I reviewed read-only the updated workflow, the README publication note, the J23 progress entry, the targeted recovery-test timeout and the new local Simulador-Mus branch. My earlier required finding is handled correctly within the authorized scope (protected locally, publication documented as a prerequisite, no push), and the two CI recommendations are applied.

EVIDENCE
1. Submodule protection.
   - Branch review/mus-audit-fixes resolves to 2fda65eb22995588f0eb7605cc77cf2d8c8882d5, whose parent is c837741. Both audit commits are therefore reachable from a ref and safe from garbage collection.
   - Nothing changed in the checked-out commit or the working tree.
   - Nothing was pushed, which is consistent with the authorization.
2. Documentation.
   - The README tells users to publish the submodule commits a root branch references before publishing that branch.
   - The progress entry names c837741 and 2fda65e, the local branch, and the requirement to publish it before the root branch. It states that no publication was requested and that GitHub CI has not run, and that C-CFR's commit is already on its remote. This scope statement is accurate.
3. Workflow.
   - sudo sysctl -w vm.mmap_rnd_bits=28 runs only when the instrumentation is not normal, so only on the ephemeral sanitizer runners and never on the workstation. The README and progress entry cite GCC bug 113430 and the upstream sanitizer report.
   - The instrumented matrix has timeout-minutes 60, and build-contracts keeps 30.
   - The rest is unchanged and was verified before: the pinned checkout is the v7 tag commit, permissions are read-only, the matrix is muses {1, 2} × {normal, asan-ubsan, tsan}, one mus runs test and two muses run test-core.
4. tests/test_score_recovery.py. run() gains a timeout parameter that defaults to the previous 30 seconds. Only the deliberately long completion of the interrupted run uses 180 seconds, with a comment explaining the sanitizer slowdown. Every other call keeps its original bound, and the assertions are unchanged.
5. The rest of J23 is unchanged since my approval of the code: the counter, score-workers, fseeko, the seed contract and test-core.

OUTSTANDING (not corrections)
- The final local TSan runs are still in progress.
- Remote CI can only run after the owner publishes review/mus-audit-fixes on Simulador-Mus and then the root branch. The documentation records this correctly as a publication prerequisite, not as a completed step.
- Performance samples are still to come.
