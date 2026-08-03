# PrimeForge MVP search configuration

`primeforge inspect --config <file>` accepts the closed
`primeforge.search.v1` schema illustrated by `examples/mvp/search.yaml`. It is a
strict YAML mapping subset implemented without a third-party parser.

## Accepted syntax

- spaces only, with exactly two spaces per mapping level;
- printable ASCII keys and scalar values; comments occupy a complete line;
- unique known keys and sections only;
- unquoted scalars, or double-quoted scalars with only `\\` and `\"` escapes;
- canonical unsigned decimals without a sign or leading zero;
- `true` for the two mandatory mathematical constraints;
- portable relative paths using `/`, with no root or `..` component;
- lowercase 64-character SHA-256 engine hashes;
- UTF-8 input without a BOM. Meaningful scalar content is restricted to
  printable ASCII, which is a valid UTF-8 subset.

All relative paths are interpreted from the run root, which is the process
working directory. Launch PrimeForge from the directory containing the packaged
`search.yaml`, or use the packaged wrapper that sets this directory explicitly.
The run-root rule is retained in the recovery copy so the package can be moved as
a whole without embedding machine-specific absolute paths.

Aliases, sequences, tags, implicit booleans, inline comments and duplicate keys
are rejected. Unknown or missing fields are errors; they are never silently
defaulted.

## Mathematical domain

The MVP supports only `N = k*2^n+1`. Both `k` and `n` are inclusive arithmetic
progressions. Every generated `k` must be positive and odd, every `n` must be
positive, `k < 2^n` must hold for the whole Cartesian domain, and every `N` must
fit exactly in `uint64_t`.

Flat candidate indices use k-major, n-minor order. The known configuration has
16 k values and 10 n values, hence 160 candidates. It is partitioned into five
half-open work units of 32 candidates. Its pipeline-v2 canonical configuration
SHA-256 is:

```text
b72d1b3f2bfc5bdd3f3ee651d04735b20f0957c415b9afd412d40e3eb24591e0
```

Line endings and comments do not enter the canonical representation. Parsed
values are serialized as the restricted canonical JSON documented in
`docs/CANONICAL_JSON.md`; integer-valued configuration fields are decimal
strings. The campaign id is `sha256:<configuration hash>`.
The canonical identity also includes `primeforge.mvp.pipeline.v2` and the exact
native-Proth/FLINT/PARI-fallback proof policy. A proof-policy implementation
change therefore cannot silently resume a checkpoint created by an older path.

## Commands available at MVP-01

From the repository root after a Release build:

```powershell
& .\out\build\msvc-release\primeforge.exe selftest
& .\out\build\msvc-release\primeforge.exe inspect `
  --config examples\mvp\search.yaml
```

`inspect` validates configuration, cardinality, work-unit coverage and local
engine hashes. A missing engine is reported `UNAVAILABLE`; a present engine with
the wrong hash fails closed. It does not execute either engine. `search`,
`resume`, and `verify` use this exact inspected plan. `search --stop-after N`
requests a deterministic clean stop after candidate `N`; it exists for recovery
gates and does not weaken the normal `Ctrl+C` cooperative-stop path.
