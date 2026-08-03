# PIVOT-10 — Limited known-range campaign

**Status:** COMPLETE  
**Run:** `20260803T011727Z`  
**Implementation commit:** `2cf0264e70c5b231b84f0a2fdda761935d3505ba`

## Scope

The gate used the complete known 160-candidate `uint64_t` Proth domain defined by
`benchmarks/pivot10/known_proth_small.yaml`. It is a local controlled validation
range, not a novelty interval. Every record retains `novelty_status=NOT_CHECKED`;
no external work was requested and no discovery is claimed.

The pipeline identity is `primeforge.mvp.pipeline.v2` with policy
`native_proth_then_flint_with_pari_fallback`. A positive base-2 PRP first reaches
the native exact Proth prover. Persisted certificates are strictly parsed and
replayed; each verdict then requires a separately pinned FLINT process. PARI/GP
remains a rigorous fallback only when the native witness bound returns
`UNTESTED`.

## Recovery and verification gate

The committed Release executable started a fresh campaign, stopped cooperatively
after 37/160 candidates, and left a checkpoint without final coverage or
manifest. `resume` authenticated that prefix and completed all 160 candidates.
`verify` then validated the exact manifest, ledger, checkpoint, all native
certificates, stored FLINT outputs and fresh independent FLINT decisions.

- exact records: 160;
- native Proth certificates: 34;
- proven primes: 34;
- composites: 126;
- PARI/GP fallback invocations: 0 in this known domain;
- manifest entries: 106;
- verifier status: `PASS`.

The final `results.jsonl` SHA-256 is
`84AB8B59EC6CE7B4FE802412D50E33638B03D4D831A910F096D679130DAFB0CF`.
The final `MANIFEST.sha256` SHA-256 is
`D500BB069C8FA90C869689181CD9173ED192FBEC13606543DBC98E5145FA5E4C`.
The 34 certificate identities are retained in the tracked
`benchmarks/pivot10/results/20260803T011727Z/certificates.tsv`; the complete
campaign directory remains local and ignored.

## Build and safety gates

- Debug: 34/34 CTest tests passed in 48.30 s.
- Release: 34/34 CTest tests passed in 14.88 s.
- CUDA: 37/37 CTest tests passed in 15.80 s.
- Compute Sanitizer: validation, modular batch and async pipeline each reported
  zero errors.
- GPU maximum around the short campaign: 51 °C and 44.73 W.
- GPU throttling: none observed.
- minimum available RAM: 43,292,905,472 bytes.
- minimum free VRAM: 13,552 MiB.
- CPU temperature, CPU package power, GPU memory temperature and energy:
  `UNKNOWN`.

This campaign is a correctness/recovery gate. `performance_valid=NO` and
`performance_claim=NONE`; no timing rank is inferred.

## Contribution directe au logiciel final

Ce jalon remplace réellement le chemin de preuve générique du logiciel par le
prouveur Proth natif pour tout premier candidat compatible. Il démontre le flux
complet attendu du produit : famille, crible, filtre PRP, preuve native,
certificat durable, vérification indépendante, arrêt, reprise et manifeste. Le
jalon est terminé pour le domaine borné 64 bits. Il faudra y revenir pour les
grands entiers et pour brancher les lots CPU/GPU, mais pas pour ajouter davantage
d'infrastructure. Le prochain travail doit mesurer le pipeline complet, choisir
son goulot dominant, puis optimiser uniquement ce goulot sans affaiblir les
preuves.
