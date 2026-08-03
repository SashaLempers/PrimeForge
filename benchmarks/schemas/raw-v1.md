# PrimeForge benchmark raw schema v1

`raw.jsonl` is UTF-8 without BOM, one JSON object per line. Durations are
non-negative integer nanoseconds. Hashes are lowercase SHA-256. No floating
point value is permitted. Unknown observations use the string `UNKNOWN`; they
are never inferred from TDP or another proxy.

Required identity fields are `timestamp_utc`, `commit_sha`, `binary_sha256`,
`profile_id`, `profile_sha256`, `dataset_sha256`, `backend`, `bits`,
`batch_size`, `candidate_count`, `repetition`, `result_sha256`, and
`valid_measurement`. `batch_size` is the maximum bounded chunk submitted to a
backend; `candidate_count` is the complete number of values covered by
`total_ns`. Older v1 rows without `candidate_count` are interpreted as having
`candidate_count == batch_size`.

The stage fields are `generation_ns`, `sieve_ns`, `packing_ns`, `h2d_ns`,
`kernel_ns`, `d2h_ns`, `prp_cpu_ns`, `proof_ns`, `verification_ns`, `io_ns`,
`checkpoint_ns`, and `total_ns`. A stage not exercised by a focused workload is
zero, not `UNKNOWN`. For a chunked focused workload, `total_ns` is the external
wall-clock interval around all chunks, so it includes dispatch and tail-handling
overhead; individual stage counters remain the checked sums reported by the
backend. Unavailable physical observations remain `UNKNOWN`.

The same logical row is also emitted in CSV form. Summary scripts may emit
decimal statistics, but raw timing and identity records remain integer-only.
