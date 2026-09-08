#!/usr/bin/env python3
"""Measure shared-strategy throughput in separate, sequential processes."""

import argparse
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8, 14])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.repeats < 1 or any(t < 1 for t in args.threads) or 1 not in args.threads:
        parser.error("Use positive repeats and thread counts, including one thread.")

    workloads = [(39, 39, 500000), (35, 18, 10000), (20, 20, 1000), (0, 0, 300)]
    result = {
        "platform": platform.platform(),
        "cpu_affinity": sorted(os.sched_getaffinity(0)),
        "seed": args.seed,
        "repeats": args.repeats,
        "threads": args.threads,
        "continuations": "Synthetic immutable V[a][b] = (a-b)/40; target 40",
        "timing": "Training only, including concurrent-store setup and worker creation",
        "memory": "Linux getrusage peak RSS immediately after training, in KiB",
        "reservation": 0,
        "runs": [],
    }
    binary = str(args.binary.resolve())
    for a, b, iterations in workloads:
        for repeat in range(args.repeats):
            # Change the order between repetitions to reduce order effects.
            offset = (repeat // 2) % len(args.threads)
            order = args.threads[offset:] + args.threads[:offset]
            if repeat % 2:
                order = list(reversed(order))
            for threads in order:
                process = subprocess.run(
                    [binary, str(a), str(b), str(iterations), str(threads), str(args.seed)],
                    check=True, capture_output=True, text=True,
                )
                if process.stderr:
                    raise RuntimeError(process.stderr)
                run = json.loads(process.stdout)
                assert run["iterations"] == iterations and run["threads"] == threads
                run["repeat"] = repeat + 1
                result["runs"].append(run)
                args.output.write_text(json.dumps(result, indent=2) + "\n")
                print(json.dumps(run), flush=True)

    result["summary"] = []
    for a, b, iterations in workloads:
        score = f"{a}:{b}"
        baseline = statistics.median(
            run["training_seconds"] for run in result["runs"]
            if run["score"] == score and run["threads"] == 1
        )
        for threads in args.threads:
            runs = [run for run in result["runs"]
                    if run["score"] == score and run["threads"] == threads]
            elapsed = statistics.median(run["training_seconds"] for run in runs)
            rss = [run["peak_rss_kib"] / 1024 for run in runs]
            result["summary"].append({
                "score": score, "iterations": iterations, "threads": threads,
                "training_seconds_median": elapsed,
                "training_seconds_min": min(run["training_seconds"] for run in runs),
                "training_seconds_max": max(run["training_seconds"] for run in runs),
                "iterations_per_second": iterations / elapsed,
                "speedup_vs_one_thread": baseline / elapsed,
                "peak_rss_mib_median": statistics.median(rss),
                "peak_rss_mib_min": min(rss), "peak_rss_mib_max": max(rss),
                "nodes_per_iteration_median": statistics.median(
                    run["visited_nodes"] / iterations for run in runs),
                "nodes_per_second_median": statistics.median(
                    run["visited_nodes"] / run["training_seconds"] for run in runs),
                "information_sets_median": statistics.median(
                    run["information_sets"] for run in runs),
            })
    args.output.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
