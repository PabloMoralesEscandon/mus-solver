#!/usr/bin/env python3
"""Compare score-training binaries with fixed workloads and exact checkpoints."""

import argparse
import json
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--baseline")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--samples", type=int, default=100000)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--reserve", type=int, default=0)
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    if args.repeats < 1 or args.samples < 1 or args.seed < 0 or args.reserve < 0:
        parser.error("repeats and samples must be positive; seed and reserve must be nonnegative")
    cases = [(39, 39, 100000), (35, 18, 10000), (20, 20, 1000), (0, 0, 300)]
    if args.quick:
        cases = [(39, 39, 1000), (35, 18, 100), (20, 20, 50), (0, 0, 20)]
    binaries = {"current": args.binary}
    if args.baseline:
        binaries["baseline"] = args.baseline
    signatures = ("checkpoint_hash", "value", "information_sets", "visited_nodes")
    for a, b, iterations in cases:
        runs = {label: [] for label in binaries}
        expected = None
        for repeat in range(args.repeats):
            # Alternate execution order to reduce temperature and load bias.
            order = list(binaries)
            if repeat % 2:
                order.reverse()
            for label in order:
                command = [binaries[label], str(a), str(b), str(iterations),
                           str(args.samples), str(args.seed), str(args.reserve)]
                result = json.loads(subprocess.check_output(command, text=True))
                signature = {key: result[key] for key in signatures}
                if expected is None:
                    expected = signature
                if signature != expected:
                    raise SystemExit(f"Results differ for {a}:{b} ({label}): "
                                     f"{signature} != {expected}")
                runs[label].append(result)
        summary = {"score": f"{a}:{b}", "iterations": iterations,
                   "samples": args.samples, "seed": args.seed,
                   "repeats": args.repeats, "results_equal": True,
                   **expected}
        for label, results in runs.items():
            summary[label] = {
                key: statistics.median(result[key] for result in results)
                for key in ("training_seconds", "iterations_per_second", "evaluation_seconds")
            }
        if args.baseline:
            for operation in ("training", "evaluation"):
                key = f"{operation}_seconds"
                summary[f"{operation}_speedup"] = (
                    summary["baseline"][key] / summary["current"][key])
        print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
