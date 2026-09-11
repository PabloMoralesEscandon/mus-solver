#!/usr/bin/env python3
"""Check real dependency changes in a disposable copy of the build inputs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="incremental-test-", dir=ROOT / "build") as tmp:
        root = Path(tmp)
        for name in ("src", "app", "include", "tests", "make",
                     "C-CFR/src", "C-CFR/include", "Simulador-Mus/src", "Simulador-Mus/include"):
            shutil.copytree(ROOT / name, root / name)
        for name in ("Makefile", "C-CFR/Makefile"):
            shutil.copy2(ROOT / name, root / name)
        log = root / "compiler.jsonl"
        cc = root / "cc-record"
        cc.write_text("#!/usr/bin/env python3\nimport json,os,sys\n"
                      f"fd=os.open({str(log)!r},os.O_WRONLY|os.O_APPEND|os.O_CREAT,0o600)\n"
                      "os.write(fd,(json.dumps(sys.argv[1:])+'\\n').encode());os.close(fd)\n"
                      "os.execvp('cc',['cc',*sys.argv[1:]])\n")
        cc.chmod(0o755)
        base = ["make", "-j4", f"CC={cc}", "RELEASE_LTO_FLAGS=", "RELEASE_LINK_FLAGS="]
        goals = ["all", "debug", "benchmark-score", "benchmark-score-threads",
                 "benchmark-evaluation", "build/debug/mus_adapter_candidates_tests"]

        def run(args, expected=0):
            result = subprocess.run(args, cwd=root, text=True, capture_output=True)
            assert result.returncode == expected, (args, result.stdout, result.stderr)
            return result.stdout

        def calls():
            return [json.loads(s) for s in log.read_text().splitlines()]

        def clear():
            log.write_text("")

        def snapshot():
            return {str(p.relative_to(root)): p.stat().st_mtime_ns
                    for directory in (root / "build", root / "C-CFR/build")
                    for p in directory.rglob("*") if p.is_file()}

        clear()
        run(base + ["-n"] + goals)
        assert not calls(), "A fresh dry run invoked the compiler"
        run(base + goals)
        before = snapshot()
        clear()
        run(base + goals)
        assert not calls(), "An unchanged build invoked the compiler or linker"
        assert snapshot() == before, "An unchanged build changed an artifact"
        dry = run(base + ["-n"] + goals)
        assert not calls(), "A dry run invoked the compiler"
        assert str(cc) + " " not in dry, dry
        assert snapshot() == before
        run(base + ["-q", "build/release/mus-solver"])
        assert not calls(), "An up-to-date query invoked the compiler"

        # Link flags rebuild executables but preserve every compiled object.
        clear()
        run(base + ["LDFLAGS=-Wl,--build-id=none"] + goals)
        assert calls() and all("-c" not in row for row in calls())
        assert all(snapshot()[name] == stamp for name, stamp in before.items()
                   if name.endswith((".o", ".a")))
        run(base + goals)
        clear()
        before = snapshot()

        # Change a genuine C-CFR source in the copy, never in the submodule.
        source = root / "C-CFR/src/info_store.c"
        with source.open("a") as stream:
            stream.write("\nint mus_build_dependency_test_marker(void) { return 1; }\n")
        dry = run(base + ["-n"] + goals)
        assert "src/info_store.c" in dry, dry
        assert not calls(), "A changed-source dry run invoked the compiler"
        assert snapshot() == before, "A changed-source dry run changed artifacts"
        run(base + goals)
        compiled = [row for row in calls() if "-c" in row]
        assert len(compiled) == 2 and all("src/info_store.c" in row for row in compiled), compiled
        after = snapshot()
        binaries = [name for name in before if "/build/" not in name and
                    name.startswith("build/") and os.access(root / name, os.X_OK)]
        assert len(binaries) >= 16, binaries
        assert all(after[name] > before[name] for name in binaries), binaries
        assert all(after[name] == stamp for name, stamp in before.items()
                   if name.endswith(".o") and not name.endswith("/src/info_store.o"))
        clear()
        run(base + goals)
        assert not calls(), "The rebuild did not settle after a C-CFR change"
        run(["build/debug/mus_solver_tests"])
        run(["build/debug/mus_adapter_configuration_tests"])
        print("All incremental build checks passed.")


if __name__ == "__main__":
    main()
