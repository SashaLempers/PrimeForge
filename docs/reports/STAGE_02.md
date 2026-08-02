# Stage 2 report — systematic prior-art and source audit

Date: 2026-08-02

Status: PASS.

## Gate result

- 22 project audit sheets contain every mandatory field.
- 38 source/dependency/tool rows and 21 claims parse with their expected TSV column counts.
- All hexadecimal SHA-256 values in the audit sheets have 64 digits.
- Every selected or candidate component has a pinned revision/archive and an individual license decision.
- Every state-of-the-art capability claim is DOC_VERIFIED, SOURCE_AUDITED, or REPRODUCED.
- The mandatory Proth/Riesel search and remaining gaps are explicit.
- No component is described as best and no author performance statement is a PrimeForge benchmark.
- No PRP is described as proven prime.
- No external assignment, network work unit, publication, or long computation was initiated.

## Delivered inventory

The milestone adds 22 project sheets plus an index, template, research-paper register, and Proth/Riesel gap report under audits/. The normalized technical map is docs/STATE_OF_THE_ART.md. SOURCES.lock, CLAIMS.tsv, DECISIONS.md, NEGATIVE_RESULTS.md, README.md, and THIRD_PARTY_NOTICES.txt were updated.

The decisions are:

- LINK: primesieve after the stage 3 distribution gate.
- LINK candidates, deferred: GMP and FLINT.
- ADAPTER: PARI/GP, Prime95/gwnum, gpuowl PRPLL, Mlucas, OpenPFGW, Genefer22, GMP-ECM, mfaktc, and mfakto.
- REFERENCE_ONLY: Frey-PRPLL, CGBN, PRST, proth20, srsieve, NewPGen, sr2sieve, and PrimeGrid.
- REJECTED for new integration: LLR2 and PSieve-CUDA.

PRST is specifically blocked by the absence of an unambiguous project-wide license. Historical NewPGen/sr2sieve revisions and licenses remain UNKNOWN and therefore cannot be selected.

## Pinned source evidence

| Component | Revision | Source/archive SHA-256 | License decision |
|---|---|---|---|
| primesieve | v12.15 / 4f85384851da23c36c01ec01ef85b5d9d246e556 | 40CFE3DDFE2659D7056BCC193C743C7B106EAB16F2DD7CC3891BB4139650982D | BSD-2-Clause |
| GMP | 6.3.0 | A3C2B80201B89E68616F4AD30BC66AEE4927C3CE50E33929CA819D5C43538898 | LGPL-3.0-or-later or GPL-2.0-or-later, per component |
| FLINT | v3.6.0 / 8d5454b96761fafe4d5a9da76a369a602f500f49 | 7CEA49DE94F0267D5DA13F6A5EF6422FE9640EB8933ACD0F5243F8498CC2888F | LGPL-3.0-or-later |
| Prime95 | 30.19b21 source | BDC843A547A6F91DC67004A3EFBCD99858AF7DB075ECD77B7188B23E5AC2CE2A | custom GIMPS EULA/composite |
| Prime95 | 30.19b20 Win64 | D9475F2FF3F4A6A701ABC49A86A66126CB48ABD10BDA6FA87039D98FA8756BCA | custom GIMPS EULA/composite |
| gpuowl PRPLL | v/prpll/0.15 / 60818e75ef6985b4bff0939a53575c37b0b4c79a | 4387B9F3EABABCE74876EE0151B5F8FE6FEFEE0B8910ECC2CF50CDEB0AD7ABE0 | GPL-3.0-only |
| Frey-PRPLL | 8dc650cfc8d4da5b6b862f87125bfd24af522f54 | 5EE9C5EA20B8481FF435B08BCC404A498DA806A2E162AB85F8A3CF040D71BCD2 | GPL-3.0-only |
| Mlucas | 91188f71c1cb992e796c754a444924ec4a149649 | 06D8A1BE175330F056C2FE358807B21FB7830D7D7828B249DFEA7E9F4E262F97 | GPL-3.0-only |
| OpenPFGW mirror | 3e7ecda4e9b0d08ec1767498a107e2d7654f7b90 | 0F2CF555E45CCDA76EDD5A0506E215CB4B83FE856E2A0BC1A0E9D2B02E9DE7B8 | composite/custom |
| Genefer22 | d5060c61090942f42a908492628eba13ebd7cd82 | 7D07B12F193833615232332D8664519C1343028ED6BD280CE108CEB9333E4A34 | MIT |
| PARI/GP | 2.17.4 | 02651D99C391007D384B3FADBC20ABC6916B77036F9E496C99E9CE8688CA4B53 | GPL-2.0-or-later |
| GMP-ECM | git-7.0.7 / 1c038e2224992ebb40cb6a87710f64080e67dd09 | A0B47DCFD235380EF12EDABC56B91E211201D6C5D6C39A0F47110270C41C3C34 | GPL executable/LGPL library |
| CGBN | 114601b07ae8c3e4d46b250208ea8d9def7a12f0 | 548CAE13BB130A13EF00B86D51A0594E98A2555968825E7E1BF0B4642BB28AA7 | MIT-style NVIDIA |
| PRST | v14.0 / 4c01b7a5ba0f63a202951797476c4f2d6ac6907a | 9B09AB4A3AD669AAFCB275F20A43A0B2FF71535E45B1D6EBC73AF3285773FEDC | UNKNOWN, integration blocked |
| LLR2 | v1.3.3 / 51f23c6ca0da4942e0105d8b82fc14e49a812a79 | 658534594AC0D560814554CC64ADE933BCC3ED666E7EDAC4E9EDCFB21E17BBC2 | composite/UNKNOWN |
| proth20 | 6771325939a7ceef2c75644c79981c7df4a61882 | 1B4537FF7538F8FA2E0117DD2B587C789EC371ED5E2441B1E856B7989E306858 | MIT |
| srsieve | 0.6.17 archive | E8ED7378A70A511C74D033AEF07B4664F83AEB15FF7380E319CA3200E51CBC80 | GPL-2.0-or-later |
| mfaktc | 0.24.1 / 4ba8bd695a4a1e5689dce6f3d50810985ab39a41 | D1C0D0E9113F202429684665A9DF412C3B853B0CD3809FAC883BF03ED840B481 | GPL-3.0-only |
| mfakto | v0.16.0-beta.5 / 7c9f20bc054c12aef7b44f412eb457a2960da8f2 | 62A788FE6C92EA2DCECA36DFBCF429E25CDEF603568C143BAFB836FFC43FD3BC | GPL-3.0-only |
| PSieve-CUDA | 401d6591461cace13029d4996b5e4202a02e748c | 28F9ED7797EE0687255C592640A580E57FF365A35EC25FC8B4E8EEDDC57559EA | GPL-2.0-only |

## Local reproduction

Only primesieve was selected for immediate local reproduction. Exact result:

- MSVC Release configure: PASS.
- Ninja build: 97/97 steps.
- Upstream CTest: 34/34 PASS in 25.57 seconds.
- pi(1,000,000): 78,498.
- count in [10^12, 10^12 + 10^6]: 36,249.
- built primesieve.exe SHA-256: 0C35F0776C84380DED58214631337C53E8C2C2BE0BC0F91D89421475799C661E.
- three diagnostic pi(1,000,000) process launches: 9.362 ms, 5.920 ms, 5.947 ms, including startup.

The diagnostic timings are explicitly not a stage 5 benchmark. All other local execution statuses and reasons are in the individual sheets.

## Host capability observations

- CUDA driver/runtime exposed through the NVIDIA driver, but nvcc: NOT_FOUND.
- OpenCL platforms: NVIDIA CUDA OpenCL 3.0 and AMD APP OpenCL 2.1.
- WSL executable present, installed subsystem/distribution: absent.
- GCC, G++, make, mingw32-make: NOT_FOUND.
- Electrical power: UNKNOWN.

No CUDA toolkit, WSL distribution, GMP, FLINT, PARI/GP, OpenSSL, primesieve runtime dependency, or external engine was installed into PrimeForge.

## PrimeForge gate results

Final non-overlapping clean command:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean

Debug:

- configure/generate: PASS;
- Ninja build: 8/8;
- CTest: primeforge.unit PASS 0.54 s, primeforge.selftest PASS 0.48 s, 2/2 PASS;
- explicit self-test: PASS;
- compiler mode: MSVC 19.51.36252.0, _MSC_VER 1951, _MSC_FULL_VER 195136252, __cplusplus 202400, C++23.

Release:

- configure/generate: PASS;
- Ninja build: 8/8;
- CTest: primeforge.unit PASS 0.49 s, primeforge.selftest PASS 0.52 s, 2/2 PASS;
- explicit self-test: PASS;
- compiler and hardware report identical to Debug.

Final script line: PrimeForge complete local verification: PASS.

There were zero compiler warnings from PrimeForge code. MSVC show-includes lines are informational and not warnings.

## Chronological command log

The research inspection used the following command families, with every checkout and archive pinned as shown in the evidence table:

    git clone <primary-url> out/audit_sources/<project>
    git -C out/audit_sources/<project> checkout <tag-or-commit>
    git -C out/audit_sources/<project> rev-parse HEAD
    git -C out/audit_sources/<project> describe --tags --always
    git -C out/audit_sources/<project> archive --format=tar --output <project>.tar HEAD
    Get-FileHash -Algorithm SHA256 <archive-and-license-files>
    Get-Content <README-build-license-changelog-and-source-files>
    Select-String <pinned-source-files> -Pattern <algorithm-capability>

Repositories cloned in order:

    https://github.com/kimwalisch/primesieve
    https://github.com/flintlib/flint
    https://github.com/patnashev/prst
    https://github.com/primesearch/mfaktc
    https://github.com/primesearch/Mlucas
    https://github.com/galloty/genefer22
    https://github.com/NVlabs/CGBN
    https://github.com/shitcoinsherpa/Frey-PRPLL
    https://github.com/primesearch/mfakto
    https://github.com/patnashev/llr2
    https://github.com/primesearch/OpenPFGW
    https://github.com/galloty/proth20
    https://github.com/Ken-g6/PSieve-CUDA
    https://github.com/preda/gpuowl
    https://gitlab.inria.fr/zimmerma/ecm

Official archives:

    Invoke-WebRequest https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz -OutFile out/audit_sources/archives/gmp-6.3.0.tar.xz
    Invoke-WebRequest https://pari.math.u-bordeaux.fr/pub/pari/unix/pari-2.17.4.tar.gz -OutFile out/audit_sources/archives/pari-2.17.4.tar.gz
    Invoke-WebRequest https://www.mersenne.org/download/software/v30/30.19/p95v3019b21.source.zip -OutFile out/audit_sources/archives/p95v3019b21.source.zip
    Invoke-WebRequest https://www.mersenne.org/download/software/v30/30.19/p95v3019b20.win64.zip -OutFile out/audit_sources/archives/p95v3019b20.win64.zip
    Invoke-WebRequest https://primegrid.com/download/sr2sieve/srsieve-0.6.17-src.zip -OutFile out/audit_sources/archives/srsieve-0.6.17-src.zip
    tar -xf out/audit_sources/archives/gmp-6.3.0.tar.xz -C out/audit_sources/packages
    tar -xf out/audit_sources/archives/pari-2.17.4.tar.gz -C out/audit_sources/packages
    Expand-Archive <Prime95-and-srsieve-archive> out/audit_sources/packages/<name>
    Get-FileHash -Algorithm SHA256 out/audit_sources/archives/*

Environment and reproduction:

    Get-Command wsl,nvcc,clinfo,gcc,g++,make,mingw32-make
    wsl.exe --list --quiet
    clinfo.exe --list
    Set-ExecutionPolicy -Scope Process Bypass -Force
    Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    cmake -S out/audit_sources/primesieve -B out/audit_builds/primesieve-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
    cmake --build out/audit_builds/primesieve-release --parallel
    ctest --test-dir out/audit_builds/primesieve-release --output-on-failure
    out/audit_builds/primesieve-release/primesieve.exe 1000000
    out/audit_builds/primesieve-release/primesieve.exe 1000000000000 -d 1000000
    out/audit_builds/primesieve-release/primesieve.exe --version

Governance/build gate:

    Import-Csv SOURCES.lock -Delimiter tab
    Import-Csv CLAIMS.tsv -Delimiter tab
    audit-field and SHA-256-length PowerShell consistency check
    git diff --check
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
    out/build/msvc-release/primeforge-tests.exe
    ctest.exe --test-dir out/build/msvc-release -R primeforge.unit -VV
    ctest.exe --test-dir out/build/msvc-debug -R primeforge.unit -VV
    Get-Process and Get-CimInstance Win32_Process overlap diagnosis
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean

## Negative results and limitations

- The first primesieve configure lacked a usable developer shell because process execution policy blocked the Visual Studio script. Process-scoped Bypass fixed it; 34/34 tests then passed.
- PRST integration is blocked by an absent project-wide license.
- Historical srsieve could not be built without installing an obsolete GCC/MinGW or WSL path before selection.
- CUDA sources could not be built because nvcc is absent.
- A too-short external timeout left child build processes overlapping a retry; one Release CTest launch reported BAD_COMMAND. Direct and verbose retries passed, and two later single clean gates passed. NR-0008 records the evidence.
- NewPGen and sr2sieve exact source/license hashes remain UNKNOWN.
- Energy remains UNKNOWN; no TDP-based estimate is used.
- No comparative benchmark has been run.
