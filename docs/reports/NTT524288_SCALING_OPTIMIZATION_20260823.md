# PrimeForge — optimisation du régime NTT 524 288 sur RTX 5080

Date : 2026-08-23

Base de mesure : `614ae1941619b071f43a384be8e0e08b70fdf41b`

Périmètre : benchmark fermé à environ 830 000 chiffres, aucun calcul de découverte.

## Verdict

Le fallback B1 du régime NTT 524 288 est remplacé, uniquement sur la RTX 5080 mesurée, par le
profil **B6 + `256_4 sq_2048 p2i_8_64` + réduction radix-256/WG128**.

Sur le même corpus fermé :

- fallback B1/radix-64 : **10,699900 candidats complets/h** en moyenne ;
- B6/radix-64 : **28,823295 candidats complets/h**, soit **2,693791×** ;
- B6/radix-256 : **29,617120 candidats complets/h**, soit **2,767981×** face au fallback B1 ;
- gain total retenu : **+176,798106 %**.

Les deux exécutions finales radix-256 ont pris 729,540726 s et 729,075240 s pour six candidats.
Elles produisent le même hash de résultats
`01fb7f283efa00fb99e6d79c975e32f3a1960fb928f6013d7ff4c40b056e0c84`, les mêmes témoins,
classifications et RES64 que radix-64, avec `Gerbicz PASS`.

Une reconstruction propre depuis la pile de patches finale a ensuite produit le binaire SHA-256
`7a135f0671a1f8caf76f4976e498732c8eec0861966368f200f4e9bff043e1ca`. Sa reproduction complète
sur les six mêmes candidats a pris 731,000669 s, soit 29,548537 candidats/h : écart de -0,232 %
face à la moyenne A/B, hash de résultats identique et `Gerbicz PASS`. La pile livrée reproduit donc
le résultat retenu sans régression significative.

## Correction de la carte de scaling

La supposition antérieure « 750k chiffres implique probablement NTT 524 288 » était fausse sur le
corpus représentatif testé. Un contrôle borné à exactement 750 000 chiffres a déclaré une
transformée **262 144**. Le corpus à exactement 830 000 chiffres déclare **524 288**. Les anciennes
projections 750k/1M restent historiques et ne doivent pas être relues comme des mesures du nouveau
régime.

La sélection du corpus est déterministe : formule de chiffres explicite, commit de base, graine et
algorithme enregistrés. Le manifeste porte le SHA-256
`b0806f19a2beef8dbb12c0e48204710ab70518061bfec3bc2ad7e563deee2eb9`.

## Sélection du plan et du lot

Les 22 séquences de carré disponibles ont d'abord été balayées sur 4 096 itérations. Les trois
meilleures ont ensuite été répétées sur 32 768 itérations dans les deux sens. Le signal court initial
en faveur de `256_8 sq_2048` ne s'est pas maintenu : la confirmation longue donne :

| Plan | débit de voie confirmé |
|---|---:|
| `256_4 sq_2048` | 8 256,766 itérations/s |
| `256_8 sq_2048` | 8 241,841 itérations/s |
| `1024_4 sq_512` | 8 185,548 itérations/s |

L'écart entre les deux premiers n'est que de 0,181 %. Le meilleur est conservé, mais ce signal
faible interdit de prolonger la micro-optimisation du plan. Le balayage poly2int retient
`p2i_8_64`; son avance sur `p2i_4_64` n'est que de 0,268 % et reçoit la même règle d'arrêt.

Le lot a ensuite été mesuré autour de son optimum :

| Lot | travail utile/s | écart face à B6 |
|---:|---:|---:|
| B5 | 21 668,272 | -2,806 % |
| **B6** | **22 293,758** | — |
| B7 | 22 183,599 | -0,494 % |
| B8 | 21 818,368 | -2,132 % |

B10, B12 et B16 régressent davantage. La VRAM n'est pas la limite : B16 tient en mémoire mais tombe
à environ 14 570 unités utiles/s. B6 est donc un optimum de calcul mesuré, pas une taille choisie
pour maximiser artificiellement l'utilisation GPU.

## Profil après B6/radix-64

Un profil borné de 2 048 itérations donne :

| Groupe | Part du temps événement |
|---|---:|
| réduction | 30,256 % |
| pointwise square | 25,096 % |
| poly2int | 15,675 % |
| NTT inverse | 11,646 % |
| NTT avant | 10,405 % |
| autre | 6,922 % |

La réduction redevenant le premier goulot, radix-256/WG128 a été testé sans changer le lot ni le
plan. Le proxy A/B puis B/A indiquait +2,319 %. La porte complète A/B puis B/A confirme
**+2,744011 % end-to-end** : 29,617120 c/h contre 28,826128 c/h. Le changement est retenu.

## Exactitude et matériel

- six résultats complets identiques entre B6/radix-64 et B6/radix-256 ;
- candidat commun B1/B6 : même classification, témoin et RES64 `FD6B0DD7BED1A694` ;
- toutes les exécutions complètes : `Gerbicz PASS` ;
- GPU maximal : 70 °C ;
- puissance GPU maximale observée : 319,62 W ;
- VRAM maximale : 2 085 Mio ;
- WHEA maximal : 0 ;
- reproduction depuis la pile finale : 69 °C, 318,30 W, 2 083 Mio de VRAM, 96,176 % GPU moyen,
  WHEA 0 ;
- aucun benchmark individuel n'a dépassé 30 minutes ;
- aucune campagne de découverte n'a été lancée.

## Intégration bornée

Le routeur choisit B6 seulement pour : NVIDIA RTX 5080, transformée exactement 524 288 et capacité
moteur d'au moins six voies. Le moteur active le plan et radix-256 seulement avec la même identité
GPU, au moins 8 Gio de mémoire globale, NTT 524 288 et B6. Une capacité inférieure conserve son lot
explicite/autotuné et n'annonce pas le profil mesuré. Tout autre matériel ou toute autre transformée
reste sur le fallback conservateur.

Le banc borné dispose d'un mode explicite `--native-probe-iterations`. Ce mode produit une mesure
sans verdict mathématique et l'étiquette `NOT_APPLICABLE_BOUNDED_PROBE`; il ne peut donc jamais être
confondu avec une preuve. Les plans forcés exigent deux variables d'environnement portant le préfixe
`PRIMEFORGE_EXPERIMENTAL_` et sont signalés `EXPERIMENTAL_OVERRIDE`.

## Règle d'arrêt

Après B6 et radix-256, les voisins immédiats du lot sont inférieurs et les différences de plan ou de
poly2int restantes sont inférieures à 0,3 %. Ces micro-ajustements sont figés. Un prochain travail ne
sera justifié que par un nouveau profil complet montrant une transformation structurelle plausible,
pas par le sauvetage répété de variantes sous le bruit.

## Validation logicielle locale

- MSVC 19.51.36252.0, `_MSC_FULL_VER=195136252`, mode C++23 ;
- Debug : compilation PASS, CTest 43/43 PASS, self-test PASS ;
- Release : compilation PASS, CTest 43/43 PASS, self-test PASS ;
- parseurs Windows PowerShell 5.1 et PowerShell 7 : PASS ;
- lancement borné réel sous Windows PowerShell 5.1 : PASS ;
- reconstruction propre du commit Proth20 épinglé avec la pile complète : PASS ;
- reproduction complète du binaire reconstruit : six résultats identiques, Gerbicz PASS ;
- manifeste des preuves : SHA-256
  `ed53d27147a39bdb2194e90540a3b038191aef68abccad31c47ce33f8efd0766`.

## Contribution directe au logiciel final

Ce jalon retire un fallback B1 très pénalisant au premier mur NTT supérieur et porte le débit réel de
10,70 à 29,62 candidats complets/h sur la machine cible, avec preuves Gerbicz et RES64 inchangés. Il
améliore directement le moteur de campagne, sans lancer de campagne. Le profil RTX 5080/NTT 524 288
est terminé pour le pilote, le code et le matériel actuels ; il devra être remesuré après un changement
de pilote, de kernels fondamentaux ou de GPU.

## Preuves

- `benchmarks/evidence/ntt524288-b6-radix256-20260823/corpus.tsv` ;
- `benchmarks/evidence/ntt524288-b6-radix256-20260823/batch_sweep.tsv` ;
- `benchmarks/evidence/ntt524288-b6-radix256-20260823/throughput.tsv` ;
- `benchmarks/evidence/ntt524288-b6-radix256-20260823/results.tsv` ;
- `benchmarks/evidence/ntt524288-b6-radix256-20260823/correctness.tsv` ;
- `benchmarks/evidence/ntt524288-b6-radix256-20260823/profile.tsv` ;
- `benchmarks/evidence/ntt524288-b6-radix256-20260823/experiments.tsv`.
