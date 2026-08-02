# MVP-02 - Complete search, proof and independent-verification path

Date: 2026-08-02. Status: PASS locally; private CI pending at milestone commit.

MVP-02 connects the exact campaign plan to congruence compilation, the retained
family sieve, base-2 strong PRP separation, PARI/GP certificate production and
validation, and an independent FLINT decision. It emits one deterministic
canonical JSONL record per candidate and retains raw external evidence.

The local Release executable completed the 160-candidate known campaign in a
short functional run. It classified 117 candidates through revalidated
congruence factors, nine through negative base-2 strong witnesses, and sent 34
positive PRPs to the proof chain. PARI/GP produced and validated 34 certificates;
FLINT independently agreed on all 34. Final totals were 126 `COMPOSITE` and 34
`PROVEN_PRIME`, with no completed `PROBABLE_PRIME`. The result ledger SHA-256 was
`4C0BF7E5554A257BE36C9F4CA54FD2E7D7B601CF848C1A3AAC48610254361C1A`.

The clean local gate passed 31/31 Debug tests in 47.17 s and 31/31 Release tests
in 13.95 s, with both explicit C++23 self-tests passing and no PrimeForge warning.
The pipeline gate executes the 160-candidate corpus twice, requires byte-identical
logical ledgers, validates the 34-prime oracle set and rejects an injected FLINT
disagreement.

No prolonged load ran. Sensor observations around the functional campaign had a
maximum observed GPU temperature of 54 C, power between 49.47 W and 50.61 W, no
reported throttling, at least 43,577,597,952 RAM bytes available and at least
13,377 MiB VRAM free. CPU temperature and power remained `UNKNOWN`; nothing is
inferred from TDP. These snapshots are operational evidence, not a benchmark.

## Contribution directe au logiciel final

This milestone implements the central scientific path of the final product: a
configured family now becomes exact candidates, witnessed composites and primes
with stored proof artifacts plus a distinct independent verdict. It removes the
largest functional gap between the validated primitives and usable software.

The search/proof path is complete for the restricted uint64 Proth MVP. It will
need later performance work and larger-number backends, but those are explicitly
post-release. MVP-03 must still make this path interruption-safe and add final
coverage, manifest and independent artifact verification commands.
