# Stage 4 — independent correctness corpus

## Outcome

The stage-4 correctness gate is implemented. The versioned corpus contains 68 cases, all general cases agree between PARI/GP 2.17.4 and FLINT 3.6.0, both exponent-32 Proth cases agree with proth20 0.9.1, and the portable reference agrees with exhaustive enumeration on 100001 consecutive inputs. There are zero unexplained disagreements.

No oracle is linked into `primeforge_core` or redistributed. The main build remains dependency-free. Oracle sources, versions, executable hashes, integration modes, and license decisions are recorded in `corpus/v1/oracles/manifest.tsv` and `SOURCES.lock`.

## Corpus evidence

- `cases.tsv`: 68 cases; SHA-256 `2B61CB65EF67D14CD8DED6F19EC1285FA1F1AB1A1EDD33717C8EA8326AB9D79B`.
- `oracle_results.tsv`: PARI/FLINT agreement; SHA-256 `BC712AB281644367E4DE59425A024AF4270A05D8090B6F21486D0A0DA3D0CB55`.
- `special_form_results.tsv`: two proth20 results; SHA-256 `32F4021229D197C9C7B4E7427E6DD274F7132B175A583802E76D9BA8FF69D5E8`.
- valid PARI certificate: SHA-256 `36CF0AA625B24BA54637F33245BE6EA25FF2028F975ABD7FE4A46B86018EB7A5`.
- corrupted PARI certificate: SHA-256 `19E0128DAE896AFB12F334F12E4C4DCB4F704CC95AA201475CF2BABF7EAA383A`.

The corpus distinguishes `REJECTED_NON_CANDIDATE` from `COMPOSITE`; 0 and 1 are never mislabeled as composite. These corpus outcomes do not add a fourth state to `PrimalityStatus` and do not promote any PRP to proof.

## Oracle reproduction

Visual Studio's bundled vcpkg version `2026-05-27-d5b6777d666efc1a7f491babfcdab37794c1ae3e` used registry baseline `39344dff01c5a5a0134caf2624cdd492f05d30ea`. It installed FLINT 3.6.0, GMP 6.3.0#4, MPFR 4.2.2#1, and pthreads 3.0.0#14 under ignored `out/oracles/vcpkg_installed`.

The locally compiled FLINT oracle uses dynamic DLLs and has SHA-256 `5E62BCAC0E324D14914979E4F565EAB2080DA0E215CFFF5C97E3FB48368FACD4`. It reported `FLINT 3.6.0` and returned the rigorous `fmpz_is_prime` verdict for every case.

The official standalone PARI/GP executable `gp64-2-17-4.exe` has SHA-256 `518EA54D23832211356C99D1BB58B74A3F0ACD354A965543E7BCCA9B34030119`. It reported PARI 2.17.4, released 2026-06-20, with its MinGW/GMP kernel.

proth20 was built from commit `6771325939a7ceef2c75644c79981c7df4a61882` with `/DNOMINMAX` and a locally generated import library for the system OpenCL DLL. The source was not modified. The resulting local executable has SHA-256 `41BBFE6FBCA8976AF9D00C9FC58926BF51FF460D20B9C739B9EF8870FC752C23`. On NVIDIA GeForce RTX 5080, OpenCL 3.0 CUDA driver 610.74, it proved `43*2^32+1` prime and reported `3*2^32+1` composite. These are correctness observations, not performance benchmarks.

## Build and test results

Final clean Debug, MSVC 19.51.36252.0:

- build succeeded with zero PrimeForge warnings;
- `primeforge.unit` PASS, 0.93 s;
- `primeforge.selftest` PASS, 0.48 s;
- `primeforge.corpus` PASS, 0.70 s;
- both license tests PASS, 0.02 s each;
- 5/5 tests PASS; explicit self-test PASS.

Final clean Release, MSVC 19.51.36252.0:

- build succeeded with zero PrimeForge warnings;
- `primeforge.unit` PASS, 0.47 s;
- `primeforge.selftest` PASS, 0.44 s;
- `primeforge.corpus` PASS, 0.51 s;
- both license tests PASS, 0.02 s each;
- 5/5 tests PASS, total 1.47 s; explicit self-test PASS.

`powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean` ended with `PrimeForge complete local verification: PASS`. An earlier non-clean Release attempt reproduced NR-0009 and passed 4/5 because Application Control blocked the self-test before start; the final clean retry passed without changing or weakening host policy. Initial hosted run `30761294078` passed Linux/GCC but failed Windows/MSVC because Git converted certificate fixtures to CRLF; NR-0014 records the failure and the explicit `.gitattributes` LF fix. Corrected private workflow run `30761364430` passed both `linux-gcc` (10 s) and `windows-msvc` (53 s) at commit `5b3d5648ed8719cf63bc4d8e3cbf2015f5bbd748`. Stage 4 is closed.

## Commands executed

```text
gh run view 30759766252 --repo SashaLempers/PrimeForge --json status,conclusion,url,jobs
vcpkg install --x-manifest-root=out/oracles/vcpkg-manifest --x-install-root=out/oracles/vcpkg_installed --triplet x64-windows
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/oracles/install_flint.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/oracles/build_flint_oracle.ps1
pari-gp64-2.17.4.exe -q -f out/oracles/search.gp
proth20.exe -d 0 -q 43*2^32+1
proth20.exe -d 0 -q 3*2^32+1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/oracles/validate_stage4_corpus.ps1
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
Get-FileHash corpus/v1/* -Algorithm SHA256
```

## Limitations

- The corpus is a regression foundation, not a claim that every future algorithm or input size is covered.
- The portable 64-bit Miller–Rabin reference is test-only and is not exposed as a proof engine.
- PARI/GP scalar certificates are deliberately small fixtures; later proof stages must define full PrimeForge certificate formats and independent verification.
- FLINT's upstream/vcpkg license metadata discrepancy remains conservatively quarantined; no FLINT binary or DLL is redistributed.
- Electrical power and energy remain `UNKNOWN`; TDP was not used.
- No timing above is a performance result under the stage-5 protocol.
