# Feuille de route de rupture — jalon D

**Statut :** `PROTOTYPE_EXACT_PERFORMANCE_PASS`

**Date de mesure :** 2026-08-13

**Révision de départ :** `c92c3f9`

**SHA-256 de la directive :**
`8233222851779bdf9fd18622b8e18ea90bac19286589ccb869f1070bbc91674e`

## Cadre

Le jalon C est définitivement figé pour sa variante à paramètres `k` runtime :
elle est exacte, mais son débit complet régresse de 2,320800 %. Aucun travail
supplémentaire de micro-optimisation de `gpmp` n'a été entrepris.

Le jalon D attaque le coût mesuré dominant : la boucle NTT représente environ
96,4 % du temps du candidat complet. La campagne de découverte est restée
arrêtée. Tous les essais de ce rapport sont des benchmarks courts sur des
survivants déjà calculés et connus composites.

## Profil de la boucle critique

Le profil synchronisé de 10 000 carrés montre une chaîne de douze kernels
récurrents dont chacun représente entre 7,7967 % et 8,7048 % du temps GPU
enregistré. Aucun petit kernel isolé ne domine : NTT, iNTT, conversion
polynôme-entier et réduction doivent avancer ensemble. Les données exactes sont
dans `benchmarks/evidence/breakthrough-jalon-d/kernel-profile.tsv`.

Le compilateur NVIDIA a rapporté, pour les variantes NTT inspectées :

- 40 à 68 registres par thread ;
- zéro spill load et zéro spill store ;
- 16 392 ou 32 776 octets de mémoire partagée ;
- une barrière par kernel NTT inspecté.

La RTX 5080 expose 84 unités de calcul et 48 Kio de mémoire locale OpenCL.
Les plans à 32 776 octets limitent donc fortement le nombre de groupes
résidents par unité. Un candidat seul ne fournit que quelques groupes
indépendants aux grandes étapes NTT ; deux processus en fournissent davantage,
mais dupliquent programme, commandes et états.

La file NVIDIA de production est ordonnée et n'effectue pas de `clFinish`
entre les kernels. Le mode de profil kernel, lui, synchronise volontairement
chaque événement ; ses pourcentages servent à comparer les kernels, pas à
prédire directement le temps de production. Nsight Compute 2026.2.1 n'a pas
capturé les kernels OpenCL (`No kernels were profiled`) et l'implémentation
OpenCL locale n'expose pas les compteurs d'occupation ou de bande passante
requis. L'occupation réalisée et le trafic matériel mesuré restent donc
`UNKNOWN`. Les ressources compilées, les tailles de buffers et les temps
d'événements sont, eux, enregistrés sans extrapolation.

Cette observation a écarté une fusion locale prématurée : fusionner un seul
couple de kernels aurait augmenté la pression registres/mémoire partagée sans
attaquer le manque principal de travail indépendant.

## Prototype NTT multi-candidats

Le patch reproductible
`patches/proth20-native-batch-prototype.patch`, SHA-256
`036DDEB485FA1DA67FB682AA170F5ADF7D5F621D987C3FC55AA8E97DF62C17A1`,
s'applique après les patches batch persistant, profil, cache de plan et contexte
invariant sur Proth20 épinglé au commit
`6771325939a7ceef2c75644c79981c7df4a61882`.

Le prototype :

- conserve un programme, une file, un plan et une table de racines communs ;
- alloue un état, les tables `bp/ibp`, les paramètres et les erreurs par lane ;
- ajoute la lane candidat comme seconde dimension de chaque lancement OpenCL ;
- exécute le même carré NTT pour B candidats en une seule suite de commandes ;
- conserve des réductions propres à chaque `k` ;
- exécute le contrôle Gerbicz indépendamment pour chaque lane ;
- autotune séparément B=2, B=4 et B=8 ;
- prépare séquentiellement le petit préfixe `a^k`, car témoins et bits de `k`
  diffèrent, puis injecte les états dans le lot.

La disposition reste volontairement `candidate-major`. Ce prototype ne tente
ni vectorisation coefficient-major, ni fusion de kernels, ni augmentation
au-delà de B=8. Il isole donc le gain structurel dû au partage de la suite de
commandes et à l'augmentation du nombre de groupes indépendants.

Une reconstruction propre du patch a produit
`proth20-native-batch.exe`, SHA-256
`D237C3D9B853915195509665CA27DEECAD918F3B68C9BB1B6C7D7949D99B5FF7`,
puis a reproduit les deux premiers résidus exacts et le contrôle Gerbicz.

## Exactitude stricte

Huit survivants à 20 000 chiffres ont été comparés aux résultats des deux
processus Proth20 retenus. Chaque passage normal a été répété avec l'ordre des
lanes inversé.

| k | Témoin | RES64 |
|---:|---:|---|
| 75 939 141 | 7 | `6D1E409A23F04802` |
| 75 939 159 | 5 | `CA636F1CB086AC03` |
| 75 939 161 | 3 | `808B4577F02D1E54` |
| 75 939 195 | 13 | `7E180BD073E83C61` |
| 75 939 329 | 3 | `337945987757D9A2` |
| 75 939 357 | 5 | `E2E161AD272D0E51` |
| 75 939 369 | 5 | `5DC98A3090F13E24` |
| 75 939 411 | 7 | `C202AD2E8BD8628B` |

Les 28 résultats natifs retenus (2×B2, 2×B4, 2×B8) sont `COMPOSITE`,
avec témoins et RES64 strictement identiques aux références. Aucun contrôle
Gerbicz ni code d'erreur GPU n'a échoué. Les sorties d'erreur sont vides.

## Benchmark end-to-end

Les temps muraux incluent découverte OpenCL, construction du contexte,
compilation/cache pilote, autotuning, préparation `a^k`, boucle complète,
résidu, Gerbicz final, sortie et nettoyage. La fonction objectif est le nombre
de candidats complets par heure.

| Variante | Passage 1 | Passage 2 | Médiane | Débit médian |
|---|---:|---:|---:|---:|
| deux processus, 2 candidats | 13,8348617 s | 14,5898145 s | **14,2123381 s** | **506,602077 candidats/h** |
| lot natif B=2 | 9,0912040 s | 8,8943667 s | **8,99278535 s** | **800,641817 candidats/h** |
| lot natif B=4 | 9,1251816 s | 9,0508742 s | **9,08802790 s** | **1 584,502178 candidats/h** |
| lot natif B=8 | 9,3647144 s | 9,2130874 s | **9,28890090 s** | **3 100,474460 candidats/h** |

B=2 bat les deux processus de **58,041558 %**. B=4 puis B=8 passent leurs
portes successives. Le temps du lot varie peu quand B double, ce qui confirme
le sous-remplissage structurel observé.

Une comparaison finale a utilisé exactement les huit mêmes survivants : quatre
par processus de référence contre un lot natif B=8. Les deux processus ont pris
53,5382136 s, soit 537,933526 candidats/h. La médiane B=8 prend 9,2889009 s,
soit 3 100,474460 candidats/h : facteur **5,763676**, gain de débit
**476,367583 %** et réduction du temps mural **82,649961 %**. Les huit sorties
sont strictement identiques.

Les plans choisis par l'autotuner varient entre passages ; cette variabilité
est incluse dans la mesure end-to-end. Aucun résultat de performance n'est
extrapolé à une autre taille NTT ou à une campagne réelle.

## Validation du projet

La compilation MSVC ne produit aucun nouveau travail ni avertissement. La
suite complète passe **42/42** en Debug en 53,08 s et **42/42** en Release en
19,97 s. Les deux `primeforge-selftest` annoncent MSVC `19.51.36252`, C++23 et
`PASS`. Le contrôle des hashes compacts et `git diff --check` passent.

Le compilateur MSVC 19.51 est la version installée observée ; le prototype ne
l'impose pas comme exigence artificielle.

## Décision

La porte D est validée. B=8 est retenu comme meilleur prototype testé. Les
tailles sont maintenant figées : aucun B supérieur, aucune fusion locale et
aucune micro-optimisation de radix ne sont testés dans ce jalon.

Le patch reste expérimental et n'est pas encore le moteur de campagne. Son
intégration de production devra grouper uniquement les candidats de même
`n`, largeur et taille NTT, gérer le dernier lot incomplet, préserver les
checkpoints durables, et garantir l'arrêt au premier `PROVEN_PRIME` sans trou
ni doublon. Ces exigences ne sont pas contournées par le benchmark.

La consommation énergétique totale est `UNKNOWN`. Aucune valeur n'est déduite
du TDP ou d'une puissance instantanée. Aucun benchmark long et aucune campagne
de recherche n'ont été lancés.

## Contribution directe au logiciel final

Ce jalon apporte au moteur un chemin concret capable de traiter huit tests de
Proth complets dans presque le temps auparavant nécessaire à un seul. Il
attaque directement la boucle qui consomme 96,4 % du temps, conserve
l'arithmétique NTT exacte et vérifie chaque lane indépendamment.

Le prototype et sa décision B=8 sont terminés. Il faudra y revenir une seule
fois pour l'intégrer proprement au scheduler, aux checkpoints et à l'arrêt sur
premier ; la recherche de fusion de kernels ou de layout coefficient-major ne
sera justifiée qu'après une nouvelle mesure du moteur intégré.
