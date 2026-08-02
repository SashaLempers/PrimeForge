# PrimeForge family language v1

## Purpose and boundary

The stage-8 language describes a finite family of integer candidates without embedding user code in PrimeForge. It has no loop, branch, variable assignment, arbitrary function call, floating-point value, file/network operation, or unbounded parameter. Parsing produces a typed immutable expression tree; only a validated definition is returned.

This language defines candidates. It does not assert that a candidate is prime, proven, novel, or fast to search. The objective `primality` requests a later test, and the proof policy controls later workflow only. A probable-prime result must remain `PROBABLE_PRIME` until a valid proof path establishes `PROVEN_PRIME`.

## Grammar

Every statement ends in `;`. Whitespace and `#` line comments are ignored.

```text
family IDENTIFIER;
param IDENTIFIER in [SIGNED_INT,SIGNED_INT];
allow_product IDENTIFIER;
value EXPRESSION;
constraint gcd(EXPRESSION,EXPRESSION)==1;
constraint even(EXPRESSION);
constraint odd(EXPRESSION);
constraint compare(EXPRESSION,COMPARISON,EXPRESSION);
constraint congruent(EXPRESSION,UINT64_MODULUS,UINT64_RESIDUE);
objective primality;
proof none|prove_if_survives|required;
```

Expressions contain canonical decimal integer constants, declared parameters, parentheses, unary `-`, `+`, `-`, `*`, and `^`. Precedence is power, unary negation, multiplication, then addition/subtraction. A power base must be a constant. Its exponent must be a nonnegative integer constant or a parameter whose entire declared domain is nonnegative.

Multiplication is accepted when at least one direct factor is a constant, or when a direct parameter factor has a matching `allow_product` declaration. This makes potentially expensive parameter-by-expression products visible during review. Parameters and bounds are signed 64-bit integers and every domain is closed and finite. Congruence moduli are at least 2 and residues are less than the modulus.

Identifiers are deliberately restricted to printable ASCII in v1. This is an NFC-safe subset and avoids silently incomplete Unicode normalization. A future Unicode extension would require a real normalization implementation and new golden vectors.

## Validation before computation

Parsing rejects duplicate or missing required statements, duplicate/unknown parameters, reversed or overflowing bounds, uncontrolled powers, unauthorized products, unsupported objectives or proof policies, invalid congruences, arbitrary calls, and floating-point syntax. Errors expose a stable code plus a one-based line and column.

The parser computes a saturating upper bound on result bits from the declared domains without constructing the integer. Exact evaluation is refused above 10,000,000 estimated bits. The same pre-computation guard covers exact comparison and gcd operands. Assignment completeness and bounds are checked before expression evaluation. Modular evaluation accepts only a nonzero modulus and never constructs the full candidate.

The estimate is conservative. It is a safety bound, not a storage promise or performance measurement.

## Canonical form and identity

The canonical representation is the restricted canonical JSON defined in `docs/CANONICAL_JSON.md`: UTF-8 without BOM, ASCII field values in v1, unique byte-sorted keys, no optional whitespace, no final newline, no floating point, and all domain integers serialized as canonical decimal strings. Parameters, allowed-product names, and constraints are sorted; duplicate allowed-product declarations collapse. Addition and multiplication sort their two canonical operands, and subtraction is represented as addition of a negated operand.

The planned-equivalence set in v1 covers comments/whitespace, statement ordering, parameter ordering, constraint ordering, and operand exchange for an individual addition or multiplication node. It does not claim general symbolic algebra, distributivity, or arbitrary reassociation. Two definitions equivalent under that declared set produce identical bytes and therefore the same lowercase SHA-256.

Golden Proth-like definition identity:

```text
SHA-256 82b8d4092dc444268cf0fb4376ccec502a1f3197aebcead837176d887e1c31cb
canonical byte length 348
```

The hash was independently calculated over the expected UTF-8 bytes using the Windows cryptographic provider and is also checked against PrimeForge's portable SHA-256 backend.

## Evaluation agreement gate

`primeforge-family-tests` checks exact arbitrary-precision arithmetic, canonical bytes and the independent golden hash, all constraint kinds, explicit domain failures, size rejection before construction, and exactly 2,000,000 deterministic small assignments. For every assignment it verifies:

```text
evaluate_exact(definition, assignment) mod q
    == evaluate_modulo(definition, assignment, q)
```

The test varies signed parameter values and moduli from 2 through 65,520. It is a correctness gate, not a benchmark.

## Command-line inspection

From a configured build:

```powershell
& .\out\build\msvc-release\primeforge-family.exe `
  --definition examples\families\proth_like.pf `
  --set k=3 --set n=5 --modulus 11
```

The command prints the canonical definition, SHA-256, maximum-bit estimate, constraint result, exact value, and modular value. It performs no search and contacts no external service.
