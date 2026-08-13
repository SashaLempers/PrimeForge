# Intégration de production NTT native B=8

**Statut avant lancement :** `INTEGRATION_AND_PREFLIGHT_GATES_PASS`

**Date :** 2026-08-13

**Directive SHA-256 :**
`6435e8596d93985ee8852342de4cf54b9e32cf6ed0abf0db0031c27a313c6669`

## Portée

Ce jalon transforme le prototype NTT multi-candidats B=8 en chemin de campagne
reprenable. Il ne change ni la cible mathématique ni la taille de lot retenue.
La campagne historique reste immuable dans son dossier d'origine ; la reprise
utilise un dossier séparé `resume-native-b8`.

Le chemin de production contient un superviseur PowerShell local, un scheduler
C++ compilé, un seul worker GPU Proth20, un watchdog local, des résultats par
lot, un checkpoint atomique et un fichier d'état courant remplacé atomiquement.
Un lot contient exactement un à huit vrais candidats ; aucun remplissage n'est
autorisé.

## Reconstruction historique

Les 134 artefacts couverts par le manifeste historique ont été relus et leurs
SHA-256 vérifiés. La reconstruction donne :

- 3 563 survivants 2G ;
- 1 300 résultats durables valides, tous composites ;
- 2 263 candidats 2G non terminés ;
- zéro doublon, zéro ligne invalide et zéro résultat hors survivants.

Les résultats des deux anciens contrôleurs sont traités comme un ensemble, pas
comme un compteur ou un préfixe supposé. Le script
`scripts/prepare_native_b8_resume.ps1` reproduit ce contrôle et vérifie aussi
chaque marqueur mathématique dans le journal source et `presults.txt`.

## Moteur et exactitude

Le build propre part de Proth20
`6771325939a7ceef2c75644c79981c7df4a61882`, applique les patches épinglés puis
`patches/proth20-native-b8-production.patch`. Le binaire propre observé a le
SHA-256
`c5215168bcd7e99abab9a59e7a2906b70310fe9c1f85c0c39eadb5f7cb103c18`.

Les 28 classifications du jalon D ont été rejouées en B2, B4 et B8, en ordre
normal et inversé. Les classifications, témoins et RES64 sont identiques aux
références et tous les contrôles Gerbicz passent. B=1 reproduit aussi le RES64
attendu et le stop coopératif ne produit aucune lane partielle.

Un lot de huit nombres à l'exposant 32 a placé le premier Proth connu
`43*2^32+1` en lane 5. Les sept composites et le premier ont tous été conservés,
le checkpoint est terminal `PRIME_FOUND` et aucun lot suivant n'a été lancé.

## Checkpoints et reprise

Les résultats durables sont groupés par `batch_id`, avec hash de la liste
ordonnée, hash individuel et hash du lot. Au chargement, le scheduler exige des
lots contigus, complets, ordonnés et préfixes exacts de la queue immuable.

La petite fenêtre de crash entre le remplacement atomique du fichier de
résultats et celui du checkpoint est traitée comme un journal d'écriture : un
lot complet dont tous les hashes passent peut être en avance sur le checkpoint.
Le checkpoint est alors reconstruit avant tout nouveau travail. Un lot incomplet
n'est jamais fusionné. Les tests couvrent B8+B8+B1, arrêt/reprise, checkpoint en
retard, absence de trou/doublon et conservation du suffixe exact.

Chaque tentative possède ses propres entrées et journaux ; une tentative après
crash ne réécrit donc pas les artefacts bruts de la précédente.

## Watchdog

`SW_POWER_CAP` seul est enregistré mais n'est plus fatal. Une option explicite
permet encore de le rendre fatal dans une autre politique. Restent fatals :
température au-dessus du seuil, throttling matériel ou thermique, WHEA, NVIDIA
Xid, perte d'un capteur obligatoire, RAM/VRAM insuffisante, worker non nul,
erreur GPU/Gerbicz, résultat mal formé, checkpoint corrompu et absence de
progression pendant trente minutes.

Le watchdog de campagne échantillonne la sécurité toutes les deux secondes et
réagit à la sortie du worker avec une boucle d'attente de 100 ms, sans attendre
inutilement la prochaine mesure. Son ancien JSONL détaillé est explicitement
désactivé : toute télémétrie détaillée accumulative va dans le TSV de campagne.

## Télémétrie compacte

`campaign-telemetry.tsv` possède un schéma versionné et les types requis :
`RUN_START`, `BATCH_START`, `BATCH_PHASES`, `CANDIDATE_RESULT`, `BATCH_END`,
`RESOURCE_SAMPLE`, `CHECKPOINT`, `SAFETY_EVENT`, `ERROR`, `RUN_END`.

Les mesures exactes par candidat sont séparées des temps partagés du lot. Le
temps moyen `batch_wall_us/batch_size` est étiqueté `DERIVED_AVERAGE` et n'est
pas présenté comme une mesure GPU physique par candidat. Les métriques système
et processus utilisent deux lignes `RESOURCE_SAMPLE` de scopes distincts.

Après le premier A/B à +5,543897 %, rejeté, l'analyse a trouvé une attente du
watchdog à la fin du processus. La boucle de sortie a été rendue réactive et les
seules lignes de ressources optionnelles ont été ramenées à quatre secondes,
sans changer la surveillance de sécurité à deux secondes. L'unique répétition
mesure -2,156317 % sur la médiane des deux lots : la porte de 3 % passe.
Les résultats mathématiques sont identiques.

Le TSV répété fait 15 612 octets pour 16 candidats. Une projection linéaire
conservatrice sur les 2 196 candidats restants donne 2,043483 MiB. À 80 MiB,
l'intervalle passe à dix secondes ; à 90 MiB, les échantillons périodiques sont
supprimés mais les événements essentiels continuent. Une dernière ligne
tronquée est supprimée avant reprise, sans toucher aux lignes complètes.

## Carte ressource vers travail

- CPU : scheduler, queue, paramètres hôte, parsing, persistance, checkpoints et
  télémétrie ; les compteurs `process_*` du watchdog décrivent le worker actif.
- GPU : préparation `a^k`, boucle NTT partagée, réduction, contrôle Gerbicz et
  résidu final.
- RAM : listes de survivants, queue restante, résultats en mémoire, buffer TSV
  et état du scheduler.
- VRAM : racines et twiddles, buffers NTT, huit états de lane, réductions et
  état Gerbicz.
- Disque : résultats durables, checkpoint atomique, manifestes, journaux bruts
  par tentative et télémétrie compacte.

Les champs indisponibles restent `UNKNOWN`. L'énergie GPU est intégrée à partir
des mesures de puissance disponibles et cumulée entre lots ; aucune énergie
totale n'est déduite d'un TDP.

## Choix 2G ou 4G

Deux passages à 2G donnent 3 563 survivants et le hash
`747d73f1ce7fe85b4bbc61ce5247c1598f68d85207acbb240b7d4083364fab0b`.
Deux passages à 4G donnent 3 455 survivants et le hash
`9dd994b3ffe3da4a167e8ef49d780b01730eaea245ab480c0daa802596fb88ca`.
Le set 4G est un sous-ensemble exact du set 2G.

Le passage 2G→4G élimine 108 survivants. Avec 1,302994875 seconde mesurée par
survivant B8 et 3,498 secondes de crible supplémentaires, le gain net est
positif de 137,225447 secondes. La reprise retient donc 4G : 3 455 survivants,
1 300 résultats historiques conservés, dont 67 éliminés par le nouveau crible,
et 2 196 candidats encore à calculer.

## Contribution directe au logiciel final

Ce jalon remplace deux processus Proth20 concurrents par un seul moteur NTT
natif B=8, relie ce moteur à une queue exacte et rend chaque lot récupérable.
Il améliore directement le débit de recherche tout en conservant preuve,
arrêt-au-premier et reprise. L'intégration est terminée pour cette campagne ;
aucune nouvelle optimisation n'est autorisée après son lancement.

## Préflight public final

Une capture fraîche de 80 sources a été effectuée entre
`2026-08-13T21:00:09Z` et `2026-08-13T21:00:57Z`. Le manifeste des réponses a
le SHA-256
`54b22464e58a6a29651ae284a3b373f187f747aabc7d752f24250e9902fbb4b0`.
Les sources critiques ont toutes été capturées, leurs 80 artefacts passent le
contrôle d'intégrité et aucun chevauchement numérique public n'est retenu.

L'analyse initiale a correctement échoué fermé sur quatre occurrences GitHub.
Elles correspondent toutes à la PR 5 du dépôt `SashaLempers/PrimeForge`
lui-même : deux requêtes contenant chacune les deux bornes. GitHub a confirmé
au moment de l'audit que ce dépôt est `PRIVATE`. Le second passage exclut
uniquement ce dépôt privé explicitement nommé, conserve les quatre occurrences
dans `machine_api_excluded_first_party_private_hits`, et continuerait à bloquer
toute occurrence provenant d'un autre dépôt. Le verdict final est donc :

```text
coverage_verdict=PUBLICLY_UNCOVERED_CANDIDATE
preflight_status=PREFLIGHT_PASS
public_overlap_count=0
excluded_first_party_private_hits=4
```

Ce verdict signifie seulement qu'aucun chevauchement public documenté n'a été
trouvé dans les réponses capturées à cette date. Il ne signifie pas « jamais
calculée » et ne révèle pas les calculs privés ou non indexés.

## Commandes opérationnelles

`scripts/prepare_native_b8_resume.ps1` reconstruit et vérifie la queue depuis
les artefacts historiques. `scripts/supervise_native_b8_campaign.ps1` lance un
unique scheduler, lequel lance à son tour un unique worker et un unique
watchdog. `scripts/request_native_b8_stop.ps1` écrit le signal coopératif ; une
reprise n'est permise qu'à partir d'un état durable `STOPPED` avec l'option
`-Resume`. Aucun de ces scripts ne modifie le matériel.
