# External proof engines - not redistributed

PrimeForge does not redistribute these external programs or their runtime files.
The paths below are relative to the extracted MVP directory.

| Required path | SHA-256 |
|---|---|
| `out/oracles/pari-gp64-2.17.4.exe` | `518ea54d23832211356c99d1bb58b74a3f0acd354a965543e7bcca9b34030119` |
| `out/oracles/flint/flint-primality-oracle.exe` | `bd0150840cc268b9cd88400535d4474c73260114ee221d2131a93586bb4cc804` |
| `out/oracles/flint/flint-24.dll` | `00d4d34b091b145885368cb2737871ca98d84aadb52e7ac386fb56ac0016d08b` |
| `out/oracles/flint/gmp-10.dll` | `9909aefb265224648bc7055b305c47a7f19319410775a05a91e88081799c0677` |
| `out/oracles/flint/mpfr-6.dll` | `e1852ef40d93f08eb341aa6ba726d529879ccc067867194df1166c2026f35eb2` |
| `out/oracles/flint/pthreadVC3.dll` | `d5348d53b70d994265f776a7b6be73fd86c40ed06442f954aa6624df963cbb02` |

The repository's provenance records and `docs/reports/STAGE_04.md` describe the
audited local-oracle setup. `scripts/oracles/build_flint_oracle.ps1` builds the
PrimeForge FLINT wrapper from a separately provisioned local dependency tree.

The external-process boundary does not grant redistribution rights. FLINT's
binary-package licensing discrepancy remains unresolved, so every file in this
table is deliberately absent from the PrimeForge archive.
