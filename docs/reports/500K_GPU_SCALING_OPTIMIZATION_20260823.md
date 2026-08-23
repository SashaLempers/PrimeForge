# PrimeForge — optimisation GPU ciblée 500k chiffres

Date : 2026-08-23  
Périmètre : RTX 5080, nombres de Proth d'environ 500 000 chiffres, NTT 262 144.  
Fonction objectif : candidats complets validés par heure.  
Statut : optimisation retenue ; aucune campagne de découverte lancée.

## Verdict

Le profil de production retenu est **B12 + plan `256_8 sq_1024 p2i_8_64` + réduction
radix-256/WG128**. Sur les mesures nocturnes isolées, il atteint en moyenne
**121,334755 candidats/h**, contre **104,916809 candidats/h** pour la baseline B8 de la
même session, soit **+15,648537 %**. Une reconstruction depuis la révision Proth20 figée et
la pile de patches versionnée reproduit ensuite **121,540999 candidats/h** avec les mêmes
résultats exacts.

Le chiffre historique de 98,123619 candidats/h reste une référence antérieure. Le comparer
au meilleur résultat donne +23,654994 %, mais cette comparaison inter-session n'est pas la
porte principale. La revendication retenue est le +15,648537 % mesuré dans la même série.

## Corpus et exactitude

Le corpus fermé B12 contient douze couples `k,n` avec `n=1660936` :

- SHA-256 du corpus : `d007ebdf3ac700d92862f9ba38be48e10992b7f56c67f4dc5d3d2efd7bc4d817` ;
- SHA-256 canonique des douze résultats :
  `85bea2e820455e9dbd507e624912212a73d04a8c91b620181e3afff4ea267c9b` ;
- verdicts : 12 composites, mêmes témoins et mêmes RES64 dans tous les bras complets ;
- Gerbicz : PASS ; stderr : 0 octet.

Le chemin positif est contrôlé séparément sur huit premiers de Proth prouvés à `n=256` :

- SHA-256 du corpus : `ec9f904340b79bec1ab3b4ae958cd77c342cd7da240a88dc0256f5462fb72aa9` ;
- résultat canonique : `a85d2fb23b9afc4e467bf04da0b60f9a644614673f4cc576b5eb82baf9c1e6b5` ;
- 8/8 `PROVEN_PRIME`, Gerbicz PASS, stderr vide.

## Décomposition du gain

| Profil | Exécutions | Moyenne candidats/h | Gain incrémental |
|---|---:|---:|---:|
| B8 figé | 2 | 104,916809 | baseline |
| B12 + plan figé | 3 | 115,389386 | +9,981792 % face à B8 |
| B12 + radix-256/WG128 | 2 | 121,334755 | +5,152440 % face à B12 ; +15,648537 % total |
| reconstruction intégrée | 1 | 121,540999 | reproduction, pas une nouvelle moyenne |

Le passage B8→B12 augmente le travail utile par lot sans multiplier proportionnellement le temps.
Le radix-256 réduit ensuite les niveaux de balayage global de la division de cinq synchronisations
à trois pour NTT 262 144. Le work-group 128 est le seul WG retenu : WG64 n'a donné qu'un signal
micro de +0,733 %, trop faible pour justifier un gate complet.

## Profil après optimisation

Le profil kernel instrumenté est volontairement exclu du débit, car la collecte d'événements porte
le mur à 681,90645 s. Sa répartition explique cependant le goulot restant :

| Groupe | Temps événement | Part |
|---|---:|---:|
| réduction | 120,693 s | 27,640 % |
| pointwise square | 104,699 s | 23,978 % |
| poly2int | 90,939 s | 20,826 % |
| NTT avant | 55,885 s | 12,799 % |
| NTT inverse | 52,002 s | 11,909 % |
| autre | 12,436 s | 2,848 % |

La réduction et poly2int restent ensemble à 48,467 % du temps événement, mais les variantes courtes
qui les ciblaient n'ont pas franchi la porte. Le prochain gain devra réduire structurellement le
nombre de passages ou de synchronisations ; modifier seulement des index, une taille de groupe ou
un branchement local a été insuffisant.

## Variantes rejetées

- mul/rem combiné au meilleur chemin : 120,153989 candidats/h, soit -0,973147 % face à la moyenne
  retenue ;
- replay poly2int spéculatif : +0,340788 % apparent, mais compteur de déclenchement nul ; aucun effet
  algorithmique démontré ;
- B13 : environ +0,17 % sur proxy court seulement ; pas de benchmark complet ;
- WG64, index 32 bits et unroll poly2int0 : signaux de +0,733 %, +0,498 % et +0,480 %, sous la porte ;
- WG128 poly2int8 : +0,043 %, bruit ;
- layout SoA square1024 : -3,215 % ;
- poly2int1 branchless : -18,056 % ;
- `cl_khr_command_buffer` absent du pilote OpenCL NVIDIA 610.74, donc aucune implantation command
  buffer OpenCL n'est revendiquée.

Ces résultats sont figés ; ils ne seront pas « sauvés » par une suite de micro-ajustements.

## Intégration de production

Le patch `proth20-500k-b12-radix256.patch` s'applique après la pile Proth20 existante. Son activation
est fermée par toutes les conditions suivantes : GPU NVIDIA RTX 5080, mémoire globale d'au moins
8 Gio, transformée exactement 262 144 et lot exactement B12. Les autres matériels, tailles et lots
conservent le chemin radix-64 existant.

Le routeur PrimeForge choisit désormais B12 automatiquement pour ce régime et annonce le plan
`primeforge.native-plan.rtx5080-500k-b12-radix256-wg128.v1`. Si le moteur déclare une capacité
inférieure à 12, la capacité est respectée et le routeur n'annonce pas à tort le plan mesuré.

La reconstruction intégrée a mesuré : GPU maximal 71 °C, CPU maximal 73,125 °C, puissance GPU
moyenne 339,226 W, utilisation GPU moyenne 95,706 %, VRAM maximale 2 058 Mio, zéro WHEA. Le motif
NVIDIA `SW_POWER_CAP` a été observé ; il n'est ni présenté comme une erreur ni ignoré comme donnée.

## Au-delà de 500k

Le fallback B1 pour une transformée 524 288 reste volontairement inchangé. Les valeurs 750k
**49–65 candidats/h** et 1M **36–49 candidats/h** restent `EXTRAPOLATED` avec confiance limitée.
Avant une campagne au-delà de 500k, la prochaine étape correcte est : B1 de validation/warm-up,
puis autotune borné B2/B4/B8 sur quelques candidats fermés, cache par GPU/driver/transformée, et
gate end-to-end exact. Aucune performance 750k ou 1M n'est revendiquée ici.

## Contribution directe au logiciel final

Ce jalon améliore directement le moteur mathématique de campagne 500k, pas son infrastructure :
il traite davantage de candidats complets par heure tout en conservant témoins, RES64 et Gerbicz.
Le profil 500k est terminé pour la combinaison RTX 5080/NTT 262 144 actuelle. Il faudra le remesurer
si le pilote, les kernels fondamentaux ou le matériel changent. Le travail 524 288 reste séparé et
devra commencer par un autotune court ; aucune campagne de découverte n'a été lancée.

## Preuves

- `benchmarks/evidence/500k-scaling-optimization-20260823/throughput.tsv`
- `benchmarks/evidence/500k-scaling-optimization-20260823/profile.tsv`
- `benchmarks/evidence/500k-scaling-optimization-20260823/correctness.tsv`
- `benchmarks/evidence/500k-scaling-optimization-20260823/experiments.tsv`

## Validation locale finale

- reconstruction Proth20 depuis `6771325939a7ceef2c75644c79981c7df4a61882` : PASS ;
- application répétée de la pile de patches : PASS ;
- corpus positif : 8/8 `PROVEN_PRIME`, hash exact, Gerbicz PASS ;
- corpus 500k intégré : 12/12 résultats exacts, Gerbicz PASS, stderr vide ;
- Debug MSVC : build PASS, CTest final 43/43 en 48,95 s, self-test PASS ;
- Release MSVC : build PASS, CTest final 43/43 en 20,31 s, self-test PASS ;
- compilateur : MSVC 19.51.36252, mode C++23 ;
- CI Windows/Linux : exigée avant fusion, statut à enregistrer dans la pull request.
