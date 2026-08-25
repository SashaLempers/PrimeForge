# PrimeForge — crible profond parallèle pour une campagne à 500k chiffres

Date : 2026-08-23

Base du travail : `dc4e90991b8fa2e0bc138cd6f86958f375adef55`

Périmètre : optimisation du **temps attendu de campagne** à 500k chiffres. Aucune
campagne de découverte n'a été lancée. Le corpus fermé ci-dessous n'a fait l'objet
d'aucun audit de nouveauté et ne doit jamais être présenté comme une plage de découverte.

## Résultat

Le crible Proth accepte désormais un plafond 64 bits, un découpage déterministe de
l'intervalle de nombres premiers et jusqu'à 64 workers CPU. Sur la machine cible,
32 workers et un plafond de **256 000 000 000** sont retenus pour le régime 500k.

Sur le corpus fermé représentant exactement 1 724 480 valeurs impaires de `k`, le
nombre réel de survivants descend de 90 001 à 2 milliards à **73 344 à 256 milliards**.
C'est 16 657 tests GPU évités, soit **18,507 %** du travail GPU de la référence 2G.
Le crible profond a pris 82,6240841 s, sans throttling ni WHEA, avec 71 °C CPU au maximum.

En combinant ce compte réel de survivants au débit GPU séparément mesuré de
121,540998638853 candidats complets/h, le temps calculé passe de 740,499195 h
(30,854133 j) à **603,473645 h (25,144735 j)** : gain projeté de 137,025550 h,
soit **5,709398 jours** et un facteur **1,227061×**. Ce calcul est
`DERIVED_FROM_MEASURED_COMPONENTS`, pas une campagne end-to-end exécutée.

Face au plafond 4G précédemment disponible, le calcul passe de 716,828270 h à
603,473645 h : **4,723109 jours** projetés gagnés, facteur **1,187837×**.

## Corpus et heuristique

- famille : `k*2^1660936+1` ;
- plage fermée : `k=57000001..60448959`, valeurs impaires seulement ;
- candidats bruts : 1 724 480 ;
- construction : taille 95 % issue de `p≈2/ln(N)` autour du `k` représentatif
  58 668 980, puis bornes fixées avant le criblage ;
- ce corpus mesure le coût et les survivants ; il ne constitue pas une revendication
  de probabilité exacte, de couverture ou de nouveauté.

## Exactitude du crible

Le chemin historique 32 bits reste utilisé jusqu'à `UINT32_MAX`; au-delà, les produits
modulaires emploient l'abstraction 128 bits déjà testée sous MSVC et GCC. Les portes
suivantes passent :

- ancien et nouveau chemins à 4G : 3 489 survivants et SHA-256 identique ;
- 1, 4, 8, 16 et 32 workers à 16G : 3 285 survivants, 712 799 820 nombres premiers
  appliqués et SHA-256 identique ;
- test isolé d'un diviseur premier strictement supérieur à `UINT32_MAX` contre la
  multiplication modulaire portable de référence ;
- test de deux intervalles premiers adjacents : intersection des survivants et somme
  des comptes identiques au passage monolithique ;
- A/B/B/A, 1 contre 32 workers : mêmes octets, stderr vide.

Le temps moyen A/B/B/A à 16G est de 73,1318734 s avec un worker et 5,01141965 s avec
32 workers, soit **14,593045×**. Le débit n'est pas parfaitement linéaire, mais le
résultat mathématique est bit-à-bit déterministe.

## Profondeur mesurée

| Plafond | Workers | Survivants sur 68 980 | Temps |
|---:|---:|---:|---:|
| 2G | 1 | 3 600 | 4,1965461 s |
| 4G | 1 | 3 489 | 7,8591141 s |
| 8G | 1 | 3 378 | 28,8413 s |
| 16G | 32 | 3 285 | 4,7724048 s |
| 32G | 32 | 3 202 | 10,0699594 s |
| 64G | 32 | 3 116 | 20,5474927 s |
| 128G | 32 | 3 041 | 41,6269018 s |
| 256G | 32 | **2 964** | **82,8441304 s** |

Le plafond 256G a constitué le premier point retenu. La mesure a ensuite été prolongée
par segments disjoints, car chaque segment restait moins coûteux que les appels GPU
qu'il supprimait. Cette extension ne recalcule jamais les diviseurs déjà couverts.

| Segment ajouté | Workers | Survivants cumulés | Temps du segment | État |
|---:|---:|---:|---:|---|
| jusqu'à 64T | 16 | 60 584 | 2 331,8570 s pour 32T--64T | PASS |
| 64T--128T | 16 | 59 214 | 4 216,7310 s | PASS |
| 128T--256T | 16 | 57 984 | 8 372,30 s | PASS |
| 256T--512T | 32 | 56 797 | 5 053,5781171 s | PASS |
| 512T--1 024T | 32 | **55 688** | **10 434,227852 s** | **PASS** |
| 1 024T--2 048T | 32 | **54 633** | **21 940,6076818 s** | **PASS — PAUSED** |

Le dernier segment applique exactement 14 946 942 261 342 diviseurs premiers et
supprime 1 109 survivants supplémentaires. Il coûte 2,898397 h et évite 9,124493 h
GPU au débit mesuré de 121,540998638853 candidats/h : gain marginal net
**6,226096 h**. Le résultat cumulé donne une projection de **468,053468 h
(19,502228 jours)**. Il s'agit toujours de composants mesurés combinés, et non d'une
campagne 500k exécutée de bout en bout.

Le segment 512T--1 024T a terminé avec stderr vide, maximum CPU 87,375 C, zéro
throttling, zéro WHEA et zéro Xid. Ses SHA-256 sont
`5d601e6a...f5188` (sortie du segment), `e3dacb6e...95bbb` (télémétrie) et
`197da825...e554` (intersection cumulative).

Le segment final 1 024T--2 048T applique 29 300 953 446 128 diviseurs premiers
et supprime encore 1 055 survivants. Il coûte 6,094613 h et évite 8,680199 h GPU :
gain marginal net **2,585585 h**. La projection mesurée devient **465,467883 h
(19,394495 jours)**. L'intersection cumulative de 54 633 lignes a été reproduite
en mémoire, dans le même ordre, avec le SHA-256
`7220f1ee...912e`.

Ce segment termine avec les deux codes de sortie à zéro, stderr vide, maximum CPU
88,75 C, zéro throttling, zéro WHEA et zéro Xid. Les recherches sont volontairement
en pause à 2 048T ; aucun segment suivant n'a été lancé.

## Expériences rejetées

Deux processus GPU B12 simultanés ont atteint 51 166,585 candidate-itérations/s contre
51 482,416 pour un seul, soit **-0,613 %**. La RTX 5080 partage simplement ses ressources
entre les deux contextes et paie davantage de démarrage. Cette voie est rejetée sans
benchmark complet.

Un moteur CPU externe n'a pas été intégré : les copies PRST/LLR2 disponibles ont une
licence locale non résolue et les audits du dépôt interdisent leur construction. Un
processus externe ne règle pas automatiquement la licence.

Une spécialisation qui évite la première multiplication Montgomery a produit le même
SHA-256 mais seulement 1,36057% de gain sur l'écran apparié, sous la porte de 2% : elle
est rejetée. Un prototype de crible CUDA exact a atteint 2,757 milliards de diviseurs/s
dans le noyau, mais seulement 844,655 millions/s pour la chaîne complète avec 32 sources
persistantes. Le CPU figé atteint 1,432 milliard/s sur le segment réel ; l'intégration
CUDA aurait donc régressé de 41,035914% et est rejetée.

## Utilisation

Le contrôleur accepte maintenant un plafond 64 bits et le nombre de workers :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_prime_discovery.ps1 `
  -SieveBound 256000000000 -SieveThreads 32 -PrepareOnly
```

La commande est illustrative et ne doit être utilisée sur une vraie plage qu'après ses
portes de campagne. Le nombre de workers ne fait pas partie de l'identité mathématique :
il ne modifie ni le plafond ni les survivants et peut être réduit pour reprendre sur une
machine plus contrainte.

## Contribution directe au logiciel final

Ce jalon réduit directement le nombre d'appels au moteur GPU, qui reste le coût dominant
d'une campagne 500k. Il ne crée pas une infrastructure parallèle au moteur : il déplace un
filtrage mathématique très bon marché vers les 32 threads CPU avant le calcul NTT coûteux.

Le chemin est terminé pour chaque segment déjà validé ; la profondeur finale est décidée
uniquement par gain marginal mesuré. Les micro-variantes CPU et le prototype CUDA n'ont
pas franchi leurs portes. Après le dernier segment encore rentable, le prochain gain
structurel devra cibler le coût NTT par survivant, pas micro-ajuster ce crible.

## Preuves versionnées

- `benchmarks/evidence/500k-deep-sieve-20260823/thread_scaling.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/depth.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/full95.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/gpu_concurrency.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/projection.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/correctness.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/progressive_depth.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/hotloop_abba.tsv`
- `benchmarks/evidence/500k-deep-sieve-20260823/structural_probes.tsv`
