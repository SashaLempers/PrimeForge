# Optimisation 03 — journal durable aligné sur les checkpoints

Date : 2026-08-03

Baseline précédente : `bbc5bebfe568049c520e1a61bbb7ac438b252cd3`

Code optimisé mesuré : `eff1161887ba7ce2ff16170faf44ecee402edb0b`

## Hypothèse et modification

Le moteur synchronisait le fichier de résultats après chacun des 160 candidats,
mais n'authentifiait la progression que tous les 16 candidats. Les lignes sont
désormais accumulées dans une mémoire bornée par l'intervalle de checkpoint,
puis écrites et synchronisées en une opération juste avant le checkpoint, la
fin ou un arrêt propre.

Un crash peut perdre uniquement le suffixe postérieur au dernier checkpoint,
qui n'était déjà pas authentifié. La reprise valide le préfixe durable et retire
les artefacts suffixes comme auparavant.

## Avant et après

| Backend | Médiane avant | Médiane après | Différence absolue | Amélioration |
|---|---:|---:|---:|---:|
| CPU | 450 264 400 ns | 366 340 100 ns | -83 924 300 ns | 18,639 % |
| CUDA | 472 203 200 ns | 364 994 000 ns | -107 209 200 ns | 22,704 % |
| auto → CPU | 445 388 700 ns | 365 729 000 ns | -79 659 700 ns | 17,885 % |

Les MAD après optimisation sont 3 950 500 ns, 2 288 300 ns et 4 150 000 ns.
Le temps d'I/O médian passe d'environ 75–78 ms à environ 14–15 ms. Le hash
logique reste
`f2286a3ee8de22ad750eb8d6dc1d845c457658fc23340327854ea6f98a683fd6`.

Le gain cumulé depuis le Commit A atteint 94,184 % sur CPU, 94,178 % sur CUDA
et 94,197 % en routage automatique.

## Validation

- Debug et Release : 37/37 tests.
- CUDA Release : 41/41 tests et quatre Compute Sanitizer sans erreur.
- Arrêt au candidat 37, reprise et vérification complète : `PASS`.
- Exécution reprise et ininterrompue byte-identiques :
  `60de12b757a3e5c6f265c3a06d68c3a1f9ad8fac0130d6368ddeae4125e1755c`.
- 42 mesures finales et six groupes, sans divergence.

Les données sont dans `benchmarks/baselines/optimization-03/`.

## Contribution directe au logiciel final

Le moteur écrit moins souvent sans réduire sa granularité de reprise ni son
authentification. Cette optimisation est terminée pour le format actuel. La
vérification FLINT (~200 ms), les checkpoints (~75 ms) et la preuve (~65 ms)
sont maintenant les principaux coûts à traiter.
