# Feuille de route de rupture — jalon B

**Statut :** `PASS`

**Date de mesure :** 2026-08-13

**Révision de départ :** `f2b11d8`

**SHA-256 de la directive :**
`8233222851779bdf9fd18622b8e18ea90bac19286589ccb869f1070bbc91674e`

## État de la campagne

La campagne `n=66411`, `k=75939069..76077027` demeure arrêtée. Aucun
contrôleur, worker Proth20 ou watchdog n'a été relancé pendant ce jalon. Le
checkpoint antérieur à `1300/3563` et tous ses artefacts restent inchangés.

## Changements retenus

Le crible spécialisé calcule maintenant directement
`((q + 1) / 2)^n mod q`, soit `2^-n mod q`, au lieu d'enchaîner
`2^n mod q` puis une seconde exponentiation à `q-2`. Les premiers proviennent
de l'itérateur segmenté de primesieve 12.15 épinglé à
`4f85384851da23c36c01ec01ef85b5d9d246e556`; la liste globale de tous les
premiers a disparu. Les candidats éliminés sont conservés dans des mots
`uint64_t`, avec `popcount` de cohérence. Pour `q` plus large que la fenêtre de
`k`, le code teste directement l'unique représentant possible.

Le plafond validé passe de deux à quatre milliards. Ce n'est pas une profondeur
choisie arbitrairement : la bibliothèque adaptative compare le temps de crible
mesuré au temps Proth20 réellement évité. Son test contient la courbe complète
de ce jalon et interdit de réintroduire l'ancien seuil relatif de 3 %.

Enfin, le patch local Proth20 met en cache le plan retenu par classe
`(taille NTT, digit bits, capacités 512/1024)` dans chaque processus. Le premier
candidat d'une classe est autotuné ; les suivants réutilisent les deux indices
de plan. Le cache est volontairement limité au processus et au périphérique
déjà ouvert. La persistance inter-processus, qui exigerait aussi UUID GPU,
pilote et hash source dans la clé, reste au jalon C.

## Exactitude

- Les sorties à 250 M, 500 M, 1 G et 2 G ont exactement les mêmes nombres de
  survivants et les mêmes SHA-256 que l'ancien exécutable dans l'ordre AB/BA.
- À 4 G, les deux répétitions donnent 3 455 survivants et le SHA-256
  `9dd994b3ffe3da4a167e8ef49d780b01730eaea245ab480c0daa802596fb88ca`,
  identique à l'artefact historique produit par l'ancien algorithme.
- Les tests différentiels comparent plusieurs fenêtres et exposants à une
  division modulaire indépendante, sans utiliser la formule d'inverse directe.
- Le cache Proth20 a produit une séquence `MISS, HIT, HIT` sur trois survivants
  complets de 20 000 chiffres. Les trois témoins et `RES64` sont identiques au
  corpus archivé : `6D1E409A23F04802`, `CA636F1CB086AC03` et
  `808B4577F02D1E54`.
- La construction propre du Proth20 épinglé avec les trois patches passe ; les
  builds PrimeForge Debug et Release passent sans avertissement PrimeForge.

## Mesures courtes

Sur la même plage de 68 980 candidats, la médiane de deux passages à 2 G est
passée de **13,5335096 s** à **3,85671215 s**, soit **3,50908×** et
**71,5025 %** de temps en moins. La mesure historique à 4 G était
27,132604 s ; les deux nouvelles mesures ont une médiane de **7,5807339 s**,
soit **3,57915×**. Cette comparaison locale reproductible n'est pas extrapolée
à d'autres machines ou familles.

Le processus 4 G a atteint **11 415 552 octets** de working set, contre
1 697 804 288 octets pour l'ancienne liste globale mesurée, soit environ
148,7× moins. Son fichier de survivants conserve le hash exact attendu.

Le modèle utilise le débit deux-workers conservateur mesuré au jalon A,
517,531 survivants/heure, donc 6,956105045 secondes murales par survivant. Les
résultats sont :

| Borne | Crible médian | Survivants | Temps complet projeté |
|---:|---:|---:|---:|
| 250 M | 0,549993750 s | 3 927 | 27 317,174505132 s |
| 500 M | 1,021892500 s | 3 801 | 26 441,177168223 s |
| 1 G | 1,936894100 s | 3 675 | 25 565,622934163 s |
| 2 G | 3,797972250 s | 3 563 | 24 788,400247283 s |
| 4 G | 7,580733900 s | 3 455 | **24 040,923664082 s** |

Le bloc 2–4 G coûte 3,78276165 s et élimine 108 survivants, représentant
751,259344851 s de calcul Proth20 évité. Sa valeur nette mesurée est donc
positive de 747,476583201 s. Face à l'ancien chemin 2 G complet, le nouveau
chemin 4 G réduit la projection de 757,212120551 s, soit **3,05350 %**.

Le test court du cache a mesuré un autotuning initial de 125 808 700 ns, une
seule absence et deux réutilisations. Il établit la suppression fonctionnelle
des autotunings répétés ; un chiffre de débit de campagne spécifique au cache
attendra le futur test structurel, car trois candidats ne suffisent pas pour
une revendication plus large.

## Décision et limites

Le jalon B passe sa porte : couverture et survivants identiques, puis temps de
campagne projeté réduit. Quatre milliards devient la profondeur retenue pour
une future campagne de cette classe, mais la campagne arrêtée n'est ni modifiée
ni reprise. La borne de 4 G reste une limite d'implémentation `uint32_t`, pas la
preuve que la valeur marginale devient négative au-delà. Aucun essai plus long
n'est lancé pour explorer ce point pendant ce jalon.

La consommation énergétique totale est `UNKNOWN` : aucune énergie n'est
déduite d'une puissance instantanée ou du TDP. Les mesures étaient courtes et
n'ont signalé ni stderr, ni WHEA, ni résultat mathématique divergent. Aucune
campagne longue, aucun réglage matériel et aucun benchmark de 100 survivants
n'ont été exécutés.

primesieve est maintenant une dépendance statique BSD-2-Clause, et non plus un
simple oracle. Sa révision, son origine et sa licence sont épinglées ; son avis
est inclus dans le paquet sans étendre Apache-2.0 au code tiers. Le code tiers
n'hérite pas de `/WX` ou `-Werror`.

Les preuves compactes sont dans
`benchmarks/evidence/breakthrough-jalon-b/`. Les journaux bruts restent dans
`out/benchmarks/breakthrough-jalon-b/` et leurs hashes sont enregistrés dans
`raw-artifacts.sha256`.

## Contribution directe au logiciel final

Ce jalon accélère directement la génération de la file de survivants, réduit
sa mémoire de plus de deux ordres de grandeur sur la plage cible et évite de
recalculer le plan NTT pour chaque candidat d'un même worker. Il réduit donc le
temps attendu jusqu'à un résultat vérifiable sans ajouter de couche générale.

Le crible simple est terminé jusqu'à 4 G. Il faudra y revenir seulement pour
dépasser cette borne, vectoriser les grands `q` ou amortir plusieurs plages si
une mesure de temps complet le justifie. La prochaine cible principale est le
jalon C : conserver réellement le contexte, les kernels, les racines et les
buffers Proth20 entre candidats, puis vérifier le gain sur un essai court avant
tout corpus de 100 à 300 survivants.
