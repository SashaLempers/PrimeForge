# Congruence compiler v1

## Scope

The stage-9 compiler handles the finite affine-exponential family

```text
F(k,n) = k*b^n + c
```

where `k` and nonnegative `n` each follow a closed signed-64-bit progression with an explicit positive step. `b` and `c` are signed 64-bit integers. Independent `ANY`, `EVEN`, or `ODD` constraints can be applied to each parameter. Inputs are mathematical definitions only: compilation does not start a search, assert primality or novelty, or contact a service.

PrimeForge accepts only prime moduli in the compiler. Every supplied `q` is checked by the deterministic 64-bit primality predicate before a table is produced.

## Invertible-base rule

For prime `q` with `gcd(b,q)=1`, the compiler calculates the minimal multiplicative order `r=ord_q(b)`. A selected period is `R=r*m`, where the explicit multiplier `m>=1` allows correct deliberately non-minimal periods to be tested. Since `b^R=1 mod q`, advancing the exponent by `R` preserves its residue.

With `n=n0+j*s_n`, the exponent-index sequence has a valid period

```text
J = R / gcd(R,s_n).
```

For each relevant `j mod J`, the compiler calculates `p=b^n mod q` and the forbidden `k` residue

```text
k = -c * p^(-1) mod q.
```

For `k=k0+i*s_k`, it then solves the index congruence. If `s_k` is invertible modulo `q`,

```text
i = (k-k0) * s_k^(-1) mod q.
```

If `q|s_k`, either every `k` index has the target residue or none does. This is represented by modulus 1 or by omitting the rule. The emitted vector of Cartesian index classes is the compressed-list representation; it does not allocate a bit for the full domain.

## Noninvertible and boundary cases

When `q|b`, no inverse is attempted:

- at `n=0`, integer exponentiation uses `b^0=1`, including `0^0=1`, so the compiler solves `k+c=0 mod q`;
- at `n>0`, `b^n=0 mod q`; all `k` are forbidden only when `q|c`, otherwise none are;
- `q|c` is recorded independently in every period entry, including the invertible-base case where the forbidden `k` residue is zero.

Signed `b`, `c`, and `k` are normalized with exact mathematical modulo, so negative intermediate values have residues in `[0,q)`. Parameter steps are applied in index space. Parity is checked before a table match can become an elimination.

A table match is only a possible factor rule. The application path always reevaluates it and refuses excluded semantic values (`N<=1`) and the important case `N=q`.

## Local correctness proof

PrimeForge records an elimination only after verifying all four obligations:

1. `q` is prime;
2. exact modular evaluation gives `F(theta) mod q = 0`;
3. `abs(F(theta)) > q`;
4. the positive-prime-candidate semantics accept `F(theta)>1`.

Then `q` is a proper prime divisor. If the accepted positive integer `F(theta)` were prime, its only positive prime divisor would be itself, contradicting `F(theta)>q`. Therefore the eliminated candidate is composite.

This proves each emitted elimination locally; it does not infer that survivors are prime. Survivors remain untested until a later engine assigns an explicit primality status.

`reconstruct_factor` reruns these obligations before returning `q`. A missing, composite, non-dividing, or non-proper witness is rejected.

## Table integrity and evidence

The compiler emits:

- a period record per `q`;
- compressed forbidden index classes;
- a deterministic ASCII proof sentence for every rule;
- restricted canonical JSON with decimal strings and stable key order;
- an internal SHA-256 over those exact bytes.

Validation first checks the stored hash, then recompiles the complete table from its family, prime list, and options and compares canonical bytes. Accidental mutation is therefore detected even if a new hash was calculated over the corrupted table. SHA-256 is integrity and identity, not authentication.

Golden sample table:

```text
family: k in [1,31] step 2, n in [0,12] step 2, b=2, c=1, k odd
primes: 3,5,7
period multiplier: 2
rules: 9
canonical bytes: 4235
SHA-256: 6a8409d912d12478a439267cdea662776d765ecbb90e79488c15fe7fc9f4e20b
```

The byte length and SHA-256 were independently calculated with the Windows cryptographic provider and are checked against PrimeForge's portable backend.

## Validation gate

`primeforge-congruence-tests` performs exhaustive enumeration over bases and constants `[-5,5]`, multiple nonunit steps and parity combinations, every prime through 43, deliberate non-minimal periods, special `q|b`/`q|c` fixtures, and 2,000 fixed-seed fuzz families. It compares every optimized elimination with a direct scalar modular reference and directly tests each small prime candidate for survival.

The retained local run covers 150,039 parameter assignments and reconstructs 29,972 proper factor eliminations with `false_prime_eliminations=0`. Counts are correctness coverage, not a performance benchmark.

Mutation tests alter a forbidden class, first retaining and then recomputing the hash. Hash verification catches the first mutation; semantic recompilation catches the second. A composite reconstructed witness is also rejected.

## Command-line use

```powershell
& .\out\build\msvc-release\primeforge-congruence.exe `
  --base 2 --constant 1 `
  --k-min 1 --k-max 31 --k-step 2 `
  --n-min 0 --n-max 12 --n-step 2 `
  --k-parity odd `
  --prime 3 --prime 5 --prime 7 `
  --period-multiplier 2 --canonical
```

The CLI prints period rows, all proof sentences, rule/elimination counts, table validity, and optionally the canonical bytes. It performs only finite local validation.
