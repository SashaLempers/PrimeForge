# PrimeForge MVP for Windows x64

This private MVP searches the known finite family `N = k*2^n+1`, saves every
candidate result, resumes after interruption and independently verifies the
completed campaign.

## Prerequisites

- Windows x64;
- the separately installed, exact PARI/GP and FLINT oracle files described in
  `ORACLES.md`;
- no external file from `ORACLES.md` is included in this archive.

Place the external files at their documented paths below this extracted folder.
PrimeForge checks the executable and FLINT runtime-file hashes before starting a
proof or verification process.

## One-command known campaign

### Graphical launcher (recommended)

Double-click `primeforge-launcher.exe`. It starts a new campaign automatically,
resumes the authenticated checkpoint when one exists, and verifies a completed
campaign. The **Arrêter proprement** button, `Ctrl+C` in the launcher window, and
closing the window all request the same cooperative stop: PrimeForge finishes
the current candidate, durably writes the result prefix and checkpoint, and
then exits. Starting the launcher again resumes from that checkpoint.

The live PrimeForge output is displayed in the launcher window. The command-line
engine remains `primeforge.exe` beside it.

From this extracted directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_known_campaign.ps1
```

The command starts a new campaign, resumes an interrupted one, or verifies an
already completed one. `Ctrl+C` requests a clean checkpoint after the current
candidate.

## Direct commands

```powershell
.\primeforge.exe selftest
.\primeforge.exe inspect --config .\search.yaml
.\primeforge.exe search --config .\search.yaml
.\primeforge.exe resume --checkpoint .\out\campaigns\known-proth-small\campaign.checkpoint.json
.\primeforge.exe verify --result .\out\campaigns\known-proth-small\results.jsonl
```

Successful completion produces `results.jsonl`, `coverage_report.json`,
`campaign.checkpoint.json`, `MANIFEST.sha256`, raw engine outputs and PARI proof
certificates below `out\campaigns\known-proth-small`.

`PROBABLE_PRIME` is never presented as `PROVEN_PRIME`. Novelty is not checked.
The MVP supports only the bounded uint64 Proth family and uses the CPU search
path; CUDA and arbitrary-size campaigns follow after this release.

From a PrimeForge source checkout, verify the archive contents with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify_mvp_package.ps1 `
  -PackagePath <PrimeForge-archive.zip>
```

The verification script is intentionally a source-repository tool and is not
required at runtime; the archive carries its own `PACKAGE_MANIFEST.sha256`.
