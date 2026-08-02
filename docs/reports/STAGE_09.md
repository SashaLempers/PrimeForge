# Stage 9 — congruence compiler

## Outcome

Stage 9 implements original PrimeForge compilation for `F(k,n)=k*b^n+c`: overflow-checked finite progressions with arbitrary positive steps, independent parity semantics, multiplicative-order tables, valid non-minimal periods, compressed forbidden index classes, special noninvertible-base rules, exact signed arithmetic, modular evaluation, generated proof sentences, canonical table identity, table mutation detection, local proper-factor validation, scalar reference evaluation, and a table-inspection CLI.

It performs no real search, engine call, probable-prime test, proof, novelty check, external assignment, or performance experiment.

## Absolute correctness gate

The retained deterministic suite covers:

- every base and constant in `[-5,5]` across multiple `k`/`n` steps and all parity combinations;
- every prime `q<=43` in the exhaustive matrix;
- 2,000 additional fixed-seed fuzz families;
- `q|b`, `q|c`, `N=q`, signed intermediates, `n=0`, nonunit steps, and periods three times the minimal order;
- exact agreement between compressed-rule application and scalar direct modular evaluation;
- independent primality checks on all small valid candidates;
- reconstruction of every retained proper factor;
- stale-hash mutation, recomputed-hash mutation, and composite-witness rejection.

The Release executable reports:

```text
candidate_assignments_checked=150039
proper_factor_eliminations_checked=29972
false_prime_eliminations=0
```

The absolute gate passes: no tested valid prime is eliminated. These counts describe correctness coverage and support no throughput claim.

## Canonical table vector

The sample with `b=2`, `c=1`, odd `k in [1,31] step 2`, `n in [0,12] step 2`, primes 3/5/7, and period multiplier 2 emits nine compressed rules. Its canonical table is 4,235 UTF-8 bytes with SHA-256 `6a8409d912d12478a439267cdea662776d765ecbb90e79488c15fe7fc9f4e20b`, independently calculated with the Windows cryptographic provider.

## Local clean verification

`scripts/run_all.ps1 -Clean` completed successfully after the final golden vector:

- Debug: 38 compile/link steps, zero PrimeForge warnings, CTest 14/14 passed in 36.35 s; congruence gate 19.47 s;
- Release: 38 compile/link steps, zero PrimeForge warnings, CTest 14/14 passed in 3.87 s; congruence gate 1.62 s;
- both explicit self-tests reported MSVC 19.51.36252.0, C++23 and `PASS`;
- power remains `UNKNOWN`, with no TDP inference;
- orchestration reported `PrimeForge complete local verification: PASS`.

## Boundaries

- The compiler supports the prioritized affine-exponential form only, not every stage-8 AST.
- Compressed rules are a correctness implementation; stage 10 must benchmark alternative storage/loop designs before selecting an optimized default.
- Exact application has the existing 10,000,000-bit safety limit; table compilation itself does not construct `N`.
- SHA-256 is identity/integrity, not table authenticity.
- Hosted Windows/Linux CI evidence will be added after the private workflow completes.

**Local status:** Accepted — 2026-08-02
