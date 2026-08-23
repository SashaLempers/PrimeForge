# PrimeForge — optimisation active des murs de scaling

Date de clôture expérimentale : 2026-08-22  
Base validée : `cd18043bc75d460845d2db892d8b90317a8e3a52`  
Périmètre : optimisation bornée, aucun calcul de découverte.

> Mise à jour du 2026-08-23 : le verdict GPU de ce rapport est historique. Le
> jalon suivant a retenu B12 + radix-256/WG128 à NTT 262 144 ; voir
> `500K_GPU_SCALING_OPTIMIZATION_20260823.md`. Les résultats de persistance du
> présent rapport restent valides.

## Verdict

Le mur générique de persistance quadratique est supprimé. Le journal des résultats est désormais
append-only et synchronisé durablement par lot ; le checkpoint v2 ne contient plus les collections
complètes, mais un curseur, les identités immuables et une chaîne de hashes de lots. Sur le corpus
fermé de 4 096 résultats, la médiane passe de **10 200,796 ms à 1 623,415 ms**, soit **6,284×**,
et le ratio de temps lors du doublement 2 048→4 096 revient à **1,956×** au lieu de **3,418×**.

Aucune modification du moteur GPU n'est retenue. Les trois pistes courtes testées à 300k chiffres
n'apportent pas de gain reproductible : plan figé dans le bruit, fusion réduction/poly2int en
régression de 2,0 %, B24 sous le débit B16. Conformément à la porte, aucun essai 500k de ces variantes
n'a été lancé. Le moteur mathématique de production et ses résultats restent donc inchangés.

## 1. Persistance linéaire retenue

### Chemin durable

`candidate-results.tsv` conserve son format compatible, mais n'est plus resérialisé et remplacé en
entier à chaque lot. Le chemin retenu est :

1. création atomique unique de l'en-tête ;
2. sérialisation du seul lot terminé ;
3. append au descripteur ouvert en mode append ;
4. `FlushFileBuffers` sous Windows ou `fsync` sous Linux ;
5. mise à jour atomique du checkpoint après la durabilité du résultat.

La queue n'est plus amputée avec un `erase(begin)` répété : elle reste immuable et un curseur
`remaining_offset` avance. Les hashes des fichiers source parent/queue sont calculés une fois au
démarrage. Le SHA-256 physique complet du journal reste produit une fois à la clôture pour le résumé.

### Checkpoint v2

Le checkpoint v2 enregistre notamment :

- `candidate_results_hash_kind=BATCH_CHAIN_V1` ;
- la racine incrémentale de la chaîne des hashes de lots ;
- `completed_resume_count`, `remaining_offset` et `remaining_count` ;
- les SHA-256 immuables de la queue source et du préfixe parent ;
- l'identité du dernier lot et son hash.

La taille observée reste comprise entre 989 et 991 octets pour 1 024 à 4 096 résultats. Le lecteur
accepte encore un checkpoint v1 valide, l'authentifie selon ses règles historiques puis écrit du v2
au checkpoint suivant.

### Reprise après crash

L'ordre d'écriture est volontairement write-ahead. Au redémarrage :

- une ligne physique finale tronquée est supprimée ;
- chaque ligne complète et chaque lot complet sont réauthentifiés ;
- un suffixe contenant seulement une partie d'un lot est ramené au dernier lot complet ;
- un lot complet durable en avance sur le checkpoint est repris sans recalcul ;
- toute divergence de campagne, moteur, candidat, ordre, hash, verdict ou doublon reste fatale.

Les tests couvrent la troncature, le suffixe partiel, le résultat en avance, arrêt/reprise B8 et B32,
arrêt au premier résultat, migration v1→v2 et absence de trou/doublon.

## 2. Mesure A/B puis B/A de la persistance

Corpus déterministe fermé, faux exécuteur mathématique identique, B32, build Debug. Les fichiers de
résultats ont exactement les mêmes tailles dans les deux variantes.

| Résultats | ancienne médiane | nouvelle médiane | accélération | ratio ancien au doublement | ratio nouveau au doublement |
|---:|---:|---:|---:|---:|---:|
| 1 024 | 1 017,410 ms | 425,590 ms | 2,391× | — | — |
| 2 048 | 2 984,699 ms | 829,9995 ms | 3,596× | 2,934× | 1,950× |
| 4 096 | 10 200,796 ms | 1 623,415 ms | 6,284× | 3,418× | 1,956× |

Le débit optimisé reste autour de 2 474–2 574 résultats/s à 4 096, alors que l'ancien chemin tombe
à environ 401–402 résultats/s. Cette expérience mesure le scheduler/persistence, pas le coût GPU.

À un million de survivants, le journal n'écrit plus qu'environ **418,9 MiB** de lignes une seule
fois, au lieu des **6,24 à 25,0 TiB** de réécritures cumulées précédemment dérivées. Les petits
checkpoints atomiques restent proportionnels au nombre de lots ; l'explosion du nombre de fichiers
bruts par tentative n'est pas corrigée ici et demeure un risque séparé.

## 3. Profil GPU et essais bornés

Le profil conservé avant optimisation montre, à 300k/B1, près d'un million de lancements pour
plusieurs kernels. Les groupes dominants mesurés sont : réduction 55,883 s, poly2int 52,803 s,
pointwise square 19,861 s, forward NTT 18,478 s et inverse NTT 16,274 s. À 300k/B16, le chemin
`main_ntt_gpu` représente environ 97,88 % du temps ; à 500k/B8, 98,35 %.

### Plan figé 131072/262144

Le prototype imposait les plans observés comme favorables pour NTT 131072/B16 et 262144/B8.
Séquence A/B puis B/A à 300k/B16 :

- baseline médiane : 169,646162 s ;
- plan figé médian : 169,400134 s ;
- écart : **+0,145 %**, inférieur au bruit et non reproductible comme gain utile.

Verdict : **REJETÉ**. Aucun changement de plan n'est intégré.

### Fusion réduction/poly2int

Le prototype retire le lancement `poly2int2` sur les grands régimes, mais ajoute le branchement
nécessaire au chemin de réduction. Sur le même corpus et le même plan :

- contrôle médian : 165,525975 s ;
- fusion médiane : 168,848825 s ;
- régression : **2,007 %**.

Verdict : **REJETÉ**. Le coût ajouté dans les kernels massivement répétés dépasse le lancement
supprimé ; aucune série de micro-ajustements n'est poursuivie.

### B24 à NTT 131072

Le gate exploratoire donne 344,676 candidats/h, sous le B16 retenu de l'étude de référence
(347,396 candidats/h). Gerbicz passe et le résultat est exact, mais le gate de gain échoue.

Verdict : **REJETÉ**, sans répétitions ni essai 500k.

Tous les essais GPU ont produit les mêmes verdicts exacts, le même hash de résultats par corpus,
`Gerbicz PASS` et zéro WHEA. Le `SW_POWER_CAP` observé n'est pas une erreur de calcul. Les maxima GPU
restent à 69–70 °C.

## 4. Dispatch batch/plan retenu

Une politique interne versionnée sélectionne la concurrence à partir du matériel et de la longueur
de transformée, jamais à partir d'un nombre arbitraire de chiffres :

| Matériel validé | transformée | lot |
|---|---:|---:|
| RTX 5080 | ≤ 65 536 | B32 |
| RTX 5080 | ≤ 131 072 | B16 |
| RTX 5080 | ≤ 262 144 | B8 |

Un lot explicite valide gagne toujours. Matériel inconnu, transformée inconnue ou régime >262 144
revient à **B1**, choix conservateur mesuré comme sûr mais non présenté comme optimal. La capacité
maximale déclarée par le moteur borne toujours le résultat. Le CLI accepte `--batch-size auto`,
`--transform-length` et `--gpu-name`; le superviseur transmet ces valeurs. Le défaut historique B32
reste inchangé tant que l'opérateur n'active pas `auto`, afin de ne pas modifier silencieusement une
campagne figée.

Le plan est lui aussi explicite dans la décision : `ENGINE_AUTOTUNE`. Le prototype de plan figé ayant
échoué sa porte, aucun mapping de plan non démontré n'est livré. Le mode mesuré utilise la politique
`primeforge.native-plan.proth20-autotune.v1`; le fallback utilise une identité séparée
`primeforge.native-plan.safe-autotune.v1` pour rendre l'absence de mesure visible.

OpenCL 3.0 du pilote NVIDIA 610.74 n'expose pas `cl_khr_command_buffer`. La piste command-buffer
OpenCL est donc indisponible sur ce système, sans extrapolation à d'autres pilotes.

## 5. Avant/après du moteur complet

Le binaire Proth20 de production n'a pas changé, car aucune variante GPU n'a franchi la porte. Les
mesures de référence restent donc les mesures finales du chemin mathématique :

| Taille | transformée | lot retenu | avant | après retenu | statut |
|---:|---:|---:|---:|---:|---|
| 300k chiffres | 131 072 | B16 | 347,395976 c/h | 347,395976 c/h | même binaire, aucune variante retenue |
| 500k chiffres | 262 144 | B8 | 98,123619 c/h | 98,123619 c/h | même binaire, gate 300k non franchi |

Il ne serait pas honnête d'annoncer un gain GPU. Le gain livré est celui du volume de campagne : le
coût de persistance ne croît plus quadratiquement et le lot adapté peut être choisi sans modifier le
moteur mathématique.

## 6. Projection mise à jour

Ces valeurs sont **EXTRAPOLATED**, pas mesurées. Aucune exécution 750k/1M n'a été lancée.

| Taille | transformée probable | lot de sécurité | projection calcul GPU | persistance |
|---:|---:|---:|---:|---|
| 750k chiffres | 524 288 probable | B1 tant que non mesuré | 49–65 c/h | linéaire, checkpoint compact |
| 1M chiffres | 524 288 probable | B1 tant que non mesuré | 36–49 c/h | linéaire, checkpoint compact |

La projection GPU reste volontairement une plage prudente issue de la mesure 500k et de la prochaine
falaise de transformée. La politique ne prétend pas connaître le lot optimal à 524 288. Une mesure
fermée de quelques candidats devra précéder toute campagne à cette taille.

## 7. Limites et prochaine étape recommandée

- L'append durable effectue un flush par lot ; c'est linéaire, mais une campagne réelle devra mesurer
  le coût du stockage utilisé.
- Le journal complet et la queue sont encore relus une fois au démarrage ; ce coût est O(N), pas O(N²).
- Les artefacts stdout/stderr/pending par tentative ne sont pas compactés.
- Occupation SM, bande passante et stalls restent inconnus avec l'observabilité OpenCL actuelle.
- Aucun backend CUDA/graph n'a été prototypé dans ce jalon.

Prochaine étape recommandée : un prototype **CUDA Graph ou CUDA natif strictement borné à 300k** sur
le même corpus, visant à amortir les ~millions de lancements sans modifier l'arithmétique. Il ne doit
être poursuivi à 500k que si le gain end-to-end A/B puis B/A est reproductible et que résultats,
RES64 et Gerbicz restent identiques. Une autre option plus risquée est une fusion branchless des
passes réduction/poly2int ; la fusion branchée actuelle est définitivement rejetée.

## 8. Contribution directe au logiciel final

Ce jalon retire un mur qui aurait rendu les campagnes de centaines de milliers de survivants
impraticables, sans affaiblir la reprise ni la preuve d'absence de trou/doublon. Le dispatch encode
les régimes réellement mesurés de la RTX 5080 et garde un fallback sûr. La partie persistance est
terminée pour son objectif asymptotique ; il faudra seulement reprofiler son coût sur une grande
campagne réelle. La partie GPU n'est pas terminée : aucune micro-optimisation testée n'a mérité d'être
livrée, et le prochain gain devra être structurel.

## 9. Preuves

- `benchmarks/evidence/large-scaling-optimization-20260822/persistence-abba.tsv`
- `benchmarks/evidence/large-scaling-optimization-20260822/persistence-summary.tsv`
- `benchmarks/evidence/large-scaling-optimization-20260822/gpu-experiments.tsv`
- `benchmarks/evidence/large-scaling-optimization-20260822/dispatch-policy.tsv`
- `benchmarks/evidence/large-scaling-optimization-20260822/validation.tsv`

Validation locale finale : build Debug PASS, CTest Debug **43/43** en 53,62 s, build Release PASS,
CTest Release **43/43** en 20,79 s, self-tests Debug et Release PASS en C++23 avec MSVC
19.51.36252. La CI Windows/Linux est exigée avant fusion et sera enregistrée par la pull request.
