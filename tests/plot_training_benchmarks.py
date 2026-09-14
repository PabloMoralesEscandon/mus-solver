#!/usr/bin/env python3
r"""Plot throughput and memory from a completed training benchmark JSON file.

Requires matplotlib. Each error bar shows the observed minimum and maximum,
not a confidence interval. Example:
  python3 tests/plot_training_benchmarks.py build/performance/final-comparison.json \
      --output docs/performance-mccfr-20260914/scaling.svg
"""

import argparse
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.input.read_text())
    rows = data["summary"]
    workloads = [work["name"] for work in data["configuration"]["workloads"]]
    labels = list(data["metadata"]["binaries"])
    expected = data["configuration"]["repeats"]
    if any(row["repeats"] != expected for row in rows):
        parser.error("Every group must contain all requested repetitions.")
    if len(rows) != len(workloads) * len(labels) * len(data["configuration"]["threads"]):
        parser.error("The benchmark does not contain every requested group.")
    # ru_maxrss can retain memory used by the launcher before exec. VmHWM
    # belongs to the new executable's memory image. Preserve the raw counters.
    for row in rows:
        peaks = []
        for run in data["runs"]:
            if (run["workload"], run["threads"], run["label"]) != (
                    row["workload"], row["threads"], row["label"]):
                continue
            values = [sample.get("status", {}).get("VmHWM", 0)
                      for sample in run.get("samples", []) if sample.get("during_training")]
            if not values or max(values) == 0:
                parser.error("Every run must contain a VmHWM measurement during training.")
            peaks.append(max(values))
        row.update(observed_hwm_kib_median=statistics.median(peaks),
                   observed_hwm_kib_min=min(peaks), observed_hwm_kib_max=max(peaks))

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({"font.size": 10, "svg.hashsalt": "mus-mccfr-scaling"})
    figure, axes = plt.subplots(len(workloads), 2, figsize=(10, 2.7 * len(workloads)),
                               squeeze=False, layout="constrained")
    colors = ["#747b86", "#087f68", "#a54a00", "#6361ae"]
    for index, workload in enumerate(workloads):
        fields = workload.split(":")
        title = "Órdago público" if fields[0] == "ordago-public" else f"Marcador {fields[1]}:{fields[2]}"
        for column, (metric, scale, ylabel) in enumerate([
                ("iterations_per_second", 1, "Iteraciones / segundo"),
                ("observed_hwm_kib", 1 / 1048576, "VmHWM observado (GiB)")]):
            axis = axes[index][column]
            for label_index, label in enumerate(labels):
                group = sorted((row for row in rows if row["workload"] == workload and
                                row["label"] == label), key=lambda row: row["threads"])
                medians = [row[metric + "_median"] * scale for row in group]
                errors = [[(row[metric + "_median"] - row[metric + "_min"]) * scale for row in group],
                          [(row[metric + "_max"] - row[metric + "_median"]) * scale for row in group]]
                axis.errorbar([row["threads"] for row in group], medians, yerr=errors,
                              marker="o", capsize=3, linewidth=1.8,
                              linestyle="--" if label_index == 0 else "-",
                              color=colors[label_index % len(colors)], label=label)
            axis.set(title=title, xlabel="Hilos", ylabel=ylabel)
            axis.set_xticks(data["configuration"]["threads"])
            axis.set_ylim(bottom=0)
            axis.grid(axis="y", alpha=0.25)
            axis.spines[["top", "right"]].set_visible(False)
            axis.legend(frameon=False)
    figure.suptitle(f"MCCFR: mediana y rango de {expected} repeticiones\n"
                   "Misma carga y semilla; los recorridos paralelos pueden variar", fontsize=13)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(args.output, metadata={"Date": None} if args.output.suffix == ".svg" else None)
    plt.close(figure)


if __name__ == "__main__":
    main()
