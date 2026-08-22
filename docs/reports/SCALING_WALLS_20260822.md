# PrimeForge — étude bornée des murs de passage à l'échelle

Date des mesures : 2026-08-22

Commit moteur figé : `54ffb095a81e75d414b328c60ff48eddce977f1b`

Exécutable figé SHA-256 : `8bcc0540860ee2ff16f87ae50eda0d972532ea00d90c68ab460b3cf73a3367be`

Périmètre : **MEASURE / EXPLAIN / ANTICIPATE**. Aucun moteur n'a été modifié et aucune campagne de découverte n'a été lancée.

Les tailles `100k`, `200k`, `300k` et `500k` désignent ici le nombre approximatif de
**chiffres décimaux par candidat**, jamais un nombre de candidats. Les corpus sont fermés,
déterministes et générés à partir d'une graine et d'un algorithme consignés dans leur manifeste.

Les termes utilisés dans ce rapport sont stricts :

- **MEASURED** : observation directe de cette étude ;
- **DERIVED** : calcul exact à partir d'une mesure ou du chemin de code audité ;
- **EXTRAPOLATED** : projection, jamais présentée comme une mesure ;
- **UNKNOWN** : information que l'instrumentation disponible ne permet pas d'établir.

## 1. Résumé exécutif

Le premier mur de taille est le **doublement par paliers de la transformée NTT** : 32 768,
65 536, 131 072 puis 262 144 points. Le débit du meilleur régime mesuré descend de
2 930,5 candidats/h à 100k chiffres à 98,1 candidats/h à 500k chiffres. Ce mur n'est ni
la RAM, ni la VRAM, ni la préparation CPU : la phase GPU principale représente environ
95 à 99 % du temps mural et les buffers OpenCL dérivés ne dépassent que 248,1 MiB pour
le plus gros lot mesuré.

Le meilleur lot change avec la taille : **B32 à 100k et 200k, B16 à 300k, B8 à 500k**.
À 500k, B16 et B8 ont le même débit à 0,1 % près ; B8 est donc retenu car sa latence et
sa mémoire sont divisées par deux. B32 à 500k n'a volontairement pas été exécuté : B16
avait déjà démontré l'absence de gain de débit et un essai supplémentaire aurait consommé
beaucoup de secondes GPU sans lever une incertitude décisionnelle.

Dans les profils détaillés disponibles de 100k à 300k, `reduction` et `poly2int`
représentent ensemble environ **65,6 % du temps d'événements GPU profilé**. C'est la cible
structurelle principale du calcul, devant les micro-optimisations CPU. Attention : le profilage
détaillé ralentit le chemin d'environ 3,4 fois ; ces pourcentages donnent une composition du
travail GPU, pas une attribution directe de 65,6 % du temps mural normal.

Le premier mur des **grandes campagnes** est différent : à chaque lot, le contrôleur
resérialise et remplace tout le fichier de résultats, puis retrie et rehache les ensembles
terminé/restant. Le coût cumulé est quadratique en nombre de survivants pour un lot fixé.
À un million de survivants, le modèle dérivé prévoit 6,4 TiB de réécritures cumulées même
en B32 et 93 750 fichiers d'artefacts hors reprises. Cette persistance doit être rendue
append-only/chunkée avant une telle campagne.

Toutes les sorties comparables sont strictement identiques, tous les contrôles Gerbicz passent,
les codes de sortie sont nuls, WHEA reste à zéro et les maxima thermiques sont de 71 °C GPU et
76,625 °C CPU. Les compteurs SM/warps/bande passante/stalls restent `UNKNOWN` : Nsight Compute
n'a capturé aucun kernel NVIDIA OpenCL malgré deux essais bornés.

## 2. Tableau principal des mesures

### Tous les régimes normaux mesurés

| Chiffres | NTT | Lot | s/lot médian | candidats/h | GPU moyen | contrôleur mémoire moyen | puissance moyenne | buffer OpenCL dérivé | choix |
|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 100k | 32 768 | 8 | 37,084 | 776,6 | 59,9 % | 1,7 % | 121,7 W | 16,1 MiB | non |
| 100k | 32 768 | 16 | 35,635 | 1 616,4 | 56,9 % | 1,8 % | 154,4 W | 31,1 MiB | non |
| 100k | 32 768 | 32 | 39,314 | **2 930,5** | 74,7 % | 2,6 % | 215,9 W | 61,1 MiB | **oui** |
| 200k | 65 536 | 8 | 71,944 | 400,3 | 57,2 % | 1,6 % | 154,2 W | 32,1 MiB | non |
| 200k | 65 536 | 16 | 80,585 | 714,8 | 78,5 % | 2,1 % | 212,9 W | 62,1 MiB | non |
| 200k | 65 536 | 32 | 104,263 | **1 109,4** | 91,3 % | 16,9 % | 297,7 W | 122,1 MiB | **oui** |
| 300k | 131 072 | 8 | 116,477 | 247,3 | 85,6 % | 2,1 % | 228,9 W | 64,1 MiB | non |
| 300k | 131 072 | 16 | 165,998 | **347,4** | 91,7 % | 15,7 % | 290,2 W | 124,1 MiB | **oui** |
| 300k | 131 072 | 32 | 336,207 | 342,7 | 94,9 % | 65,3 % | 338,3 W | 244,1 MiB | non |
| 500k | 262 144 | 8 | 293,507 | **98,1** | 90,7 % | 14,5 % | 274,3 W | 128,1 MiB | **oui** |
| 500k | 262 144 | 16 | 586,486 | 98,2 | 94,8 % | 49,6 % | 311,0 W | 248,1 MiB | non |
| 500k | 262 144 | 32 | **UNKNOWN** | **UNKNOWN** | **UNKNOWN** | **UNKNOWN** | **UNKNOWN** | 488,1 MiB dérivés | non exécuté |

Règle de sélection : le plus petit lot situé à moins de 1 % du débit maximal observé.
Cette règle évite de doubler latence et mémoire pour un gain non significatif.

### Répétabilité disponible

- 100k/B32 : étendue de temps 2,03 % autour de la médiane ; même plan.
- 200k/B32 : étendue 12,67 % ; deux plans auto-réglés différents, donc variance réelle à
  intégrer avant de figer un plan de production.
- 300k/B16 : étendue 6,82 %.
- 300k/B32 : étendue 3,16 %.
- Les autres couples n'ont qu'une mesure normale : leur variance est `UNKNOWN`.

## 3. Falaises de transformée

| Intervalle | Chiffres | NTT | Effet observé sur le meilleur débit |
|---|---:|---:|---:|
| régime 1 | 100k | 32 768 | 2 930,5 c/h |
| falaise 1 | 200k | 65 536 | 1 109,4 c/h, soit ÷2,64 |
| falaise 2 | 300k | 131 072 | 347,4 c/h, soit ÷3,19 |
| falaise 3 | 500k | 262 144 | 98,1 c/h, soit ÷3,54 |

Une loi lisse unique en fonction des chiffres serait trompeuse : la taille NTT est quantifiée en
puissances de deux et le lot optimal change. Les projections doivent donc être faites **par régime
de transformée**, puis réévaluées à chaque falaise.

À 1M chiffres, une NTT de 524 288 points est probable par prolongement du même mécanisme,
mais c'est `EXTRAPOLATED`. Un intervalle prudent de 36 à 49 candidats/h en B8 est obtenu en
appliquant au coût par candidat 500k un facteur de falaise de 2,0 à 2,7. Il ne remplace pas une
mesure courte réelle.

## 4. Passage à l'échelle par phase

### Phases natives end-to-end du lot retenu

Les valeurs ci-dessous sont les agrégats natifs mesurés sur le lot retenu ; chaque cellule donne
`secondes = pourcentage du mur médian`.

| Chiffres / lot | construction paramètres | H2D | sélection témoin | a^k GPU | boucle NTT principale | Gerbicz GPU | réduction finale | D2H |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 100k / B32 | 0,0343 = 0,087 % | 0,0038 = 0,010 % | 0,0074 = 0,019 % | 1,669 = 4,245 % | **37,366 = 95,044 %** | 0,0729 = 0,185 % | 0,0005 = 0,001 % | 0,0011 = 0,003 % |
| 200k / B32 | 0,2829 = 0,271 % | 0,0081 = 0,008 % | 0,0143 = 0,014 % | 1,622 = 1,556 % | **102,059 = 97,886 %** | 0,1134 = 0,109 % | 0,0007 = 0,001 % | 0,0013 = 0,001 % |
| 300k / B16 | 0,3033 = 0,183 % | 0,0079 = 0,005 % | 0,0099 = 0,006 % | 2,883 = 1,737 % | **162,485 = 97,884 %** | 0,1442 = 0,087 % | 0,0033 = 0,002 % | 0,0010 = 0,001 % |
| 500k / B8 | 0,4414 = 0,150 % | 0,0083 = 0,003 % | 0,0073 = 0,002 % | 3,830 = 1,305 % | **288,669 = 98,352 %** | 0,3745 = 0,128 % | 0,0092 = 0,003 % | 0,0013 < 0,001 % |

Le CRT, le scheduling interne fin, les synchronisations par kernel et les conversions distinctes
de `poly2int` ne sont pas exposés séparément par cette interface : leurs valeurs individuelles sont
`UNKNOWN`, et non zéro.

### Décomposition kernel profilée

Les profils détaillés sont volontairement limités à 100k/B8, 200k/B1 et 300k/B1. Leur forte
surcharge interdit de comparer leurs temps muraux aux runs normaux, mais la composition interne
est stable :

| Chiffres | forward NTT | inverse NTT | square | reduction | poly2int | autre |
|---:|---:|---:|---:|---:|---:|---:|
| 100k | 6,634 s = 12,03 % | 5,863 s = 10,63 % | 6,333 s = 11,48 % | **18,867 s = 34,20 %** | **17,352 s = 31,46 %** | 0,112 s = 0,20 % |
| 200k | 12,752 s = 11,54 % | 12,205 s = 11,05 % | 12,061 s = 10,92 % | **37,254 s = 33,72 %** | **35,911 s = 32,50 %** | 0,302 s = 0,27 % |
| 300k | 18,478 s = 11,04 % | 16,274 s = 9,72 % | 19,861 s = 11,87 % | **55,883 s = 33,39 %** | **52,803 s = 31,55 %** | 4,078 s = 2,44 % |

Moyenne des trois profils : réduction 33,77 %, poly2int 31,84 %, ensemble 65,61 %.
Les exposants locaux dérivés des temps d'événements restent proches de 1 pour ces deux phases
(réduction : 0,98 puis 1,00 ; poly2int : 1,05 puis 0,95). Cela ne nie pas la falaise murale :
les profils changent de transformée, de lot et subissent une instrumentation lourde.

Limite d'Amdahl, à considérer comme un plafond du chemin profilé et non comme une promesse
end-to-end : suppression parfaite de réduction seule ≤ 1,51× ; de poly2int seule ≤ 1,47× ; des
deux ensemble ≤ 2,91×. Une optimisation réaliste sera bien inférieure.

Les temps absolus réduction/poly2int/NTT à 500k sont `UNKNOWN`. Aucun profil détaillé 500k n'a
été lancé car son coût estimé ne changeait pas la décision de régime.

## 5. Goulot actuel selon la taille

### 100k

B32 amortit encore efficacement le travail par candidat. Le GPU moyen à 74,7 % et le contrôleur
mémoire à 2,6 % montrent qu'un compteur d'occupation serait nécessaire avant toute conclusion
roofline. Le temps GPU principal représente néanmoins environ 95 % du mur.

### 200k

B32 maximise le débit, mais deux plans auto-réglés donnent une dispersion de 12,7 %. Le goulot
est le chemin GPU/driver de la boucle de transformée ; la stabilité du choix de plan devient une
question paramétrique importante.

### 300k

B32 sature davantage le contrôleur mémoire (65,3 %) et le power cap sans battre B16. B16 garde
le même débit à moins de 1 % avec moitié moins de candidats en vol. Le goulot est désormais
le coût par lot du chemin multi-passes, pas le manque de taille de lot.

### 500k

B8 et B16 donnent exactement le même débit pratique. B16 double le temps du lot, presque double
le buffer et augmente le contrôleur mémoire de 14,5 à 49,6 % sans accélération. Le premier mur est
donc la latence de la boucle NTT complète par candidat et ses passes de réduction/conversion.

## 6. Goulots futurs : 500k, 1M et grandes campagnes

### Mur de taille de nombre

- **MEASURED à 500k** : NTT 262 144, 98,1 c/h en B8, 36,7 s/candidat.
- **EXTRAPOLATED à 1M** : nouvelle falaise probable à 524 288 ; ordre de grandeur prudent
  36–49 c/h en B8 ; à confirmer par un corpus de quelques candidats.
- Le coût de preuve indépendante et la taille des artefacts mathématiques peuvent ensuite devenir
  significatifs, mais ils ne sont pas mesurés dans cette étude et restent `UNKNOWN`.

### Mur de volume de campagne

Indépendamment du nombre de chiffres, la persistance actuelle réécrit tout l'historique et recalcule
des états globaux à chaque lot. Elle finit par dominer bien avant que la RAM/VRAM du calcul soit
épuisée. C'est le premier mur architectural des campagnes comportant des centaines de milliers ou
millions de survivants.

### Deuxième mur après correction de la persistance

Une fois la persistance rendue linéaire, le mur suivant redevient le chemin GPU : falaises de
transformée, réduction/poly2int et choix de lot par régime. Aucun indice ne justifie une refonte du
CPU ou une augmentation générale de B.

## 7. RAM, VRAM et taille de lot

| Chiffres | lot choisi | buffer OpenCL dérivé | working set max | private max | VRAM système max observée |
|---:|---:|---:|---:|---:|---:|
| 100k | 32 | 61,1 MiB | 156,8 MiB | 474,8 MiB | 2 642 MiB |
| 200k | 32 | 122,1 MiB | 169,3 MiB | 636,8 MiB | 2 863 MiB |
| 300k | 16 | 124,1 MiB | 169,4 MiB | 636,1 MiB | 2 807 MiB |
| 500k | 8 | 128,1 MiB | 170,5 MiB | 636,2 MiB | 2 979 MiB |

La VRAM système comprend les autres applications et n'est pas une attribution exclusive à
PrimeForge. L'incrément observé reste de quelques centaines de MiB et très loin des 16 GiB. Le
buffer B8 à 1M serait d'environ 256 MiB par simple doublement dérivé ; même B16 resterait sous
500 MiB. La capacité mémoire semble donc confortable à 1M, mais cette conclusion est
`EXTRAPOLATED` et ne préjuge ni de l'allocation du driver ni d'un futur moteur différent.

Le changement B32 → B16 → B8 est un choix de débit/latence, pas une réponse à un manque de VRAM.

## 8. Attentes CPU/GPU

Sur les régimes retenus, le temps natif `main_ntt_gpu` vaut environ 95 à 99 % du temps mural.
L'utilisation CPU équivalente passe d'environ 0,99 cœur à 100k à 0,54 cœur à 300k/B16 et 0,61
cœur à 500k/B8. À 300k/B32 et 500k/B16, elle tombe autour de 0,21–0,22 cœur pendant que le GPU
et le contrôleur mémoire montent.

Conclusion supportée : le CPU de préparation n'est pas sur le chemin critique de ces micro-runs ;
le processus hôte attend principalement le chemin GPU/driver.

Répartition exacte entre attente de queue OpenCL, barrières, stalls mémoire, dépendances de kernel
et occupation insuffisante : `UNKNOWN`. Nsight Compute 2026.2.1 a terminé deux captures bornées
avec le message « No kernels were profiled » sur ces kernels OpenCL. Il serait incorrect d'inventer
une occupation SM ou une bande passante à partir des seuls pourcentages `nvidia-smi`.

## 9. Passage à l'échelle du volume de campagne

### Chemin de code observé

`native_b8_campaign.cpp` conserve tous les résultats en mémoire, les resérialise puis remplace
atomiquement `candidate-results.tsv` après chaque lot. Le checkpoint reconstruit aussi les listes
complète/restante et leurs hashes. Les fichiers `.stdout.log`, `.stderr.log` et `.pending.txt` sont
conservés par tentative. La télémétrie, elle, est bornée à 80 MiB souples / 90 MiB durs et n'est pas
le problème non borné.

Sur la campagne réelle auditée, la ligne résultat moyenne mesure 439,274 octets et les artefacts
log+pending environ 4 133,7 octets par lot. Le temps de persistance passe de 1,060 ms à 32 résultats
à 10,349 ms à 7 392 résultats ; le checkpoint de 3,457 à 12,247 ms. Les pentes régressées sont
environ 1,20 µs par résultat déjà terminé pour chacune des deux opérations. Ce coût est encore
négligeable devant un lot de 36 s à 7k résultats, mais sa croissance est structurelle.

| Survivants | Lot | lots | résultat final | réécritures cumulées | données rehachées | fichiers hors reprises |
|---:|---:|---:|---:|---:|---:|---:|
| 100k | 8 | 12 500 | 41,9 MiB | 255,7 GiB | 19,8 GiB | 37 500 |
| 100k | 16 | 6 250 | 41,9 MiB | 127,9 GiB | 9,9 GiB | 18 750 |
| 100k | 32 | 3 125 | 41,9 MiB | 63,9 GiB | 4,9 GiB | 9 375 |
| 1M | 8 | 125 000 | 418,9 MiB | **25,0 TiB** | 1,93 TiB | 375 000 |
| 1M | 16 | 62 500 | 418,9 MiB | **12,5 TiB** | 989,5 GiB | 187 500 |
| 1M | 32 | 31 250 | 418,9 MiB | **6,24 TiB** | 494,8 GiB | 93 750 |

Ces volumes sont `DERIVED` du format mesuré et du chemin de code actuel. Ils n'incluent ni retries,
ni snapshots externes, ni coût du système de fichiers. Le tri `N log N` répété ajoute encore un coût
CPU qui devient important.

## 10. Risques architecturaux

1. **Quadratic persistence — élevé, générique.** Réécriture complète des résultats à chaque lot.
2. **Checkpoint global — élevé, générique.** Tri/hash répétés de toutes les collections.
3. **Explosion de fichiers — élevé à grand volume.** Trois fichiers par lot/tentative sans compaction.
4. **Falaises NTT — élevé, spécifique au régime.** Un petit changement de chiffres double la transformée.
5. **Plan auto-réglé variable — moyen, paramétrique.** Dispersion de 12,7 % à 200k/B32.
6. **Power cap logiciel — moyen, spécifique machine/régime.** Observé sur les gros lots ; aucune
   erreur thermique ou matérielle.
7. **Observabilité kernel insuffisante — moyen.** Occupation, GB/s et stalls restent inconnus sous
   OpenCL avec l'outil disponible.
8. **Résultats et queue entièrement en mémoire — moyen à très grand volume.** Compact aujourd'hui,
   mais non streaming.

## 11. Opportunités d'optimisation classées

### P0 — journal append-only et checkpoint incrémental

- Classe : **GENERIC**.
- Preuve : 6,24 à 25,0 TiB de réécritures dérivées à 1M survivants.
- Proposition : résultats append-only par segments immuables, manifeste atomique compact, hash
  incrémental/Merkle et checkpoint ne contenant que le curseur durable et les racines.
- Gain maximal : ramener la persistance de `O(N²/B)` à `O(N)` ; le gain est faible à 7k mais devient
  décisif aux grands volumes.
- Difficulté/risque : moyen/élevé, car reprise exacte, absence de trou/doublon et audit des hashes.
- Tests requis : crash à chaque frontière d'écriture, troncature, replay, reprise identique au continu,
  hash final identique, migration de l'ancien format.

### P0 — rotation/compaction bornée des artefacts par lot

- Classe : **GENERIC**.
- Preuve : jusqu'à 375 000 fichiers hors retries à 1M/B8.
- Proposition : archive segmentée/indexée après validation durable, en conservant les artefacts bruts
  nécessaires aux incidents et aux premiers.
- Gain maximal : surtout métadonnées, démarrage, sauvegarde et maniabilité ; le débit mathématique
  n'est pas le motif premier.
- Difficulté/risque : moyen ; ne jamais supprimer une preuve non archivée.

### P1 — sélection de lot par régime de transformée

- Classe : **PARAMETRIC / REGIME_SPECIFIC**.
- Preuve : B32, B32, B16, B8 retenus respectivement à 32k, 65k, 131k et 262k NTT.
- Proposition : table/version de politique validée, jamais une règle « plus gros est mieux ».
- Gain maximal mesuré par rapport à B8 : 3,77× à 100k, 2,77× à 200k, 1,41× à 300k et nul à 500k.
- Difficulté/risque : faible/moyen ; la variance des plans impose répétitions et garde-fous.
- Tests requis : A/B puis B/A, corpus identique, RES64 et Gerbicz identiques, thermiques comparables.

### P1 — réduire/fusionner les passes réduction et poly2int

- Classe : **GENERIC dans le kernel, effet REGIME_SPECIFIC**.
- Preuve : 65,6 % des événements GPU profilés de 100k à 300k.
- Plafond d'Amdahl profilé : 2,91× si les deux devenaient gratuitement instantanés, scénario impossible.
- Proposition : réduire les passes plein-buffer et le trafic intermédiaire ; tester fusion seulement si
  la pression registres/occupation ne régresse pas.
- Difficulté/risque : élevé ; risque mathématique direct.
- Tests requis : résultats bit-à-bit, RES64, Gerbicz, témoins connus, différentiel indépendant,
  répétitions par taille et vérification des reprises.

### P2 — instrumentation GPU compatible avec le chemin réellement utilisé

- Classe : **GENERIC d'observabilité**.
- Preuve : Nsight Compute n'a capturé aucun kernel OpenCL.
- Proposition : compteurs CUPTI/CUDA dans un prototype borné ou outil OpenCL compatible, uniquement
  pour décider d'une transformation précise.
- Gain direct : zéro ; elle évite une optimisation à l'aveugle.
- Difficulté/risque : moyen ; ne justifie pas à elle seule une réécriture CUDA.

## 12. Ce qu'il ne faut pas optimiser maintenant

- **Préparation CPU, génération, H2D/D2H** : trop petits face aux 95–99 % du chemin GPU principal.
- **Capacité VRAM** : aucune pression mesurée, même à 500k.
- **Télémétrie** : déjà bornée ; ce n'est pas l'explosion de volume.
- **B32 à 500k** : B16 n'apporte aucun débit face à B8 ; le gain plausible est insuffisant pour le coût.
- **Micro-ajustements de plan isolés** : la variance 200k exige d'abord une politique reproductible.
- **Réécriture CUDA complète** : non justifiée sans compteurs montrant que l'API/driver est le mur.
- **Optimisation pour un pourcentage d'utilisation GPU** : la fonction objectif reste le nombre de
  candidats complets et validés par heure.
- **Campagne de découverte** : explicitement hors périmètre de cette étude.

## Validation et limites finales

- 20 résumés de runs, 112 verdicts croisés : `PASS`.
- Résultats chevauchants : strictement identiques.
- Gerbicz : `PASS` partout.
- WHEA : 0 ; throttling thermique : aucun.
- `SW_POWER_CAP` : observé sur certains gros lots, rapporté comme contrainte de puissance et non erreur.
- B32/500k, sous-phases 500k, occupation SM, GB/s exacts, warps actifs, causes de stalls : `UNKNOWN`.
- Aucun calcul n'a dépassé le budget borné ; aucune découverte n'a été recherchée.

Les données tabulaires, les validations, le modèle de volume et leurs hashes sont conservés dans
`benchmarks/evidence/native-scaling-walls-20260822/`.
