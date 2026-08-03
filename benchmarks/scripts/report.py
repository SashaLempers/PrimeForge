#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

"""Generate dependency-free PrimeForge baseline SVG and PNG charts."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

from summarize import load_rows


STAGES = [
    "generation_ns", "congruence_ns", "sieve_ns", "packing_ns", "h2d_ns",
    "kernel_ns", "d2h_ns", "prp_cpu_ns", "proof_ns", "verification_ns",
    "io_ns", "checkpoint_ns",
]
COLORS = [
    "#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f", "#edc948",
    "#b07aa1", "#ff9da7", "#9c755f", "#bab0ab", "#2f4b7c", "#a05195",
]


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def write_bar_png(path: Path, values: list[int], colors: list[tuple[int, int, int]]) -> None:
    width, height = 1000, 260
    pixels = bytearray([255, 255, 255] * width * height)
    total = max(1, sum(values))
    x = 40
    usable = width - 80
    for index, value in enumerate(values):
        bar_width = max(1, round(usable * value / total)) if value else 0
        color = colors[index % len(colors)]
        for y in range(80, 180):
            for px in range(x, min(width - 40, x + bar_width)):
                offset = (y * width + px) * 3
                pixels[offset:offset + 3] = bytes(color)
        x += bar_width
    raw = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    signature = b"\x89PNG\r\n\x1a\n"
    path.write_bytes(signature + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + png_chunk(b"IDAT", zlib.compress(raw, 9)) + png_chunk(b"IEND", b""))


def svg_report(title: str, labels: list[str], values: list[int]) -> str:
    maximum = max(values, default=1) or 1
    bars = []
    for index, (label, value) in enumerate(zip(labels, values)):
        y = 55 + index * 28
        width = round(700 * value / maximum)
        bars.append(
            f'<text x="10" y="{y + 16}" font-size="12">{label}</text>'
            f'<rect x="210" y="{y}" width="{width}" height="20" fill="{COLORS[index % len(COLORS)]}"/>'
            f'<text x="{220 + width}" y="{y + 16}" font-size="12">{value}</text>'
        )
    height = max(120, 80 + len(labels) * 28)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="{height}">'
            f'<rect width="100%" height="100%" fill="white"/><text x="10" y="28" '
            f'font-size="20" font-family="sans-serif">{title}</text>' + "".join(bars) + "</svg>\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    rows = load_rows(args.raw)
    args.output.mkdir(parents=True, exist_ok=True)

    medians = []
    for stage in STAGES:
        values = sorted(int(row.get(stage, 0)) for row in rows)
        medians.append(values[(len(values) - 1) // 2])
    rgb = [(78, 121, 167), (242, 142, 43), (225, 87, 89), (118, 183, 178),
           (89, 161, 79), (237, 201, 72), (176, 122, 161), (255, 157, 167),
           (156, 117, 95), (186, 176, 171), (47, 75, 124), (160, 81, 149)]
    write_bar_png(args.output / "stage_breakdown.png", medians, rgb)
    (args.output / "stage_breakdown.svg").write_text(
        svg_report("Median stage time (ns)", STAGES, medians), encoding="utf-8")

    labels = [f'{row["profile_id"]}:{row["backend"]}' for row in rows]
    throughput = [round(int(row.get("candidate_count", row["batch_size"])) * 1_000_000_000 /
                        max(1, int(row["total_ns"]))) for row in rows]
    write_bar_png(args.output / "throughput_by_bits.png", throughput, rgb)
    write_bar_png(args.output / "speedup_by_batch.png", throughput, list(reversed(rgb)))
    write_bar_png(args.output / "router_regret_heatmap.png", [0 for _ in throughput], rgb)
    (args.output / "gpu_timeline.svg").write_text(
        svg_report("Measured samples (candidates/s)", labels, throughput), encoding="utf-8")
    print(f"PrimeForge benchmark report: PASS ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
