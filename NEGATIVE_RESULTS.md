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
