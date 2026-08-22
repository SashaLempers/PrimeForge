#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def median(values: Iterable[float]) -> float:
    return float(statistics.median(list(values)))


def metric(summary: dict[str, Any], name: str, statistic: str) -> float | None:
    value = summary["telemetry"]["metrics"].get(name, "UNKNOWN")
    if value == "UNKNOWN":
        return None
    return float(value[statistic])


def result_tuple(line: str) -> tuple[int, int, int, str, str]:
    fields = line.split("\t")
    if len(fields) != 7 or fields[0] != "PRIMEFORGE_NATIVE_BATCH_RESULT":
        raise ValueError(f"malformed result record: {line}")
    return int(fields[2]), int(fields[3]), int(fields[4]), fields[5], fields[6]


def write_tsv(path: Path, fieldnames: list[str], rows: list[dict[str, Any]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, delimiter="\t", fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({name: row.get(name, "UNKNOWN") for name in fieldnames})


def parse_profile_stdout(path: Path, digits: int, batch: int, wall_seconds: float) -> dict[str, Any]:
    groups = {
        "forward_ntt": {"launches": 0, "nanoseconds": 0},
        "inverse_ntt": {"launches": 0, "nanoseconds": 0},
        "pointwise_square": {"launches": 0, "nanoseconds": 0},
        "reduction": {"launches": 0, "nanoseconds": 0},
        "poly2int": {"launches": 0, "nanoseconds": 0},
        "other": {"launches": 0, "nanoseconds": 0},
    }
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("PRIMEFORGE_KERNEL\t"):
            continue
        fields = line.split("\t")
        name, launches, nanoseconds = fields[4], int(fields[5]), int(fields[6])
        if name.startswith(("lst_intt", "intt")):
            group = "inverse_ntt"
        elif name.startswith(("sub_ntt", "ntt")):
            group = "forward_ntt"
        elif name.startswith("square"):
            group = "pointwise_square"
        elif name.startswith("poly2int"):
            group = "poly2int"
        elif name.startswith("reduce_"):
            group = "reduction"
        else:
            group = "other"
        groups[group]["launches"] += launches
        groups[group]["nanoseconds"] += nanoseconds
    total_ns = sum(group["nanoseconds"] for group in groups.values())
    return {
        "run_id": "100k-b8-profiled-manual",
        "digits": digits,
        "batch": batch,
        "wall_seconds": wall_seconds,
        "total_launches": sum(group["launches"] for group in groups.values()),
        "total_event_nanoseconds": total_ns,
        "groups": {
            name: {
                **value,
                "seconds": value["nanoseconds"] / 1.0e9,
                "percent": 100.0 * value["nanoseconds"] / total_ns,
            }
            for name, value in groups.items()
        },
        "source": str(path),
    }


def profile_from_summary(summary: dict[str, Any]) -> dict[str, Any]:
    groups = {}
    for name, value in summary["kernel_profile"]["groups"].items():
        groups[name] = {
            "launches": int(value["launches"]),
            "nanoseconds": int(value["nanoseconds"]),
            "seconds": float(value["seconds"]),
            "percent": float(value["percent_of_profiled_event_time"]),
        }
    return {
        "run_id": summary["run_id"],
        "digits": int(summary["digits"]),
        "batch": int(summary["batch"]),
        "wall_seconds": float(summary["wall_seconds"]),
        "total_launches": int(summary["kernel_profile"]["total_launches"]),
        "total_event_nanoseconds": int(summary["kernel_profile"]["total_event_nanoseconds"]),
        "groups": groups,
        "source": summary["run_id"] + ".summary.json",
    }


def parse_campaign_phases(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        schema = stream.readline().rstrip("\n")
        if schema != "#schema\tprimeforge.campaign.telemetry.v1":
            raise ValueError("unexpected campaign telemetry schema")
        reader = csv.DictReader(stream, delimiter="\t")
        phases = []
        for row in reader:
            if row["record_type"] != "BATCH_PHASES" or row["scope"] != "BATCH_SHARED":
                continue
            phases.append(
                {
                    "batch_id": int(row["batch_id"]),
                    "batch_size": int(row["batch_size"]),
                    "result_persist_us": int(row["result_persist_us"]),
                    "checkpoint_us": int(row["checkpoint_us"]),
                    "batch_wall_us": int(row["batch_wall_us"]),
                }
            )
    for row in phases:
        row["completed_candidates"] = (row["batch_id"] + 1) * row["batch_size"]

    def slope(name: str) -> float:
        xs = [row["completed_candidates"] for row in phases]
        ys = [row[name] for row in phases]
        x_mean, y_mean = statistics.mean(xs), statistics.mean(ys)
        denominator = sum((x - x_mean) ** 2 for x in xs)
        return sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys)) / denominator

    return phases, {
        "batches": len(phases),
        "first": phases[0],
        "last": phases[-1],
        "result_persist_slope_us_per_completed_candidate": slope("result_persist_us"),
        "checkpoint_slope_us_per_completed_candidate": slope("checkpoint_us"),
    }


def cumulative_completed(count: int, batch: int) -> tuple[int, int, float]:
    completed = 0
    cumulative_rows = 0
    sort_work = 0.0
    batches = 0
    while completed < count:
        completed += min(batch, count - completed)
        remaining = count - completed
        cumulative_rows += completed
        if completed > 1:
            sort_work += completed * math.log2(completed)
        if remaining > 1:
            sort_work += remaining * math.log2(remaining)
        batches += 1
    return batches, cumulative_rows, sort_work


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile-100k", type=Path, required=True)
    parser.add_argument("--profile-100k-wall", type=float, required=True)
    parser.add_argument("--corpus-manifest", type=Path, required=True)
    parser.add_argument("--campaign-telemetry", type=Path, required=True)
    parser.add_argument("--observed-result-file", action="append", type=Path, default=[])
    parser.add_argument("--observed-log-directory", type=Path, required=True)
    parser.add_argument("--observed-work-directory", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    summary_paths = sorted(args.runs.glob("*.summary.json"))
    summaries = [json.loads(path.read_text(encoding="utf-8")) for path in summary_paths]
    if not summaries:
        raise ValueError("no scaling summaries found")

    verdicts: dict[tuple[int, int], tuple[int, str, str]] = {}
    exact = True
    for summary in summaries:
        for record in summary["result_records"]:
            k, n, witness, classification, res64 = result_tuple(record)
            value = (witness, classification, res64)
            if (k, n) in verdicts and verdicts[(k, n)] != value:
                exact = False
            verdicts[(k, n)] = value

    run_rows = []
    for summary in summaries:
        telemetry = summary["telemetry"]
        reasons = ";".join(telemetry["throttling_reasons"]) or "NONE"
        run_rows.append(
            {
                "run_id": summary["run_id"],
                "kind": summary["run_kind"],
                "digits": summary["digits"],
                "batch": summary["batch"],
                "transform": summary["transform_length"],
                "plan": summary["plan"],
                "wall_seconds": f'{summary["wall_seconds"]:.6f}',
                "candidates_per_hour": f'{summary["candidates_per_hour"]:.6f}',
                "main_ntt_seconds": f'{float(summary["native_timing"]["main_ntt_gpu_us"]) / 1e6:.6f}',
                "main_ntt_wall_percent": f'{100.0 * float(summary["native_timing"]["main_ntt_gpu_us"]) / 1e6 / summary["wall_seconds"]:.6f}',
                "cpu_core_equivalents": f'{summary["process"]["mean_cpu_core_equivalents"]:.6f}',
                "process_working_set_peak_mib": f'{float(summary["process"]["peak_working_set_bytes"]) / 1048576.0:.6f}',
                "process_private_peak_mib": f'{float(summary["process"]["peak_private_bytes"]) / 1048576.0:.6f}',
                "opencl_buffer_mib_derived": f'{summary["exact_opencl_buffer_bytes_derived"] / 1048576.0:.6f}',
                "vram_peak_mib": metric(summary, "vram_used_mib", "maximum"),
                "gpu_util_mean_percent": metric(summary, "gpu_utilization_percent", "mean"),
                "gpu_memory_controller_mean_percent": metric(summary, "gpu_memory_utilization_percent", "mean"),
                "gpu_power_mean_w": metric(summary, "gpu_power_w", "mean"),
                "gpu_temperature_max_c": metric(summary, "gpu_temperature_c", "maximum"),
                "cpu_temperature_max_c": metric(summary, "cpu_temperature_c", "maximum"),
                "power_limit_samples": telemetry["throttling_samples"],
                "throttling_reasons": reasons,
                "whea_max": telemetry["maximum_recent_whea_errors"],
                "gerbicz": summary["gerbicz"],
                "result_sha256": summary["result_sha256"],
            }
        )

    measured = [summary for summary in summaries if summary["run_kind"] == "MEASURED"]
    grouped: dict[tuple[int, int], list[dict[str, Any]]] = defaultdict(list)
    for summary in measured:
        grouped[(int(summary["digits"]), int(summary["batch"]))].append(summary)
    aggregate_rows = []
    for (digits, batch), rows in sorted(grouped.items()):
        walls = [float(row["wall_seconds"]) for row in rows]
        throughputs = [float(row["candidates_per_hour"]) for row in rows]
        wall_median = median(walls)
        aggregate_rows.append(
            {
                "digits": digits,
                "batch": batch,
                "transform": rows[0]["transform_length"],
                "runs": len(rows),
                "wall_median_seconds": f"{wall_median:.6f}",
                "wall_min_seconds": f"{min(walls):.6f}",
                "wall_max_seconds": f"{max(walls):.6f}",
                "wall_range_percent_of_median": "UNKNOWN" if len(rows) == 1 else f"{100.0 * (max(walls) - min(walls)) / wall_median:.6f}",
                "candidates_per_hour_median": f"{median(throughputs):.6f}",
                "plans": ";".join(sorted({row["plan"] for row in rows})),
                "main_ntt_seconds_median": f'{median(float(row["native_timing"]["main_ntt_gpu_us"]) / 1e6 for row in rows):.6f}',
                "cpu_core_equivalents_median": f'{median(float(row["process"]["mean_cpu_core_equivalents"]) for row in rows):.6f}',
                "process_working_set_peak_mib": f'{max(float(row["process"]["peak_working_set_bytes"]) for row in rows) / 1048576.0:.6f}',
                "process_private_peak_mib": f'{max(float(row["process"]["peak_private_bytes"]) for row in rows) / 1048576.0:.6f}',
                "opencl_buffer_mib_derived": f'{rows[0]["exact_opencl_buffer_bytes_derived"] / 1048576.0:.6f}',
                "vram_peak_mib": max(metric(row, "vram_used_mib", "maximum") or 0.0 for row in rows),
                "gpu_util_mean_percent_median": f'{median(metric(row, "gpu_utilization_percent", "mean") or 0.0 for row in rows):.6f}',
                "gpu_memory_controller_mean_percent_median": f'{median(metric(row, "gpu_memory_utilization_percent", "mean") or 0.0 for row in rows):.6f}',
                "gpu_power_mean_w_median": f'{median(metric(row, "gpu_power_w", "mean") or 0.0 for row in rows):.6f}',
                "gpu_temperature_max_c": max(metric(row, "gpu_temperature_c", "maximum") or 0.0 for row in rows),
                "cpu_temperature_max_c": max(metric(row, "cpu_temperature_c", "maximum") or 0.0 for row in rows),
                "power_limit_samples": sum(int(row["telemetry"]["throttling_samples"]) for row in rows),
                "whea_max": max(int(row["telemetry"]["maximum_recent_whea_errors"]) for row in rows),
            }
        )

    best_rows = []
    for digits in sorted({int(row["digits"]) for row in aggregate_rows}):
        choices = [row for row in aggregate_rows if int(row["digits"]) == digits]
        peak = max(float(row["candidates_per_hour_median"]) for row in choices)
        within_one_percent = [row for row in choices if float(row["candidates_per_hour_median"]) >= peak * 0.99]
        selected = min(within_one_percent, key=lambda row: int(row["batch"]))
        best_rows.append({**selected, "selection_rule": "LOWEST_BATCH_WITHIN_1_PERCENT_OF_PEAK"})

    host_phase_rows = []
    phase_units = (
        ("parameter_build", "parameter_build_us"),
        ("host_to_device", "host_to_device_us"),
        ("witness_selection", "witness_selection_us"),
        ("a_pow_k_gpu", "a_pow_k_gpu_us"),
        ("main_ntt_gpu", "main_ntt_gpu_us"),
        ("gerbicz_gpu", "gerbicz_gpu_us"),
        ("final_reduce_gpu", "final_reduce_gpu_us"),
        ("device_to_host", "device_to_host_us"),
        ("worker_total", "worker_total_us"),
    )
    for best in best_rows:
        rows = grouped[(int(best["digits"]), int(best["batch"]))]
        wall_median = median(float(row["wall_seconds"]) for row in rows)
        for phase, key in phase_units:
            seconds = median(float(row["native_timing"][key]) / 1.0e6 for row in rows)
            host_phase_rows.append(
                {
                    "digits": best["digits"],
                    "batch": best["batch"],
                    "transform": best["transform"],
                    "phase": phase,
                    "seconds_median": f"{seconds:.9f}",
                    "percent_of_wall_median": f"{100.0 * seconds / wall_median:.6f}",
                    "runs": len(rows),
                    "classification": "MEASURED_NATIVE_AGGREGATE",
                }
            )

    profiles = [parse_profile_stdout(args.profile_100k, 100000, 8, args.profile_100k_wall)]
    profiles.extend(profile_from_summary(summary) for summary in summaries if summary["run_kind"] == "PROFILED")
    profiles.sort(key=lambda profile: profile["digits"])
    profile_rows = []
    for profile in profiles:
        for name, value in profile["groups"].items():
            profile_rows.append(
                {
                    "digits": profile["digits"],
                    "batch": profile["batch"],
                    "transform": next(int(row["transform"]) for row in run_rows if int(row["digits"]) == profile["digits"]),
                    "group": name,
                    "launches": value["launches"],
                    "event_seconds": f'{value["seconds"]:.9f}',
                    "event_percent": f'{value["percent"]:.6f}',
                    "profile_wall_seconds": f'{profile["wall_seconds"]:.6f}',
                    "source": profile["source"],
                    "measurement_class": "MEASURED_PROFILED_WITH_OVERHEAD",
                }
            )

    exponent_rows = []
    by_phase: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in profile_rows:
        by_phase[row["group"]].append(row)
    for phase, rows in sorted(by_phase.items()):
        rows.sort(key=lambda row: int(row["digits"]))
        for left, right in zip(rows, rows[1:]):
            alpha = math.log(float(right["event_seconds"]) / float(left["event_seconds"])) / math.log(
                int(right["digits"]) / int(left["digits"])
            )
            exponent_rows.append(
                {
                    "phase": phase,
                    "digit_span": f'{left["digits"]}-{right["digits"]}',
                    "transform_span": f'{left["transform"]}-{right["transform"]}',
                    "alpha_vs_digits": f"{alpha:.6f}",
                    "classification": "DERIVED_PROFILED_EVENTS",
                }
            )

    campaign_phases, persistence = parse_campaign_phases(args.campaign_telemetry)
    sampled_campaign_rows = [
        row for index, row in enumerate(campaign_phases) if index == 0 or index == len(campaign_phases) - 1 or index % 25 == 0
    ]

    observed_bytes = 0
    observed_rows = 0
    for path in args.observed_result_file:
        lines = sum(1 for _ in path.open("r", encoding="utf-8"))
        observed_bytes += path.stat().st_size
        observed_rows += max(0, lines - 1)
    result_bytes_per_row = observed_bytes / observed_rows
    log_bytes = sum(path.stat().st_size for path in args.observed_log_directory.glob("*") if path.is_file())
    work_files = [path for path in args.observed_work_directory.glob("*") if path.is_file()]
    attempt_count = len(work_files)
    work_bytes = sum(path.stat().st_size for path in work_files)
    artifact_bytes_per_batch = (log_bytes + work_bytes) / attempt_count

    volume_rows = []
    for candidates in (10_000, 100_000, 1_000_000):
        for batch in (8, 16, 32):
            batches, cumulative_rows, sort_work = cumulative_completed(candidates, batch)
            volume_rows.append(
                {
                    "survivors": candidates,
                    "batch": batch,
                    "batches": batches,
                    "final_results_mib": f"{(result_bytes_per_row * candidates) / 1048576.0:.6f}",
                    "cumulative_atomic_result_rewrite_gib": f"{(result_bytes_per_row * cumulative_rows) / (1024.0 ** 3):.6f}",
                    "candidate_set_hash_input_gib": f"{(17.0 * candidates * batches) / (1024.0 ** 3):.6f}",
                    "candidate_sort_nlogn_units": f"{sort_work:.6f}",
                    "log_and_pending_artifacts_mib": f"{(artifact_bytes_per_batch * batches) / 1048576.0:.6f}",
                    "artifact_file_count_without_retries": 3 * batches,
                    "classification": "DERIVED_FROM_MEASURED_ROW_AND_ARTIFACT_SIZES_PLUS_CODE_PATH",
                }
            )

    max_cpu_temp = max(metric(summary, "cpu_temperature_c", "maximum") or 0.0 for summary in summaries)
    max_gpu_temp = max(metric(summary, "gpu_temperature_c", "maximum") or 0.0 for summary in summaries)
    non_power_throttling = sorted(
        {
            reason
            for summary in summaries
            for reason in summary["telemetry"]["throttling_reasons"]
            if reason != "SW_POWER_CAP"
        }
    )
    validation = {
        "schema": "primeforge.native_scaling_study.validation.v1",
        "summary_files": len(summaries),
        "result_records_consistent_across_overlapping_batches": exact,
        "all_exit_zero": all(summary["exit_code"] == 0 and not summary["timed_out"] for summary in summaries),
        "all_gerbicz_pass": all(summary["gerbicz"] == "PASS" for summary in summaries),
        "all_whea_zero": all(summary["telemetry"]["maximum_recent_whea_errors"] == 0 for summary in summaries),
        "non_power_throttling_reasons": non_power_throttling,
        "sw_power_cap_is_reported_not_treated_as_thermal_error": True,
        "maximum_cpu_temperature_c": max_cpu_temp,
        "maximum_gpu_temperature_c": max_gpu_temp,
        "temperature_gate": "PASS" if max_cpu_temp < 92.0 and max_gpu_temp < 85.0 else "FAIL",
        "ncu_opencl_kernel_profile": "UNAVAILABLE_NO_KERNELS_CAPTURED",
        "status": "PASS" if exact and not non_power_throttling and max_cpu_temp < 92.0 and max_gpu_temp < 85.0 else "FAIL",
    }

    analysis = {
        "schema": "primeforge.native_scaling_study.analysis.v1",
        "measurement_date": "2026-08-22",
        "engine_commit": "54ffb095a81e75d414b328c60ff48eddce977f1b",
        "executable_sha256": summaries[0]["executable_sha256"],
        "best_batch_rows": best_rows,
        "host_phase_rows": host_phase_rows,
        "profile_rows": profile_rows,
        "persistence_observation": persistence,
        "result_bytes_per_row_observed": result_bytes_per_row,
        "artifact_bytes_per_batch_observed": artifact_bytes_per_batch,
        "validation": validation,
        "limitations": [
            "500K_B32_NOT_RUN_INFORMATION_PER_GPU_SECOND_STOP_RULE",
            "500K_KERNEL_SUBPHASES_UNKNOWN_NO_BOUNDED_LOW_OVERHEAD_PROVIDER",
            "NSIGHT_COMPUTE_DID_NOT_CAPTURE_NVIDIA_OPENCL_KERNELS",
            "SM_OCCUPANCY_UNKNOWN",
            "EXACT_MEMORY_BANDWIDTH_UNKNOWN",
            "STALL_REASONS_UNKNOWN",
            "GPU_CPU_WAIT_SPLIT_DERIVED_ONLY_FROM_WALL_AND_NATIVE_TIMING",
            "SINGLE_RUN_VARIANCE_UNKNOWN_FOR_NON_CROSSOVER_BATCHES",
        ],
    }

    write_tsv(args.output / "runs.tsv", list(run_rows[0].keys()), run_rows)
    write_tsv(args.output / "aggregates.tsv", list(aggregate_rows[0].keys()), aggregate_rows)
    write_tsv(args.output / "best-batches.tsv", list(best_rows[0].keys()), best_rows)
    write_tsv(args.output / "host-phases.tsv", list(host_phase_rows[0].keys()), host_phase_rows)
    write_tsv(args.output / "kernel-profiles.tsv", list(profile_rows[0].keys()), profile_rows)
    write_tsv(args.output / "scaling-exponents.tsv", list(exponent_rows[0].keys()), exponent_rows)
    write_tsv(args.output / "campaign-persistence-samples.tsv", list(sampled_campaign_rows[0].keys()), sampled_campaign_rows)
    write_tsv(args.output / "campaign-volume-projection.tsv", list(volume_rows[0].keys()), volume_rows)
    (args.output / "validation.json").write_text(json.dumps(validation, indent=2) + "\n", encoding="utf-8")
    (args.output / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n", encoding="utf-8")
    (args.output / "corpus-manifest.json").write_bytes(args.corpus_manifest.read_bytes())

    inputs = summary_paths + [
        args.corpus_manifest,
        args.profile_100k,
        args.campaign_telemetry,
        *args.observed_result_file,
    ]
    hashes = [f"{sha256(path)}  {path.as_posix()}" for path in inputs]
    (args.output / "SHA256SUMS").write_text("\n".join(hashes) + "\n", encoding="utf-8")

    print(f"scaling_study.summaries={len(summaries)}")
    print(f"scaling_study.candidates_checked={len(verdicts)}")
    print(f"scaling_study.best_batches={','.join(f'{row['digits']}:B{row['batch']}' for row in best_rows)}")
    print(f"scaling_study.max_cpu_temperature_c={max_cpu_temp:.3f}")
    print(f"scaling_study.max_gpu_temperature_c={max_gpu_temp:.3f}")
    print(f"scaling_study.output={args.output}")
    print(f"scaling_study.status={validation['status']}")
    return 0 if validation["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
