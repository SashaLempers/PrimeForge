# OPTIMIZATION-08 evidence

This directory preserves the rejected bounded FLINT process-pool experiment
described in `docs/reports/OPTIMIZATION_08.md`.

- `inputs/` contains the exact 32,768-candidate workload.
- `flint-processes/` contains the randomized schedule, all 32 raw records,
  summaries, paired comparisons, deterministic hashes, environment capture and
  final decision.
- Repeated campaign directories, proof files, stdout/stderr logs and watchdog
  logs are intentionally excluded. Their logical outputs are covered by the
  five hashes recorded for every run in `determinism-hashes.csv`.

The experiment used candidate commit
`14fa5c29e83f9ca6ab32196b3da00665f1a61773`. The candidate remains available
in Git history, but its runtime parallelism is not retained because no variant
passed the preregistered performance gate. The exact harness is
`scripts/run_optimization08_flint_process_sweep.ps1` at that commit; its SHA-256
is also recorded in `environment.json`.

To reproduce from a clean detached worktree with the candidate's CUDA Release
targets already built:

```powershell
git switch --detach 14fa5c29e83f9ca6ab32196b3da00665f1a61773
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_optimization08_flint_process_sweep.ps1 -Repetitions 7 -Warmups 1 -Seed 20260804
```

Verify every versioned evidence byte from PowerShell:

```powershell
$root = 'benchmarks\baselines\optimization-08'
Get-Content "$root\SHA256SUMS" | ForEach-Object {
    if ($_ -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed line: $_" }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root $Matches[2])).Hash.ToLowerInvariant()
    if ($actual -ne $Matches[1]) { throw "Hash mismatch: $($Matches[2])" }
}
```
