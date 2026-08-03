# Negative Results

This register preserves failed hypotheses, optimizations that do not survive reproducible testing, integration attempts that cannot meet correctness or license requirements, and other useful negative evidence.

Each future entry must include:

- identifier and date;
- hypothesis or change tested;
- exact commit and environment;
- versioned workload and commands;
- raw evidence location and hashes;
- failure criterion;
- conclusion and scope;
- whether retry conditions are known.

## NR-0001 — Specification blob newline normalization

- **Date:** 2026-08-02
- **Change tested:** comparison of the original specification, working-tree copy, and staged Git blob.
- **Environment:** Git 2.53.0.windows.3 with `core.autocrlf=true`; introduced by stage 0 commit `ef62c79`.
- **Evidence:** original and working-tree SHA-256 `A87A3DDDF710E3080C9A3F1247644FF9505ECBCF470358015E9EDF19214A01C1`; pre-correction index blob SHA-256 `44F67AB9D69F897A14702EB188DAC38062BCC6ACEBE1ED474D37B5AF46BA99BD`.
- **Failure criterion:** the committed blob did not preserve the user-provided bytes exactly.
- **Conclusion:** automatic text normalization is unsuitable for versioned specifications. `docs/specifications/*.txt -text` was added, the index entry was recreated without changing the working file, and both corrected hashes are `A87A3DDDF710E3080C9A3F1247644FF9505ECBCF470358015E9EDF19214A01C1`.
- **Retry condition:** every new specification must pass original, worktree, and index-blob hash comparison before its milestone commit.

## NR-0002 — Empty PowerShell argument binding

- **Date:** 2026-08-02
- **Change tested:** first complete clean execution of `scripts/run_all.ps1 -Clean`.
- **Environment:** Windows PowerShell 5.1, Visual Studio Community 2026 18.8.2.
- **Evidence:** Debug configure, build, and 2/2 CTest tests passed; the script then stopped before the explicit self-test because an empty mandatory `Arguments` array could not be bound.
- **Failure criterion:** the complete clean automation did not reach its final PASS line.
- **Conclusion:** `Invoke-External` now defaults its argument array to an empty array, and self-tests are invoked without explicitly binding an empty value. A full clean Debug and Release rerun passed.
- **Retry condition:** retained as a regression path in every `run_all.ps1 -Clean` execution.

## NR-0003 — Deprecated CI action runtime

- **Date:** 2026-08-02
- **Change tested:** hosted CI for stage 1 commit `dd64b2152b3397b5150828f5348cf6fdd7eb8ea0`.
- **Environment:** GitHub-hosted Windows and Ubuntu runners; workflow run `30758589531`.
- **Evidence:** both jobs passed, but GitHub annotated `actions/checkout` v4 because its Node.js 20 runtime is deprecated and forcibly upgraded on hosted runners.
- **Failure criterion:** no avoidable deprecated component should remain in the reproducibility path.
- **Conclusion:** the workflow now pins official `actions/checkout` v6 commit `d23441a48e516b6c34aea4fa41551a30e30af803`, whose `action.yml` declares `node24`; its MIT license hash is unchanged and recorded in `SOURCES.lock`.
- **Retry condition:** every CI-action revision change requires an immutable pin, runtime inspection, license check, and a passing hosted workflow.

## NR-0004 — Developer-shell execution policy blocked the first primesieve configure

- **Date:** 2026-08-02
- **Change tested:** first MSVC/Ninja configuration of pinned primesieve 12.15.
- **Environment:** Windows PowerShell 5.1; Visual Studio Community 2026 developer-shell module.
- **Evidence:** the first invocation could not load Launch-VsDevShell because the process execution policy blocked the script; CMake consequently had no usable compiler environment.
- **Failure criterion:** the pinned source did not configure with the documented command in the current shell.
- **Conclusion:** the retry set execution policy to Bypass for the current process only, loaded the official Visual Studio developer shell, built 97 Ninja steps, and passed all 34 CTest tests.
- **Retry condition:** audit/build scripts must set only process-scoped policy or be launched from Developer PowerShell; machine policy is not modified.

## NR-0005 — PRST integration blocked by missing project-wide license

- **Date:** 2026-08-02
- **Change tested:** selection of PRST 14.0 as a modern Proth/Riesel adapter.
- **Environment:** tag v14.0, commit 4c01b7a5ba0f63a202951797476c4f2d6ac6907a.
- **Evidence:** README, source tree, history metadata, and top-level files were inspected; no unambiguous project-wide license grant was found.
- **Failure criterion:** every selected component needs an exact license and redistribution/integration decision.
- **Conclusion:** capabilities are recorded as SOURCE_AUDITED, but PRST is REFERENCE_ONLY, was not built, and cannot be redistributed.
- **Retry condition:** a license grant from the copyright holder or an authoritative already-published license file for the exact source revision.

## NR-0006 — Historical Proth/Riesel sieve build environment unavailable

- **Date:** 2026-08-02
- **Change tested:** local reproduction of srsieve 0.6.17 and related historical sieve paths.
- **Environment:** Windows 11 host with MSVC; no GCC, MinGW, make, or installed WSL distribution.
- **Evidence:** official archived source and makefiles were inspected; Get-Command reported GCC, G++, make, and mingw32-make as NOT_FOUND; WSL reported that the subsystem is not installed.
- **Failure criterion:** supplied tests could not be executed without installing an obsolete or unrelated toolchain before any component was selected.
- **Conclusion:** the algorithms, formats, limits, license, changelog, and TODO are source-audited; execution remains NOT_RUN and the tool is REFERENCE_ONLY.
- **Retry condition:** only if a later compatibility requirement justifies an isolated reproducible historical toolchain.

## NR-0007 — CUDA candidates cannot pass an execution gate in stage 2

- **Date:** 2026-08-02
- **Change tested:** build eligibility of CGBN, mfaktc, PSieve-CUDA, and CUDA PRPLL variants.
- **Environment:** NVIDIA RTX 5080 driver and OpenCL runtime present; nvcc NOT_FOUND.
- **Evidence:** source revisions and licenses were audited; local command discovery found no CUDA compiler toolkit.
- **Failure criterion:** no CUDA binary or test suite can be reproduced without the toolkit and a later stability/correctness protocol.
- **Conclusion:** no CUDA toolkit is installed in stage 2. Candidates remain ADAPTER or REFERENCE_ONLY according to their individual audit.
- **Retry condition:** the conditional CUDA stage must first justify the dependency, pin the official toolkit, and define cross-check vectors.

## NR-0008 — Overlapping clean builds after an orchestration timeout

- **Date:** 2026-08-02
- **Change tested:** stage 2 clean Debug/Release gate.
- **Environment:** the first run_all invocation was launched with an external five-second command timeout, shorter than the build; child build processes briefly outlived the timed-out parent while a retry began.
- **Evidence:** the overlapping retry linked the Release unit-test executable, but CTest reported BAD_COMMAND when starting it; immediate direct execution and a targeted verbose CTest retry both passed.
- **Failure criterion:** the complete clean gate did not pass in the overlapping state.
- **Conclusion:** after all orphaned build processes ended, a single non-overlapping clean run completed successfully: Debug 2/2, Release 2/2, and both self-tests PASS. Later Code Integrity events showed that the BAD_COMMAND itself was an Application Control signature-policy block, not proven build-directory corruption; NR-0009 supersedes the initial causal diagnosis.
- **Retry condition:** full build commands must receive a timeout longer than the whole gate and must never be relaunched while child processes remain active.

## NR-0009 — Windows Application Control intermittently blocks unsigned local Release binaries

- **Date:** 2026-08-02
- **Change tested:** final stage 3 clean Debug/Release gate.
- **Environment:** Windows 11 10.0.26200 with Smart App Control/App Control enforcement policy 0283ac0f-fff1-49ae-ada1-8a933130cad6.
- **Evidence:** Debug passed 4/4. Release compiled 8/8; primeforge.unit and both license tests passed, but CTest could not start primeforge-selftest. Direct execution produced “Une stratégie de contrôle d'application a bloqué ce fichier.” CodeIntegrity/Operational events 3033 and 3077 state that the unsigned generated executable did not meet Enterprise signing level requirements.
- **Failure criterion:** the newly generated local Release executable could not be launched even though compilation completed.
- **Conclusion:** this is an enforced host security decision, not a test assertion failure. PrimeForge does not disable or weaken Application Control automatically. The identical source had already passed a complete local Debug/Release gate before a later clean relink changed the unsigned binary, and private hosted Windows CI remains the independent Release execution gate.
- **Retry condition:** use a CA-trusted code-signing path, an authorized development machine/VM policy, or hosted CI. Do not repeatedly relink to seek a favorable reputation decision.

## NR-0010 — PARI/GP full installer was not suitable for unattended oracle setup

- **Date:** 2026-08-02
- **Change tested:** silent installation of the official `Pari64-2-17-4.exe` package.
- **Evidence:** the installer SHA-256 matched the official download listing, but the attempted silent invocation ended with a Windows user-cancellation result and created no installation.
- **Failure criterion:** an unattended, reproducible local oracle was not produced.
- **Conclusion:** the installer was not retried. The official standalone `gp64-2-17-4.exe` from the same primary distribution was hash-pinned and executed directly.
- **Retry condition:** only if a later feature needs files absent from the official standalone binary.

## NR-0011 — First vcpkg build window expired before FLINT completed

- **Date:** 2026-08-02
- **Change tested:** pinned FLINT 3.6.0 installation with a 15-minute orchestration window.
- **Evidence:** vcpkg was actively compiling GMP with 33 jobs when the wrapper timed out. GMP and MPFR completed, but FLINT was not yet present.
- **Failure criterion:** the requested FLINT package was absent from the install root.
- **Conclusion:** no duplicate build was launched while the original process remained active. A cache-preserving retry installed FLINT 3.6.0 successfully in 122.1 seconds; a subsequent manifest check completed in 197 microseconds.
- **Retry condition:** first-time oracle provisioning must allow at least 30 minutes and confirm package presence rather than relying only on wrapper lifetime.

## NR-0012 — Initial FLINT oracle build used the wrong direct MSVC flags and missed a runtime DLL

- **Date:** 2026-08-02
- **Change tested:** first direct compilation and launch of `flint_primality_oracle.cpp`.
- **Evidence:** MSVC rejected `/std:c++23`, FLINT headers emitted third-party warnings under `/WX`, and the corrected binary initially exited `0xC0000135` because `pthreadVC3.dll` was absent.
- **Failure criterion:** the oracle did not compile or start.
- **Conclusion:** the script now uses `/std:c++latest`, marks the vcpkg include tree external with `/external:W0`, retains `/W4 /WX` for PrimeForge glue code, and copies the four required runtime DLLs. The oracle reports FLINT 3.6.0 and passes known prime/composite cases.
- **Retry condition:** `build_flint_oracle.ps1` is the only supported direct build path; dependency changes require a fresh `dumpbin /dependents` audit.

## NR-0013 — Stage 4 local Release self-test blocked by host policy

- **Date:** 2026-08-02
- **Change tested:** stage-4 Release CTest after adding the corpus regression.
- **Evidence:** compilation succeeded; unit, corpus, and both license tests passed. Windows Application Control blocked the unsigned `primeforge-selftest` before process start, producing 4/5 local tests.
- **Failure criterion:** complete local Release execution was impossible under the enforced policy.
- **Conclusion:** this is a recurrence of NR-0009, not a corpus disagreement. The security policy remained enabled. A later single clean `run_all.ps1 -Clean` execution passed Release 5/5 and both explicit self-tests; private hosted Windows CI remains the independent Release gate.
- **Retry condition:** a CA-trusted signing path, an authorized development policy, or hosted CI.

## NR-0014 — Certificate fixtures checked out as CRLF on hosted Windows

- **Date:** 2026-08-02
- **Change tested:** stage-4 hosted workflow run `30761294078` at commit `04d59ed11d3b55226c56dad957fd8cdc4191bcf9`.
- **Evidence:** Linux/GCC passed. Windows/MSVC compiled all 10 steps, then `primeforge.corpus` rejected `pari-primecert-valid.txt` because checkout converted its LF terminator to CRLF; the parser intentionally enforces the v1 byte format.
- **Failure criterion:** Windows CI passed 4/5 rather than the required 5/5.
- **Conclusion:** `.gitattributes` now marks `corpus/v1/certificates/*.txt` as `text eol=lf`. The user-provided specification remains explicitly `-text`, preserving its original bytes and SHA-256.
- **Retry condition:** every new corpus extension needs an explicit line-ending rule and a passing Windows/Linux checkout test.

## NR-0015 — Clean-worktree benchmark metadata hit scalar PowerShell semantics

- **Date:** 2026-08-02
- **Change tested:** first final stage-5 evidence generation on a clean Git tree.
- **Evidence:** under PowerShell `StrictMode`, the empty output of `git status --porcelain` had no `.Count` property, although preliminary dirty-tree runs returned multiple lines and passed that expression.
- **Failure criterion:** the gate stopped before creating any final evidence file.
- **Conclusion:** the Git output is now wrapped in `@(...)`, so zero, one, and many status lines have stable array-count semantics.
- **Retry condition:** the benchmark gate is executed on a clean tree in every milestone reproduction.

## NR-0016 — Initial stage-6 timing table parsed single-line counts as characters

- **Date:** 2026-08-02
- **Change tested:** preliminary process-level comparison of PrimeForge option variants and primesieve.
- **Evidence:** the first 189 timing rows contained count `7` while the independent interval validator reported the correct counts; after fixing that conversion, raw counts were correct but the first summary read the 63-row array's own `Count` property instead of each row's `count` field.
- **Failure criterion:** benchmark variants did not retain the exact oracle count in raw evidence.
- **Conclusion:** conversions now parenthesize the complete string expression and aggregation explicitly enumerates each row field. The script rejects any per-range count disagreement and generates recalculable summary TSV. Invalid preliminary files remain ignored under `benchmarks/raw` and support no claim.
- **Retry condition:** a clean-tree run must contain one identical, nontrivial count for all variants of each range.

## NR-0017 — Stage-6 Release corpus executable blocked before start

- **Date:** 2026-08-02
- **Change tested:** first non-clean stage-6 Debug/Release gate.
- **Evidence:** Release compiled with zero PrimeForge warnings and 7/8 CTest tests passed; Windows Application Control returned `BAD_COMMAND` for `primeforge-corpus-tests.exe` before process start. The new Release sieve test passed, including corpus classification and differential arithmetic.
- **Failure criterion:** the first full local Release CTest did not reach 8/8.
- **Conclusion:** this is the same enforced unsigned-binary policy as NR-0009. PrimeForge did not weaken it. A clean retry and private hosted Windows/Linux CI are required to close the milestone.
- **Retry condition:** successful full local execution or the already documented trusted-signing/authorized-development path, plus hosted CI.

## NR-0018 — No stage-6 evidence for wheel 210 or SIMD retention

- **Date:** 2026-08-02
- **Change tested:** whether to retain wheel 210, AVX2, or AVX-512 dispatch in the first sieve.
- **Evidence:** the host advertises AVX2 and AVX-512F, but stage-6 timing lacks temperature, frequency, power, energy, and throttle telemetry and includes child-process startup. No multi-range claim-eligible gain exists.
- **Failure criterion:** capability detection or a noisy timing difference cannot justify additional optimized code.
- **Conclusion:** these paths are not implemented or retained. Wheel 30, bit packing, buckets, prefetch, and both bucket layouts remain independently testable; conservative defaults select the simple byte path.
- **Retry condition:** a preregistered, telemetry-complete multi-range benchmark demonstrates a repeatable end-to-end gain without correctness divergence.

## NR-0019 — Initial retained stage-6 TSV used host-native CRLF

- **Date:** 2026-08-02
- **Change tested:** hash stability of the corrected clean-tree benchmark evidence after Git normalization.
- **Evidence:** `.gitattributes` requires LF for TSV, but `.NET WriteAllLines` emitted host-native CRLF on Windows; staging would therefore change the evidence bytes and invalidate their pre-commit SHA-256 values.
- **Failure criterion:** a retained artifact must have identical bytes and hashes after Windows/Linux checkout.
- **Conclusion:** the provisional evidence directory was deleted before commit. The writer now serializes explicit UTF-8/LF bytes, and `.sha256` manifests also have an explicit LF rule. Evidence is regenerated only from the resulting clean commit.
- **Retry condition:** pre-commit hashes, staged normalized bytes, and post-checkout hashes must match exactly.

## NR-0020 — Stage-7 clean Debug unit executable blocked before start

- **Date:** 2026-08-02
- **Change tested:** clean local stage-7 Debug/Release gate.
- **Evidence:** all 27 Debug compilation/link steps completed with zero PrimeForge warnings. CTest passed 9/10; Windows Application Control blocked the unchanged `primeforge-tests.exe` before process start. The new work-unit test and CLI both passed. A prior non-clean Debug run passed 10/10, and the subsequently compiled Release tree passed 10/10 in 3.97 s.
- **Failure criterion:** the single clean `run_all.ps1 -Clean` orchestration could not finish its Debug phase.
- **Conclusion:** the enforced host policy remains unchanged. The failure is not a test assertion or coverage disagreement. Private hosted Windows/Linux CI is required before stage 7 closes.
- **Retry condition:** full hosted CI, or a trusted-signing/authorized-development path documented in NR-0009.

## NR-0021 — GCC rejected nodiscard on a friend declaration

- **Date:** 2026-08-02
- **Change tested:** stage-8 private workflow run `30763201015`, Linux/GCC build of the new arbitrary-precision integer.
- **Evidence:** GCC treated `[[nodiscard]]` on a non-defining friend declaration as an ignored attribute; project `-Werror` correctly stopped `big_integer.cpp` and `family.cpp`. Windows/MSVC completed the entire clean Debug/Release job successfully in 1 min 59 s.
- **Failure criterion:** Linux did not compile, so the cross-platform stage gate remained open despite all local Windows tests passing.
- **Conclusion:** the redundant attribute was removed from the friend declaration. The comparison operator semantics and ABI are unchanged; no warning suppression or weakening of `-Werror` was introduced.
- **Retry condition:** a new private workflow must compile and pass all 12 tests under Linux/GCC and Windows/MSVC.

## NR-0022 — Localized MSVC include records produced an empty Ninja dependency set

- **Date:** 2026-08-02
- **Change tested:** non-clean local rebuild after the NR-0021 header-only correction.
- **Evidence:** the header timestamp was newer than `big_integer.cpp.obj`, yet Ninja reported no work; `ninja -t deps` showed `#deps 0`. MSVC emitted the French `/showIncludes` prefix `Remarque : inclusion du fichier`, which the active Ninja rule did not recognize.
- **Failure criterion:** an incremental build must not leave an object stale after a tracked header changes.
- **Conclusion:** CMake now repairs only the observed UTF-8 double-encoding of the localized non-breaking spaces before Ninja rules are generated. Correctly detected prefixes in any locale remain unchanged. No compiler warning or security control is disabled.
- **Retry condition:** a clean configure/build must populate nonzero header dependencies, and a touched tracked header must make the dependent object dirty.

## NR-0023 — Stage-10 Windows macros were redundantly defined

- **Date:** 2026-08-02
- **Change tested:** first Debug compilation of the stage-10 family-sieve source.
- **Evidence:** MSVC emitted C4005 for `WIN32_LEAN_AND_MEAN` and `NOMINMAX`, because the target already defines both on the command line; `/WX` correctly stopped the build.
- **Failure criterion:** zero warnings from original PrimeForge code.
- **Conclusion:** the two source-level definitions were removed. CMake remains the single definition site, and no warning or error policy was weakened.
- **Retry condition:** corrected Debug and Release builds must complete with `/W4 /WX /permissive-`.

## NR-0024 — Stage-10 host telemetry cannot support an optimum claim

- **Date:** 2026-08-02
- **Change tested:** 357 clean-commit family-sieve samples across 17 one-factor configurations and three regimes.
- **Evidence:** all outputs agree with the scalar reference, but temperature, effective frequency, power, energy, hardware-error, throttle, profiler, and hardware-counter fields are `UNKNOWN`; every row states `telemetry_status=UNAVAILABLE`, `performance_valid=NO`, and `performance_claim=NONE`.
- **Failure criterion:** the stage-10 scientific gate requires a stable optimal configuration per regime, not merely a timing difference on one uncontrolled host session.
- **Conclusion:** correctness and a segment-count bottleneck are reproduced, but the optimum gate is `INCONCLUSIVE`. The reference-safe default is unchanged, and execution proceeds without a performance claim.
- **Retry condition:** preregistered multi-regime collection with reliable stability telemetry, controlled background conditions, and compatible repeated intervals.

## NR-0025 — Initial PRP separation vector was removed by prefiltering

- **Date:** 2026-08-02
- **Change tested:** first stage-11 adaptive-bound unit gate using 2047 as a base-2 strong pseudoprime.
- **Evidence:** the PRP routine deliberately trial-divides through 37 and rejected 2047 by factor 23 before reaching the strong test; 15/16 CTest tests passed and the new test failed its intended positive assertion.
- **Failure criterion:** the retained negative vector must exercise the PRP-positive/proof-negative boundary, not merely be composite.
- **Conclusion:** the fixture is now 1,373,653, which passes the implemented base-2 strong PRP and fails the independent deterministic 64-bit classifier. The corrected gate passes without weakening the prefilter.
- **Retry condition:** every future PRP backend must retain at least one backend-positive, independently composite vector.

## NR-0026 — Adaptive bound gives no retained-regime advantage

- **Date:** 2026-08-02
- **Change tested:** two calibration and two validation families over three sizes, comparing fixed bounds 7/19/43, offline adaptation, and online adaptation.
- **Evidence:** the offline model selected bound 7 for all three regimes and therefore duplicated fixed-low; online exploration selected 19 after paying cumulative probes and had a higher interval than the fixed-medium fallback. All 210 validation rows lack claim-eligible stability and energy telemetry.
- **Failure criterion:** H2 requires a statistically robust end-to-end gain on at least one validation regime, not selection of the same fixed bound or an ineligible timing difference.
- **Conclusion:** H2 is `FAILED` for the retained small, medium, and large regimes. Fixed-medium remains the conservative fallback; no general impossibility claim is made.
- **Retry condition:** a distinct preregistered family set with a materially representative external PRP cost, reliable stability/energy telemetry, and an adaptive choice that outperforms the best preregistered fixed strategy.

## NR-0027 — Empty NVIDIA telemetry inventory became null in hosted Windows CI

- **Date:** 2026-08-02
- **Change tested:** PIVOT-01 private workflow run `30765994946`, Windows/MSVC `primeforge.hardware_profile` test.
- **Evidence:** the runner has no `nvidia-smi`. PowerShell unrolled the intended empty telemetry-field array to `$null`; strict mode rejected `.Count`. The preceding 17 Windows tests passed, Linux/GCC passed, and the target host with seven NVIDIA query fields passed locally.
- **Failure criterion:** a missing GPU or telemetry provider must produce source-labelled `UNKNOWN`, never a collector failure.
- **Conclusion:** the collector now wraps the conditional result in an explicit array. The test suite forces a no-`nvidia-smi` path even on the target host and requires an empty NVIDIA inventory plus `UNKNOWN` temperature and power.
- **Retry condition:** clean local Debug/Release gates and a new private Windows runner without NVIDIA hardware must pass the hardware-profile test.

## NR-0028 — Initial watchdog process-test command was misquoted on Windows

- **Date:** 2026-08-02
- **Change tested:** first Debug process-level fault test for the independent watchdog.
- **Evidence:** 23/24 tests passed; `cmd.exe` rejected a command beginning with a quoted relative executable path, so the fixture never published its PID and no watchdog behavior was exercised.
- **Failure criterion:** the process test must launch both independent executables and observe graceful and forced termination.
- **Conclusion:** the test launcher now accepts only paths without shell metacharacters and passes them directly. The project and CI build roots satisfy that conservative constraint; no watchdog assertion is relaxed.
- **Retry condition:** both cooperative and hung fixture modes must complete, leave durable stop events, and pass in Debug, Release, Windows and Linux CI.

## NR-0029 — Hosted Windows exposed CPU sets but refused their selection

- **Date:** 2026-08-02
- **Change tested:** PIVOT-03 private workflow run `30768052506`, Windows/MSVC family-sieve placement test.
- **Evidence:** all 83 compilation/link steps completed with zero PrimeForge warnings and 25/26 tests passed. Linux passed 26/26. The Windows runner returned a non-empty CPU-set topology, but `SetThreadSelectedCpuSets` did not apply the requested sets inside the hosted job. The target Ryzen had already applied every requested worker selection locally.
- **Failure criterion:** the test incorrectly required full affinity on every Windows environment, even when the operating environment withheld CPU-set selection permission.
- **Conclusion:** the test now probes effective runtime selection. It requires every worker to apply affinity when the probe succeeds; otherwise it verifies exact counts and forbids fabricated pinning success. Engine behavior and the strict target-machine requirement are unchanged.
- **Retry condition:** clean local Debug/Release gates and a new private Windows/Linux workflow must pass; the target diagnostic must continue to exercise all 16 physical cores across both L3 domains.

## NR-0030 — A one-target CPU-set probe did not establish complete plan capacity

- **Date:** 2026-08-02
- **Change tested:** PIVOT-03 private workflow run `30768352677`, first capability-gated Windows/MSVC retry.
- **Evidence:** Linux compiled and passed 26/26 tests. Windows compiled all 83 steps without PrimeForge warnings and passed 25/26 tests. One CPU-set selection succeeded on the calling thread, but the two-worker physical-core request did not apply to every worker.
- **Failure criterion:** a successful one-target probe was incorrectly treated as proof that the hosted job supplied and permitted a complete multi-worker physical-core plan.
- **Conclusion:** physical and logical plans are now assessed independently and print exact plan/applied counts. Generic Windows hosts do not stand in for the target. A target-specific test recognizes the Ryzen 9 9950X3D, runs 16 physical workers and strictly requires the complete 16/16 path.
- **Retry condition:** a new hosted Windows/Linux run must pass while the local target continues to report and apply the full interleaved plan.

## NR-0031 - The MVP-01 hash-mismatch fixture used a Windows filename

- **Date:** 2026-08-02
- **Change tested:** private workflow run `30769392438` for MVP-01.
- **Evidence:** Windows/MSVC passed its complete job. Linux/GCC built without warnings and passed 28/29 tests, but the negative inspect fixture named `primeforge.exe`; the Linux product is `primeforge`, so the supposed present mismatched file was correctly reported unavailable and the `WILL_FAIL` test unexpectedly succeeded.
- **Failure criterion:** the same provenance-negative test must exercise a present file on Windows and Linux.
- **Conclusion:** the fixture now points to tracked `CMakeLists.txt`, and its CTest working directory is the source root. The file exists on both platforms and cannot match the all-zero expected hash. The production hash rule is unchanged.
- **Retry condition:** the next private Windows/Linux workflow must pass the mismatch-negative test on both jobs.

## NR-0032 - A targeted rebuild lacked the Visual Studio developer environment

- **Date:** 2026-08-02
- **Change tested:** targeted Debug/Release rebuild after adding FLINT runtime-file provenance.
- **Evidence:** the ordinary PowerShell process had neither `cmake` nor `ctest` on `PATH`; invoking the bundled CMake directly then found neither Ninja nor the developer `cl.exe` environment and stopped before compilation.
- **Failure criterion:** the targeted gate did not reach configure/build.
- **Conclusion:** no fallback compiler or generator was selected. The official `scripts/run_all.ps1` path loaded Visual Studio 2026 Developer PowerShell and then completed both builds and all 31 tests.
- **Retry condition:** use Developer PowerShell or the repository script for every MSVC gate.

## NR-0033 - The first package verifier expected the library self-test marker

- **Date:** 2026-08-02
- **Change tested:** first closed-directory package verification.
- **Evidence:** packaged `primeforge.exe selftest` exited zero and printed `mvp.status=PASS`, but the script expected the separate `primeforge-selftest.exe` marker `selftest.status=PASS` and rejected the package.
- **Failure criterion:** the package gate reported failure despite a successful product self-test.
- **Conclusion:** the verifier now requires the product CLI's exact `mvp.status=PASS` marker. Both unpacked and re-extracted ZIP gates pass; no binary behavior or test criterion was weakened.
- **Retry condition:** retain the packaged CLI marker contract in the package gate.

## NR-0034 - CRT premarking initially retained a noncanonical valid factor

- **Date:** 2026-08-02
- **Change tested:** exact factor-vector equality across every family-sieve option after adding single-pass witnesses.
- **Evidence:** Debug passed 30/31 tests; `primeforge.family_sieve` found that the bounded-CRT variant produced the correct elimination bitset and a valid proper factor, but not always the smallest scalar-reference factor.
- **Failure criterion:** optimized traversal order must not change canonical factor evidence.
- **Conclusion:** when witnesses are requested, a CRT-premarked candidate now evaluates only matching rules with primes below its already valid factor. All variants return the exact smallest reference factors without replaying the complete table. No assertion was removed.
- **Retry condition:** every new premark or wheel path must pass bitset and canonical-factor differential gates.

## NR-0035 - Direct bitset writes were initially enabled for transposed traversal

- **Date:** 2026-08-02
- **Change tested:** watchdog-supervised 420-row family-sieve differential diagnostic after replacing worker-local bitsets with direct segmented writes.
- **Evidence:** all warmups passed, then one randomized timed row disagreed with the scalar reference. The `by_n` traversal partitions transposed traversal indices, so two otherwise aligned segments can update different bits of the same canonical k-major word.
- **Failure criterion:** every timed result must equal the scalar reference byte for byte; a fast path may never rely on overlapping non-atomic writes.
- **Conclusion:** direct writes now require dense storage, canonical `by_k` traversal and a segment size divisible by 64. Transposed or unaligned configurations automatically retain worker-local bitsets and their merge. A dedicated transposed fallback assertion and 20 repeated 16-worker dynamic direct-write runs pass.
- **Retry condition:** any future traversal layout must prove exclusive ownership at the canonical 64-bit-word boundary before enabling direct writes.

## NR-0036 - Target CPU profile selection is inconclusive

- **Date:** 2026-08-02
- **Change tested:** 315 calibration and 315 disjoint validation executions across 15 thread, segment, placement and scheduling profiles on the optimized complete sieve path.
- **Evidence:** every execution matched the scalar bitset and canonical smallest-factor vector, but all rows state `performance_valid=NO` because validated CPU temperature and package-power providers remain unavailable. The lowest observed profile also varied by regime: calibration small/medium/large selected 2-thread scheduler, 16-thread physical with 4096-candidate segments, and 32-thread logical; validation selected 4-thread scheduler, 16-thread scheduler, and 32-thread logical.
- **Failure criterion:** PIVOT-03 may retain a target profile only after disjoint validation, complete stability telemetry and a stable selection; timing rank alone is insufficient.
- **Conclusion:** PIVOT-03 closes `INCONCLUSIVE`. The one-thread, 8192-candidate, scheduler-managed static profile remains the conservative product default. No timing or fastest claim is made.
- **Retry condition:** a validated CPU temperature provider, complete stability interval and a preregistered larger representative workload may reopen profile selection without reopening the completed engine primitives.

## NR-0037 - FindCUDAToolkit was initially forced into the wrong search mode

- **Date:** 2026-08-03
- **Change tested:** first optional CUDA 13.3 configuration using an explicit toolkit path.
- **Evidence:** CMake rejected `find_package(CUDAToolkit ... PATHS ... NO_DEFAULT_PATH)` because those arguments selected config mode, while NVIDIA's local toolkit is discovered by CMake's `FindCUDAToolkit` module.
- **Failure criterion:** the pinned local toolkit must configure without falling back to another installation.
- **Conclusion:** CMake now pins `CMAKE_CUDA_COMPILER` and `CUDAToolkit_ROOT`, then uses the module and independently requires version 13.3.x.
- **Retry condition:** any toolkit upgrade must pass the same explicit compiler, root and version checks.

## NR-0038 - Strict CUDA warnings rejected an unused validation accessor

- **Date:** 2026-08-03
- **Change tested:** first compilation of the minimal transfer/kernel validator.
- **Evidence:** NVCC 13.3 stopped on an unused const overload under `--Werror=all-warnings`.
- **Failure criterion:** original PrimeForge CUDA code must compile warning-free; warning policy may not be weakened to obtain a build.
- **Conclusion:** the unused accessor was removed. CUDA warnings remain errors.
- **Retry condition:** every new CUDA source must pass the unchanged warning gate.

## NR-0039 - NVCC generated host stub triggers MSVC C4211 under /WX

- **Date:** 2026-08-03
- **Change tested:** MSVC `/W4 /WX` host compilation of the CUDA validation target.
- **Evidence:** original `validation.cu` compiled cleanly, but NVCC's generated registration stub emitted C4211 for a generated nonstandard extension and `/WX` stopped the build.
- **Failure criterion:** future third-party/generated diagnostics must not disable strict warnings for original PrimeForge code.
- **Conclusion:** `/wd4211` is limited to the NVCC-host-compiled validation target, where the generated stub and original host portions share a compiler invocation. CUDA warnings-as-errors and all other MSVC warnings-as-errors remain active; ordinary PrimeForge targets receive no suppression.
- **Retry condition:** remove the targeted suppression if a future toolkit no longer emits the generated construct.

## NR-0040 - Direct developer-shell loading was blocked by PowerShell policy

- **Date:** 2026-08-03
- **Change tested:** launch the optional CUDA preset from an ordinary PowerShell process.
- **Evidence:** direct dot invocation of `Launch-VsDevShell.ps1` was denied by the host execution policy before CMake or CUDA ran.
- **Failure criterion:** the documented command path must enter the installed MSVC environment without changing machine policy.
- **Conclusion:** the existing per-process `powershell.exe -NoProfile -ExecutionPolicy Bypass` pattern loads the official developer shell without changing system settings. The subsequent configure, build and 32-test gate passed.
- **Retry condition:** retain the per-process wrapper for automation launched outside Developer PowerShell.

## NR-0041 - Nested PowerShell command variables were expanded by the outer shell

- **Date:** 2026-08-03
- **Change tested:** one inline retry that embedded `$ErrorActionPreference` and `$LASTEXITCODE` inside a double-quoted nested command.
- **Evidence:** the outer shell consumed those variables, causing post-command parser diagnostics even though configure, no-op rebuild and 32/32 CTest completed successfully.
- **Failure criterion:** orchestration output must be unambiguous and must preserve child exit-code checks.
- **Conclusion:** repository automation remains file-based (`scripts/run_all.ps1`), and manual nested invocations use a single-quoted command body or Developer PowerShell. No source, binary or test result was changed by this shell-only error.
- **Retry condition:** do not place PowerShell variables in a double-quoted command passed through another PowerShell process.

## NR-0042 - CUDA cache reconfigure was attempted outside the MSVC environment

- **Date:** 2026-08-03
- **Change tested:** direct full-path CMake reconfigure after an optional CMake-file comment changed.
- **Evidence:** without Developer PowerShell, CMake saw `cl.exe` and Ninja disappear from the environment, invalidated the CUDA cache and stopped before build or test.
- **Failure criterion:** the optional CUDA workflow must be safe from an ordinary PowerShell prompt and must never fall back to another compiler or generator.
- **Conclusion:** `scripts/run_cuda_validation.ps1` now owns this workflow. It discovers Visual Studio with `vswhere`, loads its developer environment, pins the toolkit root and propagates every command failure. The damaged ignored cache is deleted only through its exact known build path before the clean retry.
- **Retry condition:** clean script execution must rebuild the CUDA configuration and pass all tests, explicit validation and Compute Sanitizer.

## NR-0043 - Global CPU/GPU profile selection is not yet scientifically valid

- **Date:** 2026-08-03
- **Change tested:** eligibility of the completed CPU sieve, modular CUDA backend and bounded pipeline evidence for PIVOT-07 selection.
- **Evidence:** CPU temperature and package power remain `UNKNOWN`; PIVOT-03 rankings were unstable; PIVOT-05/06 ran correctness-sized synthetic modular tasks with an intentionally conservative kernel; no final target family supplies representative end-to-end work.
- **Failure criterion:** PIVOT-07 requires stable disjoint validation, complete safety telemetry and a workload tied to the final engine path before retaining a profile.
- **Conclusion:** outcome `INCONCLUSIVE`. No timing rank, utilization result or fastest claim is retained. Simple one-worker/one-stream bounded fallback parameters remain active.
- **Retry condition:** select the target family, implement its complete candidate-to-proof workload, obtain validated CPU stability telemetry, then preregister calibration and held-out validation before reopening global selection.

## NR-0044 - A Proth novelty interval cannot be justified from the local evidence

- **Date:** 2026-08-03
- **Change tested:** eligibility of published PrimeGrid/Proth project descriptions and the local source audit for selecting a nonoverlapping discovery interval.
- **Evidence:** official pages establish active and historical coverage for Proth-form searches, but the repository has no dated, immutable and exhaustive machine-readable map covering every relevant `(k,n)` pair.
- **Failure criterion:** a novelty campaign must prove its interval does not duplicate known or assigned work before launch; absence of a local record is not evidence of absence.
- **Conclusion:** PIVOT-08 selects the mathematical family but no novelty interval. Development, PIVOT-09 comparison and PIVOT-10 known/local validation ranges remain permitted with `novelty_status=NOT_CHECKED`.
- **Retry condition:** archive and hash authoritative coverage snapshots, reconcile their interval semantics, check the proposed range twice and obtain separate authorization before any external assignment or publication.

## NR-0045 - The first proth20 parser missed one composite output form

- **Date:** 2026-08-03
- **Change tested:** validation parser for the pinned proth20 oracle.
- **Evidence:** the preliminary validation recognized `is composite,` but treated four `is divisible by ...` results as `UNKNOWN`.
- **Failure criterion:** every fixed reference case must have an unambiguous expected classification before a measured run.
- **Conclusion:** no measured run started with the faulty parser. The parser now accepts both explicit proth20 composite forms and validation classifies all 16 cases.
- **Retry condition:** extend parser fixtures whenever a newly pinned oracle version adds an output form; reject unknown output rather than infer a result.

## NR-0046 - The outer PIVOT-09 display timed out before its worker finished

- **Date:** 2026-08-03
- **Change tested:** one sequential seven-repetition reference run launched through the desktop shell wrapper.
- **Evidence:** the wrapper returned timeout status 124 after about 184 seconds while its owned PowerShell process remained active. The run was not relaunched; that same process completed the 224 rows, 14 telemetry records and 462-entry verified manifest, then exited. A follow-up found zero owned benchmark processes.
- **Failure criterion:** orchestration must not mistake a display timeout for benchmark failure or launch duplicate work.
- **Conclusion:** mathematical and artifact gates passed, but the wrapper duration was too short for the protocol. Evidence is retained and the incident is not hidden.
- **Retry condition:** use an explicitly yielded long-running cell or a wrapper timeout above the preregistered worst-case duration; always inspect owned processes before retrying.

## NR-0047 - The first native campaign write omitted proof-directory creation

- **Date:** 2026-08-03
- **Change tested:** first Debug CTest execution of the persisted native Proth certificate path.
- **Evidence:** compilation completed with zero warnings, then `primeforge.mvp_pipeline` failed immediately with `checkpoint parent directory does not exist`; the other 33 Debug tests passed.
- **Failure criterion:** a proof artifact must be committed atomically under a campaign-owned directory without relying on pre-existing filesystem state.
- **Conclusion:** the pipeline now creates the exact `proofs/proth` parent before atomic write. The complete Debug, Release and CUDA suites then passed, including interruption/recovery and strict on-disk parsing.
- **Retry condition:** none for this defect; retain the fresh-directory test and fail if it regresses.

## NR-0048 - The first integration draft did not bind proof policy to campaign identity

- **Date:** 2026-08-03
- **Change tested:** recovery review after changing the campaign primary proof path from PARI/GP to native Proth.
- **Evidence:** the first draft changed generated work-unit proof policy but left the canonical configuration hash unchanged. A partial v1 campaign therefore had an identity that did not describe the new executable semantics.
- **Failure criterion:** recovery must never combine an authenticated prefix produced under one proof policy with a suffix produced under another policy.
- **Conclusion:** no final evidence campaign used that identity. `primeforge.mvp.pipeline.v2` and the exact proof-policy identifier now enter canonical campaign hashing; the old partial checkpoint was rejected, then a fresh v2 stop/resume gate passed.
- **Retry condition:** increment the pipeline identity whenever a result-affecting implicit policy changes, and retain the old-checkpoint rejection gate.

## NR-0049 - PIVOT-11 cannot retain an end-to-end performance rank

- **Date:** 2026-08-03
- **Change tested:** Jacobi prefilter before bounded Proth modular exponentiation.
- **Evidence:** deterministic work counters fall from 132 to 34 exponentiations for the 34 campaign proofs and from 31,469 to 10,576 over the full 160-candidate prover corpus. All certificates and correctness gates remain exact.
- **Failure criterion:** an elapsed-time or energy claim additionally requires reproducible held-out end-to-end measurements and validated CPU stability telemetry.
- **Conclusion:** the operation reduction is retained as an algorithmic optimization; wall-time, energy and fastest claims remain `INCONCLUSIVE`/`NONE` because CPU temperature and package power are `UNKNOWN`.
- **Retry condition:** instrument a representative large-number proof workload, obtain validated CPU telemetry, preregister held-out repetitions and compare complete-pipeline results.

## NR-0050 - Official LibreHardwareMonitor probe returned invalid CPU sensor zeros

- **Date:** 2026-08-03
- **Change tested:** quarantined LibreHardwareMonitor v0.9.6 library probe against the Ryzen 9 9950X3D, using official archive SHA-256 `086D9F1B5A99E643EDC2CFAAAC16051685B551E4C5AC0B32A57C58C0E529C001`.
- **Evidence:** the library enumerated `Core (Tctl/Tdie)` and package-power sensors but returned `0` for value/minimum/maximum. Existing WMI, ACPI and running-monitor probes also returned no validated CPU sensor.
- **Failure criterion:** zero or absent readings cannot be treated as real temperature/power, and a prolonged campaign requires enforceable CPU thermal stop thresholds.
- **Conclusion:** CPU temperature and package power remain `UNKNOWN`; LibreHardwareMonitor is not integrated. No privileged driver, BIOS setting, voltage, power limit or fan control was changed.
- **Retry condition:** the owner may explicitly authorize a separately reviewed privileged sensor provider/driver, or provide an already running trustworthy sensor source with a stable documented API.

## NR-0051 - Direct unprivileged sensor-library reuse still returned zero values

- **Date:** 2026-08-03
- **Change tested:** direct local probes using both official LibreHardwareMonitor v0.9.6 and the already-installed HYTE/L-Connect sensor-library context, without elevation or hardware-setting changes.
- **Evidence:** the Ryzen package temperature and power sensors were enumerated but their current/minimum/maximum values stayed zero. A direct second HWiNFO SDK instance also could not initialize while the owning service was active.
- **Failure criterion:** zero, unavailable or exclusive-provider results cannot be treated as real CPU telemetry, and PrimeForge must not copy/load a proprietary transitive DLL without a separate license decision.
- **Conclusion:** no direct sensor DLL is integrated. PrimeForge instead consumes only the fresh read-only loopback response already produced by the target's L-Connect service, with strict range/freshness validation and `UNKNOWN` fallback.
- **Retry condition:** none for private target use; re-review the provider if L-Connect changes its response, hash or licensing, or before any public redistribution.

## NR-0052 - The first transitive sensor manifest label was outside the closed vocabulary

- **Date:** 2026-08-03
- **Change tested:** complete CUDA CTest gate after adding the non-redistributed HWiNFO DLL observed inside L-Connect to the distribution manifest.
- **Evidence:** 36/37 tests passed; `primeforge.license_distribution` rejected the invented classification `EXTERNAL_TRANSITIVE` before any sanitizer or campaign ran.
- **Failure criterion:** every distribution-manifest classification must belong to its tested closed vocabulary, even for a component that PrimeForge neither calls nor redistributes.
- **Conclusion:** the component now uses allowed classification `EXTERNAL`, while `TRANSITIVE_NOT_CALLED_OR_LOADED` preserves the more precise integration decision. No license scope or redistribution status changed.
- **Retry condition:** the complete CUDA gate must pass before commit.

## NR-0053 - The first 8 GiB test threshold overflowed its 32-bit literals

- **Date:** 2026-08-03
- **Change tested:** first direct Debug runtime test of the new low-memory watchdog gate.
- **Evidence:** the test expression `8U * 1024U * 1024U * 1024U` wrapped to zero before assignment; the production watchdog rejected that value with `minimum available RAM must be positive`.
- **Failure criterion:** a test threshold must represent the intended exact byte count and must not fail before exercising the low-memory decision.
- **Conclusion:** the test now uses 64-bit literals (`8ULL`); production rejection of zero is retained as a safety property.
- **Retry condition:** the direct runtime test and complete Debug/Release/CUDA gates must pass.
