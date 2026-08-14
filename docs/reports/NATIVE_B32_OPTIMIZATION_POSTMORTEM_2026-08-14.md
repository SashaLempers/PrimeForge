# Autopsie et optimisation NTT native B32

**Statut :** `OPTIMIZATION_MISSION_COMPLETE`

**Date :** 2026-08-14

## Portée

Cette mission analyse la campagne réelle `n=66411`, puis optimise le moteur sur
des survivants déjà calculés. Elle ne lance aucune nouvelle campagne de
découverte et n'étend aucune plage. La fonction objectif est le nombre de
candidats complètement validés par heure, pas le pourcentage d'utilisation GPU.

Les comparaisons A/B utilisent les mêmes candidats, l'ordre A/B/B/A, les mêmes
conditions matérielles autant que possible et exigent l'identité stricte des
classifications, témoins et `RES64`, ainsi que `Gerbicz PASS`.

## Phase 1 — autopsie de la campagne réelle

La campagne B8 a terminé 1 016 candidats en 1 264,458 secondes, soit
2 892,622768 candidats/heure. La somme des murs de lots est 1 264,021951 s.

| Phase | Secondes | Part du mur des lots |
|---|---:|---:|
| Boucle NTT principale | 1 165,845114 | 92,232980 % |
| Construction des paramètres | 21,709347 | 1,717482 % |
| `a^k` GPU | 20,230193 | 1,600462 % |
| Gerbicz GPU | 7,160864 | 0,566514 % |
| Scheduler | 0,247858 | 0,019609 % |
| Persistance des résultats | 0,262212 | 0,020744 % |
| Checkpoints | 0,283106 | 0,022397 % |

Les copies H2D+D2H totalisent 0,079112 s. La persistance et les checkpoints
totalisent 0,545318 s, soit 0,043141 % du mur : les rendre asynchrones ne peut
pas produire un gain end-to-end significatif dans ce profil. Cette piste n'a
donc pas consommé de temps d'implémentation.

Les phases GPU nommées couvrent 94,408518 % du mur. Le résidu observable, borne
supérieure de l'inactivité GPU et des attentes non attribuées, est 5,591482 %.
Le GPU n'attend donc pas principalement le disque ou le scheduler : chaque lot
B8 exécute surtout une longue séquence NTT sérialisée.

Le profil OpenCL B8 répartit le temps événementiel comme suit : réduction
49,732100 %, `poly2int` 24,416059 %, NTT avant 8,896025 %, iNTT 8,415838 %,
square 8,161055 %, autres 0,378923 %. Le P0 mesuré est donc le travail NTT
multi-passes, particulièrement réduction+`poly2int`, et non la télémétrie.

La campagne n'a produit aucun WHEA, aucun NVIDIA Xid, aucun throttling ; la
température GPU maximale observée est 59 °C.

## Priorisation

1. **P0 : augmenter le nombre de candidats partageant une même séquence NTT.**
   Le coût principal dépend très peu du nombre de lanes entre B8 et B32, ce qui
   donne un potentiel proche de ×4 sans modifier l'arithmétique.
2. **P1 : stabiliser un plan NTT déjà observé rapide.** Faible risque et essai
   court, mais gain théorique limité à la construction de paramètres.
3. **Après B32 : réduire le trafic réduction+`poly2int`.** Potentiel structurel
   réel, mais risque supérieur ; cette piste doit partir du nouveau profil.
4. **Non prioritaire : persistance/checkpoint asynchrones.** Gain théorique
   maximal inférieur à 0,05 % avant même le coût de complexité.

## Phase 2 — expériences A/B

### Plan forcé 5/3 — rejeté

Le plan `256_4 sq_32 p2i_8_16` supprime environ 135 ms de recherche de plan,
mais ne réduit pas le mur complet. La moyenne A automatique est 9,077997 s ; la
moyenne B forcée est 9,095247 s, soit une régression end-to-end de 0,190 %.
Les résultats mathématiques sont identiques et Gerbicz passe. Le changement est
rejeté et aucun micro-ajustement supplémentaire n'est tenté.

### B16 — retenu

Deux lots B8 sur 16 candidats prennent 17,935736 s en moyenne. Un lot B16 prend
9,331327 s : 3 211,465583 contre 6 172,755824 candidats/heure, soit ×1,922099.
Les quatre passages A/B/B/A sont identiques mathématiquement et Gerbicz passe.

### B32 — retenu

Deux lots B16 sur 32 candidats prennent 18,395717 s en moyenne. Un lot B32
prend 9,499122 s : 6 262,327377 contre 12 127,436301 candidats/heure, soit
×1,936570 supplémentaire. Les quatre passages A/B/B/A sont identiques et
Gerbicz passe.

B32 monte à 63 °C et 119,48 W dans le microbenchmark, sans throttling. La VRAM
mesurée reste sous 3 215 MiB. Aucun réglage matériel n'a été modifié.

## Intégration de production

Le plafond du moteur Proth20 épinglé passe de 8 à 32 lanes sans changer les
kernels ni l'arithmétique. Le scheduler accepte `--batch-size 1..32`, utilise
B32 par défaut, et conserve les noms de cibles B8 existants pour compatibilité
des scripts et checkpoints.

Les contrôles durables acceptent désormais des lots complets jusqu'à B32. Ils
conservent les hashes de lot, l'ordre des lanes, la détection de trou/doublon,
la récupération d'un résultat en avance sur le checkpoint, l'arrêt aligné sur
un lot et l'arrêt au premier prouvé.

Le build propre de Proth20 à la révision
`6771325939a7ceef2c75644c79981c7df4a61882` produit le binaire B32 de SHA-256
`716ac9c318659476f4072bd3421f145cd52044d4d9259f834354f51b9d521df7`.

## Benchmark end-to-end final

Le corpus exact est le préfixe de 256 survivants de la campagne réelle. Les 32
lots B8 historiques couvrant ce même corpus totalisent 319,752244 s, soit
2 882,231532 candidats/heure. Les huit lots B32 totalisent 77,085977 s, soit
11 955,481864 candidats/heure.

**Gain exact sur le même corpus : ×4,147995.**

Le chronomètre extérieur, plus conservateur, inclut le lancement et la sortie
du scheduler : 77,1689263 s et 11 942,630851 candidats/heure.

Les 256 classifications, témoins et `RES64` sont identiques aux résultats B8.
Chaque lot rapporte `Gerbicz PASS`. Le terminal est `COMPLETE_NO_PRIME`, avec
256 résultats uniques, zéro restant et zéro erreur. Il ne s'agit pas d'une
nouvelle campagne : seuls des candidats déjà calculés ont été rejoués.

Un contrôle séparé B32 place le premier déjà prouvé
`76005585*2^66411+1` en lane 31. Le moteur retourne 32 lignes, le premier avec
témoin 7 et `RES64=0000000000000000`, puis sort normalement avec Gerbicz PASS.

## Nouveau profil

Après B32, la boucle NTT principale reste dominante à 88,215894 % du mur. Les
phases GPU nommées couvrent 94,575379 % ; la borne supérieure résiduelle
d'inactivité/attente est 5,424621 %.

Le profil kernel B32 attribue 49,723840 % des événements à la réduction,
22,743170 % à `poly2int`, 10,357973 % à la NTT avant, 9,953993 % à l'iNTT,
6,874535 % au square et 0,346487 % au reste. B32 a donc supprimé le manque de
lanes comme premier levier, mais le prochain goulot reste la chaîne mémoire
réduction+`poly2int` (72,467010 % des événements profilés).

Le compteur NVIDIA moyen n'est pas utilisé comme fonction objectif : il est
sensible aux périodes de démarrage et reste peu comparable entre lots. Le débit
complet, l'identité mathématique et le profil de phases sont les critères de
décision.

## Projection 100k

Sur le même type de survivants et hors criblage :

- avant B32 : 34,695339 heures pour 100 000 candidats ;
- après B32 : 8,364364 heures ;
- réduction projetée : 26,330975 heures.

Cette projection est linéaire et ne constitue pas une promesse pour une autre
taille de transformée, un autre GPU ou une autre distribution de candidats.

## Portes de qualité

- MSVC Debug : compilation sans avertissement PrimeForge, 43/43 tests ;
- MSVC Release : compilation sans avertissement PrimeForge, 43/43 tests ;
- B32+B32+B1 : PASS ;
- arrêt/reprise B32 : PASS et suffixe exact ;
- résultat durable en avance sur checkpoint : PASS ;
- stop-on-prime B32 : PASS ;
- classifications/témoins/RES64 : PASS ;
- Gerbicz : PASS ;
- WHEA/Xid/throttling : zéro ;
- CI Windows et Linux : voir l'état du commit final.

Les données synthétiques suivies sont dans
`benchmarks/evidence/native-b32-optimization/`. Les hashes des artefacts bruts
locaux ignorés sont conservés dans `raw-artifacts.sha256`.

## Contribution directe au logiciel final

Ce jalon multiplie par 4,147995 le débit complet du chemin qui prouve les
survivants GPU, sur exactement les mêmes 256 candidats. Il ne rajoute pas une
couche d'infrastructure : il augmente directement le parallélisme utile de la
boucle NTT dominante, tout en maintenant preuve, Gerbicz, checkpoint, reprise
et arrêt-au-premier.

Le travail B16/B32 est terminé et retenu. Le prochain jalon recommandé est un
prototype borné de fusion/réorganisation des passes réduction+`poly2int`, avec
mesure du trafic mémoire et le même protocole A/B/B/A. Aucune nouvelle campagne
de découverte ne doit démarrer sans GO explicite.
