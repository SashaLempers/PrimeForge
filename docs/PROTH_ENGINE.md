# Exact bounded Proth engine

PIVOT-09 begins the family-specific engine selected by PIVOT-08. The initial
primitive handles only values that fit exactly in unsigned 64-bit arithmetic:

`N = k * 2^n + 1`, where `k` is positive and odd, `n >= 1`, `k < 2^n`, and
the complete `N` is at most `2^64-1`.

This is a correctness and reference-comparison path. It is not the future
multi-precision CPU/CUDA implementation and carries no throughput claim.

## Proof contract

`try_prove_u64(k,n,max_witness)` checks bases in ascending deterministic order.
It emits a certificate only if it computes

`a^((N-1)/2) mod N = N-1`.

Together with the validated Proth-form preconditions, this is the sufficient
condition from Proth's theorem. The returned primality status is then exactly
`PROVEN_PRIME`. If the supplied witness bound is exhausted, the result is
`UNTESTED`: failure to find a witness is never serialized as evidence of
composition or even as a probable-prime result.

The certificate schema is `primeforge.proth.certificate.u64.v1`. It contains
the format, `k`, `n`, complete value, witness and residue. Canonical JSON uses
sorted keys, decimal strings, no whitespace and no final newline. SHA-256 covers
those exact bytes. `verify_u64` reconstructs the candidate and exponent and
recomputes the congruence; mutated form, coordinates, value, witness or residue
fail closed.

The proof API sets only the primality axis. It does not infer independent
verification or novelty. The CLI reports `SELF_VERIFIED` after its internal
replay and always reports `NOT_CHECKED` for novelty.

## Commands

From the repository root after a Release build:

```powershell
& .\out\build\msvc-release\primeforge-proth.exe `
  --k 43 --n 32 --max-witness 255
```

The shared local-oracle case yields `N=184683593729`, witness 3 and certificate
SHA-256:

```text
7172a2acdbae79bc90671dacafa01761d5bc632f2650ad4a4674d213efccfd22
```

The independent, pinned and non-redistributed `proth20` oracle reports the same
number prime with witness 3 on the RTX 5080 OpenCL device.

## Gate

`primeforge.proth` exhaustively revisits the 160-candidate known MVP domain. All
34 known primes produce certificates within the configured bound, all
certificates replay, and none of the 126 deterministic composites produces a
certificate. It also tests invalid forms, overflow, insufficient witness bounds,
canonical hashing and mutations of every certificate field.

## Limits

- no value wider than 64 bits;
- deterministic ascending witness search, not a tuned witness policy;
- no batch CPU SIMD or CUDA exponentiation;
- no persistent on-disk certificate parser yet;
- no novelty check and no external assignment;
- no performance statement until the committed implementation is compared under
  the preregistered protocol.

## Contribution directe au logiciel final

This module replaces a generic-primality placeholder with the first native proof
operation for the selected production family. It gives the future batched CPU and
CUDA engines an exact scalar oracle and an immutable certificate contract. The
64-bit implementation is complete as a reference path; multi-precision
arithmetic, batching, independent external replay and end-to-end integration
remain necessary for the final large-number engine.
