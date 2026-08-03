# Commit A — baseline mesurable du moteur `u64`

Date de mesure : 2026-08-03

Code mesuré : `1ea12fadc0f64ccf5540fff7639832480379dd06`

Référence antérieure : tag `baseline-u64-b0a3b96`

## Portée

Ce jalon fige le comportement du moteur `u64` existant et mesure ses étages. Il
n'introduit volontairement aucune optimisation algorithmique. Les profils
multiprécision de 256 à 4096 bits sont enregistrés, mais rejetés explicitement
tant que le moteur correspondant n'existe pas ; aucun résultat `u64` n'est
présenté comme représentatif de ces tailles.

La campagne utilise trois échauffements non conservés et sept répétitions par
backend. L'ordre CPU/CUDA/auto de la pipeline complète est déterministe mais
permuté entre les répétitions. Toutes les durées sont des nanosecondes entières.

## Environnement observé

- Git 2.53.0.windows.3 ; CMake 4.3.1-msvc1 ; Ninja 1.13.2.
- Visual Studio 2026 18.8.2 ; MSVC 19.51.36252 (`_MSC_FULL_VER=195136252`).
- Windows 10.0.26200, x86-64 ; AMD Ryzen 9 9950X3D, 16 cœurs physiques et
  32 processeurs logiques.
- NVIDIA GeForce RTX 5080, compute capability 12.0, pilote 610.74.
- CUDA Toolkit/runtime/API 13.3 ; `nvcc` 13.3.73.
- Les mesures de température, puissance et utilisation pendant chaque
  répétition sont `UNKNOWN` : elles n'ont pas été échantillonnées par cette
  courte campagne. La consommation réelle n'est pas déduite du TDP.

## Résultats retenus

Les deux expériences produisent des résultats strictement identiques entre les
backends :

- PRP sur 65 536 entiers :
  `430f77e57e47e8dbb5b457e8a24ea0688759ad26c02308e3737d6baca776dfce` ;
- pipeline complète sur 160 candidats :
  `f2286a3ee8de22ad750eb8d6dc1d845c457658fc23340327854ea6f98a683fd6`.

| Expérience | Backend demandé/routé | Médiane | MAD | CV |
|---|---:|---:|---:|---:|
| PRP `u64`, 65 536 valeurs | CPU | 1 717 200 ns | 59 400 ns | 0,056710 |
| PRP `u64`, 65 536 valeurs | CUDA | 247 900 ns | 23 000 ns | 0,153113 |
| PRP `u64`, 65 536 valeurs | auto → CUDA | 181 900 ns | 6 300 ns | 0,297355 |
| Pipeline complète, 160 candidats | CPU | 6 298 717 800 ns | 16 488 800 ns | 0,007501 |
| Pipeline complète, 160 candidats | CUDA | 6 269 633 200 ns | 15 974 600 ns | 0,004870 |
| Pipeline complète, 160 candidats | auto → CPU | 6 303 053 000 ns | 13 115 700 ns | 0,003075 |

Le benchmark PRP isolé montre un écart local favorable au GPU pour ce lot et
ce profil précis. Il ne constitue pas une revendication de performance globale.
Sur la pipeline complète, les distributions se chevauchent et aucune victoire
CPU/CUDA n'est revendiquée.

## Goulot mesuré

La médiane de la vérification indépendante vaut 6 025 002 400 ns sur le chemin
CPU, soit 95,654 % de la médiane totale. Les médianes correspondantes sont
5 999 156 900 ns (95,686 %) pour CUDA et 6 026 744 400 ns (95,616 %) pour
auto. Le PRP complet ne représente que 4 700 ns sur CPU et 273 024 ns sur CUDA
dans ce petit profil après criblage.

La conclusion mesurée est donc limitée : sur ce corpus, l'invocation et la
vérification indépendante FLINT dominent le temps de bout en bout. Le jalon
suivant devra d'abord établir les baselines externes demandées par le rapport,
puis construire la référence multiprécision. Le Commit A ne modifie pas ce
chemin afin de préserver une référence neutre.

## Validation

- Build propre non-CUDA : Debug et Release, zéro avertissement PrimeForge.
- CTest non-CUDA : 37/37 en Debug et 37/37 en Release.
- CTest CUDA Release : 41/41 en 18,39 s.
- Compute Sanitizer : validation, arithmétique modulaire, pipeline et PRP,
  quatre fois `ERROR SUMMARY: 0 errors`.
- Campagne Commit A : 42 mesures valides, six groupes ; analyse et graphiques
  générés sans divergence de résultat.

Les données brutes, résumés, graphiques et leurs hashes sont conservés sous
`benchmarks/baselines/commit-a/`. Les fichiers exécutables ne sont pas
versionnés ; leurs hashes exacts figurent dans chaque ligne brute.

## Contribution directe au logiciel final

Ce jalon donne au moteur une baseline vérifiable et localise son coût réel par
étage. Il est indispensable pour empêcher les optimisations CPU/GPU futures de
déplacer le coût ou de modifier silencieusement les résultats. L'instrumentation
`u64` et son format sont terminés pour cette phase ; il faudra étendre les mêmes
métriques au moteur multiprécision lorsqu'il sera implémenté. Aucun autre travail
d'infrastructure n'est prévu avant qu'un besoin direct du moteur ne l'exige.
