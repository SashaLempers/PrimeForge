# Optimisation 04 — classification FLINT par lot réel

Date : 2026-08-03

Baseline précédente : `eff1161887ba7ce2ff16170faf44ecee402edb0b`

Code optimisé mesuré : `a8c90dd7d5d1a12195be7133a7f9523d622274e0`

## Hypothèse et modification

La vague parallèle précédente lançait encore un processus Windows par survivant
PRP. L'oracle FLINT accepte désormais plusieurs entiers décimaux dans une seule
invocation et retourne exactement une classification par ligne, dans le même
ordre. `EngineAdapter::run_batch` fournit un contrat générique avec repli sériel ;
l'adaptateur FLINT exécute le vrai lot, vérifie strictement sa cardinalité et
conserve une sortie brute déterministe par requête.

Pour rester sous la limite de ligne de commande Windows, un lot dépassant
24 000 caractères d'arguments est découpé en sous-lots ordonnés. La RAM et la
ligne de commande restent ainsi bornées sans modifier l'ordre des résultats.

## Avant et après

Les résumés de ce tableau utilisent la médiane exacte des sept répétitions. Un
défaut découvert pendant ce jalon faisait auparavant choisir la cinquième valeur
triée au lieu de la quatrième ; les JSONL bruts étaient corrects et tous les
résumés historiques ont été régénérés avec la formule corrigée.

| Backend | Médiane avant | Médiane après | Différence absolue | Amélioration |
|---|---:|---:|---:|---:|
| CPU | 366 340 100 ns | 288 807 300 ns | -77 532 800 ns | 21,164 % |
| CUDA | 364 994 000 ns | 282 750 800 ns | -82 243 200 ns | 22,533 % |
| auto → CPU | 365 729 000 ns | 283 913 900 ns | -81 815 100 ns | 22,370 % |

Les MAD après optimisation sont respectivement 6 569 600 ns, 3 378 500 ns et
5 989 700 ns. Les trois backends produisent le même résultat lié à la nouvelle
provenance de l'oracle :
`0a6300a9b67205bad62779ebab241119e5f4df5ac45d7dd516c81eb43f64d776`.

Le gain cumulé depuis le Commit A corrigé atteint 95,415 % sur CPU, 95,490 %
sur CUDA et 95,496 % en routage automatique. La vérification indépendante reste
le premier poste à environ 122 ms, mais une seule création de processus remplace
les 34 créations de cette campagne.

## Validation

- Debug et Release : 37/37 tests dans chaque configuration.
- CUDA Release : 41/41 tests.
- Compute Sanitizer : quatre validations, `ERROR SUMMARY: 0 errors`.
- Test contractuel du lot : ordre `PROVEN_PRIME`, `COMPOSITE`, `PROVEN_PRIME`
  et sorties brutes par requête.
- Arrêt au candidat 37, reprise, manifeste final et vérification : `PASS`.
- Résultats repris et ininterrompus byte-identiques :
  `222a9648a304ee72013ba9623279232265ac072f5a9c1935e998eb7217b7c181`.
- 42 mesures finales, six groupes et aucune divergence entre backends.

Les données brutes, résumés corrigés, graphiques et hashes sont conservés sous
`benchmarks/baselines/optimization-04/`.

## Limites restantes

Le protocole utilise encore la ligne de commande et exécute les sous-lots
dépassant la borne séquentiellement. Il ne s'agit pas d'un serveur FLINT
persistant. Les températures, puissances et utilisations par répétition restent
`UNKNOWN` pour cette campagne courte ; aucune consommation n'est inférée.

## Contribution directe au logiciel final

Cette optimisation réduit directement la latence du chemin recherche → PRP →
preuve → vérification, sans relâcher l'indépendance mathématique ni la reprise.
Elle est terminée pour l'oracle `u64` actuel. Il faudra réévaluer le transport
des lots lorsque le moteur multiprécision produira des arguments dépassant
régulièrement la borne Windows.
