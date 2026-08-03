# PrimeForge MVP search results

`primeforge search --config examples/mvp/search.yaml` executes the exact plan
accepted by `inspect` and creates `results.jsonl` in the configured campaign
directory. MVP-02 refuses an existing output directory; checkpoint recovery is
added in MVP-03.

## Classification path

The fixed correctness-first path is:

1. generate primes through the configured inclusive bound;
2. compile congruence rules and run the correctness-fixed one-thread family
   sieve by enumerating its compiled residue classes, with aligned direct
   canonical-bitset writes;
3. retain the smallest proper factor during that same sieve traversal and
   revalidate it at the result boundary;
4. run the internal base-2 strong probable-prime filter on survivors;
5. for every positive PRP, run the bounded native Proth witness search;
6. accept native `PROVEN_PRIME` only after the exact Proth congruence replays,
   then persist its canonical certificate without a final newline;
7. if the native witness bound is exhausted, retain `UNTESTED` and invoke the
   hash-pinned PARI/GP proof/classification fallback;
8. require the separately hash-pinned FLINT process to return the same exact
   primality status for every survivor.

Both fallback/independent executables are hashed before process creation. A missing or
mismatched binary, timeout, nonzero exit, unknown output, missing proof or engine
disagreement stops the search without a completed ledger. Raw stdout/stderr and
proof files remain under the campaign directory for audit. External tools remain
local and are not redistributed.

## Canonical JSONL record

Every candidate owns exactly one line, in flat-index order. Keys are ordered and
there is no insignificant whitespace. Integers are decimal strings; no floating
number appears. Each line contains:

- canonical campaign id, flat index, `(k,n)`, value and work-unit id;
- `primality_status`, `verification_status` and `novelty_status` as independent
  fields;
- PRP status and the exact classification method;
- a reconstructed factor when the congruence sieve supplied one;
- native certificate format, path and hash when native proof succeeds;
- fallback primary and independent engine ids, preflight executable hashes,
  raw-output paths, and external proof path/hash when applicable.

Sieve factors and negative base-2 witnesses are `COMPOSITE` and
`SELF_VERIFIED`. A positive base-2 test is only an intermediate
`PROBABLE_PRIME`; it is never written as a completed prime result. A persisted
prime is `PROVEN_PRIME` plus `INDEPENDENTLY_VERIFIED`. Novelty remains
`NOT_CHECKED` for every MVP record.

## Known corpus

`corpus/mvp/known_proth_small_primes.tsv` lists the 34 primes in the exact
160-candidate campaign. Every omitted member of that closed campaign is expected
composite. The file was reproduced with pinned PARI/GP 2.17.4 and FLINT 3.6.0;
its SHA-256 is
`7F27FB4EA7FAC982FDB7A59B8A359F7DC65BD64200FD9783F18A825244C7B381`.

The retained pipeline test executes the whole domain twice with controlled
engine fixtures, requires byte-identical ledgers and rejects an injected
independent-engine disagreement.
