# PIVOT-09 Proth reference-comparison protocol

The comparison uses the committed 16-case dataset
`benchmarks/pivot09/proth_u64_reference.tsv`: eight proven primes and eight
composites, all with `n=32`, all accepted by the bounded PrimeForge engine and
the pinned proth20 0.9.1 oracle.

The objective is exact agreement and reproducible workflow evidence. It is not a
kernel benchmark and cannot establish that either implementation is generally
faster.

## Controls

- Release `primeforge-proth.exe` and local `proth20.exe` are each launched as a
  fresh process once per candidate;
- inputs and required final classification/proof level are identical;
- one prime and one composite per engine are warmed up before measurement;
- seven repetitions use deterministic separately seeded random order;
- process startup is inside the timed interval for both engines;
- every stdout, stderr, exit code, verdict and integer elapsed nanoseconds value
  is retained;
- one hardware-monitor sample is captured before and after each repetition;
- executable, dataset, output and commit hashes are retained;
- a single-candidate invocation has no partial checkpoint, so checkpoint policy
  is equivalently `NOT_APPLICABLE_SINGLE_CANDIDATE_INVOCATION` for both engines;
- the parser accepts only explicit proth20 prime, composite or divisible-by
  sentences. Any other output is `UNKNOWN` and fails the run.

CPU temperature and package power are unavailable on the target. Consequently
every row is preregistered `performance_valid=NO` and
`performance_claim=NONE`, even if all correction and stability checks pass.
Elapsed time remains diagnostic and is not used to claim or select an optimum.

## Commands

Environment and parser validation only:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts/run_pivot09_reference.ps1 -ValidateOnly
```

Complete short comparison:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts/run_pivot09_reference.ps1
```

Output is created under a unique UTC directory in `out/benchmarks/pivot09/`.
`raw.tsv`, `aggregate.tsv`, `summary.tsv`, `telemetry.jsonl`, complete raw engine
streams, metadata and a SHA-256 manifest are retained locally and excluded from
the product package.
