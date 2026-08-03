# OPTIMIZATION-07 evidence

This directory is the immutable, reviewable evidence set for
`docs/reports/OPTIMIZATION_07.md`.

- `inputs/` contains the exact hardware and workload descriptions.
- `batch/` contains the corrected 210-row randomized CPU/CUDA/auto sweep.
- `proof-workers/` contains the 80 campaigns used to select four proof workers.
- `rejected/cold-auto/` preserves the invalid cold-context sweep as negative evidence.
- `profiles/` contains the before/after Nsight Systems reports and exported summaries.
- `report/` contains charts generated only from the versioned CSV values.
- `validation.json` records the final build, test, sanitizer and campaign gates.

The engine optimization was introduced at `d06baaf`. The valid PRP batch sweep
was recorded at `0d51552`, and the proof-worker sweep at `55cba65`. Later
commits through `c8ae6cb` changed only the benchmark harness and invariant CSV
serialization, not the measured PrimeForge engine. The proof-worker JSONL is
the authoritative record. Its CSV representation and the two statistical CSV
summaries were regenerated with invariant decimal points at `c8ae6cb`.

Five proof-worker campaigns, one per variant, received full independent
verification. The remaining 75 campaigns have byte-identical results,
checkpoint, manifest, coverage and configuration hashes. All 400 artifact
hashes were recalculated during the evidence audit.

Verify every versioned byte from this directory in PowerShell:

```powershell
$root = 'benchmarks\baselines\optimization-07'
Get-Content "$root\SHA256SUMS" | ForEach-Object {
    if ($_ -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed line: $_" }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root $Matches[2])).Hash.ToLowerInvariant()
    if ($actual -ne $Matches[1]) { throw "Hash mismatch: $($Matches[2])" }
}
```

Open either `.nsys-rep` file with NVIDIA Nsight Systems, or regenerate a text
summary with `nsys stats`. No SQLite export is required to preserve the source
profile.
