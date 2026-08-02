# Stage 5 — benchmark laboratory before the engine

## Outcome

The benchmark laboratory is implemented and its protocol gate passes. It performs startup exclusion, three warmups per variant, deterministic randomized ordering, nine short repetitions, exact candidate-set hashing, separated transfer/kernel/proof/I/O timing, raw CSV and JSON output, integer-only summaries, conservative confidence intervals, environment capture, and two-run compatibility analysis.

This milestone makes no performance claim. The two labels execute the same validation workload. Temperature, runtime frequency, instantaneous power, ambient temperature, wall energy, and throttle state were unavailable, so every retained row is `valid_for_performance=NO`, the summaries say `performance_claim=NONE`, and the dataset is only evidence that the harness behaves correctly.

## Final evidence environment

- measured implementation commit: `e729e38363c79d38940825b923db325d5f589331`;
- repository dirty: `NO` before evidence generation;
- compiler: MSVC 19.51.36252.0 at toolset 14.51.36231;
- OS: Windows 11 Home 10.0.26200, 64-bit;
- CPU: AMD Ryzen 9 9950X3D 16-Core Processor;
- installed RAM: 66184978432 bytes;
- BIOS: American Megatrends Inc. 1605;
- GPUs/drivers: AMD Radeon Graphics 32.0.21042.62; NVIDIA GeForce RTX 5080 32.0.16.1074;
- active Windows power-plan GUID: `381b4222-f694-41f0-9685-ff5bb260df2e`;
- background process count: 337; background load not controlled;
- hardware settings: default limits reported but unverified;
- ventilation: unchanged reported but unverified;
- ambient temperature: `UNKNOWN`;
- power, wall energy, and energy source: `UNKNOWN`, `UNKNOWN`, `NONE`.

The uncontrolled background state and missing telemetry independently prohibit performance interpretation. TDP was not used.

## Compatibility evidence

Both executions used corpus SHA-256 `2B61CB65EF67D14CD8DED6F19EC1285FA1F1AB1A1EDD33717C8EA8326AB9D79B`, seed 20260802, two variants, and nine repetitions per variant.

Observed harness values, reported only to reproduce the gate:

- `reference-a`: medians 1487700 ns and 1490300 ns; absolute difference 2600 ns; larger MAD 11900 ns; intervals overlap;
- `reference-b`: medians 1496600 ns and 1495700 ns; absolute difference 900 ns; larger MAD 14300 ns; intervals overlap.

Both differences are below their recorded noise floors. They are not gains. `compatibility.json` reports `compatible=YES`, `performance_claim=NONE`, and `telemetry_status=UNAVAILABLE`.

`verify_benchmark_evidence.ps1` independently parsed both raw CSV/JSON pairs, checked phase sums and candidate hashes, recalculated every minimum, maximum, median, MAD, and confidence endpoint, and returned PASS.

## Retained artifact hashes

- `compatibility.json`: `A5E7C237B9D4389E8C97E9AFAC307B96F7DDA6D223E52CDA321029CBE33F2EBE`;
- `environment.json`: `B3B34E5CC02C34A2EBE406D8A4B6E77C8C7C1EFE1314918C1F574D63F5AD5B8C`;
- run A raw CSV/JSON: `87CB8CBB13BEB67C560B74383C0B1E47CD8EB97444DBD7B8046BCC36BB9AE3F3`, `F0A9B5409384CB5F008C19D3F09ACED929CA7B0A07FBDB7FADA31BF94C59DFE4`;
- run A summary CSV/JSON: `0DE835105AF6248BC0CB64B4D0A69104205BB5200727479219ED0899656C3206`, `AEE92F8F13D3133BF184DC881C0846848792FA3812CBECF582C4156480245CAA`;
- run B raw CSV/JSON: `8959B1CE162F3DD717D4F1F2F462702056007C608E501B4A12658D52C784B704`, `F3A4B0143CCA9D582CF5FC570E1883BADF7DAF26110D1F46E3B830E3614BE5A3`;
- run B summary CSV/JSON: `51CC28C7A22CCCF8F625E0FC053A6495C5410BFBCE3759C28DC08D2541ABBA36`, `CB1364A62AAADF663AB6E7DFD4F6CCCC14B9E3C2173DDC3A252D8CFB14B35002`.

## Build and tests

The final clean local gate used MSVC 19.51.36252.0 and produced zero PrimeForge warnings.

Debug:

- unit 0.55 s;
- self-test 0.44 s;
- corpus 0.66 s;
- benchmark protocol 0.47 s;
- benchmark smoke 0.41 s;
- both license tests 0.02 s;
- 7/7 PASS; explicit self-test PASS.

Release:

- unit 0.49 s;
- self-test 0.56 s;
- corpus 0.50 s;
- benchmark protocol 0.47 s;
- benchmark smoke 0.51 s;
- both license tests 0.02 s;
- 7/7 PASS, total 2.56 s; explicit self-test PASS.

`scripts/run_all.ps1 -Clean` ended with `PrimeForge complete local verification: PASS`.

## Commands

```text
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_benchmark_gate.ps1 -OutputDirectory benchmarks/evidence/stage5 -Repetitions 9
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/verify_benchmark_evidence.ps1
Get-FileHash benchmarks/evidence/stage5/* -Algorithm SHA256
```

## Remaining limitations

- No reliable telemetry provider or wall meter was present; all such fields remain unknown and performance claims remain disabled.
- Background applications were not controlled or closed by PrimeForge.
- Hardware default limits and ventilation were reported, not independently measured.
- The stage-5 workload validates the harness only and is not a prime engine benchmark.
- Hosted CI must still pass the new protocol and smoke tests on Windows/MSVC and Linux/GCC before closure.
