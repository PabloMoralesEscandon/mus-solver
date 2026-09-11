#!/usr/bin/env python3
"""Exercise configuration changes in one build directory with real compilers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    assert result.returncode == 0, (args, result.stdout, result.stderr)
    return result.stdout


def main():
    with tempfile.TemporaryDirectory(prefix="config-test-", dir=ROOT / "build") as tmp:
        build = Path(tmp).relative_to(ROOT)
        cfr = ROOT / "C-CFR" / build
        base = ["make", "-j4", f"BUILD_DIR={build}", f"CFR_BUILD_DIR={build}",
                "RELEASE_LTO_FLAGS=", "RELEASE_LINK_FLAGS="]
        debug = build / "debug"
        release = build / "release"
        targets = [str(debug / "mus_solver_tests"),
                   str(debug / "mus_adapter_configuration_tests")]
        try:
            # Returning to an old value must rebuild again, not reuse a stale
            # marker left by an earlier visit to that configuration.
            stamps = []
            for limit in (1, 2, 1):
                run(base + [f"MUS_MAX_MUSES={limit}"] + targets)
                run([str(debug / "mus_solver_tests")])
                run([str(debug / "mus_adapter_configuration_tests")])
                stamps.append((debug / "src/mus_game.o").stat().st_mtime_ns)
            assert stamps[0] < stamps[1] < stamps[2], stamps

            def state():
                return {str(p): p.stat().st_mtime_ns for directory in (ROOT / build, cfr)
                        for p in directory.rglob("*")
                        if p.is_file() and (p.suffix in (".o", ".a") or ".mus-" in p.name)}

            # Drop the explicit macro, then establish a stable default build.
            run(base + targets + [str(release / "src/mus_game.o")])
            before = state()
            run(base + targets + [str(release / "src/mus_game.o")])
            assert state() == before, "Unchanged commands rebuilt objects or receipts"
            dry = run(base + ["-n"] + targets)
            assert " -c " not in dry, dry
            assert state() == before, "Dry run changed objects or stable receipts"

            # Dry runs update metadata but must leave compilation pending.
            old_game = (debug / "src/mus_game.o").stat().st_mtime_ns
            dry = run(base + ["-n", "MUS_MAX_MUSES=2"] + targets)
            assert " -c " in dry, dry
            assert (debug / "src/mus_game.o").stat().st_mtime_ns == old_game
            run(base + ["MUS_MAX_MUSES=2"] + targets)
            assert (debug / "src/mus_game.o").stat().st_mtime_ns > old_game
            run([str(debug / "mus_solver_tests")])
            run([str(debug / "mus_adapter_configuration_tests")])
            run(base + targets)

            # Flags passed to the parent must also invalidate C-CFR objects.
            cfr_obj = cfr / "debug/src/mccfr.o"
            root_obj = debug / "src/mus_game.o"
            release_time = (release / "src/mus_game.o").stat().st_mtime_ns
            old_root, old_cfr = root_obj.stat().st_mtime_ns, cfr_obj.stat().st_mtime_ns
            run(base + ["CFLAGS=-DMUS_BUILD_CONFIG_TEST=1"] + targets)
            assert root_obj.stat().st_mtime_ns > old_root
            assert cfr_obj.stat().st_mtime_ns > old_cfr
            assert (release / "src/mus_game.o").stat().st_mtime_ns == release_time
            run([str(debug / "mus_solver_tests")])

            # Changing the compiler command and archiver command also matters.
            cc = ROOT / build / "cc-wrapper"
            ar = ROOT / build / "ar-wrapper"
            cc.write_text('#!/bin/sh\nexec cc "$@"\n')
            ar.write_text('#!/bin/sh\nexec ar "$@"\n')
            cc.chmod(0o755)
            ar.chmod(0o755)
            old_cfr = cfr_obj.stat().st_mtime_ns
            run(base + [f"CC={cc}", f"AR={ar}"] + targets)
            assert cfr_obj.stat().st_mtime_ns > old_cfr
            run([str(debug / "mus_solver_tests")])

            old_archive = (debug / "libmus_solver.a").stat().st_mtime_ns
            old_root = root_obj.stat().st_mtime_ns
            run(base + [f"CC={cc}"] + targets)
            assert (debug / "libmus_solver.a").stat().st_mtime_ns > old_archive
            assert root_obj.stat().st_mtime_ns == old_root

            # Bot-specific flags must not leak into the shared receipt even
            # when the bot is the first requested target in a parallel build.
            bot = str(debug / "mus-bot")
            run(base + [bot] + targets)
            old_root = root_obj.stat().st_mtime_ns
            run(base + ["JSON_C_FLAGS=-I/usr/include/json-c -DMUS_JSON_TEST=1", bot] + targets)
            assert root_obj.stat().st_mtime_ns == old_root
            assert "MUS_JSON_TEST" not in (debug / ".mus-compile-config").read_text()

            # A stale LTO archive was observed in the historical benchmarks.
            # Verify both directions without changing the directory.
            lto_targets = ["cfr-release", str(release / "src/mus_game.o")]
            cfr_release = cfr / "release/src/mccfr.o"
            run(base + ["RELEASE_LTO_FLAGS=-flto"] + lto_targets)
            assert b".gnu.lto_" in cfr_release.read_bytes()
            assert b".gnu.lto_" in (release / "src/mus_game.o").read_bytes()
            run(base + lto_targets)
            assert b".gnu.lto_" not in cfr_release.read_bytes()
            assert b".gnu.lto_" not in (release / "src/mus_game.o").read_bytes()
            print("All build configuration checks passed.")
        finally:
            shutil.rmtree(cfr, ignore_errors=True)


if __name__ == "__main__":
    os.makedirs(ROOT / "build", exist_ok=True)
    main()
