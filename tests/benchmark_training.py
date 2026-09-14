#!/usr/bin/env python3
r"""Compare MCCFR binaries in sequential processes and record Linux resources.

Example:
  python3 tests/benchmark_training.py --binary baseline=/tmp/benchmark-baseline \
    --binary candidate=build/release/benchmark-training --threads 1 2 4 8 \
    --workload ordago-public:1000 --workload ordago-score:39:39:500000 \
    --repeats 3 --output /tmp/training-comparison.json

Each process starts with an empty store. Training includes worker and store setup.
Score continuations use V[a][b] = (a-b)/40. Parallel updates are not deterministic.
The monitor samples resources throughout the process and marks training samples.
Per-thread counters are lower bounds: a thread can exit between samples. C rusage
deltas include all threads and cover the complete training interval.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import resource
import statistics
import subprocess
import tempfile
import time


def read(path):
    try:
        return Path(path).read_text()
    except (OSError, UnicodeError):
        return ""


def numeric_fields(path):
    result = {}
    for line in read(path).splitlines():
        fields = line.replace(":", " ", 1).split()
        if len(fields) >= 2:
            try:
                result[fields[0]] = int(fields[1])
            except ValueError:
                pass
    return result


def task_stat(path):
    # comm can contain spaces and parentheses. Fields after comm start at 3.
    data = read(path)
    if not data:
        return None
    fields = data[data.rfind(")") + 2:].split()
    if len(fields) < 37:
        return None
    return {
        "state": fields[0], "minor_faults": int(fields[7]),
        "major_faults": int(fields[9]), "user_ticks": int(fields[11]),
        "system_ticks": int(fields[12]), "threads": int(fields[17]),
        "start_ticks": int(fields[19]), "vsize_bytes": int(fields[20]),
        "rss_pages": int(fields[21]), "processor": int(fields[36]),
    }


def pressure(path):
    return {
        fields[0]: {key: float(value) for key, value in
                    (item.split("=", 1) for item in fields[1:])}
        for line in read(path).splitlines() if (fields := line.split())
    }


def cgroup_directory():
    for line in read("/proc/self/cgroup").splitlines():
        if line.startswith("0::"):
            path = Path("/sys/fs/cgroup") / line[3:].lstrip("/")
            if path.is_dir():
                return path
    return None


class Monitor:
    def __init__(self):
        self.cgroup = cgroup_directory()
        self.temperatures = sorted(Path("/sys/class/thermal").glob("thermal_zone*/temp"))
        self.temperatures += sorted(Path("/sys/class/hwmon").glob("hwmon*/temp*_input"))
        self.frequencies = sorted(Path("/sys/devices/system/cpu/cpufreq").glob(
            "policy*/scaling_cur_freq"))

    def host(self):
        cpus = {}
        for line in read("/proc/stat").splitlines():
            fields = line.split()
            if fields and fields[0].startswith("cpu"):
                cpus[fields[0]] = [int(value) for value in fields[1:]]
        vmstat = numeric_fields("/proc/vmstat")
        result = {
            "memory_kib": numeric_fields("/proc/meminfo"),
            "cpu_ticks": cpus,
            "load_average": read("/proc/loadavg").strip(),
            "pressure": {name: pressure(f"/proc/pressure/{name}")
                         for name in ("cpu", "memory", "io")},
            "vmstat": {key: value for key, value in vmstat.items()
                       if key.startswith(("pgfault", "pgmajfault", "pswp", "pgscan",
                                          "pgsteal", "oom_kill"))},
            "temperature_millicelsius": {}, "frequency_khz": {},
        }
        for paths, name in ((self.temperatures, "temperature_millicelsius"),
                            (self.frequencies, "frequency_khz")):
            for path in paths:
                value = read(path).strip()
                if value.lstrip("-").isdigit():
                    result[name][str(path)] = int(value)
        if self.cgroup:
            result["cgroup"] = {
                "cpu": numeric_fields(self.cgroup / "cpu.stat"),
                "memory_events": numeric_fields(self.cgroup / "memory.events"),
                "memory_current_bytes": read(self.cgroup / "memory.current").strip(),
                "swap_current_bytes": read(self.cgroup / "memory.swap.current").strip(),
            }
        return result

    def sample(self, pid):
        start = time.monotonic()
        proc = Path("/proc") / str(pid)
        result = {"monotonic": start, "process": task_stat(proc / "stat"),
                  "status": numeric_fields(proc / "status"),
                  "io": numeric_fields(proc / "io"), "threads": {}}
        try:
            tasks = list((proc / "task").iterdir())
        except OSError:
            tasks = []
        for task in tasks:
            stat = task_stat(task / "stat")
            if stat is not None:
                status = numeric_fields(task / "status")
                stat.update({key: status[key] for key in
                             ("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches")
                             if key in status})
                result["threads"][task.name] = stat
        result["host"] = self.host()
        result["collection_seconds"] = time.monotonic() - start
        return result


def metadata(monitor, binaries):
    cpu_fields = {}
    for line in read("/proc/cpuinfo").split("\n\n", 1)[0].splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            if key.strip() in ("model name", "vendor_id", "cpu family", "model", "stepping"):
                cpu_fields[key.strip()] = value.strip()
    binary_details = {}
    for label, path in binaries:
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        binary_details[label] = {"path": str(path), "bytes": path.stat().st_size, "sha256": digest}
    result = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "platform": platform.platform(), "python": platform.python_version(),
        "cpu": cpu_fields, "cpu_count": os.cpu_count(),
        "cpu_affinity": sorted(os.sched_getaffinity(0)),
        "clock_ticks_per_second": os.sysconf("SC_CLK_TCK"),
        "page_bytes": os.sysconf("SC_PAGE_SIZE"),
        "binaries": binary_details,
        "environment": {key: value for key, value in os.environ.items()
                        if key.startswith(("MALLOC_", "OMP_", "CFR_")) or key == "GLIBC_TUNABLES"},
        "cpu_governors": {str(path): read(path).strip() for path in
                          Path("/sys/devices/system/cpu/cpufreq").glob("policy*/scaling_governor")},
        "temperature_labels": {str(path): read(path.with_name(
            path.name.replace("_input", "_label")) if "_input" in path.name
            else path.with_name("type")).strip() for path in monitor.temperatures},
    }
    if monitor.cgroup:
        result["cgroup"] = {"path": str(monitor.cgroup)}
        for name in ("cpu.max", "cpuset.cpus.effective", "memory.max", "memory.swap.max"):
            result["cgroup"][name] = read(monitor.cgroup / name).strip()
    return result


def monitor_summary(samples, benchmark):
    training = [sample for sample in samples if sample.get("during_training")]
    selected = training or samples
    threads = {}
    for sample in training:
        for tid, counters in sample["threads"].items():
            # A TID can be reused. Include the kernel's thread creation time.
            key = f"{tid}:{counters['start_ticks']}"
            current = threads.setdefault(key, {
                "tid": int(tid), "samples": 0, "observed_cpu_ticks": 0,
                "processors": set(), "first_sample_monotonic": sample["monotonic"],
                # Differences between training samples exclude initialization.
                "initial_cpu_ticks": counters["user_ticks"] + counters["system_ticks"],
            })
            current["samples"] += 1
            current["last_sample_monotonic"] = sample["monotonic"]
            current["observed_cpu_ticks"] = max(current["observed_cpu_ticks"],
                                                  counters["user_ticks"] + counters["system_ticks"])
            current["processors"].add(counters["processor"])
            for name in ("voluntary_ctxt_switches", "nonvoluntary_ctxt_switches"):
                current[name] = max(current.get(name, 0), counters.get(name, 0))
    ticks = os.sysconf("SC_CLK_TCK")
    elapsed = benchmark.get("training_seconds", 0)
    for current in threads.values():
        current["processors"] = sorted(current["processors"])
        current["observed_cpu_seconds"] = (
            current.pop("observed_cpu_ticks") - current.pop("initial_cpu_ticks")) / ticks
        current["observed_cpu_percent_of_training"] = (
            100 * current["observed_cpu_seconds"] / elapsed if elapsed else None)
    result = {
        "sample_count": len(samples), "training_sample_count": len(training),
        "sampled_peak_rss_kib": max((s["status"].get("VmRSS", 0) for s in selected), default=0),
        "sampled_peak_swap_kib": max((s["status"].get("VmSwap", 0) for s in selected), default=0),
        "sampled_peak_threads": max((len(s["threads"]) for s in selected), default=0),
        "host_min_available_memory_kib": min((s["host"]["memory_kib"].get("MemAvailable", 0)
                                              for s in selected), default=0),
        "sample_collection_seconds": sum(s["collection_seconds"] for s in samples),
        "threads": list(threads.values()),
    }
    temperatures = [value for sample in selected for value in
                    sample["host"]["temperature_millicelsius"].values() if value > 0]
    result["host_peak_temperature_celsius"] = max(temperatures) / 1000 if temperatures else None
    if selected:
        first, last = selected[0], selected[-1]
        sampled_seconds = last["monotonic"] - first["monotonic"]
        result["host_sample_interval_seconds"] = sampled_seconds
        result["host_swap_in_pages"] = (last["host"]["vmstat"].get("pswpin", 0) -
                                         first["host"]["vmstat"].get("pswpin", 0))
        result["host_swap_out_pages"] = (last["host"]["vmstat"].get("pswpout", 0) -
                                          first["host"]["vmstat"].get("pswpout", 0))
        result["host_swap_used_kib_delta"] = (
            first["host"]["memory_kib"].get("SwapFree", 0) -
            last["host"]["memory_kib"].get("SwapFree", 0))
        result["host_pressure"] = {}
        for name, scopes in last["host"]["pressure"].items():
            result["host_pressure"][name] = {}
            for scope, values in scopes.items():
                initial = first["host"]["pressure"].get(name, {}).get(scope)
                if initial is not None:
                    stalled = (values["total"] - initial["total"]) / 1e6
                    result["host_pressure"][name][scope] = {
                        "stall_seconds": stalled,
                        "stall_percent_of_sample_interval": (
                            100 * stalled / sampled_seconds if sampled_seconds else None),
                        "avg10_first": initial["avg10"], "avg10_last": values["avg10"],
                    }
        if "cgroup" in first["host"] and "cgroup" in last["host"]:
            result["cgroup_cpu_delta"] = {
                key: value - first["host"]["cgroup"]["cpu"].get(key, value)
                for key, value in last["host"]["cgroup"]["cpu"].items()
            }
    if elapsed:
        result["average_cpu_cores"] = (benchmark["user_cpu_seconds"] +
                                       benchmark["system_cpu_seconds"]) / elapsed
    return result


def run_one(command, monitor, interval, timeout):
    samples = []
    started = time.monotonic()
    usage_before = resource.getrusage(resource.RUSAGE_SELF)
    timed_out = False
    with tempfile.TemporaryFile(mode="w+") as stdout, tempfile.TemporaryFile(mode="w+") as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr)
        try:
            while process.poll() is None:
                sample = monitor.sample(process.pid)
                samples.append(sample)
                if timeout and time.monotonic() - started > timeout:
                    timed_out = True
                    process.kill()
                    break
                time.sleep(max(0, interval - sample["collection_seconds"]))
            returncode = process.wait()
        except BaseException:
            process.kill()
            process.wait()
            raise
        stdout.seek(0)
        stderr.seek(0)
        out, err = stdout.read(), stderr.read()
    benchmark = {}
    error = None
    if returncode == 0:
        try:
            benchmark = json.loads(out)
            if not isinstance(benchmark, dict):
                raise ValueError("Benchmark result must be a JSON object.")
            for key in ("training_start_monotonic", "training_end_monotonic", "training_seconds",
                        "user_cpu_seconds", "system_cpu_seconds", "iterations", "threads"):
                if key not in benchmark:
                    raise ValueError(f"Missing result field: {key}")
        except (json.JSONDecodeError, ValueError) as exc:
            error = str(exc)
            benchmark = {}
    else:
        error = "Benchmark exceeded the timeout." if timed_out else f"Benchmark exited with code {returncode}."
    for sample in samples:
        sample["during_training"] = bool(benchmark) and (
            benchmark["training_start_monotonic"] <= sample["monotonic"] <=
            benchmark["training_end_monotonic"])
    usage_after = resource.getrusage(resource.RUSAGE_SELF)
    result = {"command": command, "returncode": returncode, "stderr": err,
              "process_seconds": time.monotonic() - started, "benchmark": benchmark,
              "resources": monitor_summary(samples, benchmark), "samples": samples,
              "monitor_cpu_seconds": (usage_after.ru_utime + usage_after.ru_stime -
                                      usage_before.ru_utime - usage_before.ru_stime)}
    result["monitor_average_cpu_cores"] = result["monitor_cpu_seconds"] / result["process_seconds"]
    training_cpu = benchmark.get("user_cpu_seconds", 0) + benchmark.get("system_cpu_seconds", 0)
    result["monitor_cpu_fraction_of_training_cpu"] = (
        result["monitor_cpu_seconds"] / training_cpu if training_cpu else None)
    if error:
        result.update(error=error, stdout=out)
    return result


def workload(text):
    fields = text.split(":")
    try:
        if fields[0] == "ordago-public" and len(fields) == 2:
            result = {"format": fields[0], "a": 0, "b": 0, "iterations": int(fields[1])}
        elif fields[0] == "ordago-score" and len(fields) == 4:
            result = {"format": fields[0], "a": int(fields[1]), "b": int(fields[2]),
                      "iterations": int(fields[3])}
        else:
            raise ValueError
        if result["iterations"] < 1 or not (0 <= result["a"] < 40 and 0 <= result["b"] < 40):
            raise ValueError
        return {"name": text, **result}
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "Use ordago-public:ITERATIONS or ordago-score:A:B:ITERATIONS with positive iterations and scores from 0 to 39."
        ) from exc


def summaries(runs, labels):
    result = []
    groups = {}
    for run in runs:
        if "error" not in run:
            groups.setdefault((run["workload"], run["threads"], run["label"]), []).append(run)
    for (name, threads, label), group in groups.items():
        row = {"workload": name, "threads": threads, "label": label, "repeats": len(group)}
        for metric in ("training_seconds", "iterations_per_second", "nodes_per_second",
                       "peak_rss_kib", "information_sets", "visited_nodes", "minor_faults",
                       "major_faults", "voluntary_context_switches", "involuntary_context_switches"):
            values = [run["benchmark"][metric] for run in group]
            row[metric + "_median"] = statistics.median(values)
            row[metric + "_min"] = min(values)
            row[metric + "_max"] = max(values)
        row["average_cpu_cores_median"] = statistics.median(
            run["resources"]["average_cpu_cores"] for run in group)
        single = groups.get((name, 1, label))
        if single:
            row["speedup_vs_one_thread"] = row["iterations_per_second_median"] / statistics.median(
                run["benchmark"]["iterations_per_second"] for run in single)
        baseline = groups.get((name, threads, labels[0]))
        if baseline:
            row["speedup_vs_baseline"] = row["iterations_per_second_median"] / statistics.median(
                run["benchmark"]["iterations_per_second"] for run in baseline)
            row["rss_ratio_vs_baseline"] = row["peak_rss_kib_median"] / statistics.median(
                run["benchmark"]["peak_rss_kib"] for run in baseline)
            paired = {run["repeat"]: run for run in baseline}
            ratios = [run["benchmark"]["iterations_per_second"] /
                      paired[run["repeat"]]["benchmark"]["iterations_per_second"]
                      for run in group if run["repeat"] in paired]
            row["paired_speedup_vs_baseline_median"] = statistics.median(ratios)
        result.append(row)
    return result


def save(path, result):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(result, indent=2) + "\n")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", action="append", required=True, metavar="LABEL=PATH")
    parser.add_argument("--workload", action="append", type=workload)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--report-every", type=int, default=None)
    parser.add_argument("--sample-interval", type=float, default=0.2)
    parser.add_argument("--timeout", type=float, default=0, help="Maximum seconds per process. Zero disables the limit.")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not Path("/proc/self/stat").exists():
        parser.error("The resource monitor requires Linux procfs.")
    if (args.repeats < 1 or any(t < 1 for t in args.threads) or args.seed < 0 or args.seed >= 2**64 or
            not math.isfinite(args.sample_interval) or not 0.01 <= args.sample_interval <= 60 or
            not math.isfinite(args.timeout) or args.timeout < 0 or
            (args.report_every is not None and args.report_every < 1)):
        parser.error("Use positive repeats, threads, and report interval. Use a finite nonnegative timeout and a uint64 seed. The sample interval must be from 0.01 to 60 seconds.")
    binaries = []
    for value in args.binary:
        label, separator, filename = value.partition("=")
        path = Path(filename).resolve()
        if not separator or not label or not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"Invalid binary specification: {value}")
        if label in [item[0] for item in binaries]:
            parser.error(f"Duplicate binary label: {label}")
        binaries.append((label, path))
    workloads = args.workload or [workload("ordago-public:1000"), workload("ordago-score:39:39:500000"),
                                  workload("ordago-score:35:18:10000"), workload("ordago-score:0:0:300")]
    if len(set(args.threads)) != len(args.threads) or len({w["name"] for w in workloads}) != len(workloads):
        parser.error("Thread counts and workload names must be unique.")
    monitor = Monitor()
    result = {
        "version": 1, "metadata": metadata(monitor, binaries),
        "configuration": {"threads": args.threads, "repeats": args.repeats, "seed": args.seed,
                          "report_every": args.report_every, "sample_interval_seconds": args.sample_interval,
                          "timeout_seconds": args.timeout, "workloads": workloads},
        "measurement": {
            "timing": "Training only. Includes concurrent-store setup, worker creation, and worker joins.",
            "continuations": "Synthetic immutable V[a][b] = (a-b)/40. Target 40.",
            "store": "Each process starts with an empty store. No capacity reservation.",
            "cpu": "C getrusage deltas cover all training threads. CPU cores = CPU seconds / training seconds.",
            "rss": "C peak_rss_kib is process high-water RSS through training. Procfs resources are sampled.",
            "threads": "Observed per-thread CPU and switches are lower bounds. Threads can exit between samples.",
            "host": "Host and cgroup counters include other processes. Raw samples permit interference checks.",
            "order": "Adjacent binary runs alternate direction each repetition. Thread order rotates and reverses.",
            "replay": "Parallel updates are not deterministic. Compare node rate and store size with iteration rate.",
        },
        "runs": [], "summary": [],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    save(args.output, result)
    labels = [label for label, _ in binaries]
    for work in workloads:
        for repeat in range(args.repeats):
            offset = (repeat // 2) % len(args.threads)
            thread_order = args.threads[offset:] + args.threads[:offset]
            if repeat % 2:
                thread_order.reverse()
            for threads in thread_order:
                order = binaries if repeat % 2 == 0 else list(reversed(binaries))
                for label, binary in order:
                    command = [str(binary), work["format"], str(work["iterations"]), str(threads),
                               str(args.seed), str(work["a"]), str(work["b"]),
                               str(args.report_every or work["iterations"])]
                    print(json.dumps({"event": "start", "label": label, "workload": work["name"],
                                      "threads": threads, "repeat": repeat + 1}), flush=True)
                    run = run_one(command, monitor, args.sample_interval, args.timeout)
                    run.update(label=label, workload=work["name"], threads=threads, repeat=repeat + 1)
                    if "error" not in run:
                        observed = run["benchmark"]
                        expected = {"format": work["format"], "iterations": work["iterations"],
                                    "threads": threads, "seed": args.seed,
                                    "score": f"{work['a']}:{work['b']}",
                                    "report_every": args.report_every or work["iterations"]}
                        if any(observed.get(key) != value for key, value in expected.items()):
                            run["error"] = "Benchmark result does not match the requested workload."
                    result["runs"].append(run)
                    result["summary"] = summaries(result["runs"], labels)
                    save(args.output, result)
                    print(json.dumps({"event": "finish", "label": label, "workload": work["name"],
                                      "threads": threads, "repeat": repeat + 1,
                                      **run["benchmark"], "error": run.get("error")}), flush=True)
                    if "error" in run:
                        raise SystemExit(f"{run['error']} Results: {args.output}")


if __name__ == "__main__":
    main()
