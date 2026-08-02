# PrimeForge MVP roadmap

Date: 2026-08-02. Status: governing implementation priority until the first
private MVP release passes its known-campaign gate.

## Locked scope

The MVP is one Windows C++23 executable, `primeforge.exe`, with five commands:

```text
primeforge selftest
primeforge inspect --config search.yaml
primeforge search --config search.yaml
primeforge resume --checkpoint <file>
primeforge verify --result <file>
```

The first supported campaign is deliberately narrow:

- expression `N = k*2^n+1`;
- finite positive `k` and `n` progressions, odd `k`, and `k < 2^n`;
- every candidate must fit in `uint64_t`;
- CPU execution with a fixed, correctness-validated sieve configuration;
- internal base-2 strong PRP filter, which yields only `PROBABLE_PRIME`;
- rigorous proof through the pinned local PARI/GP 2.17.4 process;
- independent primality decision through the pinned local FLINT 3.6.0 process;
- exact coverage, atomic checkpoints and deterministic SHA-256 manifests.

PARI/GP and FLINT remain separately installed local tools. PrimeForge records and
checks their binary hashes but does not link or redistribute them. CUDA is not on
the MVP critical path.

## End-to-end path

```text
search.yaml
  -> strict parse and canonical campaign id
  -> flattened deterministic (k,n) domain
  -> disjoint hashed work units and coverage preflight
  -> congruence compilation and witnessed sieve
  -> surviving candidates
  -> base-2 strong PRP
  -> PARI rigorous decision and certificate when prime
  -> PARI certificate validation plus independent FLINT decision
  -> results.jsonl, coverage_report.json, checkpoint and MANIFEST.sha256
```

The three status axes remain independent. A positive PRP is never promoted by
text parsing. `PROVEN_PRIME` requires the configured proof step, and
`INDEPENDENTLY_VERIFIED` requires the distinct FLINT step to agree.

## Milestones

### MVP-01 — Unified CLI and campaign plan (PASS)

Implement the strict YAML subset, canonical campaign representation, flattened
two-parameter plan, work-unit identities, `selftest` and `inspect`. Add the known
small campaign definition. Gate: invalid/ambiguous configurations fail closed;
the inspected plan has exact cardinality, no gap and no duplicate.

Completed at the MVP-01 milestone. The retained domain is 160 valid candidates
with `k=1..31 step 2` and `n=5..14`. See `docs/mvp/SEARCH_CONFIG.md` and
`docs/reports/MVP_01.md`.

### MVP-02 — Complete search pipeline (PASS)

Connect the existing congruence compiler and family sieve to survivor enumeration,
the internal PRP, PARI proof production and FLINT independent verification. Emit
stable JSONL results with all three status axes and retain raw external outputs.
Gate: every known expected prime and composite is classified correctly; every
prime result has its proof/verification evidence; no PRP-only record says proven.

Implemented and validated on the full 160-candidate known campaign. The private
Windows/MSVC and Linux/GCC workflow `30769963195` passed. See
`docs/mvp/RESULTS.md` and `docs/reports/MVP_02.md`.

### MVP-03 — Recovery, finalization and verification (PASS)

Checkpoint after bounded batches, stop cleanly on request, resume without replay
or omission, create the coverage report and SHA-256 manifest, and implement
`verify`. Gate: an injected interruption plus resume produces byte-identical final
logical results to an uninterrupted run; deletion, duplication or mutation is
detected.

Implemented and validated. The retained gate stops after candidate 37, resumes from an
authenticated ledger prefix and produces a byte-identical result ledger to an
uninterrupted run. The final verifier checks the complete manifest inventory,
coverage, coordinates, factors, PRP witnesses, stored engine evidence, PARI
certificates and a fresh FLINT verdict. See
`docs/mvp/RECOVERY_AND_VERIFICATION.md` and `docs/reports/MVP_03.md`. Private
workflow `30770731160` passed Linux/GCC in 1 min 21 s and Windows/MSVC in
7 min 43 s.

### MVP-04 — Known campaign and private release (PASS locally)

Run the versioned small campaign end to end, verify all artifacts, document one
Windows command, build the private release package and attach it to a private
repository release. Gate: clean Debug/Release, all tests, green Windows CI, known
answers recovered, release hash verified and no external executable redistributed.

The closed 15-file archive builder and verifier pass locally. A copy augmented
with the separately installed local oracles completed the documented one-command
campaign and a second idempotent invocation: 160 records, 34 proven primes, 126
composites and 208 campaign-manifest files. Final status requires the MVP-04
private CI and private release asset/hash gate.

Every milestone receives updated documentation, a milestone commit and push, and
a report section named `Contribution directe au logiciel final`.

## Explicitly postponed until after MVP-04

- exhaustive thread/affinity autotuning;
- additional SIMD micro-optimizations;
- fine segment/cache/huge-page calibration;
- new candidate-storage variants;
- generic CUDA arithmetic and CPU/GPU pipeline tuning;
- global router/autotuner and last-percent performance work;
- any prolonged or novel campaign.

The already validated topology and bounded factor checks remain enabled. They are
not extended during the MVP work.

## Stop boundary

This roadmap authorizes only a small known local campaign. It does not authorize a
24-hour campaign, external assignment, third-party contact, public release,
repository visibility change or public announcement.
