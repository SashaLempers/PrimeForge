# Audit de l’état de performance de PrimeForge

Date de l’audit : 2026-08-12

Périmètre inspecté : historique jusqu’à `e783a36290082583b10cf1cdadea573d1cc0548b`,
rapports O01 à O08, candidat O09, scripts de campagne, patch Proth20, chemins de
build et checkpoints. Cet audit classe l’état réellement observable ; il ne
transforme aucune mesure historique en revendication générale de performance.

## Signification des états

- `ACTIF` : présent dans un chemin d’exécution courant.
- `VALIDÉ` : exactitude et portes annoncées étayées par des tests ou artefacts.
- `EXPÉRIMENTAL` : implantation présente, décision de performance non acquise.
- `REJETÉ` : variante mesurée puis écartée.
- `OBSOLÈTE` : mécanisme remplacé ; son historique reste utile.
- `INCONNU` : preuve comparable ou mesure nécessaire absente.

Un élément peut cumuler plusieurs états, par exemple `ACTIF + VALIDÉ`.

## État du dépôt et de l’exécution

- aucune instance de `primeforge`, `proth20-batch`,
  `primeforge-discovery-sieve` ou `benchmark_watchdog` ne tournait au moment de
  l’audit ; aucune campagne active n’a donc été interrompue ;
- `main` et `origin/main` pointaient sur `279a7c7` au début de l’audit ;
- le jalon de cible prise en charge était isolé sur
  `engine/discovery-unblock-2026-08-12` à `e783a36`, puis a été intégré après
  CI verte sur `main` à `d9eb4e8` ;
- la baseline O09 est conservée dans un worktree détaché à `96ec797` ;
- l’historique utile O01 à O09 est contenu par les branches courantes ; aucun
  checkout destructif ni réécriture de l’historique n’a été effectué.

## Deux moteurs distincts sont actuellement présents

| Chemin | État | Usage réel | Limite principale |
|---|---|---|---|
| Pipeline native `primeforge.exe` | `ACTIF + VALIDÉ` | campagnes Proth dont les valeurs tiennent sur 64 bits, avec crible, PRP CPU/CUDA, preuve native, FLINT et reprise | ne traite pas les candidats d’environ 10 000 chiffres |
| Pipeline de découverte multiprécision | `ACTIF + VALIDÉ` sur la validation courte | `primeforge-discovery-sieve.exe` produit les survivants, puis le fork Proth20/OpenCL les teste par unités sous watchdog | pas de backend CUDA multiprécision natif et performance bout en bout pas encore comparée |

Les optimisations O01 à O09 concernent principalement le premier chemin. Elles
ne doivent pas être présentées comme une accélération déjà démontrée de la
campagne Proth20 à 10 040 chiffres.

## État des optimisations historiques

| Jalon | État actuel | Décision fondée sur les artefacts |
|---|---|---|
| O01 — cache de provenance FLINT | `ACTIF + VALIDÉ` | retenu ; médiane complète réduite d’environ 85,8 % contre Commit A sur le corpus de 160 candidats |
| O02 — vague de processus FLINT | `OBSOLÈTE` | gain historique validé, puis mécanisme remplacé par le vrai lot FLINT O04 ; la variante qui chevauchait les écritures a été rejetée (`NR-0055`) |
| O03 — flush aligné sur checkpoint | `ACTIF + VALIDÉ` | retenu ; écritures bornées par l’intervalle de checkpoint et reprise byte-identique |
| O04 — vrai lot FLINT | `ACTIF + VALIDÉ` | retenu ; une invocation remplace les invocations par résultat, avec sous-lots bornés |
| O05 — preuve Proth recouverte par FLINT | `ACTIF + VALIDÉ` | retenu ; gain complet mesuré de 3,0 à 5,1 % selon le backend sur le corpus historique |
| O06 — SHA-256 Windows CNG | `ACTIF + VALIDÉ` | retenu ; octets identiques au fournisseur portable, sans dépendance externe ajoutée |
| O07 — sous-lots PRP et quatre workers de preuve | `ACTIF + VALIDÉ` | retenu ; seuil automatique CPU/CUDA fixé à 512, quatre workers choisis comme plus petit membre du plateau rapide |
| O08 — 2, 4 ou 8 processus FLINT | `REJETÉ` | toutes les variantes ont échoué à la porte ; jusqu’à 3,099 % plus lent en médiane totale ; code candidat reverté |
| O09 — journal FLINT compact | `ACTIF + VALIDÉ` pour l’exactitude, `EXPÉRIMENTAL + INCONNU` pour la performance | format V3, authentification, reprise et vérification sont testés ; aucune comparaison A/B CUDA valide et versionnée ne permet encore d’annoncer un gain |
| instrumentation et lots de certificats de `361d7e2` | `ACTIF + VALIDÉ` fonctionnellement, performance `INCONNU` | le code courant trace les attentes et compacte les preuves ; aucune décision avant/après autonome n’est archivée |

Le harnais O09 existe dans `scripts/run_optimization09_compact_evidence.ps1`.
La tentative locale trouvée sous `out/benchmarks/optimization-09/` est invalide
pour une décision : le worktree baseline `96ec797` ne possède pas de binaire
CUDA comparable, alors que le candidat en possède un. Il n’existe ni rapport
`OPTIMIZATION_09.md` ni baseline O09 versionnée. O09 ne reçoit donc aucune
revendication d’accélération.

## Chemin réellement utilisé par la prochaine campagne

La cible validée est `n=33326`, `k` impair de `21909339` à `21989339`, soit
40 001 candidats initiaux et 4 012 survivants avec la borne 65 521.

Le chemin d’exécution est :

1. `out/build/msvc-release/primeforge-discovery-sieve.exe` génère et crible la
   plage de façon déterministe ;
2. `scripts/run_prime_discovery.ps1` découpe les survivants en unités de 100 par
   défaut, donc 41 unités pour cette cible ;
3. chaque unité lance `out/oracles/proth20-batch/proth20-batch.exe` avec un seul
   contexte OpenCL persistant pendant l’unité et `--stop-on-prime` ;
4. `out/build/msvc-release/benchmark_watchdog.exe` surveille le processus à une
   seconde d’intervalle ;
5. les marqueurs complets des journaux reconstruisent `results.tsv`, puis
   `checkpoint.json` est remplacé atomiquement ;
6. une relance ignore les `k` déjà présents et reprend au premier survivant
   manquant.

Le checkpoint `primeforge.discovery.checkpoint.v1` contient l’identité de
campagne, l’état, les nombres total et terminé, `next_k`, l’éventuel
`proven_prime_k` et l’horodatage. L’identité lie notamment la plage, l’exposant,
la borne de crible, la taille d’unité et le SHA-256 exact de Proth20.

Proth20 est épinglé au commit
`6771325939a7ceef2c75644c79981c7df4a61882`, modifié par le patch versionné
`patches/proth20-persistent-batch.patch`. Le patch ajoute le traitement par lot,
l’arrêt coopératif et le flush de chaque résultat. Le binaire local n’est pas
redistribué. Le contexte est persistant à l’intérieur d’une unité, pas encore
pendant toute la campagne.

PARI/GP 2.17.4 sert d’implantation indépendante pour la validation et pour la
procédure suivant un éventuel premier. Il n’est pas exécuté sur chaque composite
de la campagne complète. FLINT et le pipeline CUDA 64 bits ne participent pas
au PRP multiprécision de cette cible.

## Validation disponible pour cette cible

- preflight : `PREFLIGHT_PASS`, avec 79/79 captures intègres et aucun
  chevauchement public trouvé dans les sources consultées ;
- crible : 40 001 candidats, 35 989 éliminés, 4 012 survivants ;
- trois premiers survivants : accord Proth20/PARI de 3/3, tous composites ;
- arrêt/reprise : arrêt après deux résultats, reprise du troisième seulement,
  trois lignes uniques, aucun trou ni doublon ;
- validation locale C++ : Debug et Release réussis, 41/41 tests dans chaque
  configuration ;
- CI Linux : réussie pour `e783a36` ; CI Windows/MSVC : réussie, y compris la
  construction et la vérification du paquet MVP ; jalon intégré à `d9eb4e8`.

La validation courte a consommé 15,071 s pour trois survivants, soit 5,024 s
par survivant en moyenne. Une extrapolation linéaire des 4 012 survivants donne
environ 5,60 h, et l’attente heuristique du premier environ 1,62 h. Ce sont des
estimations de capacité, pas un benchmark publiable.

Enveloppe observée pendant cette validation : CPU 68,875 °C maximum, GPU 47 °C
maximum, puissance GPU 83,68 W maximum, RAM disponible 40 706 715 648 octets au
minimum, VRAM libre 12 488 MiB au minimum, zéro WHEA et zéro throttling. La
température mémoire GPU reste `UNKNOWN`.

## Goulets établis et inconnus

Pour la pipeline native 64 bits après O07, la médiane sur 32 768 candidats
attribue 1,884 s à FLINT et 1,546 s à la preuve, partiellement recouvertes ; I/O
et finalisation coûtent 0,401 s, checkpoint 0,139 s et crible 0,086 s. H2D,
kernel et D2H totalisent seulement 0,634 ms. O08 démontre qu’ajouter des
processus FLINT ne résout pas ce goulet sur ce corpus.

La campagne historique de dix millions de valeurs 64 bits a montré un autre
régime : 669,217 s de checkpoints sur 847,227 s de reprise instrumentée, contre
0,626 s de PRP GPU complet. Ces artefacts sont `OBSOLÈTES` comme modèle de
stockage courant après les compactages, mais `VALIDÉS` comme diagnostic du coût
d’une persistance qui croît avec l’historique.

Pour le chemin multiprécision Proth20, le goulet détaillé est `INCONNU` : la
validation courte établit la correction et l’ordre de grandeur, mais pas une
décomposition professionnelle du calcul OpenCL, des créations de processus et
des attentes du contrôleur. Il serait prématuré d’optimiser CUDA natif ou la RAM
sur cette seule observation.

## Décisions de travail

- Ne pas relancer O08 sans changement de granularité arithmétique ou
  d’intégration FLINT.
- Ne pas annoncer O09 plus rapide tant que le protocole A/B n’utilise pas deux
  builds CUDA réellement comparables.
- Ne pas transférer les gains du moteur 64 bits à Proth20.
- Conserver la taille d’unité 100 pour la campagne autorisée : elle est validée
  pour la reprise et borne la perte potentielle, sans prétendre être optimale.
- Après la campagne, construire un corpus fixe de 100 à 300 survivants et
  mesurer d’abord profondeur de crible, durée de vie Proth20, concurrence GPU,
  partage CPU/GPU et référence concurrente.
- Ne pas commencer un backend CUDA multiprécision natif avant ces cinq
  expériences.

## Contribution directe au logiciel final

Ce jalon empêche d’optimiser le mauvais moteur. Il identifie précisément la
chaîne qui peut découvrir un premier de 10 040 chiffres, confirme ses garanties
de reprise et sépare les gains 64 bits réellement validés des performances
multiprécision encore inconnues. L’audit est terminé pour l’état courant ; il
devra être actualisé seulement après une campagne ou une modification mesurée
du chemin Proth20.
