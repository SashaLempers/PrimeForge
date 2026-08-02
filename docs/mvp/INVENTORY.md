# PrimeForge MVP component inventory

Date: 2026-08-02. Basis: source tree at commit `5a14eaf`, the complete PrimeForge
specification, and the immediate MVP directive.

`READY` means the primitive has a retained automated correctness gate. `PARTIAL`
means useful code exists but is not connected to a campaign. `MISSING` means the
MVP must add it.

| MVP need | State | Reusable evidence | Minimum missing work |
|---|---|---|---|
| Load `search.yaml` | READY | Strict `primeforge.search.v1` parser; duplicate/unknown/ambiguous input tests | No MVP work remaining |
| Validate and canonicalize | READY | Canonical campaign SHA-256 and exact `uint64_t`/Proth preflight | No MVP work remaining |
| Work units without gaps/duplicates | READY | Deterministic `(k,n)` flattening, five hashed units and exact coverage gate | No MVP work remaining |
| Compile congruences | READY | `compile_congruences`, mutation and exhaustive differential tests | Call it from the campaign plan with a fixed prime list |
| Sieve candidates | READY | `family_sieve::run`, scalar-reference equality | Enumerate survivors and reconstruct retained factor witnesses |
| Adapted PRP | READY | Base-2 strong PRP campaign path and PRP/proof separation gates | No MVP work remaining |
| Separate status axes | READY | Every canonical result serializes all three axes independently | No MVP work remaining |
| Produce a proof | READY | Hash-preflight PARI `primecert`/`primecertisvalid`, artifact hash and raw logs | Revalidate stored proof in MVP-03 `verify` |
| Verify with a second engine | READY | Hash-preflight FLINT process and fail-closed agreement gate | Re-run from final verifier in MVP-03 |
| Atomic checkpoint | READY | SHA-256 `CheckpointManager`, corruption/fault tests | Define campaign payload: config id, next unit/index and result ledger state |
| Resume without replay/omission | MISSING | Atomic primitive and durable JSONL logger | Validate checkpoint/output prefix and continue idempotently |
| `results.jsonl` | READY | Stable ordered schema with statuses, witnesses, provenance and artifact paths | Prefix validation for resume in MVP-03 |
| `coverage_report.json` | PARTIAL | Work-unit coverage verifier | Campaign completion, candidate counts and duplicate/hole checks |
| `MANIFEST.sha256` | MISSING | Portable SHA-256 provider | Deterministic inventory of all final user-facing artifacts |
| `primeforge.exe` commands | PARTIAL | Unified dispatcher; operational `selftest` and `inspect` | Implement `search/resume/verify` |
| Known small campaign | PARTIAL | Versioned 160-candidate YAML and canonical campaign identity | Version exact expected `(k,n,N,status)` set |
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
