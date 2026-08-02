# PrimeForge correctness corpus

## Version and stability

`corpus/v1/` is the immutable first corpus schema. Any incompatible field or semantic change creates `v2`; existing evidence is never silently reinterpreted. All tracked corpus files are UTF-8 without BOM, use LF line endings, and have no trailing whitespace. `.gitattributes` enforces LF for both TSV evidence and certificate text fixtures on Windows and Linux checkouts.

The main file is `cases.tsv`. It uses exactly nine tab-separated fields:

1. `schema_version`: the decimal value `1`;
2. `id`: stable unique ASCII identifier;
3. `category`: stable test category;
4. `decimal`: canonical unsigned decimal integer, with no sign or leading zero;
5. `expected_outcome`: `REJECTED_NON_CANDIDATE`, `COMPOSITE`, or `PROVEN_PRIME`;
6. `provenance`: oracle set that established the outcome;
7. `seed`: `20260802` for seeded samples and `NONE` otherwise;
8. `form`: exact special-form expression or `GENERAL`;
9. `notes`: concise evidence note without tabs or newlines.

Corpus outcomes are not the `PrimalityStatus` enum. In particular, 0 and 1 are neither prime nor composite, so they are `REJECTED_NON_CANDIDATE`; PrimeForge must reject them before assigning an engine status. A corpus `PROVEN_PRIME` is accepted only after rigorous independent or exhaustive validation. A probable-prime result is never sufficient.

## Coverage

Version 1 has 68 cases covering 0 and 1, small primes, even composites, perfect powers, prime squares, semiprimes, Carmichael numbers, Fermat and strong pseudoprimes, 32-bit and 64-bit boundaries, Proth and Riesel forms, small Mersenne numbers, generalized Fermat numbers, and eight fixed-seed random values. The Proth case `3*2^1+1` explicitly exercises the condition where a sieve factor equals the candidate.

The seeded values use xorshift64star with initial state 20260802. For every sample, the state updates with shifts 12 right, 25 left, and 27 right in 64-bit unsigned arithmetic; the output multiplies by 2685821657736338717 modulo 2^64 and sets the low bit. The exact generated values are tracked, so a generator change cannot rewrite v1.

Certificate fixtures use PARI/GP's `primecert` scalar certificate form. `pari-primecert-valid.txt` is accepted by `primecertisvalid`; the one-value mutation in `pari-primecert-corrupt.txt` is rejected. Results are in `certificates/oracle_results.tsv`.

## Independent validation

All 68 general cases agree between:

- PARI/GP 2.17.4 `isprime`;
- FLINT 3.6.0 `fmpz_is_prime` through a locally built dynamic oracle;
- the portable PrimeForge test reference, which is also compared with exhaustive trial division for every integer from 0 through 100000.

The two Proth cases with exponent 32 additionally agree with proth20 0.9.1 using Proth's theorem on the NVIDIA OpenCL device. `oracle_results.tsv`, `special_form_results.tsv`, and `oracles/manifest.tsv` pin all verdicts, versions, sources, and executable hashes. Any missing case, duplicate identifier, malformed decimal, uncovered category, changed oracle verdict, or unexplained disagreement fails `primeforge.corpus`.

The FLINT, PARI/GP, and proth20 executables are local ignored artifacts and are not redistributed. FLINT's vcpkg SPDX metadata declares GPL-3.0-only while the pinned upstream source files state LGPL-3.0-or-later. PrimeForge does not resolve that metadata discrepancy by assumption: the binary remains quarantined as a local oracle and is excluded from every distribution.

## Reproduction on Windows

From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/oracles/install_flint.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/oracles/build_flint_oracle.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/oracles/validate_stage4_corpus.ps1
ctest --preset msvc-debug --output-on-failure
```

The PARI/GP and proth20 paths default to the local files recorded in `corpus/v1/oracles/manifest.tsv`. They must be obtained from their pinned primary sources and verified by SHA-256 before validation. No oracle command is part of a PrimeForge distributable.
