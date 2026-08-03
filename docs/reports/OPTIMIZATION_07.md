# Optimisation 07 — lots PRP bornés et preuves Proth parallèles

Date : 2026-08-03

Code optimisé : `d06baafa3ea11cc6ce41949dd637f82a01cd5eb9`

Protocole PRP corrigé : `0d51552cc9ec488f52ca898ab7347cff93352005`

Sweep proof-workers : `55cba65098c7292d50d021db8ed6ea662ae9e650`

Correctif CSV invariant et profil final : `c8ae6cbad750df9afc198a7f0116da2347720236`

## Question mesurée

Le profil complet avant modification attribuait l'essentiel du temps à la
production et à l'écriture séquentielles des certificats Proth. Deux décisions
ont donc été mesurées séparément :

1. le seuil et la taille des sous-lots du PRP CPU/CUDA ;
2. le nombre de workers bornés pour calculer, sérialiser, hacher et écrire les
   certificats Proth, tout en conservant la sortie dans l'ordre canonique.

Le protocole interdit toute conservation sur intuition : ordre randomisé par
blocs, répétitions appariées, médiane et MAD, bootstrap apparié et identité
byte-à-byte des artéfacts. Les variantes 1, 2, 4, 8 et 16 workers utilisent le
même exécutable et les mêmes 32 768 candidats.

## Modification retenue

La preuve native utilise un ensemble borné de workers, avec quatre workers par
défaut sur la machine cible. Les tâches peuvent terminer dans n'importe quel
ordre, mais leur résultat est remis au sérialiseur dans l'ordre exact des
candidats. L'exception de plus petit index est la seule propagée. Les fichiers
restent écrits atomiquement et le manifeste, le checkpoint et le ledger ne
changent pas.

Le chemin PRP sait aussi exécuter des sous-lots bornés. Le routeur automatique
reste sur CPU sous 512 candidats et choisit CUDA à partir de 512. Aucun moteur
externe supplémentaire n'a été ajouté.

## Résultat contrôlé proof-workers

Chaque variante compte 15 répétitions mesurées après cinq passages de chauffe,
dans 15 blocs randomisés appariés.

| Workers | Médiane totale | MAD | Médiane preuve | Gain total vs 1 |
|---:|---:|---:|---:|---:|
| 1 | 3 517 115 200 ns | 82 823 600 ns | 2 317 193 300 ns | 0 % |
| 2 | 3 074 140 900 ns | 39 660 800 ns | 1 742 108 400 ns | 12,594819 % |
| **4** | **2 971 664 700 ns** | **48 969 600 ns** | **1 546 061 000 ns** | **15,508463 %** |
| 8 | 2 960 099 700 ns | 44 563 500 ns | 1 551 710 800 ns | 15,837283 % |
| 16 | 2 932 271 500 ns | 22 861 900 ns | 1 531 681 100 ns | 16,628506 % |

Pour quatre workers :

- économie totale : `545 450 500 ns`, soit `15,508462731 %` ;
- accélération totale : `1,183550486×` ;
- débit : `9 316,73` à `11 026,82 candidats/s`, soit `+18,355049 %` ;
- économie de l'étage preuve : `771 132 300 ns`, soit `33,278721288 %` ;
- accélération de l'étage preuve : `1,498772235×` ;
- gain apparié médian : `16,425363919 %` ;
- bootstrap apparié 95 % : `[15,803781487 % ; 18,097162003 %]` ;
- victoires appariées : `15/15`.

Seize workers sont nominalement 39,3932 ms plus rapides que quatre, mais quatre,
huit et seize appartiennent au même plateau de bruit après correction
Bonferroni sur les dix paires. Face à quatre, le gain apparié médian de seize
est `-0,049533 %`, avec intervalle familial
`[-1,377140 % ; 4,373887 %]`. Deux workers sont significativement plus lents.
La décision conservatrice est donc le plus petit membre du plateau rapide :
**quatre workers**.

## Routage et taille des lots PRP

Le sweep corrigé contient 210/210 mesures valides : 30 combinaisons, sept
répétitions appariées et un appel backend non chronométré dans chaque processus.
Le hash de résultat unique est
`430f77e57e47e8dbb5b457e8a24ea0688759ad26c02308e3737d6baca776dfce`.

| Sous-lot | CPU médian | CUDA médian | Décision directe |
|---:|---:|---:|---|
| 256 | 11 965 000 ns | 31 485 200 ns | CPU |
| 512 | 23 008 900 ns | 15 900 300 ns | CUDA |
| 16 384 | 5 592 800 ns | 558 800 ns | CUDA |

Le seuil mesuré et retenu est donc `512`. À 16 384 candidats, CUDA est
`10,0086×` plus rapide que CPU sur ce microbenchmark. Le mode automatique ne
présente aucun regret significatif sur les dix tailles. La taille 16 384 est
la meilleure taille PRP synthétique testée ; elle ne remplace pas encore le lot
pipeline 8 192 sans comparaison bout en bout dédiée.

## Déterminisme et vérification

Les 80 campagnes proof-workers donnent exactement les cinq mêmes hashes :

- `results.jsonl` : `1e003a08d10a97fcf0d0bcf2e86bdd1a654a07e1fcc353afdc0307d949a161c3` ;
- checkpoint : `cd6b446ffb998f0d2397686ff6da1c9cd991474b1bf14f829c17ab831692e4c8` ;
- manifeste : `fdb1f6b0fc752d55ac3d1a9701a52b8d83086ffa5e91fe739dd5bc195e77a7f0` ;
- couverture : `2d207b28472de89fd742e379cee946f00aa40584c0943f983cbc7387512f3b00` ;
- configuration : `34fe76303a1f8ea4aa5bfd28e4140db1062753044c56762034a878fbc6342a2e`.

Les 400 fichiers ont été rehachés : zéro divergence. Une vérification complète
a été exécutée pour chacune des cinq variantes ; les 75 autres campagnes sont
couvertes par identité byte-à-byte avec ces références. Les 80 workers et les
80 watchdogs ont terminé sous contrat, sans stderr ni arrêt forcé.

Debug et Release passent chacun 39/39 tests. CUDA passe 43/43 tests. Compute
Sanitizer signale zéro erreur sur le validateur, le PRP, la pipeline modulaire
et la pipeline complète. Le profil final a aussi été revérifié : 32 768
résultats, 1 506 premiers prouvés, 31 262 composés et 4 522 fichiers de
manifeste.

## Enveloppe matérielle observée

Sur les 80 campagnes :

- CPU : `73,125 °C` maximum et `156,925260 W` maximum ;
- GPU : `50 °C` maximum et `58,5 W` maximum ;
- RAM disponible : minimum `32 316 919 808` octets ;
- VRAM libre : minimum `12 605 MiB` ;
- WHEA : `0` ;
- throttling : aucun.

La température mémoire et le hotspot GPU restent `UNKNOWN`. Les lignes du
microbenchmark PRP n'ont pas de télémétrie GPU par mesure ; ses huit captures
de préflight atteignent 65,125 °C CPU, 109,112224 W CPU, 52 °C GPU et 62,60 W
GPU. Le driver absent des lignes brutes PRP est consigné `UNKNOWN` ; le profil
matériel séparé enregistre le pilote NVIDIA `610.74`.

## Profil post-optimisation et prochain goulet

À quatre workers, les médianes principales sont :

| Rang | Étage | Médiane | MAD |
|---:|---|---:|---:|
| 1 | Vérification indépendante FLINT | 1 884 215 900 ns | 54 658 600 ns |
| 2 | Preuve Proth native | 1 546 061 000 ns | 46 868 600 ns |
| 3 | I/O et finalisation | 401 113 800 ns | 6 136 300 ns |
| 4 | Checkpoint | 139 004 100 ns | 2 383 400 ns |
| 5 | Crible | 86 463 300 ns | 791 200 ns |

Les branches FLINT et preuve se recouvrent ; leurs durées ne doivent pas être
additionnées. H2D, kernel et D2H totalisent seulement `633 888 ns`, environ
`0,0213 %` du mur. Une nouvelle micro-optimisation CUDA ne peut donc pas
améliorer matériellement ce corpus. La prochaine hypothèse est un lot borné de
processus FLINT, en conservant l'ordre, la reprise et les mêmes artéfacts.

## Résultats rejetés et limites

Le premier sweep de routage au commit `d06baaf` est rejeté. Chaque mesure
lançait un processus neuf sans appel backend non chronométré ; le coût de
création du contexte CUDA était donc facturé au mode automatique. À 16 384,
la mesure invalide donnait 62,343 ms contre 0,5623 ms après correction, soit
une inflation `110,871×`. Le calendrier et le résultat étaient identiques :
il s'agit d'un défaut de protocole, pas de calcul.

Le sweep proof-workers final a été produit au commit `55cba65`. Les commits
suivants ne modifient pas le moteur : ils corrigent le contrôle du processus,
la conversion de télémétrie et l'écriture CSV. `raw.jsonl` reste l'autorité ;
`raw.csv`, `summary.csv` et `plateau-comparisons.csv` ont été régénérés au HEAD
avec des décimales invariantes et contrôlés par SHA-256. Aucun sweep de 80
campagnes n'a été répété pour une modification qui ne touche pas le binaire.

## Preuves versionnées

Les données brutes CSV/JSONL, calendriers, décisions, profils Nsight Systems,
graphiques et entrées exactes se trouvent sous
`benchmarks/baselines/optimization-07/`. `validation.json` conserve les portes
de build, de test, de sanitizer et la vérification finale du profil.
`SHA256SUMS` couvre le jeu complet.

## Contribution directe au logiciel final

Ce jalon accélère directement la chaîne finale candidat → PRP CUDA → preuve
Proth → vérification indépendante. Il réduit de 15,508 % le temps complet
contrôlé, sans modifier les résultats, certificats ou checkpoints, et fixe le
seuil CPU/CUDA à partir d'une mesure réelle sur la RTX 5080. Le travail sur la
preuve parallèle et le seuil 512 est terminé pour ce corpus. Il faudra revenir
sur la taille de lot uniquement avec une comparaison bout en bout plus large.
Le prochain travail porte immédiatement sur FLINT, désormais premier goulet
mesuré ; aucune nouvelle infrastructure n'est justifiée.
