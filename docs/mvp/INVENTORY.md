# PrimeForge MVP component inventory

Date: 2026-08-02. Basis: source tree at commit `5a14eaf`, the complete PrimeForge
specification, and the immediate MVP directive.

`READY` means the primitive has a retained automated correctness gate. `PARTIAL`
means useful code exists but is not connected to a campaign. `MISSING` means the
MVP must add it.

| MVP need | State | Reusable evidence | Minimum missing work |
|---|---|---|---|
| Load `search.yaml` | MISSING | Closed `.pf` family parser exists | Strict dependency-free YAML subset; duplicate/unknown key rejection |
| Validate and canonicalize | PARTIAL | `family::parse_family`, canonical bytes and SHA-256 | Canonical campaign configuration and `uint64_t`/Proth preflight |
| Work units without gaps/duplicates | PARTIAL | `work::partition_work_units`, identities and coverage verifier | Deterministic flattening of the `(k,n)` domain and campaign mapping |
| Compile congruences | READY | `compile_congruences`, mutation and exhaustive differential tests | Call it from the campaign plan with a fixed prime list |
| Sieve candidates | READY | `family_sieve::run`, scalar-reference equality | Enumerate survivors and reconstruct retained factor witnesses |
| Adapted PRP | PARTIAL | Base-2 strong PRP routine with PRP/proof separation test | Campaign result record and composite witness/diagnostic handling |
| Separate status axes | READY | `PrimalityStatus`, `VerificationStatus`, `NoveltyStatus` | Serialize all axes in every result; never infer one from another |
| Produce a proof | PARTIAL | Audited local PARI/GP 2.17.4 and hardened process adapter | Generate/store `primecert`, exact binary hash and raw logs |
| Verify with a second engine | PARTIAL | Audited local FLINT 3.6.0 oracle; PARI certificate fixtures | Run PARI certificate validation and independent FLINT decision |
| Atomic checkpoint | READY | SHA-256 `CheckpointManager`, corruption/fault tests | Define campaign payload: config id, next unit/index and result ledger state |
| Resume without replay/omission | MISSING | Atomic primitive and durable JSONL logger | Validate checkpoint/output prefix and continue idempotently |
| `results.jsonl` | MISSING | Canonical JSON rules and append-only logger | Stable result schema, factor/proof/log references and status axes |
| `coverage_report.json` | PARTIAL | Work-unit coverage verifier | Campaign completion, candidate counts and duplicate/hole checks |
| `MANIFEST.sha256` | MISSING | Portable SHA-256 provider | Deterministic inventory of all final user-facing artifacts |
| `primeforge.exe` commands | MISSING | Separate diagnostic CLIs and libraries | One dispatcher implementing `selftest/inspect/search/resume/verify` |
| Known small campaign | MISSING | Proth/Riesel corpus and independent oracle evidence | Versioned YAML plus exact expected `(k,n,N,status)` set |
| One-command Windows use | MISSING | Reproducible CMake presets and `run_all.ps1` | Document release command and stable output directory behavior |
| Private release | MISSING | Distribution-license gate | Package original binary/docs/config only; publish private asset and hash |

## Local proof engines already available

These ignored local files match the pinned, previously reproduced hashes:

| Role | Path | SHA-256 | Distribution |
|---|---|---|---|
| Rigorous proof/certificate | `out/oracles/pari-gp64-2.17.4.exe` | `518EA54D23832211356C99D1BB58B74A3F0ACD354A965543E7BCCA9B34030119` | not redistributed |
| Independent primality decision | `out/oracles/flint/flint-primality-oracle.exe` | `5E62BCAC0E324D14914979E4F565EAB2080DA0E215CFFF5C97E3FB48368FACD4` | not redistributed |
| Additional Proth oracle | `out/oracles/proth20/proth20.exe` | `41BBFE6FBCA8976AF9D00C9FC58926BF51FF460D20B9C739B9EF8870FC752C23` | not required by the MVP path |

The first two tools are necessary for the fully accepted local campaign. Tests and
Linux CI use controlled fixtures and internal references; absence of an oracle
must be reported as `UNKNOWN`/unavailable, never silently replaced by a proof.

## Deliberate simplifications

- A flat index owns each `(k,n)` pair. Existing one-dimensional work-unit coverage
  therefore proves ownership without adding a database.
- The MVP writes files in one campaign directory; SQLite is unnecessary.
- Fixed validated CPU settings replace the unfinished autotuner.
- Values above `uint64_t` are rejected before execution. Arbitrary-size campaign
  support follows the MVP rather than weakening the first proof chain.
- No novelty check is attempted; every result uses `NOT_CHECKED`.

## Critical implementation order

The only blocking chain is CLI/config -> deterministic plan -> sieve/PRP ->
proof/independent verification -> checkpoint/resume -> final verification. Work
outside this chain is deferred.
