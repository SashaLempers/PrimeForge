# PIVOT-08 — sélection de la première famille cible

**Statut :** COMPLETE

**Décision :** PrimeForge spécialise sa première voie de production sur les
nombres de Proth

`N = k * 2^n + 1`, avec `k` impair, `k >= 1`, `n >= 1` et `k < 2^n`.

Cette décision porte sur la forme mathématique et le moteur. Elle n'autorise ni
zone de nouveauté, ni attribution externe, ni campagne prolongée.

## Pourquoi cette famille

Le recadrage MVP fourni par le propriétaire demandait déjà de privilégier cette
forme. Elle réutilise sans extension incompatible le langage `k*b^n+c`, le
compilateur de congruences, le crible, les unités de travail et la reprise. Elle
possède en outre un chemin de preuve spécialisé : le théorème de Proth permet de
conclure à la primalité lorsqu'un témoin `a` vérifie
`a^((N-1)/2) == -1 (mod N)`. Un survivant ou un PRP qui ne possède pas encore
un tel certificat reste `PROBABLE_PRIME`, jamais `PROVEN_PRIME`.

## Comparaison des voies étudiées

| Famille | Réutilisation PrimeForge | Criblage CPU/GPU | PRP et preuve | Couverture/risque | Décision |
|---|---|---|---|---|---|
| Proth `k*2^n+1` | Directe : `b=2`, `c=1` | Classes de congruence massives, lots homogènes | Théorème de Proth ; proth20 comme oracle local | Recherche historique et PrimeGrid actives : nouveauté à auditer | Retenue |
| Sierpiński/Riesel `k*2^n±1` | Directe pour un `k` borné | Très favorable | LLR/PRST et preuves selon la forme ; PRST sans licence globale | Problèmes distribués actifs et attribution externe sensible | Reportée |
| Fermat généralisé `b^(2^n)+1` | Faible : la forme et le partitionnement diffèrent | Excellent sur GPU | Genefer offre PRP, déterministe et preuves | Plages PrimeGrid activement suivies ; moteur FFT/NTT nouveau | Reportée |
| Cullen/Woodall `n*2^n±1` | Faible : couplage coefficient/exposant absent du langage v1 | Crible favorable | Références LLR, preuve moins directement intégrée | Projet PrimeGrid actif | Reportée |

La famille de Fermat généralisée est la concurrente GPU la plus sérieuse :
Genefer documente un chemin CPU/OpenCL, des tests probabilistes et déterministes
et une preuve. Elle imposerait toutefois maintenant un moteur transformé large,
un nouveau partitionnement et beaucoup moins de réutilisation du logiciel déjà
validé. Le choix Proth minimise donc le temps jusqu'à une chaîne propre
crible -> PRP -> preuve -> vérification.

## Contrat borné de développement

Le domaine canonique de développement PIVOT-08 est :

- `family_id = primeforge.proth.v1` ;
- `base = 2`, `constant = 1` ;
- `k` impair et strictement positif ;
- `n >= 1` ;
- validation obligatoire de `k < 2^n` sans dépassement ;
- intervalles `k` et `n` finis dans chaque définition de campagne ;
- chaque unité de travail contient le hash de la définition canonique ;
- les configurations de test et de benchmark sont des domaines connus ou
  localement contrôlés, jamais présentés comme une recherche de nouveauté.

Le premier corpus de développement reste volontairement petit. PIVOT-09 fixe
séparément un domaine de comparaison reproductible après vérification des limites
communes de PrimeForge et des moteurs de référence. PIVOT-10 ne pourra utiliser
qu'une petite plage connue ou localement contrôlée. La plage d'une éventuelle
campagne de nouveauté n'est pas choisie ici.

## Chemin de calcul

1. Le CPU génère les couples `(k,n)` dans l'ordre canonique.
2. Le compilateur élimine les classes divisibles par les petits nombres premiers.
3. Le crible segmenté produit un ledger exact des survivants.
4. Les survivants sont groupés par longueur et envoyés au moteur modulaire.
5. Un test PRP spécialisé peut produire uniquement `PROBABLE_PRIME`.
6. Pour un candidat Proth, une recherche bornée de témoin tente le théorème de
   Proth et produit un certificat contenant `N`, `k`, `n`, `a`, l'algorithme,
   les identifiants de moteur et les hashes d'entrée/sortie.
7. PrimeForge rejoue le certificat sur le backend CPU de référence.
8. Un moteur distinct vérifie ensuite le résultat avant
   `INDEPENDENTLY_VERIFIED`.

Le chemin CUDA actuel ne traite que des lots modulaires 64 bits. Il reste un
socle de correction et de scheduling ; il ne suffit pas aux grands nombres de
Proth. Le prochain travail moteur utile est donc une arithmétique modulaire
multi-précision bornée, avec oracle CPU bit à bit, puis l'exponentiation par lots.

## Plan de preuve

- Condition de forme vérifiée avant tout essai de preuve : `k` impair et
  `0 < k < 2^n`.
- Le résultat d'un test probable reste `PROBABLE_PRIME`.
- `PROVEN_PRIME` exige un certificat Proth valide ou une autre preuve complète
  explicitement reconnue.
- L'échec à trouver un témoin n'est pas une preuve de composition.
- Le certificat est vérifié depuis ses octets canoniques et rattaché au ledger.
- L'oracle proth20, sous licence MIT et non redistribué, peut servir de contrôle
  local indépendant ; PARI/GP reste un second oracle isolé pour les tailles qu'il
  peut traiter raisonnablement.

## Plan de couverture et nouveauté

PrimeGrid recherche activement plusieurs familles structurées, y compris Proth,
et son historique indique une coordination ancienne avec Proth Search. Par
conséquent :

- chaque résultat PIVOT-08/09/10 porte `novelty_status=NOT_CHECKED`, sauf preuve
  documentaire contraire ;
- aucune sortie locale n'est qualifiée de découverte ;
- aucune plage PrimeGrid/GIMPS n'est demandée ou revendiquée ;
- avant toute future campagne de nouveauté, un snapshot daté des couvertures
  officielles et des bases de nombres premiers connus doit être archivé et hashé ;
- la plage doit être démontrée sans chevauchement au moment du lancement ;
- `DUE_DILIGENCE_COMPLETE` exige une seconde vérification après le calcul ;
- publication, attribution et contact externe restent soumis à autorisation.

L'absence actuelle d'une carte officielle machine-lisible et exhaustive de toute
la couverture Proth empêche de choisir honnêtement une nouvelle plage cette nuit.
Cela ne bloque ni la spécialisation du moteur ni les essais sur plages connues.

## Références primaires et officielles

- E. Proth, mémoire original (scan du *Comptes rendus*, 1878) :
  https://fr.wikisource.org/wiki/Page:Comptes_rendus_hebdomadaires_des_s%C3%A9ances_de_l%E2%80%99Acad%C3%A9mie_des_sciences,_tome_087,_1878.djvu/932
- proth20, dépôt de l'auteur, implémentation OpenCL du théorème de Proth et
  licence MIT : https://github.com/galloty/proth20
- PrimeGrid, liste officielle des applications et formes recherchées :
  https://www.primegrid.com/apps.php
- PrimeGrid, historique officiel du partenariat Proth Search :
  https://www.primegrid.com/forum_thread.php?id=9178
- Genefer, dépôt de l'auteur et documentation de la voie Fermat généralisée :
  https://github.com/galloty/genefer22
- PrimeGrid, état officiel des plages Fermat généralisées :
  https://www.primegrid.com/gfn_history.php

## Porte PIVOT-08

- famille unique et bornable : **oui** ;
- coût de crible analysé : **oui, réutilisation directe** ;
- chemin PRP/preuve/vérification : **oui, avec séparation stricte des statuts** ;
- oracles indépendants identifiés : **oui** ;
- couverture existante évaluée : **oui, mais pas assez exhaustive pour une
  revendication de nouveauté** ;
- attribution/contact externe : **aucun** ;
- campagne réelle ou prolongée : **aucune** ;
- affirmation de performance : **aucune**.

## Vérification locale

Après la décision documentaire, `scripts/run_all.ps1` a confirmé :

- build Debug : aucun travail restant, zéro erreur et zéro avertissement ;
- CTest Debug : 32/32, 48,08 s ;
- build Release : aucun travail restant, zéro erreur et zéro avertissement ;
- CTest Release : 32/32, 14,06 s ;
- self-tests Debug et Release : C++23, `PASS`.

`scripts/run_cuda_validation.ps1` a ensuite confirmé :

- build CUDA Release : aucun travail restant ;
- CTest : 35/35, 14,69 s ;
- validation : 4 096 vecteurs, checksum
  `1797897575905442555` ;
- lots modulaires : 101 000 vecteurs exacts ;
- pipeline : 4 097 tâches, reprise exacte depuis 771 ;
- Compute Sanitizer : zéro erreur sur les trois exécutables.

Le relevé post-porte indiquait GPU 51 °C, 43,77 W, aucun throttling, 13 593 MiB
de VRAM libre et 43 737 571 328 octets de RAM disponibles. Température et
puissance CPU ainsi que température mémoire GPU restent `UNKNOWN`. Ces valeurs
sont de la télémétrie de sécurité, pas un benchmark.

## Contribution directe au logiciel final

Ce jalon fixe la forme que le moteur doit réellement générer, cribler, tester,
prouver et vérifier. Il rend possible le remplacement des petits lots modulaires
synthétiques par un pipeline Proth complet et représentatif. La sélection est
terminée pour la première voie de production ; il faudra revenir séparément sur
la plage de nouveauté, sur l'arithmétique multi-précision CPU/CUDA et sur
l'autotuning end-to-end lorsque ces composants existeront.
