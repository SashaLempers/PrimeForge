# Optimisation 01 — mise en cache de la provenance FLINT

Date : 2026-08-03

Baseline : `1ea12fadc0f64ccf5540fff7639832480379dd06`

Code optimisé mesuré : `c140b36a8645ab97e38ffee602d0391888160aaf`

## Hypothèse

Le Commit A attribuait environ 95,6 % du temps complet à la vérification
indépendante. L'audit du chemin réel a montré que chaque candidat provoquait
deux lectures et deux calculs SHA-256 de l'exécutable FLINT et de ses quatre
fichiers d'exécution. Pour 34 classifications, environ 750 Mo étaient donc
rehashés alors que l'installation ne change pas pendant la durée de vie de
l'adaptateur.

L'optimisation testée calcule et compare tous les hashes attendus exactement
une fois avant la première invocation d'une instance. Chaque invocation reste
isolée, conserve ses sorties brutes, vérifie leur présence et échoue en
`UNTESTED` si la provenance initiale ou les artefacts sont invalides.

## Protocole

- Même profil complet `FULL_U64_KNOWN_160` et mêmes exécutables externes.
- Trois échauffements non retenus et sept répétitions par backend.
- Ordre CPU/CUDA/auto permuté de façon déterministe.
- Comparaison contre les données retenues du Commit A.
- Hash de résultat exigé identique entre toutes les répétitions et variantes.

## Avant et après

| Backend | Médiane avant | Médiane après | Différence absolue | Amélioration |
|---|---:|---:|---:|---:|
| CPU | 6 298 717 800 ns | 895 259 600 ns | -5 403 458 200 ns | 85,787 % |
| CUDA | 6 269 633 200 ns | 884 712 500 ns | -5 384 920 700 ns | 85,889 % |
| auto → CPU | 6 303 053 000 ns | 887 272 200 ns | -5 415 780 800 ns | 85,923 % |

Les MAD après optimisation sont respectivement 5 940 800 ns, 3 510 600 ns et
2 255 700 ns. Le hash de résultat reste exactement :
`f2286a3ee8de22ad750eb8d6dc1d845c457658fc23340327854ea6f98a683fd6`.

La vérification indépendante reste le premier poste, avec une médiane de
644 936 300 ns sur CPU, 641 253 300 ns sur CUDA et 642 521 400 ns sur auto.
Elle représente désormais environ 72 % du temps complet. Le prochain essai
doit donc mesurer le coût de lancement, le calcul FLINT et les I/O, puis tester
le regroupement ou un processus persistant sans affaiblir la vérification.

## Validation

- Test dédié : deux classifications successives, résultats exacts et seulement
  deux appels SHA-256 (exécutable plus exigence d'exécution) au lieu de quatre.
- Debug et Release : 37/37 tests dans chaque configuration.
- CUDA Release : 41/41 tests.
- Compute Sanitizer : quatre validations, zéro erreur.
- Arrêt après le candidat 37, reprise, vérification finale : `PASS`.
- Exécution reprise et exécution ininterrompue : résultats byte-identiques,
  SHA-256 `cdc70610b5385dd17b32eaf4a72e9d311675345c13205997d7fbd99c19cffc4d`.

Les données brutes, résumés, graphiques et leur manifeste se trouvent dans
`benchmarks/baselines/optimization-01/`.

## Limitation explicite

La provenance est liée à la durée de vie de l'instance d'adaptateur : une
modification externe des DLL après le contrôle initial et avant sa destruction
n'est pas rehashée à chaque candidat. PrimeForge ne modifie jamais ces fichiers,
et une reprise construit une nouvelle instance et refait le contrôle complet.
Ce compromis supprime un travail redondant mesuré tout en conservant un contrôle
cryptographique exact à chaque lancement ou reprise de campagne.

## Contribution directe au logiciel final

Cette optimisation accélère le chemin produit réel sans modifier le crible, le
PRP, les preuves, les certificats, les résultats ou la reprise. Elle est retenue
définitivement pour le moteur actuel. Il faudra réévaluer la granularité du
contrôle si les moteurs externes deviennent rechargeables à chaud.
