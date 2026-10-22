"""Tests for mus-score-compact and the compact policy format.

Builds synthetic checkpoints in the frozen v1 format, converts them, and uses
the converter's own verification pass, which queries every node.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parent.parent
TOOL = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/release/mus-score-compact"
SCHEMA = b"mus.ordago-score/v4/base-test/target-40/score-00-00/mano/signed-one/V-0"


def checkpoint(nodes, iterations=1000, schema=SCHEMA, node_count=None, variant=2):
    body = b"CFRCKPT\0" + struct.pack("<II", 1, len(schema)) + schema + struct.pack("<I", variant)
    if variant == 2:
        body += struct.pack("<Q", 12345)
    count = len(nodes) if node_count is None else node_count
    body += struct.pack("<6Q", iterations, iterations, 2 * iterations, 99, 0, count)
    for key, regrets, sums in nodes:
        body += struct.pack("<qQ", key, len(sums))
        body += struct.pack("<" + "d" * len(regrets), *regrets)
        body += struct.pack("<" + "d" * len(sums), *sums)
    return body + struct.pack("<I", zlib.crc32(body))


def compress(path, data):
    subprocess.run(["zstd", "-q", "-1", "-f", "-o", str(path)], input=data, check=True)


def run(*args):
    return subprocess.run([str(TOOL), *map(str, args)], capture_output=True, text=True)


NODES = [
    (-40, [0.0, 0.0], [3.0, 1.0]),            # 0.75 / 0.25
    (-7, [1.0, -1.0, 0.5], [0.0, 0.0, 0.0]),  # No mass: omitted
    (0, [0.0], [2.5]),                        # Single action
    (5, [0.0, 0.0, 0.0, 0.0], [1.0, 1.0, 1.0, 1.0]),
    (2**40, [0.0, 0.0], [1e-9, 1.0]),         # Large key delta, tiny share
]
WITH_MASS = [n for n in NODES if sum(n[2]) > 0]


def expect_failure(result, text=""):
    assert result.returncode != 0, result.stdout
    assert text in result.stderr, result.stderr


def main():
    checks = 0
    with tempfile.TemporaryDirectory() as directory:
        d = Path(directory)
        source, output = d / "cell.cfr.zst", d / "cell.mscz"
        compress(source, checkpoint(NODES))

        result = run(source, output)
        assert result.returncode == 0, result.stderr
        summary = json.loads(result.stdout)
        assert summary["source_nodes"] == len(NODES), summary
        assert summary["stored_nodes"] == len(WITH_MASS), summary
        assert 0 < summary["bytes"] < source.stat().st_size, summary
        checks += 3

        result = run("--verify", source, output)
        assert result.returncode == 0, result.stderr
        report = json.loads(result.stdout)
        assert report["checked_nodes"] == len(WITH_MASS), report
        assert report["mismatched_nodes"] == 0, report
        assert report["largest_difference"] < 1 / 65535, report
        checks += 3

        # Refuses to overwrite, and never leaves a partial file behind.
        expect_failure(run(source, output), "invalid_argument")
        checks += 1

        cases = {
            "crc": (checkpoint(NODES)[:-1] + b"\x00", "format_error"),
            "unsorted": (checkpoint([NODES[3], NODES[0]]), "format_error"),
            "truncated": (checkpoint(NODES)[:-60], "format_error"),
            "short_count": (checkpoint(NODES, node_count=len(NODES) + 2), "format_error"),
            "sequential": (checkpoint(NODES, variant=0), "incompatible_game"),
            "negative": (checkpoint([(1, [0.0], [-1.0])]), "numeric_error"),
        }
        for name, (data, message) in cases.items():
            bad, bad_out = d / f"{name}.cfr.zst", d / f"{name}.mscz"
            compress(bad, data)
            expect_failure(run(bad, bad_out), message)
            assert not bad_out.exists(), name
            checks += 1

        # A checkpoint whose nodes all lack mass still produces a readable file.
        empty, empty_out = d / "empty.cfr.zst", d / "empty.mscz"
        compress(empty, checkpoint([NODES[1]]))
        assert run(empty, empty_out).returncode == 0
        report = json.loads(run("--verify", empty, empty_out).stdout)
        assert report["checked_nodes"] == 0 and report["mismatched_nodes"] == 0, report
        checks += 2

        # Verification rejects a compact file from a different checkpoint.
        other, other_out = d / "other.cfr.zst", d / "other.mscz"
        compress(other, checkpoint(NODES, iterations=2000))
        assert run(other, other_out).returncode == 0
        expect_failure(run("--verify", source, other_out), "incompatible_game")
        checks += 1

        # Tampering with the stored bytes is detected.
        for name, position in [("header", 20), ("index", -12)]:
            copy = d / f"tampered-{name}.mscz"
            raw = bytearray(output.read_bytes())
            raw[position] ^= 0x40
            copy.write_bytes(bytes(raw))
            expect_failure(run("--verify", source, copy))
            checks += 1

        # A truncated compact file is rejected rather than read short.
        cut = d / "cut.mscz"
        cut.write_bytes(output.read_bytes()[:-40])
        expect_failure(run("--verify", source, cut))
        checks += 1
    print(json.dumps({"tests": "passed", "checks": checks}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
