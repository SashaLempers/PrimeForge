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
