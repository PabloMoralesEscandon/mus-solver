#!/usr/bin/env python3
r"""Create a compact artifact from training or CLI benchmark JSON files.

Example:
  python3 tests/summarize_training_benchmarks.py \
    build/performance/key-snapshots.json build/performance/score-cli.json \
    --output build/performance/results-compact.json

The output preserves each run's metrics, command, validation, stdout, and resource
summary, including its per-thread CPU summary. Sample timelines become counter
deltas and extrema. Source metadata, binary hashes, and source-file hashes remain
available. Original files are not modified. A legacy pilot list is also accepted;
the output explicitly identifies runs that have no resource samples.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile


def number(value):
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return value if math.isfinite(value) else None
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return None
    return None


def extrema(records):
    fields = {}
    for record in records:
        for key, raw in record.items():
            value = number(raw)
            if value is None:
                continue
            if key not in fields:
                fields[key] = {"first": value, "last": value, "min": value, "max": value,
                               "observations": 1}
            else:
                entry = fields[key]
                entry.update(last=value, min=min(entry["min"], value), max=max(entry["max"], value))
                entry["observations"] += 1
    return fields


def counter_delta(first, last):
    first = {key: value for key, raw in first.items() if (value := number(raw)) is not None}
    last = {key: value for key, raw in last.items() if (value := number(raw)) is not None}
    return {"first": first, "last": last,
            "delta": {key: value - first[key] for key, value in last.items() if key in first},
            "missing_at_first": sorted(last.keys() - first.keys()),
            "missing_at_last": sorted(first.keys() - last.keys())}


def cpu_ticks(first, last):
    result = {}
    for cpu in sorted(first.keys() | last.keys()):
        initial, final = first.get(cpu), last.get(cpu)
        result[cpu] = {"first": initial, "last": final,
                       "delta": ([b - a for a, b in zip(initial, final)]
                                 if initial is not None and final is not None and
                                 len(initial) == len(final) else None)}
    return result


def pressure_summary(hosts, duration):
    result = {}
    names = {name for host in hosts for name in host.get("pressure", {})}
    for name in sorted(names):
        records = [host.get("pressure", {}).get(name, {}) for host in hosts]
        scopes = {scope for record in records for scope in record}
        result[name] = {}
        for scope in sorted(scopes):
            values = extrema(record.get(scope, {}) for record in records)
            if "total" in values:
                total = values["total"]
                delta = total["last"] - total["first"]
                values["total_delta_microseconds"] = delta
                values["stall_percent_of_sample_interval"] = (
                    100 * delta / 1e6 / duration if duration > 0 else None)
            result[name][scope] = values
    return result


def compact_samples(run, metadata):
    samples = run.get("samples", [])
    if not samples:
        return {"samples_available": False, "sample_count": 0}
    if any("during_training" in sample for sample in samples):
        flag, scope = "during_training", "training"
    elif any("during_execution" in sample for sample in samples):
        flag, scope = "during_execution", "execution"
    else:
        flag, scope = None, "process_lifetime"
    selected = [sample for sample in samples if flag is None or sample.get(flag)]
    if not selected:
        selected, scope = samples, "process_lifetime_fallback"
    first, last = selected[0], selected[-1]
    duration = last["monotonic"] - first["monotonic"]
    hosts = [sample.get("host", {}) for sample in selected]
    initial_host, final_host = hosts[0], hosts[-1]
    process_records = [sample["process"] for sample in selected if sample.get("process")]
    io_records = [sample["io"] for sample in selected if sample.get("io")]
    temperatures = extrema(host.get("temperature_millicelsius", {}) for host in hosts)
    for sensor, values in temperatures.items():
        values["label"] = metadata.get("temperature_labels", {}).get(sensor) or None
    frequencies = extrema(host.get("frequency_khz", {}) for host in hosts)
    for sensor, values in frequencies.items():
        values["label"] = Path(sensor).parent.name
    result = {
        "samples_available": True, "sample_count": len(samples),
        "selected_sample_count": len(selected), "scope": scope,
        "first_monotonic": first["monotonic"], "last_monotonic": last["monotonic"],
        "sample_interval_seconds": duration,
        "clock_ticks_per_second": metadata.get("clock_ticks_per_second"),
        "cpu_tick_fields": ["user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal",
                            "guest", "guest_nice"],
        "host_cpu_ticks": cpu_ticks(initial_host.get("cpu_ticks", {}), final_host.get("cpu_ticks", {})),
        "frequency_khz": frequencies, "temperature_millicelsius": temperatures,
        "process_memory_kib": extrema(
            {key: value for key, value in sample.get("status", {}).items()
             if key.startswith(("Vm", "Rss"))} for sample in selected),
        "process_thread_count": extrema(
            {"threads": len(sample.get("threads", {}))} for sample in selected).get("threads"),
        "host_memory_kib": extrema(
            {key: value for key, value in host.get("memory_kib", {}).items()
             if not key.startswith("HugePages_")} for host in hosts),
        "host_hugepage_counts": extrema(
            {key: value for key, value in host.get("memory_kib", {}).items()
             if key.startswith("HugePages_")} for host in hosts),
        "host_vmstat": counter_delta(initial_host.get("vmstat", {}), final_host.get("vmstat", {})),
        "host_pressure": pressure_summary(hosts, duration),
        "host_load_average": {"first": initial_host.get("load_average"), "last": final_host.get("load_average")},
    }
    if process_records:
        counters = ("user_ticks", "system_ticks", "minor_faults", "major_faults")
        result["process_stat"] = counter_delta(
            {key: value for key, value in process_records[0].items() if key in counters},
            {key: value for key, value in process_records[-1].items() if key in counters})
    if io_records:
        result["process_io"] = counter_delta(io_records[0], io_records[-1])
    cgroups = [host["cgroup"] for host in hosts if host.get("cgroup")]
    if cgroups:
        result["cgroup"] = {
            "cpu": counter_delta(cgroups[0].get("cpu", {}), cgroups[-1].get("cpu", {})),
            "memory_events": counter_delta(cgroups[0].get("memory_events", {}),
                                           cgroups[-1].get("memory_events", {})),
            "memory_bytes": extrema({key: group.get(key) for key in
                                     ("memory_current_bytes", "swap_current_bytes")} for group in cgroups),
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    inputs = [path.resolve() for path in args.inputs]
    output = args.output.resolve()
    if output in inputs:
        parser.error("The output must differ from every input file.")
    if len(set(inputs)) != len(inputs):
        parser.error("Input files must be unique.")
    result = {
        "schema": "mus.training-benchmarks.compact/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "measurement": {
            "runs": "Preserve all original per-run fields except the sample timeline. Existing per-thread CPU summaries remain in resources.threads.",
            "samples": "Use samples marked as training or execution. If none exist, use the process lifetime and identify the fallback.",
            "cpu": "Preserve initial, final, and delta ticks for every observed processor and the aggregate cpu entry. Guest ticks are already included in user/nice ticks.",
            "sensors": "Retain extrema and endpoint values for each frequency policy and temperature sensor. A null temperature label means the source did not record a label.",
            "counters": "Counter deltas cover the sampled interval. Preserve exact process counters separately in the original run fields.",
            "missing": "Do not infer resources that were not recorded. Legacy pilot runs can lack samples, binary hashes, or per-thread CPU summaries.",
        },
        "sources": [], "runs": [],
    }
    for source_index, path in enumerate(inputs):
        try:
            with path.open("rb") as stream:
                # The same open file supplies hash and JSON, even when a live
                # benchmark atomically replaces its output path during this read.
                size = os.fstat(stream.fileno()).st_size
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
                stream.seek(0)
                document = json.load(stream)
            if isinstance(document, list):
                runs, header = document, {}
            elif isinstance(document, dict) and isinstance(document.get("runs"), list):
                runs = document["runs"]
                header = {key: value for key, value in document.items() if key != "runs"}
            else:
                raise ValueError("Expected a benchmark object with runs or a legacy run list.")
            result["sources"].append({"path": str(path), "sha256": digest, "bytes": size,
                                      "run_count": len(runs), "document": header})
            metadata = header.get("metadata", {})
            for run in runs:
                if not isinstance(run, dict):
                    raise ValueError("Every run must be a JSON object.")
                compact = {key: value for key, value in run.items() if key != "samples"}
                compact["source_index"] = source_index
                compact["sample_resources"] = compact_samples(run, metadata)
                result["runs"].append(compact)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            parser.error(f"Cannot summarize {path}: {exc}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=output.parent, prefix=output.name + ".",
                                         suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(result, stream, indent=2)
            stream.write("\n")
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(json.dumps({"output": str(output), "source_count": len(inputs),
                      "run_count": len(result["runs"]), "bytes": output.stat().st_size}))


if __name__ == "__main__":
    main()
