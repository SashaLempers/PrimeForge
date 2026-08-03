#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/out/build/gcc-release"
output="${1:-$root/out/benchmarks/commit-a-linux}"

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target primeforge-bench --parallel

rm -rf "$output"
mkdir -p "$output/raw/prp-cpu"
"$build/primeforge-bench" run \
  --profile "$root/benchmarks/profiles/s64_prp_65536.json" \
  --backend cpu \
  --output "$output/raw/prp-cpu" \
  --warmup 3 \
  --repetitions 7

python3 "$root/benchmarks/scripts/summarize.py" \
  --input "$output/raw" \
  --output "$output/summary" \
  --bootstrap-samples 10000
python3 "$root/benchmarks/scripts/report.py" \
  --raw "$output/raw" \
  --output "$output/report"

printf 'PrimeForge commit-A Linux CPU baseline: PASS (%s)\n' "$output"
