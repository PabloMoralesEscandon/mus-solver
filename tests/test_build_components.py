#!/usr/bin/env python3
"""Check solver dependencies, archive replacement and scoped cleanup."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="components-test-", dir=ROOT / "build") as tmp:
        root = Path(tmp)
        for name in ("src", "app", "include", "make", "C-CFR/src", "C-CFR/include"):
            shutil.copytree(ROOT / name, root / name)
        for name in ("Makefile", "C-CFR/Makefile"):
            shutil.copy2(ROOT / name, root / name)
        # A solver build must not need the simulator checkout or query json-c.
        log = root / "pkg-config-called"
        pkg = root / "pkg-config-reject"
        pkg.write_text(f"#!/bin/sh\ntouch '{log}'\nexit 1\n")
        pkg.chmod(0o755)
        base = ["make", "-j4", "RELEASE_LTO_FLAGS=", "RELEASE_LINK_FLAGS=",
                f"PKG_CONFIG={pkg}"]

        def run(args, success=True):
            result = subprocess.run(args, cwd=root, capture_output=True, text=True)
            assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
            return result.stdout

        def members():
            return run(["ar", "t", "build/release/libmus_solver.a"]).splitlines()

        run(base + ["solver", "build/debug/mus-solver"])
        assert not log.exists(), "A solver build queried json-c"
        assert not (root / "Simulador-Mus").exists()
        assert len(members()) == 9 and not {"cartas.o", "baraja_espanola.o", "mus.o"} & set(members())
        run(["build/release/mus-solver", "--help"])
        run(base + ["bot"], success=False)
        assert log.exists(), "The bot did not request its json-c dependency"

        # A stable bot build needs one flags query and one libraries query.
        pkg.write_text(f"#!/bin/sh\nprintf '%s\\n' \"$*\" >> '{log}'\n"
                       "exec pkg-config \"$@\"\n")
        run(base + ["bot"])
        log.write_text("")
        run(base + ["bot"])
        assert sorted(log.read_text().splitlines()) == ["--cflags json-c", "--libs json-c"], log.read_text()

        # Link math functions without allowing the compiler to replace calls.
        log.unlink()
        run(base + ["CFLAGS=-fno-builtin", "solver"])
        assert not log.exists()
        run(["build/release/mus-solver", "--help"])
        run(base + ["solver"])

        # Populate the archive as an old build did, then change membership back
        # in the same directory. ar rcs alone would retain the obsolete members.
        for name in ("Simulador-Mus/src", "Simulador-Mus/include"):
            shutil.copytree(ROOT / name, root / name)
        run(base + ["LIB_SOURCES=$(MUS_SOURCES) $(SIMULATOR_SOURCES)",
                    "build/release/libmus_solver.a"])
        assert {"cartas.o", "baraja_espanola.o", "mus.o"} <= set(members())
        run(base + ["solver"])
        assert len(members()) == 9 and "mus.o" not in members()

        # A failing archiver must preserve the previously published library.
        archive = root / "build/release/libmus_solver.a"
        original = hashlib.sha256(archive.read_bytes()).hexdigest()
        ar = root / "ar-reject"
        ar.write_text("#!/bin/sh\nexit 1\n")
        ar.chmod(0o755)
        run(base + [f"AR={ar}", "build/release/libmus_solver.a"], success=False)
        assert hashlib.sha256(archive.read_bytes()).hexdigest() == original
        run(base + ["solver"])
        assert not log.exists(), "Archive or solver operations queried json-c"

        # Ordinary cleanup retains C-CFR. Full cleanup respects CFR_BUILD_DIR.
        other = root / "C-CFR/other-cache/keep"
        other.parent.mkdir()
        other.write_text("keep")
        run(base + ["clean"])
        assert not (root / "build").exists()
        assert (root / "C-CFR/build").exists()
        run(base + ["clean-all"])
        assert not (root / "C-CFR/build").exists()
        assert other.read_text() == "keep"
        selected = root / "selected-root"
        selected.mkdir()
        cache = root / "C-CFR/selected-cache"
        cache.mkdir()
        run(base + ["BUILD_DIR=selected-root", "CFR_BUILD_DIR=selected-cache", "clean-all"])
        assert not selected.exists() and not cache.exists()
        assert other.read_text() == "keep"
        assert not log.exists(), "Cleanup queried json-c"
        print("All build component checks passed.")


if __name__ == "__main__":
    main()
