# PrimeForge benchmark raw schema v1

`raw.jsonl` is UTF-8 without BOM, one JSON object per line. Durations are
non-negative integer nanoseconds. Hashes are lowercase SHA-256. No floating
point value is permitted. Unknown observations use the string `UNKNOWN`; they
are never inferred from TDP or another proxy.

Required identity fields are `timestamp_utc`, `commit_sha`, `binary_sha256`,
`profile_id`, `profile_sha256`, `dataset_sha256`, `backend`, `bits`,
`batch_size`, `repetition`, `result_sha256`, and `valid_measurement`.

The stage fields are `generation_ns`, `sieve_ns`, `packing_ns`, `h2d_ns`,
`kernel_ns`, `d2h_ns`, `prp_cpu_ns`, `proof_ns`, `verification_ns`, `io_ns`,
`checkpoint_ns`, and `total_ns`. A stage not exercised by a focused workload is
zero, not `UNKNOWN`. Unavailable physical observations remain `UNKNOWN`.

The same logical row is also emitted in CSV form. Summary scripts may emit
decimal statistics, but raw timing and identity records remain integer-only.
