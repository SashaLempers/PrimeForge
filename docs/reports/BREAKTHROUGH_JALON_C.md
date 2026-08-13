# Feuille de route de rupture — jalon C

**Statut :** `PROTOTYPE_EXACT_REJECTED_PERFORMANCE`

**Date de mesure :** 2026-08-13

**Révision de départ :** `5f207c4`

**SHA-256 de la directive :**
`8233222851779bdf9fd18622b8e18ea90bac19286589ccb869f1070bbc91674e`

## Portée bornée

Le jalon C a volontairement été limité au plus petit prototype sûr de contexte
Proth20 invariant. La campagne `n=66411`, `k=75939069..76077027` est restée
arrêtée et aucun calcul de découverte n'a été repris.

Le prototype conserve, entre deux candidats de même classe, le programme
OpenCL, les kernels, les racines NTT, les buffers et le plan autotuné. Il
remplace les constantes OpenCL propres à `k` par un petit
`candidate_params_t` runtime et recharge les deux tables de réduction propres
au candidat. Un changement d'exposant, de largeur numérique ou de taille NTT
reconstruit le contexte au lieu de tenter une réutilisation risquée.

Le patch expérimental reproductible est
`patches/proth20-invariant-context-prototype.patch`, SHA-256
`BF3E5AC2ACF0EAD73BC3C717AD143B9025134E680ACEAE29C1F672EC849FE13F`.
Il s'applique proprement après les trois patches Proth20 épinglés et une
construction MSVC propre produit le binaire local de hash
`A5D9AFACC3E58D9512C0C55126530966664968279E45B5AD588416338539017A`.
Le patch n'est pas intégré au build de production, car sa porte de performance
échoue.

## Exactitude

Le corpus borné contient exactement trois survivants complets à 20 000
chiffres. Deux passages retenus par variante ont donné les mêmes témoins et
les mêmes résidus :

| k | Témoin | RES64 |
|---:|---:|---|
| 75 939 141 | 7 | `6D1E409A23F04802` |
| 75 939 159 | 5 | `CA636F1CB086AC03` |
| 75 939 161 | 3 | `808B4577F02D1E54` |

Les six résultats runtime et les six résultats spécialisés sont
`COMPOSITE`. Aucun contrôle Gerbicz n'a échoué. La construction propre a aussi
validé une réutilisation sur le petit corpus connu `3*2^32+1` composite et
`43*2^32+1` premier.

## Benchmark complet A/B

Le premier passage runtime est exclu des statistiques, car le nouveau hash de
source OpenCL a déclenché une compilation pilote froide de 1,8724889 s. Les
quatre passages retenus alternent ensuite les variantes sur le même corpus et
la même RTX 5080. Ils incluent découverte OpenCL, autotuning, calcul, Gerbicz,
résultats et nettoyage.

| Variante | Passage 1 | Passage 2 | Médiane | Débit médian |
|---|---:|---:|---:|---:|
| spécialisation complète retenue | 26,0870772 s | 25,6469016 s | **25,8669894 s** | **417,520564 candidats/h** |
| contexte runtime | 26,7805945 s | 26,1825528 s | **26,48157365 s** | **407,830748 candidats/h** |

Le contexte runtime augmente donc le temps complet de **614,584250 ms**, soit
**2,375940 %**, et diminue le débit de **2,320800 %**. Il réduit bien la somme
des préparations de contexte de 175,654000 ms à 150,976850 ms, soit seulement
24,677150 ms gagnées sur trois candidats. En revanche, la somme médiane des
boucles principales passe de 25,3604426 s à 26,0104843 s, une régression de
**2,563211 %**. Le coût des paramètres runtime dans les kernels de réduction
annule largement le petit gain de préparation.

Les plans choisis par l'autotuner varient entre passages. C'est inclus dans la
mesure end-to-end, car la fonction objectif est le nombre de candidats complets
par heure, pas un kernel isolé ni l'utilisation GPU.

## Validation du projet

La suite locale complète passe après archivage du prototype : **42/42** tests
Debug en 49,84 s et **42/42** tests Release en 20,04 s. Les deux self-tests
annoncent MSVC `19.51.36252`, C++23 et `PASS`. Aucun avertissement ou erreur du
code PrimeForge n'est apparu.

## Décision

La porte d'exactitude passe, mais la porte de performance échoue. Le prototype
est conservé comme résultat négatif reproductible et n'est pas activé dans le
moteur. Aucun cache disque, découpage hybride supplémentaire ou autre
micro-optimisation de `gpmp` n'est entrepris.

La consommation énergétique totale reste `UNKNOWN`; aucune énergie n'est
déduite d'une puissance instantanée ou du TDP. Tous les essais étaient courts.
Aucune campagne longue n'a été lancée.

Les tableaux compacts sont dans
`benchmarks/evidence/breakthrough-jalon-c/`; les journaux bruts restent dans
`out/benchmarks/breakthrough-jalon-c/` et leurs hashes sont enregistrés.

## Contribution directe au logiciel final

Ce jalon évite une mauvaise direction : il démontre que supprimer la
reconstruction de contexte, après le cache de plan du jalon B, ne suffit pas à
accélérer le moteur et peut même ralentir sa boucle critique. Le résultat est
terminé définitivement pour cette variante runtime ; il ne faudra y revenir
que si une architecture multi-candidats rend naturellement les paramètres
partagés ou masque totalement leur chargement.

La priorité passe immédiatement au coût mesuré dominant : profil kernel par
kernel de la NTT, trafic mémoire, occupation, synchronisations, plans/radices
et prototype de NTT multi-candidats native. Toute variante devra battre les
deux processus Proth20 actuels sur des résultats strictement identiques en
candidats complets par heure.
