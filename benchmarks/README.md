# Benchmark artifacts

`evidence/` contains small, reviewable milestone-validation datasets. `raw/` is ignored for later large or repeated campaigns. Every retained result must identify its candidate-set hash, commit, environment metadata, repetitions, telemetry state, and claim scope.

Stage-5 evidence validates the measurement protocol only. It is not evidence that one prime-search algorithm is faster than another.

`profiles/` contains the versioned product workloads introduced by commit A.
Only `S64_PRP_65536` is executable at this gate. Multiprecision profiles are
present so their identity is fixed, but are explicitly deferred and rejected by
the binary.

`schemas/raw-v1.md` defines the stable integer-only JSONL/CSV contract.
`scripts/summarize.ps1` and `scripts/report.ps1` are the dependency-free Windows
report path; their Python equivalents serve Linux and CI. Generated repeated
runs remain below ignored `out/benchmarks/` until a reviewed evidence snapshot
is retained under `baselines/`.

The commit-A baseline compares three u64 PRP routes and three complete-pipeline
routes in randomized order. It never compares a PRP result with a proof result,
and it makes no performance claim from fewer than seven retained repetitions.

`baselines/optimization-07/` is the first retained evidence set that combines
the corrected steady-state PRP routing sweep, an end-to-end paired proof-worker
sweep, before/after Nsight Systems profiles and an explicitly rejected protocol.
Its local `README.md` records commit provenance and verification depth.
