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

Le crible spécialisé exact jusqu'à 65 521 conserve **4 008 survivants** sur
40 001 candidats (35 993 éliminés par 6 541 nombres premiers). Le SHA-256 de
la liste canonique `k n` est
`f4e60c6cbd66030d2034976a8b8aa663899e1dd61ebb995cd053c58abf13c5e5`.
Cela borne provisoirement la campagne à :

- **~6,58 h** si chaque candidat redémarre le processus OpenCL ;
- **~3,38 h** si le contexte GPU reste vivant et que le coût interne de 3 s se
  reproduit ;
- temps attendu avant le premier premier : environ **1 à 2 h** dans le chemin
  persistant, sous l'heuristique précédente.

Ces nombres sont des estimations de décision issues d'un seul cas, pas un
benchmark publiable. Une validation courte sur un petit segment de la même
taille devra mesurer le vrai débit avant la campagne complète.

## Validation courte de la chaîne retenue

Trois survivants à la taille cible ont été testés : `k=10013`, `10035` et
`10055`, toujours avec `n=33221`. Le `proth20` épinglé les a classés composites
avec les témoins respectifs 3, 7 et 3. Le temps mural total mesuré pour les
trois tests persistants a été **17,879841 s**, soit **5,959947 s par
survivant**. Une projection linéaire prudente donne **environ 6 h 38 min** pour
les 4 008 survivants ; ce n'est pas une revendication de performance.

PARI/GP, exécuté séparément, a classé les trois mêmes entiers composites :
accord **3/3**. Une interruption coopérative a ensuite été provoquée après le
premier verdict. Le checkpoint indiquait exactement `1/3`, avec `k=10035`
comme reprise. La relance n'a traité que les deux candidats restants et a
terminé à `3/3`; aucun candidat déjà journalisé n'a été retraité.

Sur les 12 échantillons de cette validation interruption/reprise : température
CPU maximale 68,75 °C, température GPU maximale 52 °C, puissance GPU maximale
72,84 W, RAM disponible minimale 40 575 934 464 octets, VRAM libre minimale
13 377 MiB, zéro throttling et zéro erreur WHEA récente. La température mémoire
GPU reste `UNKNOWN`. Cette observation courte valide la porte fonctionnelle,
pas la stabilité d'une campagne prolongée.

La campagne préparée, sans calcul Proth, se reproduit avec :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_prime_discovery.ps1 -PrepareOnly
```

La même commande sans `-PrepareOnly` lancera ou reprendra automatiquement la
campagne après autorisation de son lancement. Les sorties utiles sont compactes
(`campaign.json`, `checkpoint.json`, `results.tsv` et un journal par unité).

## Modifications minimales nécessaires

Le pipeline natif de recherche reste limité à 64 bits. Le chemin minimal retenu
et désormais préparé est donc :

1. accepter un exposant multiprécision dans la campagne Proth sans matérialiser
   les entiers décimaux composites ;
2. conserver seulement `(k,n)`, les facteurs trouvés, les survivants et les
   résultats de preuve ;
3. ajouter un adaptateur persistant ou un traitement en lots autour de la
   source MIT de `proth20`, sans un processus OpenCL par candidat ;
4. produire un certificat Proth compact : `(k,n,a)` et les hashes de provenance ;
5. arrêter au premier résultat prouvé et le vérifier avec une implantation
   indépendante avant toute qualification de découverte ;
6. checkpoint limité à l'unité courante, au dernier `k` traité et aux journaux
   segmentés, sans JSON par composite.

Aucun moteur multiprécision général, ECPP générique ou nouvelle infrastructure
de benchmark n'est nécessaire pour cette cible. Les points 1, 2, 3 et 6 sont
implémentés par le crible spécialisé et le lanceur segmenté. Les points 4 et 5
ne s'activent qu'au premier résultat `PROVEN_PRIME`.

## Preuve et vérification indépendante

- **Preuve primaire :** `proth20`, par théorème de Proth, après validation stricte de `k`
  impair et `k < 2^33221`, avec un témoin `a` satisfaisant
  `a^((N-1)/2) ≡ -1 (mod N)`.
- **Orchestration PrimeForge :** conservation de `(k,n)`, du témoin, de la
  sortie brute, des versions et des hashes, puis arrêt immédiat de la campagne.
- **Vérification indépendante :** reconstruction et preuve CPU avec PARI/GP ou
  FLINT dans un processus séparé. Un simple PRP ne sera jamais enregistré comme
  `PROVEN_PRIME`.
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

Le petit segment de même taille, l'arrêt/reprise, la couverture exacte, la
comparaison indépendante des classifications, le débit et les ressources ont
été validés. La couverture publique a été vérifiée une seconde fois le 4 août
2026. Si elle change avant le lancement, si la durée projetée dépasse 24 h ou
si la zone apparaît déjà couverte, la décision repasse à `NO-GO` et la cible
est réévaluée. La campagne complète reste volontairement non lancée dans ce
jalon initial.

GO
