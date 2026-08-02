# PIVOT-00 — hardware-specialized audit

Date: 2026-08-02. Status: PASS locally; private Windows/Linux CI required for
closure.

## Outcome

The former generic stage 0-20 execution order is subordinated to
`docs/pivot/NEW_ROADMAP.md`. Eight target documents define the exclusive Ryzen 9
9950X3D, RTX 5080, 64 GB-class DDR5 and Windows/MSVC/CUDA direction, while
Linux/GCC remains correctness CI. `docs/pivot/PIVOT_REPORT.md` classifies every
completed or unfinished former stage.

The prolonged-benchmark addition is incorporated at SHA-256
`1dbf44b1938aece3e4e35f35656dc8699ba2d23adcca24c040560df97300e5fe`.
Duration is not an arbitrary stop condition, but no long run may start before the
independent-watchdog, threshold, checkpoint and recovery gate in PIVOT-02. No
prolonged computation was launched in PIVOT-00.

## Preserved guarantees

- The scalar and portable references remain present.
- PRP, proof, verification and novelty remain separated.
- Existing local factor-witness, corpus, coverage, canonical identity, checkpoint
  and license tests remain mandatory.
- Unknown telemetry stays `UNKNOWN`; no TDP-derived power exists.
- No external numerical, cryptographic or CUDA dependency was added.
- No performance claim, search result, external assignment or publication exists.

## Clean local gate

Command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Clean
```

Observed results:

- MSVC 19.51.36252.0, `_MSC_VER=1951`, `_MSC_FULL_VER=195136252`,
  `__cplusplus=202400`, C++23;
- Debug configure PASS, build 59/59, zero PrimeForge warnings;
- Debug CTest 17/17 PASS in 37.49 s;
- Debug explicit self-test PASS;
- Release configure PASS, build 59/59, zero PrimeForge warnings;
- Release CTest 17/17 PASS in 4.47 s;
- Release explicit self-test PASS;
- final script result: `PrimeForge complete local verification: PASS`.

The self-tests detected Windows `10.0.26200`, x86_64, AMD Ryzen 9 9950X3D,
16 physical/32 logical processors, SSE2/AVX/AVX2/AVX-512F/BMI2, NVIDIA GeForce
RTX 5080 and AMD Radeon Graphics. Electrical power remained `UNKNOWN`. These are
gate observations, not the canonical PIVOT-01 hardware profile.

An earlier tool invocation stopped capturing after five seconds while its child
gate continued. After confirming completion, the full clean gate above was rerun
from the beginning and is the only accepted validation result.
