# Stage 8 — bounded family language

## Outcome

Stage 8 implements a closed version-1 family language with a lexer, parser, typed immutable expression tree, semantic validation, schema-specific canonical JSON, SHA-256 identity, conservative bit estimation without candidate construction, exact signed arbitrary-precision evaluation, deterministic modular evaluation, all required constraint kinds, explicit error codes, a CLI, and a documented example.

The language has no loops, arbitrary functions, floating point, unbounded parameter, I/O, or nondeterministic evaluation. It describes candidates but performs no search and establishes no primality, proof, novelty, or performance claim.

## Gate evidence

`primeforge-family-tests` verifies:

- canonical equality across reordered statements, parameters, constraints, duplicate constraints, commuted addition/multiplication, and commuted gcd operands;
- exact 348-byte golden serialization and independently calculated SHA-256 `82b8d4092dc444268cf0fb4376ccec502a1f3197aebcead837176d887e1c31cb`;
- signed/multi-limb integer arithmetic, including an independently calculated large product and `2^256`;
- parser and semantic rejection of reversed bounds, negative exponent domains, unauthorized products, arbitrary calls, floating syntax, invalid congruences, incomplete/out-of-bound assignments, zero modulus, and oversized exact values/constraints;
- gcd, parity, comparison, and congruence evaluation;
- exact/modular agreement for exactly 2,000,000 deterministic signed small-domain cases and varying moduli.

The example CLI reports candidate `97`, residue `9 mod 11`, satisfied constraints, and the golden family hash. These are functional test values, not search results.

## Local clean verification

`scripts/run_all.ps1 -Clean` removed only the two known build trees and completed successfully:

- Debug: 33 compilation/link steps, zero PrimeForge warnings, CTest 12/12 passed in 16.19 s on the final clean run; the family gate took 14.70 s;
- Release: 33 compilation/link steps, zero PrimeForge warnings, CTest 12/12 passed in 2.27 s on the final clean run; the family gate took 1.53 s;
- both explicit self-tests reported MSVC 19.51.36252.0, C++23 (`__cplusplus=202400`), and `PASS`;
- complete clean orchestration reported `PrimeForge complete local verification: PASS`.

Host metadata remains Windows 11 `10.0.26200`, x86_64, AMD Ryzen 9 9950X3D (16 physical/32 logical cores), NVIDIA GeForce RTX 5080 plus AMD Radeon Graphics. Electrical power is `UNKNOWN`; no value is inferred from TDP.

## Conservative boundaries

- Planned equivalence is the documented finite canonicalization set, not a general computer-algebra claim.
- Family identifiers and other user strings are printable ASCII in v1, an NFC-safe strict subset.
- The 10,000,000-bit exact-evaluation ceiling is a safety policy, not a benchmark-derived optimum.
- The internal integer is a correctness implementation and carries no throughput claim.
- No external engine, OpenSSL, GMP, FLINT, CUDA component, work assignment, network service, or real campaign is used.
- Private Windows/Linux CI is mandatory before closure.

The first hosted run `30763201015` passed Windows/MSVC in 1 min 59 s but exposed GCC's `-Werror=attributes` rejection of a redundant `[[nodiscard]]` on a non-defining friend declaration. NR-0021 records the failure and conservative correction; no warning was suppressed.

The correction audit also found that CMake had double-encoded non-breaking spaces in the French-localized `/showIncludes` prefix, leaving Ninja with zero header dependencies locally. NR-0022 records the issue. CMake now narrowly normalizes that sequence while preserving correctly detected locales; the final clean run verifies a populated dependency set before closure.

After normalization, `ninja -t deps` records both PrimeForge headers for `big_integer.cpp.obj`. Advancing the timestamp of `big_integer.hpp` made 13 dependent build steps dirty; the subsequent Debug and Release rebuilds executed those steps and again passed 12/12 tests. This closes NR-0022's local retry condition.

Private workflow run `30763448217` passed at corrective commit `152dcc47907c4ac8341984e21438b1be942b41f6`: Linux/GCC completed in 31 s and Windows/MSVC completed clean Debug and Release in 2 min 30 s. This closes NR-0021's cross-platform retry condition and the complete stage-8 gate.

**Status:** Accepted — 2026-08-02
