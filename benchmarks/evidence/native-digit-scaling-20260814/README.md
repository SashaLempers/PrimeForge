# Native digit scaling and adaptive GPU optimization evidence

This directory records the concise, versioned evidence for the bounded
`100k_digits` study performed on 2026-08-14. It is not a discovery campaign
and `100k_digits` does not mean 100,000 candidates.

- `scaling.tsv` contains the five-size B32 baseline.
- `scaling-exponents.tsv` contains empirical local and global power-law
  exponents. Transform cliffs make the local values descriptive rather than a
  fitted universal law.
- `optimizations.tsv` contains the A/B/B/A acceptance results.
- `adaptive-plan-table.tsv` is the measured RTX 5080 dispatch table.
- `SHA256SUMS` authenticates the ignored raw summaries and the final patch.

Raw logs remain under `out/benchmarks/native-digit-scaling/`. Their hashes are
versioned here so a local copy can be checked without committing bulky logs.
