# Stage 3 report — licensing and provenance

Date: 2026-08-02

Status: PASS.

## Outcome

A third party can now determine, independently for every audited program, whether it is:

- original PrimeForge code;
- linked or a linking candidate;
- called through an external adapter;
- studied only as a reference;
- rejected;
- used only in CI or development.

Redistribution is a separate YES/NO field. Stage 3 contains 30 classified components and redistributes one: original PrimeForge code.

## License review

Apache License 2.0 is retained, unmodified, for original PrimeForge code. LICENSE SHA-256:

    04D223DDC28F864FF52ADF675D7C02B359AF9BD54F9CC074D4491185786F1A6F

NOTICE identifies PrimeForge and the copyright holder. Original C++, CMake, PowerShell, and workflow files carry SPDX-License-Identifier: Apache-2.0 where their syntax permits comments.

The decision relied on primary Apache and GNU guidance recorded in LICENSING.md and SOURCES.lock. It is not legal advice. It does not cover the research specification, third-party works, data, proofs, checkpoints, benchmarks, or generated artifacts.

## Distribution gate

licenses/DISTRIBUTION_MANIFEST.tsv is checked by cmake/VerifyDistributionLicenses.cmake. For each redistributed row it requires:

1. a concrete license path;
2. a present and non-empty file;
3. a matching SHA-256;
4. the declared notice file.

The target is:

    cmake --build --preset msvc-debug --target primeforge-distribution-check

Debug result:

    Distribution license verification PASS: 30 components, 1 redistributed
    exit 0

Release result:

    Distribution license verification PASS: 30 components, 1 redistributed
    exit 0

The negative fixture points to tests/data/DOES_NOT_EXIST.license. Direct execution returned exit 1 with:

    Required license file is missing for Deliberately missing fixture

CTest marks that test WILL_FAIL. It therefore passes only when the gate rejects the invalid manifest.

## Third-party notices and clean-room policy

scripts/generate_third_party_notices.ps1 generated THIRD_PARTY_NOTICES.txt from the manifest. It reports that no third-party numerical library, runtime component, or external engine is redistributed.

docs/PROVENANCE_POLICY.md prohibits copying third-party implementation details without a compatible grant and attribution. docs/CLEAN_ROOM_LOG.md defines the evidence required for strict clean-room and source-exposed independent reimplementations. The log is empty because no such implementation has been merged.

## PrimeForge build and test gate

Command:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean

Debug:

- configure/generate PASS;
- Ninja 8/8;
- primeforge.unit PASS, 0.40 s;
- primeforge.selftest PASS, 0.44 s;
- primeforge.license_distribution PASS, 0.02 s;
- primeforge.license_missing_negative PASS, 0.02 s;
- 4/4 tests PASS;
- explicit self-test PASS.

Release:

- configure/generate PASS;
- Ninja 8/8;
- primeforge.unit PASS, 0.65 s;
- primeforge.selftest PASS, 0.48 s;
- primeforge.license_distribution PASS, 0.02 s;
- primeforge.license_missing_negative PASS, 0.02 s;
- 4/4 tests PASS;
- explicit self-test PASS.

Compiler mode remained MSVC 19.51.36252.0, _MSC_VER 1951, _MSC_FULL_VER 195136252, __cplusplus 202400, C++23. There were zero warnings from PrimeForge code.

The preceding stage 2 hosted CI run 30759463561 also completed successfully on Windows MSVC and Linux GCC.

A later clean relink reproduced a host-policy limitation: Debug again passed 4/4, while Windows Application Control blocked the newly generated unsigned Release primeforge-selftest before main. Release compilation was 8/8; the unit test and both licensing tests passed. CodeIntegrity events 3033/3077 identify policy 0283ac0f-fff1-49ae-ada1-8a933130cad6 and an unmet Enterprise signing level. NR-0009 records this separately from test correctness. Private hosted workflow run `30759766252` subsequently passed both `windows-msvc` and `linux-gcc`, so stage 3 is closed.

## Commands executed

    Get-FileHash LICENSE -Algorithm SHA256
    web review of official Apache and GNU license guidance
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/generate_third_party_notices.ps1
    cmake.exe -DPRIMEFORGE_ROOT=<repo> -DMANIFEST=<repo>/licenses/DISTRIBUTION_MANIFEST.tsv -P <repo>/cmake/VerifyDistributionLicenses.cmake
    cmake.exe -DPRIMEFORGE_ROOT=<repo> -DMANIFEST=<repo>/tests/data/distribution_manifest_missing_license.tsv -P <repo>/cmake/VerifyDistributionLicenses.cmake
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
    gh run view 30759463561 --repo SashaLempers/PrimeForge --json status,conclusion,jobs,url
    cmake --build --preset msvc-debug --target primeforge-distribution-check
    cmake --build --preset msvc-release --target primeforge-distribution-check
    Get-WinEvent -FilterHashtable CodeIntegrity/Operational (events 3033 and 3077)
    Get-AuthenticodeSignature out/build/msvc-*/primeforge-selftest.exe

## Remaining limitations

- This policy is an engineering control, not a legal opinion.
- No third-party component has passed a redistribution YES gate.
- GMP/FLINT linking remains deferred and needs an exact binary/source distribution plan.
- Strong-copyleft and custom-EULA programs remain external or reference-only as recorded individually.
- Electrical consumption remains UNKNOWN.
- Stage 4 selects its independent oracles separately; none changes the stage-3 distribution scope.
- Fresh unsigned Release executables can be blocked by the host's Application Control policy; PrimeForge does not weaken that security control.
