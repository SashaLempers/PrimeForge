# PrimeForge — preflight de nouveauté de la plage recommandée

Audit live : `2026-08-13T11:03:22Z` à `2026-08-13T11:04:25Z`

Branche : `work/fast-prime-feasibility-20260813`

Base de sélection : `c7a1a3812ec03a0f3e7be08549816c847ffee3ba`

## Verdict exécutable

```text
coverage_verdict=PUBLICLY_UNCOVERED_CANDIDATE
preflight_status=PREFLIGHT_PASS
launch_status=AWAITING_SASHA_GO
campaign_started=false
```

Aucun enregistrement exact antérieur ni chevauchement public documenté n'a été
trouvé dans les sources consultées à la date de l'audit. Cette phrase ne porte
que sur les réponses publiques capturées. Elle n'exclut pas un calcul privé,
hors ligne, supprimé, non publié ou non indexé, et ne constitue pas une
affirmation de nouveauté mondiale.

La campagne longue n'a pas été lancée.

## Cible reproductible

```text
range_id=fp-20000-c0-a210bbba7489
family=k*2^n+1
n=66411
k_min=75939069
k_max=76077027
k_step=2
candidate_count=68980
digits_min=20000
digits_max=20000
generator_version=primeforge.fast-prime-range.v1
counter=0
generation_hash=a210bbba7489ddc9221f3f6266ebc6c96947a80e089be6bb736f96f12fffafd3
seed_sha256=844e6a95798a57fa757cd45aaa3755245ba256bc4e29645f9d65a84d4f9fa305
```

Graine exacte :

```text
PrimeForge|SashaLempers|FAST_PUBLICLY_UNCOVERED_PRIME|2026-08-13|c7a1a3812ec03a0f3e7be08549816c847ffee3ba|primeforge.fast-prime-range.v1|digits=20000|counter=0|k-domain=10000001..99999999|candidate-count=68980
```

Une seconde génération à paramètres identiques a reproduit exactement les 27
plages du portefeuille. Le compteur n'a pas été déplacé après observation des
résultats.

## Capture publique

Le dossier
`docs/reports/novelty_sources/2026-08-13-fast-prime-fp-20000-c0-a210bbba7489-r2/`
contient 80 réponses et requêtes. Le manifeste enregistre pour chacune l'URL,
la requête, l'heure UTC, le code HTTP, la taille, le SHA-256, le résultat de
capture et ses limites.

```text
source_count=80
critical_fetch_failures=0
manifest_integrity_failures=0
possible_overlaps_or_matches=0
source_manifest_sha256=6fe60c447711d8e17a099758695de8dde16af1ee5064d21952a6eb69cdd891ad
```

Contrôles structurés critiques :

| Source | État capturé | Résultat numérique | Limite principale |
|---|---:|---:|---|
| EST Proth, mise à jour affichée `2026-06-21` | `PASS` | aucun intervalle chevauchant | agrégat public, pas les calculs privés |
| FermatSearch `done` | `PASS` | aucun chevauchement | date affichée `2026-03-31`, donc ancienne |
| FermatSearch `running` | `PASS` | aucune réservation chevauchante | même limite de date affichée |
| FermatSearch requête live et table fusionnée | `PASS` | cible confirmée, aucun chevauchement | même base opérateur ; corroboration, pas indépendance totale |
| PrimeGrid PPS et PPSE | `PASS` | aucun chevauchement numérique | tables publiques, pas tout travail privé ou historique |
| PrimePages/T5K, requête Proth exacte | `PASS` | zéro résultat exact | base non exhaustive à cette taille |
| Proth20 README épinglé | `PASS` | `k` et `n` dans le domaine documenté | preuve de compatibilité, pas de couverture |

Le niveau B inclut également ProthSearch, MersenneForum, Riesel Prime Wiki,
OEIS, GitHub code/commits/issues, Zenodo, Crossref, DataCite, OpenAlex, arXiv,
OSF, Figshare, GitLab, SourceForge, Internet Archive et les recherches web
exactes des deux bornes, de 16 valeurs intérieures déterministes, des couples
`(k,n)`, des formes `k*2^n+1` et de plusieurs écritures d'intervalle. Le
manifeste constitue la liste exhaustive et vérifiable des requêtes.

## Chemins numériques et moteur

| Porte | Résultat |
|---|---:|
| calcul exact du nombre de chiffres | `PASS` |
| JSON, TSV et checkpoint `uint32` | `PASS` |
| CLI et bornes sans troncature | `PASS` |
| crible PrimeForge, `k<100000000` | `PASS` |
| adaptateur batch Proth20 | `PASS` |
| domaine upstream Proth20 : `3 <= k < 100000000`, `32 <= n < 100000000` | `PASS` |
| forme de Proth `k < 2^n` | `PASS` |

Le README upstream épinglé provient de la révision
`6771325939a7ceef2c75644c79981c7df4a61882`. Son inclusion dans le manifeste
supprime toute dépendance du verdict à un checkout ignoré localement.

## Validation courte à taille réelle

Cinq survivants de 20 000 chiffres ont été classés par Proth20 0.9.1 sur la
RTX 5080, puis recalculés par PARI/GP 2.17.4 au moyen d'un chemin indépendant
(symbole de Kronecker et exponentiation modulaire) : accord `5/5`.

| `k` | `n` | Proth20 | témoin `a` | `RES64` | PARI/GP |
|---:|---:|---|---:|---|---|
| 75939141 | 66411 | `COMPOSITE` | 7 | `6D1E409A23F04802` | `COMPOSITE` |
| 75939159 | 66411 | `COMPOSITE` | 5 | `CA636F1CB086AC03` | `COMPOSITE` |
| 75939161 | 66411 | `COMPOSITE` | 3 | `808B4577F02D1E54` | `COMPOSITE` |
| 75939195 | 66411 | `COMPOSITE` | 13 | `7E180BD073E83C61` | `COMPOSITE` |
| 75939329 | 66411 | `COMPOSITE` | 3 | `337945987757D9A2` | `COMPOSITE` |

La vérification PARI de la porte finale prend `52.139 s` pour cinq candidats,
soit `10.428 s/candidat`. Cette mesure ne transforme aucun PRP en premier prouvé :
un futur verdict `PROVEN_PRIME` devra être reproduit puis vérifié de la même
manière avant tout examen de nouveauté.

## Arrêt, reprise et intégrité

Le contrôleur a volontairement interrompu une unité après `2/5` résultats. Le
watchdog a enregistré `EXTERNAL_GRACEFUL_STOP`, le checkpoint est resté
`IN_PROGRESS`, puis le même manifeste a repris uniquement les trois candidats
restants.

```text
final_completed=5/5
gaps=0
duplicate_result_rows=0
manifest_unchanged=PASS
checkpoint_durable=PASS
independent_agreement=5/5
```

## Stabilité mesurée

Sur les essais courts retenus, les maxima agrégés sont `70.5 °C` CPU,
`62 °C` GPU, `91.44 W` GPU et `80 %` d'utilisation GPU. Le minimum disponible
est `47,656,476,672` octets de RAM et `13,061 MiB` de VRAM libre. Aucun
throttling, aucune erreur WHEA récente et aucune erreur Proth20/OpenCL n'ont été
observés. Il s'agit d'une validation courte, pas d'une preuve de stabilité sur
plusieurs jours.

## Portes finales

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

Un `GO` devra encore être suivi d'un rechargement des sources critiques et des
réservations juste avant lancement. Toute nouvelle occurrence ou couverture
publique fera échouer le preflight de façon fermée.
