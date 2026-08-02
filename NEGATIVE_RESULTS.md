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
- **Conclusion:** after all orphaned build processes ended, a single non-overlapping clean run completed successfully: Debug 2/2, Release 2/2, and both self-tests PASS. No source-code defect was found.
- **Retry condition:** full build commands must receive a timeout longer than the whole gate and must never be relaunched while child processes remain active.
