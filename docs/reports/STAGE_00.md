# Stage 0 — Governance

**Status:** PASS

**Date:** 2026-08-02

**Milestone commit:** `ef62c79d` (`stage 0: establish project governance`)

## Evidence

- Original specification SHA-256: `A87A3DDDF710E3080C9A3F1247644FF9505ECBCF470358015E9EDF19214A01C1`.
- Repository copy SHA-256: `A87A3DDDF710E3080C9A3F1247644FF9505ECBCF470358015E9EDF19214A01C1`.
- Repository: `https://github.com/SashaLempers/PrimeForge`.
- Visibility reported by GitHub: `PRIVATE`.
- Default branch: `main`; `origin/main` contains the milestone commit.
- Apache-2.0 is explicitly limited to original PrimeForge code. No dependency inherits that scope.
- H1 through H8 were recorded only as `HYPOTHESIS`.

## Commands used for the gate

```powershell
Get-FileHash 'C:\Users\sashack\Downloads\Programme_de_recherche_PrimeForge_pas_a_pas_2026-08-02.txt' -Algorithm SHA256
Copy-Item -LiteralPath 'C:\Users\sashack\Downloads\Programme_de_recherche_PrimeForge_pas_a_pas_2026-08-02.txt' -Destination 'docs\specifications\Programme_de_recherche_PrimeForge_pas_a_pas_2026-08-02.txt'
Get-FileHash 'docs\specifications\Programme_de_recherche_PrimeForge_pas_a_pas_2026-08-02.txt' -Algorithm SHA256
git init -b main
git config --local user.name 'Sasha Lempers'
git config --local user.email '263967017+SashaLempers@users.noreply.github.com'
git add --all
git commit -m 'stage 0: establish project governance'
gh repo create SashaLempers/PrimeForge --private --source . --remote origin --push
git status --short
gh repo view SashaLempers/PrimeForge --json nameWithOwner,url,visibility,defaultBranchRef
git remote -v
```

The copy operation is recorded here for reproducibility. The stage 0 working-tree copy matched the original. A stage 1 audit found that the stage 0 Git blob had been transparently normalized by `core.autocrlf`; `.gitattributes` now treats specifications as binary, and the corrected index blob has the exact original SHA-256 shown above. No CMake, CTest, self-test, or `run_all.ps1` gate was applied to stage 0.
