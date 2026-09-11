#!/usr/bin/env python3
"""Measure score-table training, evaluation and persistence with exact file checks."""

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--baseline")
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 4])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--score", default="35:35")
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--samples", type=int, default=10000)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--ignore-sampling-statistics", action="store_true",
                        help="Compare original value fields against a pre-statistics baseline.")
    args = parser.parse_args()
    try:
        a, b = map(int, args.score.split(":"))
    except ValueError:
        parser.error("score must have the format A:B")
    if not (0 <= a < 40 and 0 <= b < 40):
        parser.error("each score must be between 0 and 39")
    if min(args.threads + [args.repeats, args.iterations, args.samples]) < 1 or args.seed < 0:
        parser.error("counts must be positive and seed must be nonnegative")
    cells = sum(x + y >= a + b for x in range(40) for y in range(40))
    variants = [(f"current-{n}", args.binary, n) for n in dict.fromkeys(args.threads)]
    if args.baseline:
        variants.insert(0, ("baseline-1", args.baseline, 1))
    expected = None
    equality_key = "policy_files_and_values_equal" if args.ignore_sampling_statistics else "files_equal"
    runs = {label: [] for label, _, _ in variants}
    for repeat in range(args.repeats):
        order = variants if repeat % 2 == 0 else variants[::-1]
        for label, binary, threads in order:
            with tempfile.TemporaryDirectory(prefix="mus-score-benchmark-") as temporary:
                destination = Path(temporary) / "table"
                command = [binary, "--format", "ordago-score", "--score", args.score,
                           "--iterations", str(args.iterations), "--evaluation-samples",
                           str(args.samples), "--seed", str(args.seed), "--threads",
                           str(threads), "--continuation", str(destination)]
                start = time.perf_counter()
                subprocess.run(command, stdout=subprocess.DEVNULL, check=True)
                elapsed = time.perf_counter() - start
                # File comparison stays outside the measured process runtime.
                hashes = {}
                for path in sorted(destination.iterdir()):
                    if path.name in ("training.lock", "recovery.txt"):
                        continue  # Auxiliary metadata is not a policy artifact.
                    if path.name == "values.txt" and args.ignore_sampling_statistics:
                        lines = path.read_text().splitlines()
                        header = lines[0].replace(" statistics=sample-variance-v1", "")
                        original = header + "\n" + "\n".join(
                            " ".join(line.split()[:6]) for line in lines[1:]) + "\n"
                        hashes[path.name] = hashlib.sha256(original.encode()).hexdigest()
                        continue
                    with path.open("rb") as stream:
                        hashes[path.name] = hashlib.file_digest(stream, "sha256").hexdigest()
                if len(hashes) != cells + 1:
                    raise SystemExit(f"Unexpected file count for {label}: {len(hashes)}")
                if expected is None:
                    expected = hashes
                if hashes != expected:
                    detail = "Original table fields or checkpoint bytes" if args.ignore_sampling_statistics \
                        else "Table or checkpoint bytes"
                    raise SystemExit(f"{detail} differ for {label}")
            runs[label].append(elapsed)
            print(json.dumps({"variant": label, "repeat": repeat + 1,
                              "seconds": elapsed, equality_key: True}), flush=True)
    summary = {"score": args.score, "cells": cells,
               "iterations_per_cell": args.iterations, "samples_per_cell": args.samples,
               "seed": args.seed, "repeats": args.repeats, equality_key: True,
               "sampling_statistics_checked": not args.ignore_sampling_statistics}
    for label, measurements in runs.items():
        elapsed = statistics.median(measurements)
        summary[label] = {"seconds": elapsed,
                          "iterations_per_second": cells * args.iterations / elapsed}
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
