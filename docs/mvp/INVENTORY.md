# PrimeForge MVP component inventory

Date: 2026-08-02. Basis: the complete PrimeForge specification, the immediate MVP
directive and the implementation through MVP-04.

`READY` means the primitive has a retained automated correctness gate. `PARTIAL`
means useful code exists but is not connected to a campaign. `MISSING` means the
MVP must add it.

| MVP need | State | Reusable evidence | Minimum missing work |
|---|---|---|---|
| Load `search.yaml` | READY | Strict `primeforge.search.v1` parser; duplicate/unknown/ambiguous input tests | No MVP work remaining |
| Validate and canonicalize | READY | Canonical campaign SHA-256 and exact `uint64_t`/Proth preflight | No MVP work remaining |
| Work units without gaps/duplicates | READY | Deterministic `(k,n)` flattening, five hashed units and exact coverage gate | No MVP work remaining |
| Compile congruences | READY | `compile_congruences`, mutation and exhaustive differential tests; fixed campaign prime list | No MVP work remaining |
| Sieve candidates | READY | `family_sieve::run`, scalar-reference equality and reconstructed factor witnesses | No MVP work remaining |
| Adapted PRP | READY | Base-2 strong PRP campaign path and PRP/proof separation gates | No MVP work remaining |
| Separate status axes | READY | Every canonical result serializes all three axes independently | No MVP work remaining |
| Produce a proof | READY | Hash-preflight PARI `primecert`/`primecertisvalid`, artifact hash, raw logs and stored-certificate revalidation | No MVP work remaining |
| Verify with a second engine | READY | Hash-preflight FLINT process, stored-output parsing and fresh fail-closed verifier run | No MVP work remaining |
| Atomic checkpoint | READY | SHA-256 `CheckpointManager`, campaign/config identity, exact ledger length and prefix hash | No MVP work remaining |
| Resume without replay/omission | READY | Durable append, authenticated prefix, safe suffix rollback and interruption-at-37 differential gate | No MVP work remaining |
| `results.jsonl` | READY | Stable ordered schema with statuses, witnesses, provenance and artifact paths | No MVP work remaining |
| `coverage_report.json` | READY | Exact candidate/status totals checked against the reconstructed domain | No MVP work remaining |
| `MANIFEST.sha256` | READY | Sorted SHA-256 inventory with missing/extra/mutation rejection | No MVP work remaining |
| `primeforge.exe` commands | READY | Operational `selftest`, `inspect`, `search`, `resume` and `verify` | No MVP work remaining |
| Known small campaign | READY | Versioned 160-candidate YAML and 34-prime corpus; 126 composites reproduced | Final private release reproduction only |
| One-command Windows use | READY | Packaged wrapper starts, resumes or verifies the known campaign; two complete local invocations | No MVP work remaining |
| Private release | PARTIAL | Closed 15-file allowlist, package manifest, ZIP re-extraction, self-test/inspect and zero-external-binary gate | Attach CI-validated ZIP/hash to the private release |

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
