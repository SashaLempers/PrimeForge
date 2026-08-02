# MVP-04 - Known campaign and private Windows release

Date: 2026-08-02. Status: PASS locally; private MVP-04 CI and release attachment
pending at the milestone commit.

MVP-04 turns the validated engine into a closed Windows x64 package. The package
contains 15 allowlisted files: `primeforge.exe`, the known `search.yaml`, a
one-command wrapper, original PrimeForge documentation/license material,
`BUILD_INFO.json` and `PACKAGE_MANIFEST.sha256`. Directory and re-extracted ZIP
verification require the exact inventory, all content hashes, the governed
Apache-2.0 license hash, the product self-test and campaign inspection.

The only binary is `primeforge.exe`. PARI/GP, the FLINT oracle, FLINT, GMP, MPFR
and pthreads4w are not included. Exact local-only paths and SHA-256 values are in
`packaging/ORACLES.md`. Before packaging, the engine was strengthened so the
FLINT process cannot start unless all four dynamic runtime hashes match; the
adapter fault test proves that mismatch returns `UNTESTED` before process
creation.

A functional copy of the package was populated with the separately installed
local oracle files, then launched only through:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_known_campaign.ps1
```

The first invocation inspected, searched and verified the campaign. It produced
160 records, 117 congruence-factor composites, nine negative base-2 witnesses,
34 proven primes and 126 total composites; verification controlled 208 campaign
files. The result ledger SHA-256 was
`4C0BF7E5554A257BE36C9F4CA54FD2E7D7B601CF848C1A3AAC48610254361C1A`
and the campaign manifest-file SHA-256 was
`2CC3A3E38BFFA4BE2CBA8938547239B3B994B5A4AB803D9901E2ADE9698E4137`.
A second invocation used the completed checkpoint and independently verified the
same totals without duplicating a result.

The final clean local code gate passed 31/31 Debug tests in 43.62 s and 31/31
Release tests in 14.41 s, with both explicit C++23 self-tests passing and no
PrimeForge warning. The package builder and verifier pass on both the unpacked
directory and its freshly expanded ZIP.

No prolonged load ran. The post-campaign snapshot reported GPU 52 C, 45.67 W,
no throttling, 43,862,065,152 RAM bytes available and 13,653 MiB VRAM free. CPU
temperature and power remain `UNKNOWN`; no value is inferred from TDP.

Remaining MVP limitations are deliberate: uint64 Proth input only, CPU search,
fixed validated sieve settings, separately provisioned proof engines, no novelty
check, no code signature and no performance claim. CUDA, arbitrary-size values,
additional families and measured autotuning resume only after the private release
gate.

## Contribution directe au logiciel final

This milestone supplies the actual user-facing Windows program and the shortest
safe command from configuration to verified results. The packaging controls are
minimal but indispensable: they prove exactly what is shipped and prevent the
local proof engines with unresolved redistribution scope from entering the
archive.

The restricted MVP delivery path is complete. Packaging needs no further design
before release; future work returns to the engine, starting with measured CPU
pipeline improvements and then CUDA integration while preserving this campaign
as a non-regression gate.
