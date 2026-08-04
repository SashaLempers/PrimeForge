# PrimeForge — reconnaissance et sélection de la première cible de découverte

**Décision au 4 août 2026 :** rechercher une niche Proth indépendante, courte et
reproductible, plutôt qu'une unité PrimeGrid/GIMPS ou un mégapremier.

## Cible exacte

- Forme : `N = k * 2^33221 + 1`.
- Domaine : `k` impair, `10001 <= k <= 90001`, soit **40 001 candidats**.
- Exemple de candidat : `10001 * 2^33221 + 1` (mesuré composite, il ne s'agit
  pas d'un résultat recherché).
- Taille : **10 005 à 10 006 chiffres décimaux**.
- Découpage prévu : unités déterministes de 2 000 valeurs impaires de `k`, sans
  chevauchement, avec borne finale explicite.

Cette cible est volontairement plus petite que le seuil des 5 000 plus grands
premiers. L'objectif prioritaire est une première découverte vérifiable en
quelques heures, pas un record. PrimePages indique un seuil courant de
**909 526 chiffres** ; viser ce seuil multiplierait ici le coût sans améliorer
la probabilité de primalité par unité de temps.

## Reconnaissance des voies

### Voie A — travail coordonné

PrimeGrid exploite officiellement PPS/PPSE et des recherches GFN. Sa page des
applications fournit des exécutables LLR pour PPS/PPSE et Genefer CPU/GPU. Son
historique GFN, généré le 4 août 2026, donne des fronts actifs et des recherches
à deux passes. Cette voie donne la meilleure traçabilité d'attribution, mais :

- elle exige de rejoindre le projet et d'accepter ses règles ;
- les unités sont attribuées par le projet et exécutées avec ses applications ;
- utiliser PrimeForge comme moteur principal n'est pas actuellement un mode
  d'intégration accepté et documenté ;
- la GFN-19 active est déjà attribuée entre ses fronts (au 3 août 2026 :
  `b=24940348` à `b=25526424`) ; la tester en parallèle créerait un conflit ;
- une attribution PrimeGrid ou GIMPS exige l'autorisation préalable du
  propriétaire de PrimeForge.

GIMPS offre aussi des affectations non chevauchantes, mais les premiers tests
se situent actuellement autour d'exposants Mersenne supérieurs à 140 millions.
Cette voie utiliserait Prime95/PrimeNet, pas le pipeline Proth de PrimeForge, et
son temps attendu avant découverte sur une seule machine est nettement moins
favorable.

**Décision voie A :** conservée comme solution future de coordination, mais
non choisie pour la première découverte PrimeForge.

### Voie B — niche indépendante

La documentation publique de PrimeGrid définit PPS sur les petits
multiplicateurs et PPSE sur `1200 < k < 10000`. Les corrections de crible
annoncées en 2025–2026 portent également sur ces anciennes plages PPS/PPSE ;
elles ne déclarent pas de nouvelle couverture pour `k > 10000`.

Le domaine choisi commence donc au premier `k` impair strictement supérieur à
10 000 et fixe un exposant bien plus petit que les fronts PPS actuels. Cette
séparation ne prouve pas qu'aucun particulier n'a jamais testé un candidat
isolé, mais elle constitue une zone suffisamment distincte du projet coordonné.
Les recherches exactes effectuées le 4 août 2026 dans PrimePages, les pages
indexées de Proth Search et le Web n'ont retourné aucune occurrence de la forme
`k*2^33221+1`. Ce constat est un indice, **pas** une preuve de nouveauté ; la
vérification des bases devra être répétée pour chaque premier trouvé.

## Justification quantitative

Pour un `k` impair, les candidats occupent une classe réduite modulo
`2^33222`. L'heuristique du théorème des nombres premiers dans les progressions
arithmétiques donne une probabilité approximative

`p ≈ 2 / ln(N)`.

Au milieu géométrique de la plage (`k ≈ 30001`) :

- `ln(N) ≈ 23037.3514936` ;
- `p ≈ 0.0000868155352`, soit environ **1 premier pour 11 519 candidats** ;
- espérance sur 40 001 candidats : **3.4727 premiers** ;
- probabilité de trouver au moins un premier, modèle de Poisson :
  **1 - exp(-3.4727) = 96.8967 %**.

Il s'agit d'une estimation heuristique, pas d'une garantie. Les congruences
propres à l'exposant peuvent faire varier le rendement observé.

## Coût et débit estimés

Une mesure locale courte a été faite sur la RTX 5080 avec le `proth20` 0.9.1
déjà épinglé :

- `10001 * 2^33221 + 1` ;
- verdict : composite par théorème/test de Proth, témoin `a=3` ;
- temps interne annoncé : **3 s** ;
- temps mural avec démarrage et compilation OpenCL : **5.847216 s**.

Avec le crible actuel jusqu'à 65 521, l'estimation de Mertens donne environ
10,13 % de survivants, soit **environ 4 050 tests Proth complets**. Cela borne
provisoirement la campagne à :

- **~6,58 h** si chaque candidat redémarre le processus OpenCL ;
- **~3,38 h** si le contexte GPU reste vivant et que le coût interne de 3 s se
  reproduit ;
- temps attendu avant le premier premier : environ **1 à 2 h** dans le chemin
  persistant, sous l'heuristique précédente.

Ces nombres sont des estimations de décision issues d'un seul cas, pas un
benchmark publiable. Une validation courte sur un petit segment de la même
taille devra mesurer le vrai débit avant la campagne complète.

## Modifications minimales nécessaires

PrimeForge sait déjà générer et cribler la famille Proth, checkpoint/reprendre,
et conserver des résultats compacts. Son pipeline de recherche final reste
cependant limité à 64 bits. Le chemin minimal est donc :

1. accepter un exposant multiprécision dans la campagne Proth sans matérialiser
   les entiers décimaux composites ;
2. conserver seulement `(k,n)`, les facteurs trouvés, les survivants et les
   résultats de preuve ;
3. ajouter un adaptateur persistant ou un traitement en lots autour de la
   source MIT de `proth20`, sans un processus OpenCL par candidat ;
4. produire un certificat Proth compact : `(k,n,a)` et les hashes de provenance ;
5. vérifier chaque certificat dans PrimeForge puis avec une implantation
   indépendante ;
6. checkpoint limité à l'unité courante, au dernier `k` traité et aux journaux
   segmentés, sans JSON par composite.

Aucun moteur multiprécision général, ECPP générique ou nouvelle infrastructure
de benchmark n'est nécessaire pour cette cible.

## Preuve et vérification indépendante

- **Preuve primaire :** théorème de Proth, après validation stricte de `k`
  impair et `k < 2^33221`, avec un témoin `a` satisfaisant
  `a^((N-1)/2) ≡ -1 (mod N)`.
- **Rejeu PrimeForge :** reconstruction de `N` et vérification du certificat par
  arithmétique multiprécision CPU.
- **Vérification indépendante :** `proth20` 0.9.1 sur la RTX 5080, puis rejeu du
  critère modulaire avec PARI/GP ou FLINT dans un processus séparé. Un simple
  PRP ne sera jamais enregistré comme `PROVEN_PRIME`.
- **Nouveauté :** recherche exacte de la forme et du nombre dans PrimePages,
  l'historique PrimeGrid/Proth Search et les résultats publics pertinents,
  répétée après la preuve.

## Reconnaissance ou enregistrement prévu

Le résultat sera d'abord placé dans `discoveries/<identifiant>/` avec le nombre,
la forme, le certificat, les deux vérifications, les versions, commandes et
hashes. Aucune annonce ne sera faite automatiquement. Après autorisation du
propriétaire, le dossier pourra être transmis aux responsables de Proth Search
ou présenté sur le forum spécialisé approprié. PrimePages pourra être consulté,
mais cette taille et cette forme ne satisfont pas actuellement le seuil des
5 000 plus grands premiers ; une acceptation PrimePages n'est donc pas promise.

## Sources primaires et officielles consultées le 4 août 2026

- [PrimeGrid — applications actives](https://www.primegrid.com/apps.php)
- [PrimeGrid — historique et fronts GFN](https://www.primegrid.com/gfn_history.php)
- [PrimeGrid — définition de PPS/PPSE et historique Proth](https://www.primegrid.com/forum_thread.php?id=2665)
- [PrimeGrid — correction du crible PPSE, état 2025–2026](https://www.primegrid.com/forum_thread.php?id=11694)
- [GIMPS — règles et seuils d'attribution PrimeNet](https://www.mersenne.org/thresholds/)
- [PrimePages — seuil courant et formes archivables](https://t5k.org/top20/sizes.php)
- [PrimePages — politique des formes archivables](https://t5k.org/top20/home.php)
- [proth20 — dépôt de l'auteur, limites et licence MIT](https://github.com/galloty/proth20)
- [PARI/GP — `isprime`, `primecert` et distinction PRP/preuve](https://pari.math.u-bordeaux.fr/dochtml/html-stable/Arithmetic_functions.html)

## Conditions de passage à la campagne

Avant la campagne complète : petit segment de même taille, arrêt/reprise,
couverture exacte, certificat rejoué, comparaison indépendante, débit mesuré,
ressources contrôlées et seconde vérification de couverture. Si la durée mesurée
dépasse 24 h ou si la zone apparaît déjà couverte, la décision repasse à
`NO-GO` et la cible est réévaluée.

GO
