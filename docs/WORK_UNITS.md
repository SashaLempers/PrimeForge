# Work units and coverage proof

## Identity contract

A work unit contains the family identifier, canonical-definition SHA-256, one half-open parameter interval, sorted unique constraints, residue-compiler version, sieve bounds, proof policy, optional seed, and content hash. Stage 7 deliberately restricts user-provided strings to ASCII, which is already valid UTF-8 and NFC. Non-ASCII input is rejected until a reviewed Unicode-normalization implementation exists.

All mathematical integers are decimal strings in JSON. Keys are emitted in UTF-8 byte order, no float or unnecessary whitespace exists, and a canonical object has no final newline. The identifier is exactly:

```text
work_unit_id = lowercase_hex(SHA256(canonical_work_unit_without_id))
```

The portable SHA-256 backend is original PrimeForge code and introduces no OpenSSL or other dependency. NIST vectors for the empty string, `abc`, and a multi-block input are tested. A work-unit golden vector was independently calculated with the Windows cryptographic provider:

```text
04c5b090dd357de83ec6f98eae169e7d2ae7c6f7dfbf56c08b8ab19203975e7a
```

## Partition proof

Let the requested domain be the half-open integer interval `[a,b)` and let the maximum unit span be `s > 0`. Define `x0 = a` and `x(i+1) = min(xi + s,b)` while `xi < b`. PrimeForge emits `Ui = [xi,x(i+1))`.

Each interval is nonempty because `x(i+1) > xi`. Adjacent intervals meet only at their excluded/included boundary, so `Ui ∩ U(i+1)` is empty; monotonicity gives pairwise disjointness for all other pairs. The first begins at `a`, every successor begins at the previous end, and the last ends at `b`. Therefore their union is exactly `[a,b)`. Since each step advances by at least one and at most `s`, construction terminates after `ceil((b-a)/s)` units. The implementation uses `remaining = b-xi` and `min(s,remaining)`, avoiding addition overflow.

The verifier does not trust file order. It recomputes every content hash, rejects duplicate identifiers, sorts intervals, and advances an exact cursor. A begin above the cursor is a gap; a begin below it is an overlap; a final cursor below/above `b` is a trailing gap/overrun. It also rejects family or definition changes.

## Atomic checkpoints

`write_checkpoint_atomically` writes `target.new`, flushes the file to stable storage, closes it, then atomically replaces the target. Windows uses `CreateFileW`, `FlushFileBuffers`, and `MoveFileExW` with replace/write-through flags. Linux uses `write`, `fsync`, `rename`, and a best-effort parent-directory `fsync`.

Injected interruptions immediately after write and immediately after flush prove that the previous target remains readable and unchanged. A later successful call may safely overwrite the stale `.new` file. This is a local-filesystem guarantee; network filesystems require a separate durability audit.

## CLI

Example after a build:

```powershell
& .\out\build\msvc-release\primeforge-work-units.exe `
  --begin 0 --end 1000 --span 137 `
  --output-dir out\work-units\example `
  --family-id family.test.v1 `
  --definition-sha256 09367f1d4242c68f3546995f3566c69fc5ff80cc6fcc8bd02c4368fcd905ce60 `
  --constraint 'k%2==1' --constraint 'n%3!=0' `
  --proof-policy PROVE_IF_SURVIVES --seed 20260802
```

The command writes `work_units.jsonl` and `coverage_report.json` by the atomic path. These runtime artifacts stay under ignored `out/` unless deliberately promoted as evidence.
