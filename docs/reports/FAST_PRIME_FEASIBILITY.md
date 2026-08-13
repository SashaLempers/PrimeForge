# PrimeForge — faisabilité d'une découverte rapide et reproductible

Date de décision : `2026-08-13`

État final du jalon : `AWAITING_SASHA_GO`

## Recommandation unique

```text
RECOMMENDED_RANGE=fp-20000-c0-a210bbba7489
n=66411
k_min=75939069
k_max=76077027
k_step=2
initial_candidates=68980
digits=20000
sieve_bound=2000000000
measured_survivors=3563
gpu_workers=2
campaign_started=false
```

Cette plage maximise le temps attendu jusqu'à une découverte vérifiable parmi
les options arrivées en finale. Elle est reproductible, dans le domaine
documenté de Proth20, auditée au niveau B, mesurée à taille réelle et estimée à
moins de neuf heures même avec la marge prudente retenue. Aucune campagne
longue n'a été lancée.

## État initial préservé

- `main` et `origin/main` étaient à
  `c7a1a3812ec03a0f3e7be08549816c847ffee3ba`, avec un arbre propre.
- Aucun processus PrimeForge, Proth20 ou watchdog de découverte n'était actif.
- Le dépôt GitHub `SashaLempers/PrimeForge` était privé.
- Le dossier antérieur de `21952207*2^33326+1` n'a pas été modifié : les trois
  manifestes SHA-256 ont vérifié `404/404` fichiers, zéro divergence.
- La vérification locale finale propre a passé `41/41` tests Debug et `41/41`
  tests Release, ainsi que les deux self-tests.

## Portefeuille reproductible

Le générateur `primeforge.fast-prime-range.v1` utilise le commit de base, la
date, la bande décimale, un compteur, le domaine de `k` et la taille de plage
dans une graine SHA-256. Une reproduction indépendante a donné les mêmes 27
plages, octet pour octet pour le tableau `ranges`.

Bandes étudiées : `20 000`, `25 000`, `50 000`, `75 000`, `100 000`,
`250 000`, `500 000`, `750 000` et `1 000 000` chiffres. La bande de 20 000
chiffres est la plus petite ajoutée : elle reste plus grande que la découverte
préservée de 10 040 chiffres tout en respectant largement l'objectif de durée.

Les 27 plages ont passé le filtre rapide numérique EST Proth, FermatSearch
`done/running/merged` et PrimeGrid PPS/PPSE : aucun chevauchement évident dans
les instantanés consultés. Ce niveau A n'est pas un verdict de nouveauté ; seul
le rang 1 a ensuite reçu le preflight complet de 80 sources.

Le nombre de candidats est fixé avant calcul par :

```text
lambda = 2*C/ln(k_mid*2^n)
P(au moins un premier) = 1-exp(-lambda)
lambda cible = -ln(0.05) = 2.995732273553990
```

Il s'agit d'une heuristique de densité pour les `k` impairs, pas d'une garantie.

## Élimination adaptative des bandes lentes

Un survivant réel par bande a suffi à montrer la croissance du coût jusqu'à
250 000 chiffres. Le tableau utilise le même crible à dix millions pour la
comparaison et ne transfère pas ces temps vers une prétendue mesure p90.

| Chiffres | Survivants à 10 M | Temps Proth20 observé | Travail séquentiel indicatif |
|---:|---:|---:|---:|
| 20 000 | 4 710 | 8 s | 10,47 h |
| 25 000 | 5 937 | 10 s | 16,49 h |
| 50 000 | 11 901 | 21 s | 69,42 h |
| 75 000 | 18 185 | 32 s | 161,64 h |
| 100 000 | 24 043 | 42 s | 280,50 h |
| 250 000 | 60 056 | 108 s | 1 801,68 h |

Les bandes de 500 000, 750 000 et 1 000 000 chiffres ont été éliminées avant
un test Proth20 coûteux : la progression mesurée jusqu'à 250 000 chiffres les
rend déjà incompatibles avec la fenêtre préférée de 24–72 heures. Aucun temps
de campagne n'est annoncé pour ces bandes non mesurées.

## Profondeur de crible

Sur la plage recommandée, les profondeurs ont été testées géométriquement. La
projection utilise le débit médian mesuré de deux workers, `524,118/h`.

| Borne | Temps du crible | Survivants | Temps complet projeté |
|---:|---:|---:|---:|
| 1 000 000 | 0,032 s | 5 529 | 10,549 h |
| 10 000 000 | 0,100 s | 4 710 | 8,987 h |
| 100 000 000 | 0,671 s | 4 117 | 7,855 h |
| 1 000 000 000 | 6,619 s | 3 675 | 7,014 h |
| **2 000 000 000** | **13,625 s** | **3 563** | **6,802 h** |
| 4 000 000 000, rejeté | 27,133 s | 3 455 | 6,600 h |

Le passage de un à deux milliards réduit le travail complet d'environ `3,02 %`
et est retenu. Le passage de deux à quatre milliards tombe à environ `2,99 %`
net : il manque le seuil de conservation de 3 % et reste un résultat négatif.
Le pic mémoire mesuré du crible retenu est `1 697 804 288` octets avec le
binaire Release final.

## Concurrence GPU

Deux passages distincts, après chauffe exclue, ont comparé exactement les mêmes
classifications :

| Passage | 1 worker | 2 workers | 3 workers |
|---|---:|---:|---:|
| A | 336,808/h | 527,598/h | non testé |
| B | 414,398/h | 520,638/h | 500,279/h |

Deux workers gagnent lors des deux passages. Leur médiane est `524,118/h`.
Trois workers régressent de `3,91 %` face à deux dans le passage B ; le
protocole adaptatif s'arrête donc sans tester quatre workers. Les cinq verdicts
de validation et les verdicts répétés entre variantes sont identiques.

Le CPU n'a pas reçu un moteur de primalité concurrent : le crible complet ne
prend que 13,5 secondes, de sorte qu'un nouveau chemin CPU aurait ajouté du
risque sans pouvoir raccourcir significativement cette campagne. Il reste
disponible pour le contrôle, la journalisation, les checkpoints et la
vérification.

## Trois meilleures options

| Rang | `n` | `k_min..k_max` | Survivants à 2 G | Audit | Débit retenu | Durée complète | Borne prudente | P heuristique |
|---:|---:|---|---:|---|---:|---:|---:|---:|
| **1** | **66411** | **75939069..76077027** | **3 563** | **niveau B, `PREFLIGHT_PASS`** | **524,118/h** | **6,802 h** | **8,216 h** | **95,0005 %** |
| 2 | 66412 | 33402579..33540537 | 3 569 | niveau A seulement | 524,118/h transféré | 6,813 h | 8,229 h | 95,0005 % |
| 3 | 66411 | 99718115..99856073 | 3 604 | niveau A seulement | 524,118/h transféré | 6,880 h | 8,311 h | 95,0004 % |

Pour le rang 1, le modèle uniforme de Poisson donne :

```text
temps médian estimé jusqu'au premier = 1.574 h
temps correspondant à 90 % de probabilité cumulée = 5.228 h
temps moyen d'arrêt tronqué par la fin de plage = 2.157 h
durée si toute la plage doit être terminée = 6.802 h
borne prudente de fin = 8.216 h
```

La borne prudente n'est pas un p90 statistique : l'échantillon de débit est
trop petit. Elle applique une marge de 20 % au plus lent des deux débits
retenus, puis ajoute le crible mesuré.

Les rangs 2 et 3 sont des solutions de repli déterministes. Ils ne pourront pas
être lancés sans leur propre preflight niveau B frais.

## Exactitude, reprise et stabilité

- `5/5` survivants : accord Proth20/PARI-GP 2.17.4 ;
- arrêt volontaire après `2/5`, puis reprise exacte jusqu'à `5/5` ;
- zéro trou, zéro ligne résultat dupliquée, manifeste inchangé ;
- CPU maximal `70,5 °C`, GPU maximal `62 °C`, GPU maximal `91,44 W` ;
- RAM disponible minimale `47 656 476 672` octets ;
- VRAM libre minimale `13 061 MiB` ;
- zéro throttling, zéro erreur WHEA récente, zéro erreur OpenCL/Proth20.

Ces valeurs décrivent uniquement les validations courtes. Elles ne garantissent
pas une stabilité de plusieurs jours.

## Verdict

```text
PUBLICLY_UNCOVERED_CANDIDATE
PREFLIGHT_PASS
ENGINE_SUPPORT_PASS
SIEVE_VALIDATION_PASS
FULL_SIZE_SAMPLE_PASS
INDEPENDENT_AGREEMENT_PASS
STOP_RESUME_PASS
NO_GAPS_PASS
NO_DUPLICATES_PASS
HARDWARE_STABILITY_PASS_ON_SHORT_VALIDATION
DURATION_ESTIMATE_READY
AWAITING_SASHA_GO
```

## Contribution directe au logiciel final

Ce jalon fournit au moteur une sélection de plage impossible à déplacer après
coup, une profondeur de crible choisie sur le temps complet, le nombre optimal
de workers GPU mesuré, un checkpoint réellement repris et un vérificateur
indépendant. Il est terminé pour la décision actuelle. Il faudra seulement
recharger les sources critiques après un éventuel `GO`, puis lancer la campagne
figée avec le watchdog existant.
