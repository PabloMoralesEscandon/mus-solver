#!/usr/bin/env python3
r"""Compare complete score-table CLI runs with Linux resource measurements.

Example:
  python3 tests/benchmark_training_cli.py --binary baseline=/tmp/baseline-solver \
    --binary candidate=build/release/mus-solver --workers 2:4 --workers 4:2 \
    --region 35:35 --iterations 10000 --samples 10000 --repeats 3 \
    --output build/performance/score-cli.json

Each run uses a fresh directory beside the output artifact. The CLI trains real
continuations, evaluates every cell, writes checkpoints, reloads the selected
checkpoint, and evaluates it again. Timing covers the complete CLI process.
Checkpoint validation occurs after timing. Temporary checkpoints are removed.
The JSON artifact retains stdout, stderr, values, file sizes, and resource samples.
Parallel updates can change policy bytes. Validation checks counts and structure.
"""

import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import resource
import signal
import statistics
import subprocess
import sys
import tempfile
import threading
import time

sys.dont_write_bytecode = True
from benchmark_training import Monitor, metadata, monitor_summary, save


def pair(text):
    try:
        fields = text.split(":")
        if len(fields) != 2:
            raise ValueError
        return tuple(int(field) for field in fields)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("Use two integers separated by a colon.") from exc


def expected_cells(target, region):
    low, high = sorted(region)
    return {(a, b) for a in range(target) for b in range(target)
            if (a >= low and b >= high) or (a >= high and b >= low)}


def record_fields(line):
    return dict(field.split("=", 1) for field in line.split()[1:] if "=" in field)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(directory, stdout, args, cells, workers):
    text = (directory / "values.txt").read_text()
    lines = text.splitlines()
    require(bool(lines) and lines[0].startswith("mus.ordago-score-region/v1 "),
            "The values file has an unexpected schema.")
    header = record_fields(lines[0])
    score = f"{args.region[0]}:{args.region[1]}"
    require(header.get("target") == str(args.target) and header.get("region") == score,
            "The values file has an unexpected target or region.")
    rows = {}
    for line in lines[1:]:
        fields = line.split()
        require(len(fields) in (6, 8), "The values file has an invalid row.")
        cell = (int(fields[0]), int(fields[1]))
        require(cell not in rows, "The values file contains a duplicate cell.")
        value = float(fields[2])
        require(math.isfinite(value) and -1 <= value <= 1, "A cell value is outside [-1, 1].")
        require(int(fields[3]) == args.iterations and int(fields[4]) == args.samples,
                "A values row has unexpected iteration or evaluation counters.")
        expected_seed = (args.seed + cell[0] * 40 + cell[1]) % (1 << 64)
        require(int(fields[5]) == expected_seed, "A values row has an unexpected training seed.")
        rows[cell] = value
    require(set(rows) == cells, "The values file does not contain the expected cells.")
    reports = {}
    evaluations = []
    worker_reports = []
    for line in stdout.splitlines():
        if line.startswith("score-cell "):
            fields = record_fields(line)
            cell = pair(fields["score"])
            require(cell not in reports, "The CLI reported a cell twice.")
            require(fields.get("target") == str(args.target) and
                    fields.get("iterations") == str(args.iterations) and
                    fields.get("evaluation_samples") == str(args.samples),
                    "A CLI cell report has unexpected counters.")
            require(cell in rows and float(fields["value"]) == rows[cell],
                    "A CLI cell value does not match the values file.")
            reports[cell] = fields
        elif line.startswith("evaluation scope=score-average-policy "):
            evaluations.append(record_fields(line))
        elif line.startswith("score-workers "):
            worker_reports.append(record_fields(line))
    require(set(reports) == cells, "The CLI did not report every expected cell.")
    require(len(evaluations) == 1 and evaluations[0].get("score") == score and
            evaluations[0].get("samples") == str(args.samples) and
            evaluations[0].get("stored_samples") == str(args.samples) and
            evaluations[0].get("seed") == str(args.seed),
            "The CLI did not complete the selected checkpoint evaluation.")
    require(float(evaluations[0]["stored_value"]) == rows[args.region],
            "The selected checkpoint evaluation has an unexpected stored value.")
    estimate = float(evaluations[0]["profile_value_player_0"])
    require(math.isfinite(estimate) and -1 <= estimate <= 1,
            "The selected checkpoint evaluation is outside [-1, 1].")
    cell_limit = min(workers[0], max(Counter(a + b for a, b in cells).values()))
    require(len(worker_reports) == 1 and
            worker_reports[0].get("requested") == str(workers[0]) and
            worker_reports[0].get("cell_limit") == str(cell_limit) and
            worker_reports[0].get("threads_per_cell") == str(workers[1]),
            "The CLI worker settings do not match the requested settings.")
    checkpoints = {path.name: path.stat().st_size for path in directory.glob("*.cfr.zst")}
    expected_names = {f"score-{a:02d}-{b:02d}.cfr.zst" for a, b in cells}
    require(set(checkpoints) == expected_names and all(size > 0 for size in checkpoints.values()),
            "The checkpoint files do not match the expected cells.")
    return {"cells": len(cells), "cell_reports": len(reports), "values_text": text,
            "checkpoint_files": checkpoints, "checkpoint_bytes": sum(checkpoints.values()),
            "worker_report": worker_reports[0], "evaluation": evaluations[0],
            "validation_passed": True}


def execute(command, monitor, interval, timeout):
    samples = []
    observed_exit = threading.Event()
    exit_info = {}
    own_before = resource.getrusage(resource.RUSAGE_SELF)
    with tempfile.TemporaryFile(mode="w+") as stdout, tempfile.TemporaryFile(mode="w+") as stderr:
        start = time.monotonic()
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr)

        def observe_exit():
            try:
                # Keep the child unreaped until wait4 obtains its exact usage.
                # WNOWAIT also prevents PID reuse during timeout cancellation.
                os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOWAIT)
            except OSError as exc:
                exit_info["error"] = str(exc)
            finally:
                exit_info["end"] = time.monotonic()
                observed_exit.set()

        observer = threading.Thread(target=observe_exit)
        observer.start()
        timed_out = False
        try:
            while not observed_exit.is_set():
                sample = monitor.sample(process.pid)
                samples.append(sample)
                if time.monotonic() - start >= timeout:
                    timed_out = not observed_exit.is_set()
                    break
                remaining = timeout - (time.monotonic() - start)
                observed_exit.wait(min(remaining, max(0, interval - sample["collection_seconds"])))
        finally:
            if not observed_exit.is_set():
                os.kill(process.pid, signal.SIGKILL)
            observer.join()
            # Do not use Popen.poll/wait/kill here: they can reap the child first.
            _, status, usage = os.wait4(process.pid, 0)
            process.returncode = os.waitstatus_to_exitcode(status)
        elapsed = exit_info["end"] - start
        stdout.seek(0)
        stderr.seek(0)
        output, errors = stdout.read(), stderr.read()
    own_after = resource.getrusage(resource.RUSAGE_SELF)
    process_usage = {
        "user_cpu_seconds": usage.ru_utime, "system_cpu_seconds": usage.ru_stime,
        "peak_rss_kib": usage.ru_maxrss, "minor_faults": usage.ru_minflt,
        "major_faults": usage.ru_majflt, "voluntary_context_switches": usage.ru_nvcsw,
        "involuntary_context_switches": usage.ru_nivcsw,
        "input_blocks": usage.ru_inblock, "output_blocks": usage.ru_oublock,
    }
    for sample in samples:
        sample["during_training"] = (sample["monotonic"] <= exit_info["end"] and
                                     sample["process"] is not None and sample["process"]["state"] != "Z")
    # Reuse the resource calculations with the complete execution interval.
    # Rename the training-specific fields before publishing CLI measurements.
    resources = monitor_summary(samples, {"training_seconds": elapsed, **process_usage})
    resources["execution_sample_count"] = resources.pop("training_sample_count")
    for thread in resources["threads"]:
        thread["observed_cpu_percent_of_execution"] = thread.pop("observed_cpu_percent_of_training")
    for sample in samples:
        sample["during_execution"] = sample.pop("during_training")
    monitor_cpu = own_after.ru_utime + own_after.ru_stime - own_before.ru_utime - own_before.ru_stime
    child_cpu = usage.ru_utime + usage.ru_stime
    result = {"command": command, "returncode": process.returncode,
              "execution_seconds": elapsed, "stdout": output, "stderr": errors,
              "process_usage": process_usage, "resources": resources, "samples": samples,
              "monitor_cpu_seconds": monitor_cpu, "monitor_average_cpu_cores": monitor_cpu / elapsed,
              "monitor_cpu_fraction_of_process_cpu": monitor_cpu / child_cpu if child_cpu else None}
    if timed_out:
        result["error"] = "The CLI exceeded the timeout."
    elif process.returncode != 0:
        result["error"] = f"The CLI exited with code {process.returncode}."
    elif "error" in exit_info:
        result["error"] = "Cannot observe process completion: " + exit_info["error"]
    return result


def summarize(runs, labels):
    groups = {}
    for run in runs:
        if "error" not in run:
            groups.setdefault((run["label"], tuple(run["workers"])), []).append(run)
    summaries = []
    for (label, workers), group in groups.items():
        times = [run["execution_seconds"] for run in group]
        memory = [run["process_usage"]["peak_rss_kib"] for run in group]
        summary = {"label": label, "workers": workers, "repeats": len(group),
                   "execution_seconds_median": statistics.median(times),
                   "execution_seconds_min": min(times), "execution_seconds_max": max(times),
                   "end_to_end_iterations_per_second_median": statistics.median(
                       run["end_to_end_iterations_per_second"] for run in group),
                   "peak_rss_kib_median": statistics.median(memory),
                   "peak_rss_kib_min": min(memory), "peak_rss_kib_max": max(memory),
                   "average_cpu_cores_median": statistics.median(
                       run["resources"]["average_cpu_cores"] for run in group),
                   "checkpoint_bytes_median": statistics.median(
                       run["validation"]["checkpoint_bytes"] for run in group)}
        baseline = groups.get((labels[0], workers))
        if baseline:
            summary["speedup_vs_baseline"] = statistics.median(
                run["execution_seconds"] for run in baseline) / statistics.median(times)
            summary["rss_ratio_vs_baseline"] = statistics.median(memory) / statistics.median(
                run["process_usage"]["peak_rss_kib"] for run in baseline)
            paired = {run["repeat"]: run for run in baseline}
            ratios = [paired[run["repeat"]]["execution_seconds"] / run["execution_seconds"]
                      for run in group if run["repeat"] in paired]
            if ratios:
                summary["paired_speedup_vs_baseline_median"] = statistics.median(ratios)
        summaries.append(summary)
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", action="append", required=True, metavar="LABEL=PATH")
    parser.add_argument("--workers", type=pair, action="append", metavar="N:M")
    parser.add_argument("--region", type=pair, default=(35, 35), metavar="A:B")
    parser.add_argument("--target", type=int, default=40)
    parser.add_argument("--iterations", type=int, default=10000)
    parser.add_argument("--samples", type=int, default=10000)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--sample-interval", type=float, default=0.2)
    parser.add_argument("--timeout", type=float, default=120, help="Maximum seconds per CLI process.")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not Path("/proc/self/stat").exists() or not all(hasattr(os, name) for name in ("wait4", "waitid", "WNOWAIT")):
        parser.error("The benchmark requires Linux procfs, wait4, and waitid with WNOWAIT.")
    args.workers = args.workers or [(2, 4)]
    args.region = tuple(sorted(args.region))
    if (not 1 <= args.target <= 40 or any(score < 0 or score >= args.target for score in args.region) or
            min(args.iterations, args.samples, args.repeats) < 1 or not 0 <= args.seed < 2**64 or
            any(min(workers) < 1 for workers in args.workers) or len(set(args.workers)) != len(args.workers) or
            not math.isfinite(args.sample_interval) or not 0.01 <= args.sample_interval <= 60 or
            not math.isfinite(args.timeout) or args.timeout <= 0):
        parser.error("Use a valid region and target, positive counts, unique positive worker pairs, a uint64 seed, a finite positive timeout, and a sample interval from 0.01 to 60 seconds.")
    binaries = []
    for value in args.binary:
        label, separator, filename = value.partition("=")
        path = Path(filename).resolve()
        if not separator or not label or not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"Invalid binary specification: {value}")
        if label in [item[0] for item in binaries]:
            parser.error(f"Duplicate binary label: {label}")
        binaries.append((label, path))
    cells = expected_cells(args.target, args.region)
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    monitor = Monitor()
    result = {
        "version": 1, "metadata": metadata(monitor, binaries),
        "configuration": {key: getattr(args, key) for key in
                          ("region", "target", "iterations", "samples", "seed", "repeats",
                           "workers", "sample_interval", "timeout")},
        "expected_cells": len(cells),
        "temporary_parent": str(args.output.parent),
        "measurement": {
            "timing": "Complete CLI execution. Includes training, evaluation, checkpoint writes, and selected checkpoint reload/evaluation.",
            "wall_clock": "A waitid observer records process exit. Timing does not wait for the next resource sample.",
            "process_resources": "os.wait4 returns exact per-process CPU, high-water RSS, faults, context switches, and block IO counters.",
            "samples": "Procfs counters are sampled. Per-thread CPU deltas can omit work between observations.",
            "temperature": "The maximum includes all available host sensors, including storage sensors.",
            "artifacts": "Every run uses a fresh temporary directory beside this JSON file. Checkpoints are validated and then removed.",
            "validation": "Check expected cell coverage, counters, seeds, values, checkpoint files, worker settings, and selected checkpoint evaluation.",
            "parallelism": "N concurrent cells each use M training threads. Evaluation uses one thread per cell. Parallel policy bytes can differ.",
        },
        "runs": [], "summary": [],
    }
    save(args.output, result)
    labels = [label for label, _ in binaries]
    for repeat in range(args.repeats):
        workers_order = args.workers if repeat % 2 == 0 else args.workers[::-1]
        binary_order = binaries if repeat % 2 == 0 else binaries[::-1]
        for workers in workers_order:
            for label, binary in binary_order:
                print(json.dumps({"event": "start", "label": label, "workers": workers,
                                  "repeat": repeat + 1, "cells": len(cells)}), flush=True)
                with tempfile.TemporaryDirectory(prefix="score-cli-", dir=args.output.parent) as temporary:
                    destination = Path(temporary) / "table"
                    command = [str(binary), "--format", "ordago-score", "--target", str(args.target),
                               "--score-region", f"{args.region[0]}:{args.region[1]}",
                               "--iterations", str(args.iterations), "--evaluation-samples", str(args.samples),
                               "--seed", str(args.seed), "--threads", str(workers[0]),
                               "--threads-per-cell", str(workers[1]), "--continuation", str(destination),
                               "--evaluate"]
                    run = execute(command, monitor, args.sample_interval, args.timeout)
                    run.update(label=label, workers=workers, repeat=repeat + 1)
                    if "error" not in run:
                        try:
                            run["validation"] = validate(destination, run["stdout"], args, cells, workers)
                            run["end_to_end_iterations_per_second"] = len(cells) * args.iterations / run["execution_seconds"]
                        except (OSError, ValueError, KeyError, argparse.ArgumentTypeError) as exc:
                            run["error"] = "CLI artifact validation failed: " + str(exc)
                result["runs"].append(run)
                result["summary"] = summarize(result["runs"], labels)
                save(args.output, result)
                print(json.dumps({"event": "finish", "label": label, "workers": workers,
                                  "repeat": repeat + 1, "execution_seconds": run["execution_seconds"],
                                  "peak_rss_kib": run["process_usage"]["peak_rss_kib"],
                                  "error": run.get("error")}), flush=True)
                if "error" in run:
                    raise SystemExit(f"{run['error']} Results: {args.output}")


if __name__ == "__main__":
    main()
