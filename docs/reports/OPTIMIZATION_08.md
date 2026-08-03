# Optimisation 08 — parallélisme de vérification FLINT rejeté

Date : 2026-08-03

Commit candidat mesuré : `14fa5c29e83f9ca6ab32196b3da00665f1a61773`

Décision : **`KEEP_SINGLE_PROCESS`**

## Question mesurée

Après OPTIMIZATION-07, la vérification indépendante FLINT était le plus long
étage observé du profil. Le candidat O08 répartissait le même lot ordonné entre
1, 2, 4 ou 8 processus FLINT bornés. Chaque vague était rejointe avant la
preuve et les écritures durables ; l'ordre des résultats, le checkpoint, les
certificats et le manifeste devaient rester strictement identiques.

Le code candidat ajoutait aussi `--flint-processes`. Il est conservé dans
l'historique Git pour rendre l'expérience reproductible, mais le parallélisme
d'exécution sera retiré du moteur retenu : une optimisation non démontrée ne
justifie ni son coût ni sa complexité.

## Protocole

Le workload exact est `full_u64_high_32768.yaml`, soit 32 768 candidats, avec
PRP `auto`, lots PRP de 8 192 candidats et quatre workers de preuve Proth. La
commande variable était :

```text
primeforge search --config benchmarks\profiles\full_u64_high_32768.yaml --prp-backend auto --prp-batch-candidates 8192 --proof-workers 4 --flint-processes {1|2|4|8}
```

Un bloc de chauffe randomisé, puis sept blocs mesurés appariés et randomisés
ont été exécutés avec la graine `20260804`. Cela représente 32 campagnes :
quatre chauffes et 28 mesures. La métrique primaire est `metrics.total_ns` et
la métrique secondaire `metrics.verification_ns`.

La porte, déclarée dans le harnais avant la mesure, exigeait simultanément :

1. un gain total apparié médian d'au moins 3 % ;
2. une différence des médianes supérieure à deux fois le plus grand MAD ;
3. un intervalle bootstrap apparié bilatéral entièrement positif, avec famille
   à 95 % et correction de Bonferroni sur les trois comparaisons au baseline.

Le rapport des médianes reste descriptif ; il ne remplace pas la statistique
appariée utilisée par la porte.

## Résultats exacts

| Processus FLINT | Médiane totale | MAD total | Médiane vérification | MAD vérification | Gain total descriptif vs 1 | Gain total apparié médian | Victoires appariées | Porte |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **1** | **2 931 084 900 ns** | **18 257 200 ns** | **1 880 573 000 ns** | **26 906 600 ns** | 0 % | 0 % | — | `BASELINE` |
| 2 | 2 934 198 400 ns | 26 029 700 ns | 1 882 610 700 ns | 23 402 700 ns | -0,106223467 % | -0,614516265 % | 2/7 | `FAIL` |
| 4 | 2 964 138 200 ns | 44 874 700 ns | 1 903 903 400 ns | 34 212 300 ns | -1,127681426 % | -1,041417789 % | 1/7 | `FAIL` |
| 8 | 3 021 908 300 ns | 24 273 400 ns | 1 966 332 400 ns | 26 303 500 ns | -3,098627406 % | -1,297182803 % | 2/7 | `FAIL` |

Par rapport à un processus, les médianes totales sont plus lentes de :

- `3 113 500 ns` avec deux processus ;
- `33 053 300 ns` avec quatre processus ;
- `90 823 400 ns` avec huit processus.

Les médianes de vérification sont elles aussi plus lentes de respectivement
`2 037 700 ns`, `23 330 400 ns` et `85 759 400 ns`. Aucune variante parallèle
n'atteint même un gain apparié positif, encore moins le seuil de 3 %. Aucune
économie positive des médianes ne dépasse deux fois le plus grand MAD. Les intervalles bootstrap
familiaux sont `[-89,455166 % ; 1,426701 %]`,
`[-74,883082 % ; 4,402130 %]` et `[-4,097053 % ; 41,788062 %]` pour 2, 4 et
8 processus : leur borne basse n'est jamais positive.

## Anomalies du septième bloc

Le septième bloc contient trois temps anormalement élevés, tous conservés dans
les données brutes :

| Ordre du bloc 7 | Processus | Total | Vérification | Preuve |
|---:|---:|---:|---:|---:|
| 1 | 8 | 2 970 654 600 ns | 1 899 876 500 ns | 1 504 287 800 ns |
| 2 | 1 | 5 103 170 800 ns | 3 079 517 800 ns | 3 604 841 600 ns |
| 3 | 2 | 9 668 220 700 ns | 6 100 962 800 ns | 8 399 724 100 ns |
| 4 | 4 | 8 924 582 400 ns | 6 437 038 600 ns | 7 615 928 300 ns |

Les passages 1, 2 et 4 processus sont donc des outliers temporels dans ce
bloc ; la hausse simultanée de l'étage preuve montre que le phénomène ne se
limite pas à FLINT. La cause exacte reste `UNKNOWN`. Elle n'est pas thermique :
les maxima CPU de ces quatre passages sont 69,5, 66,5, 68,375 et 68,125 °C,
avec zéro WHEA et aucun throttling. Aucune ligne n'a été supprimée après coup.
Ces points élargissent fortement les intervalles bootstrap, mais ne changent
pas les médianes par variante ni le constat qu'aucun parallélisme ne démontre
un gain. Leur conservation rend la décision de rejet plus conservatrice.

## Exactitude, reprise et enveloppe matérielle

Les 32 campagnes ont le même identifiant logique et les cinq mêmes hashes :

- campagne : `sha256:26662e149e7f97d7b67ebbfabef95e7151165d986f026221c8fc375f63c3cc2a` ;
- résultats : `1e003a08d10a97fcf0d0bcf2e86bdd1a654a07e1fcc353afdc0307d949a161c3` ;
- checkpoint : `cd6b446ffb998f0d2397686ff6da1c9cd991474b1bf14f829c17ab831692e4c8` ;
- manifeste : `fdb1f6b0fc752d55ac3d1a9701a52b8d83086ffa5e91fe739dd5bc195e77a7f0` ;
- couverture : `2d207b28472de89fd742e379cee946f00aa40584c0943f983cbc7387512f3b00` ;
- configuration : `34fe76303a1f8ea4aa5bfd28e4140db1062753044c56762034a878fbc6342a2e`.

Le premier passage de chaque variante a reçu la vérification complète avec
restaging et replay ; les 28 autres passages ont été validés contre ces hashes.
Tous les workers et watchdogs ont satisfait leur contrat de sortie.

Sur l'ensemble des campagnes, le maximum CPU est `70,625 °C` et
`155,18671502512302 W`, le maximum GPU `50 °C` et `59,78 W`. Le minimum de RAM
disponible est `35 011 301 376` octets et le minimum de VRAM libre `12 415 MiB`.
Le nombre maximal d'erreurs WHEA est zéro et aucun throttling n'est détecté.

## Décision et portée

La décision finale est **`KEEP_SINGLE_PROCESS`**. Les variantes 2, 4 et 8 sont
rejetées pour ce moteur, ce workload et cette machine. Le runtime parallèle du
commit candidat sera revert ; les preuves négatives restent versionnées. Il
n'existe donc aucune revendication d'accélération O08.

Un nouvel essai ne serait justifié qu'avec un changement matériel de la charge :
nombres beaucoup plus grands, API FLINT persistante supprimant les lancements,
ou partition explicite des ressources entre vérification FLINT et preuve Proth.
Il devrait disposer d'un protocole preregistré distinct et de plus de répétitions.

## Limites

- Sept répétitions mesurées suffisent pour rejeter l'hypothèse de gain selon la
  porte déclarée, mais pas pour estimer précisément un effet inférieur à 3 %.
- Le septième bloc a subi une perturbation système non attribuée ; elle est
  documentée et conservée, sans exclusion post hoc.
- Le shell de lancement n'exposait pas `cmake`, `ninja` ou `nvcc` dans son
  `PATH`; `environment.json` enregistre donc ces trois versions comme
  `UNAVAILABLE`. Les hashes de l'exécutable, du watchdog et du moniteur
  identifient néanmoins exactement les binaires mesurés.
- Le sweep est Windows/MSVC/CUDA/FLINT sur la RTX 5080. La CI Linux ne possède
  ni ce bundle FLINT Windows ni cette RTX et ne peut pas valider la performance
  réelle du pool de processus. Ses tests avec adaptateur factice ne remplacent
  pas cette mesure. Le runtime candidat étant rejeté, aucune conclusion Linux
  n'est transposée au moteur retenu.
- La CI `linux-gcc` du candidat `14fa5c2` a en outre échoué avant les tests sous
  `-Werror=range-loop-construct` : la boucle
  `for (const auto [begin, end] : ranges)` copiait chaque paire. La CI Windows
  était encore en cours au moment de cette décision. Comme toute la partition
  parallèle échoue déjà à la porte de performance et doit disparaître, le hunk
  fautif est supprimé par le revert au lieu d'être corrigé et conservé. Aucun
  résultat Linux ou Windows CI en cours n'est utilisé pour sauver le candidat.

## Preuves versionnées

Les données brutes CSV/JSONL, le calendrier randomisé, les statistiques, les
hashes déterministes, l'environnement et la décision sont sous
`benchmarks/baselines/optimization-08/`. Les gros répertoires de campagne et
les logs répétitifs ne sont pas copiés ; leurs sorties logiques sont couvertes
par `determinism-hashes.csv`. `SHA256SUMS` couvre tous les fichiers conservés.

## Contribution directe au logiciel final

Ce jalon falsifie une optimisation du premier goulet mesuré sans compromettre
les résultats, certificats ou checkpoints. Il évite de livrer un pool de
processus plus complexe et jusqu'à 3,099 % plus lent en médiane totale. Cette
expérience est terminée pour le workload 32 768 candidats actuel : le moteur
final reste volontairement sur un seul processus FLINT. Il faudra y revenir
uniquement si la granularité arithmétique ou le mode d'intégration FLINT change
assez pour modifier le rapport entre coût de lancement, contention CPU et
travail utile.
