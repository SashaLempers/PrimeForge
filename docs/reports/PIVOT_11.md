# PIVOT-11 — Jacobi-filtered Proth proof kernel

**Status:** COMPLETE ALGORITHMICALLY — END-TO-END PERFORMANCE INCONCLUSIVE  
**Date:** 2026-08-03

## Change

The bounded native Proth prover now evaluates `Jacobi(a,N)` before its expensive
modular exponentiation. Only bases with symbol `-1` reach `power_mod`. This is an
exact mathematical filter, not a heuristic: any successful Proth witness implies
that `N` is prime, and Euler's criterion then requires that symbol.

`ProofAttempt` exposes separate `bases_tested` and `modular_exponentiations`
counters. The CLI prints both, making the optimization directly auditable without
using elapsed time as a proxy.

## Exact retained evidence

| Scope | Unfiltered exponentiations | Filtered exponentiations | Certificates |
|---|---:|---:|---:|
| 34 native proofs used by the known campaign | 132 | 34 | 34 unchanged |
| complete 160-candidate prover corpus | 31,469 | 10,576 | 34 unchanged |

The shared `43*2^32+1` certificate still uses witness 3 and retains SHA-256
`7172a2acdbae79bc90671dacafa01761d5bc632f2650ad4a4674d213efccfd22`.
All 126 known composites still produce no certificate.

## Gates

- Debug: 34/34 CTest tests passed in 48.62 s.
- Release: 34/34 CTest tests passed in 14.83 s.
- CUDA: 37/37 CTest tests passed in 15.65 s.
- Compute Sanitizer: three runs, zero errors.
- GPU before the short gate: 50 °C, 44.45 W, no throttling.
- CPU temperature and package power: `UNKNOWN`.

The exact operation reduction is reproduced. No end-to-end timing, energy or
engine-ranking claim is retained: `performance_valid=NO` and
`performance_claim=NONE`.

## Contribution directe au logiciel final

Ce jalon retire directement des exponentiations inutiles du noyau de preuve qui
sera ensuite porté vers les grands entiers et les lots CPU/GPU. Il améliore donc
le moteur lui-même, sans ajouter d'infrastructure. Le filtre Jacobi borné est
terminé ; il faudra réutiliser le même principe dans le futur prouveur
multi-précision. L'optimisation temporelle globale reste à rouvrir sur une charge
représentative avec télémétrie CPU valide. Une campagne prolongée n'est pas
lancée ici et reste soumise à l'autorisation explicite prévue par le plan.
