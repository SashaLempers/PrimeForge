#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Summarize PrimeForge raw JSONL without third-party Python packages."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
from pathlib import Path


def load_rows(root: Path) -> list[dict]:
    paths = [root] if root.is_file() else sorted(root.rglob("*.jsonl"))
    rows: list[dict] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                row = json.loads(line)
                if row.get("schema") != "primeforge.benchmark.raw.v1":
                    raise ValueError(f"{path}:{line_number}: unsupported schema")
                if row.get("valid_measurement") is True:
                    rows.append(row)
    if not rows:
        raise ValueError("no valid benchmark rows found")
    return rows


def nearest_rank(values: list[int], percentile: float) -> int:
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def bootstrap_median_interval(values: list[int], samples: int, seed: int) -> tuple[int, int]:
    generator = random.Random(seed)
    medians = []
    for _ in range(samples):
        medians.append(int(statistics.median(generator.choices(values, k=len(values)))))
    return nearest_rank(medians, 0.025), nearest_rank(medians, 0.975)


def summarize(values: list[int], bootstrap_samples: int) -> dict:
    median = int(statistics.median(values))
    deviations = [abs(value - median) for value in values]
    mean = statistics.fmean(values)
    standard_deviation = statistics.pstdev(values)
    low, high = bootstrap_median_interval(values, bootstrap_samples, 0x5052494D45464F52)
    return {
        "repetitions": len(values),
        "minimum_ns": min(values),
        "maximum_ns": max(values),
        "median_ns": median,
        "mad_ns": int(statistics.median(deviations)),
        "p5_ns": nearest_rank(values, 0.05),
        "p95_ns": nearest_rank(values, 0.95),
        "mean_ns": round(mean, 3),
        "coefficient_of_variation": round(standard_deviation / mean, 6) if mean else 0,
        "bootstrap_median_low_ns": low,
        "bootstrap_median_high_ns": high,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--bootstrap-samples", type=int, default=10_000)
    args = parser.parse_args()
    if args.bootstrap_samples < 100:
        parser.error("--bootstrap-samples must be at least 100")

    rows = load_rows(args.input)
    groups: dict[tuple[str, str, str, str, str, str, int, int, int], list[dict]] = {}
    for row in rows:
        batch_size = int(row["batch_size"])
        key = (row["commit_sha"], row["binary_sha256"], row["profile_sha256"],
               row["dataset_sha256"], row["profile_id"], row["backend"],
               int(row["bits"]), batch_size, int(row.get("candidate_count", batch_size)))
        groups.setdefault(key, []).append(row)

    summary_rows = []
    for (commit, binary_hash, profile_hash, dataset_hash, profile, backend, bits,
         batch_size, candidate_count), group in sorted(groups.items()):
        result_hashes = {row["result_sha256"] for row in group}
        if len(result_hashes) != 1:
            raise ValueError(f"result divergence in {profile}/{backend}")
        item = {
            "commit_sha": commit,
            "binary_sha256": binary_hash,
            "profile_sha256": profile_hash,
            "dataset_sha256": dataset_hash,
            "profile_id": profile,
            "backend": backend,
            "bits": bits,
            "batch_size": batch_size,
            "candidate_count": candidate_count,
            "result_sha256": next(iter(result_hashes)),
        }
        item.update(summarize([int(row["total_ns"]) for row in group], args.bootstrap_samples))
        summary_rows.append(item)

    args.output.mkdir(parents=True, exist_ok=True)
    fields = list(summary_rows[0])
    with (args.output / "summary.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summary_rows)
    (args.output / "summary.json").write_text(
        json.dumps({"schema": "primeforge.benchmark.summary.v1", "rows": summary_rows},
                   indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"PrimeForge benchmark summary: PASS ({len(rows)} rows, {len(groups)} groups)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
