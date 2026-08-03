# Optimisation 05 — preuve Proth recouverte par FLINT

Date : 2026-08-03

Baseline précédente : `a8c90dd7d5d1a12195be7133a7f9523d622274e0`

Code optimisé mesuré : `18822512d1431428b7cc405ddd45e864ee0bfaa8`

## Hypothèse et modification

Après le regroupement FLINT, la vérification indépendante consommait environ
122 ms et la preuve Proth environ 65 à 71 ms. Ces calculs dépendent du même lot
PRP mais pas l'un de l'autre. PrimeForge calcule désormais les tentatives de
témoin Proth dans une tâche CPU pendant que le processus FLINT classe le lot.

La tâche parallèle ne produit aucun fichier. Après la jonction des deux calculs,
le thread principal valide les statuts, écrit les certificats et sérialise les
résultats dans l'ordre canonique. Cette frontière évite la course entre création
de processus et écritures durables observée dans la variante rejetée `NR-0055`.

## Mesures

Deux séries exploratoires indépendantes ont confirmé un gain d'environ 3 à 7 %
selon le backend. La série finale attribuable au commit ci-dessus donne :

| Backend | Médiane avant | Médiane après | Différence absolue | Amélioration |
|---|---:|---:|---:|---:|
| CPU | 288 807 300 ns | 274 626 300 ns | -14 181 000 ns | 4,910 % |
| CUDA | 282 750 800 ns | 274 229 200 ns | -8 521 600 ns | 3,014 % |
| auto → CPU | 283 913 900 ns | 269 488 900 ns | -14 425 000 ns | 5,081 % |

Les MAD finales sont respectivement 3 190 300 ns, 3 848 800 ns et
3 066 000 ns. Le résultat logique/provenance reste identique à l'optimisation
04 : `0a6300a9b67205bad62779ebab241119e5f4df5ac45d7dd516c81eb43f64d776`.

Le gain cumulé depuis Commit A atteint 95,640 % sur CPU, 95,626 % sur CUDA et
95,724 % en routage automatique. Les temps de preuve et de vérification sont
maintenant des travaux partiellement simultanés ; leur somme ne doit plus être
interprétée comme une décomposition exclusive du temps total.

## Validation

- Debug et Release : 37/37 tests dans chaque configuration.
- CUDA Release : 41/41 tests et quatre Compute Sanitizer sans erreur.
- Deux séries exploratoires de 42 mesures, puis 42 mesures finales.
- Arrêt au candidat 37, reprise et vérification du manifeste : `PASS`.
- Résultats repris et ininterrompus byte-identiques :
  `ffc1ec53213e051244c7d2fb2e074f979813d7247d4b680d13637a9d5cb20e9b`.

Les preuves brutes sont conservées sous
`benchmarks/baselines/optimization-05/`.

## Contribution directe au logiciel final

Ce jalon améliore directement l'ordonnancement hybride du moteur : le CPU ne
reste plus inactif pendant la classification FLINT. Il est terminé pour le lot
`u64` actuel. La concurrence devra être réévaluée sur les grandes preuves
multiprécision, où FLINT et le prouveur pourront exercer une pression CPU et RAM
différente.
