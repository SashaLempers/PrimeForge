# MVP-03 - Recovery, finalization and independent campaign verification

Date: 2026-08-02. Status: PASS locally; private CI pending at milestone commit.

MVP-03 completes the five-command executable contract. Search now persists an
authenticated atomic checkpoint over a durable result-ledger prefix. A clean
stop finishes the current candidate, and resume validates or conservatively
rolls back an uncommitted suffix before continuing. Finalization emits exact
coverage and a deterministic SHA-256 inventory. The new verifier reconstructs
the mathematical domain and rechecks proof evidence rather than trusting the
ledger or manifest alone.

The automated gate stops the 160-candidate fixture campaign after record 37 and
requires the resumed result ledger to be byte-identical to an uninterrupted
run. It also rejects a mutated ledger and an injected independent-engine
disagreement. The final clean local gate passed 31/31 Debug tests in 48.74 s and
31/31 Release tests in 14.75 s, with both explicit C++23 self-tests passing and
no PrimeForge warning.

The retained real known campaign verifies successfully with 160 records, 34
`PROVEN_PRIME`, 126 `COMPOSITE` and 208 manifest-controlled files. Its result
ledger SHA-256 remains
`4C0BF7E5554A257BE36C9F4CA54FD2E7D7B601CF848C1A3AAC48610254361C1A`;
its `MANIFEST.sha256` file SHA-256 is
`2CC3A3E38BFFA4BE2CBA8938547239B3B994B5A4AB803D9901E2ADE9698E4137`.
These are correctness artifacts, not a novelty or performance claim.

No prolonged load ran. The final post-verification snapshot reported GPU 53 C,
47.63 W, no throttling, 43,435,790,336 RAM bytes available and 13,580 MiB VRAM
free. CPU temperature and power remain `UNKNOWN`; no value is inferred from TDP.

## Contribution directe au logiciel final

This milestone makes the actual search/proof engine safe to stop, resume and
audit. It prevents a multi-hour or multi-day campaign from silently losing,
duplicating or accepting candidates after interruption, and gives the user a
single independent command that revalidates retained mathematical evidence.
This infrastructure is indispensable because it protects engine output rather
than expanding infrastructure for its own sake.

Recovery and verification are complete for the restricted uint64 Proth MVP.
They will require extension when arbitrary-size or GPU campaign formats are
introduced, but no further infrastructure refinement is needed before the
private MVP package.
