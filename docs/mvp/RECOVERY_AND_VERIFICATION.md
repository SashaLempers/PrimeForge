# PrimeForge MVP recovery and verification

Date: 2026-08-02. Scope: restricted `primeforge.search.v1` uint64 Proth MVP.

## Campaign files

A campaign directory contains:

- `search.yaml`: exact recovery configuration written by PrimeForge;
- `results.jsonl`: durable canonical result ledger in flat-index order;
- `campaign.checkpoint.json`: atomic authenticated recovery point;
- `coverage_report.json`: exact final domain and status counts;
- `MANIFEST.sha256`: sorted SHA-256 inventory of every other regular file;
- `proofs/proth/`: canonical native Proth certificates;
- `external/`: request scripts, raw stdout/stderr and proof certificates.

The manifest does not contain itself. Paths use `/`, are relative to the campaign
root and sort bytewise. A final verifier rejects missing, additional or changed
regular files.

## Transaction and stop model

Every result line is flushed to stable storage before its progress is eligible
for a checkpoint. The checkpoint is replaced atomically and binds:

- schema `primeforge.mvp.checkpoint.v1`;
- campaign id and canonical configuration SHA-256;
- next flat candidate index;
- exact `results.jsonl` byte length;
- SHA-256 of that exact ledger prefix.

`Ctrl+C` sets a cooperative stop request. PrimeForge finishes the candidate in
progress, writes the result durably, writes a checkpoint, omits final coverage and
manifest, then exits cleanly. It never targets a process outside the active
PrimeForge campaign.

On resume, PrimeForge reloads the campaign-owned `search.yaml`, validates the
checkpoint hash and identity, authenticates the ledger prefix and verifies that
its records are contiguous and campaign-consistent. A suffix written after the
last checkpoint is outside the committed state: only that suffix and its exact
campaign-owned external job directories and native proof artifacts may be rolled
back before replay.

```powershell
& .\out\build\msvc-release\primeforge.exe resume `
  --checkpoint out\campaigns\known-proth-small\campaign.checkpoint.json
```

Resuming an already complete checkpoint is idempotent: it reconstructs and
rewrites final coverage and the manifest without rerunning candidate engines.

## Final verification

```powershell
& .\out\build\msvc-release\primeforge.exe verify `
  --result out\campaigns\known-proth-small\results.jsonl
```

Verification is not a manifest-only check. It also:

1. reconstructs the exact candidate domain and work-unit ownership;
2. requires one record for every flat index, with exact coordinates and value;
3. checks every retained congruence factor as a proper divisor;
4. reproduces every negative base-2 strong witness;
5. rejects a completed positive PRP that was not promoted by proof;
6. checks configured engine identities and executable SHA-256 values;
7. parses every stored FLINT raw output and any fallback PARI/GP output again;
8. parses each native certificate from disk, requires its exact canonical bytes,
   replays its Proth congruence and binds `k`, `n`, and `N` to the ledger;
9. validates any fallback PARI certificate in a fresh process and binds it to N;
10. reruns the independently pinned FLINT decision for each surviving candidate;
11. validates final checkpoint progress and exact coverage totals.

Any unknown output, engine disagreement, changed artifact, hole, duplicate or
unexpected file fails the command. This verifier performs no novelty check;
`novelty_status` remains `NOT_CHECKED`.

## Retained recovery gate

The automated gate executes the full 160-candidate fixture campaign twice,
requests a clean stop after candidate 37 and resumes it. The resumed
`results.jsonl` must be byte-identical to the uninterrupted ledger. Separate
fault cases mutate the ledger and force independent-engine disagreement; both
must be rejected.

This is a short functional correctness gate, not a performance benchmark or a
long search campaign.
