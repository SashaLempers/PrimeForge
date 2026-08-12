# PrimeForge — preflight de nouveauté avant campagne

Audit live : `2026-08-12T20:59:35Z` à `2026-08-12T21:00:47Z`

Branche : `engine/discovery-unblock-2026-08-12`

Base de sélection : `279a7c76e1bcd1b0f96c60214f534b84c1b00282`

Processus de découverte actif au départ : `NO_ACTIVE_DISCOVERY_PROCESS`

## Verdict exécutable

```text
coverage_verdict=PUBLICLY_UNCOVERED_CANDIDATE
preflight_status=PREFLIGHT_PASS
launch_status=AWAITING_SASHA_GO
```

Aucun chevauchement public n'a été trouvé dans les 79 réponses capturées. Les
79 tailles et SHA-256 enregistrés concordent avec les fichiers bruts. Tous les
chemins numériques utilisés par cette campagne acceptent la plage sans
troncature.

Ce verdict signifie seulement qu'aucune couverture ni occurrence publique n'a
été trouvée dans les sources consultées à la date de l'audit. Il ne prouve pas
l'absence d'un calcul privé, non publié, supprimé ou non indexé. Les termes
`UNEXPLORED`, `NOVEL` et `GUARANTEED_NEW` ne sont pas autorisés.

La campagne complète n'a pas été lancée. La porte explicite de l'opérateur
reste `AWAITING_SASHA_GO`.

## Cible finale reproductible

```text
family=k*2^n+1
n=33326
k_min=21909339
k_max=21989339
k_step=2
candidate_count=40001
digits_min=10040
digits_max=10040
```

La cible originale autour de `k=1 227 250 535` a été rejetée avant calcul : le
crible PrimeForge et le fork épinglé de Proth20 limitent leur domaine documenté
à `k<100 000 000`. La nouvelle plage reste strictement dans ce domaine et
au-dessus de la couverture FermatSearch générale observée jusqu'à dix millions
pour `n=33326`.

Sélection déterministe :

```text
seed=PrimeForge|SashaLempers|2026-08-12|n=33326|attempt=1|k-domain=10000001..99999999|candidate-count=40001|base=279a7c76e1bcd1b0f96c60214f534b84c1b00282
sha256(seed)=5470b859f79ce46d065efa27c2b3d1fca7b0481d3895645b9c7b43eefc4bdd18
algorithm=u64_be(sha256(seed)[0..7]) modulo odd_start_count
sha256_prefix_u64_be=6084565793123394669
odd_start_count=44960000
selected_start_index=5954669
```

`scripts/select_supported_discovery_target.ps1` reproduit exactement les deux
bornes. Les 16 valeurs intérieures sont définies par
`index=floor(j*(candidate_count-1)/17)`, puis `k=k_min+2*index`, pour
`j=1..16`.

L'espérance heuristique est `3.460784018130980` premiers et le modèle de
Poisson donne `0.968594869827738` pour au moins un premier. Ce sont des
heuristiques, jamais une garantie.

## Sources et intégrité

Le dossier immuable de cette tentative est :

```text
docs/reports/novelty_sources/2026-08-12-supported-attempt-01/
files=84
bytes=15410204
captured_sources=79/79
source_hash_matches=79/79
source_manifest_sha256=92577c33787cacd6210c7b56ae5db880f563f4e53f414d3d03284ad6b576abfb
target_sha256=17aab76ce44116a76f7d8ccab325a39ebfafb2451ff9661534d2f711711347d8
```

Pour chaque réponse, `source-manifest.json` enregistre l'URL, la requête,
l'heure UTC, le code HTTP, la date affichée ou `NOT_DISPLAYED`, la taille, le
SHA-256, le statut de capture et la limite de la source.

| Source | Contrôle | Résultat sur la cible | Limite principale |
|---|---|---|---|
| EST Proth | table exhaustive affichée au `2026-06-21` | zéro recouvrement | calculs privés invisibles |
| FermatSearch done/running | tables affichées au `2026-03-31` | zéro recouvrement | date affichée vieille de 134 jours |
| FermatSearch live range | POST exact `nMin=33326&nMax=33326`, exposant réaffiché par le serveur | zéro recouvrement | même base opérateur que les tables datées |
| FermatSearch merged | table fusionnée capturée pendant l'audit | zéro recouvrement | même limite de provenance |
| PrimeGrid PPS/PPSE | tables officielles par `k` | zéro `k` cible | historique public non universel |
| PrimePages/T5K | POST exact des bornes et de l'exposant | zéro premier | base non exhaustive à cette taille |
| GitHub | code, commits et issues par quatre requêtes API | zéro résultat contextuel | dépôts privés, gros fichiers et assets non indexés |
| Zenodo, Crossref, DataCite, OpenAlex | quatre requêtes par API | zéro résultat contextuel | index bibliographiques, pas registres universels |
| OEIS, arXiv, Figshare, GitLab, Archive.org | API publiques | zéro résultat contextuel | indexations incomplètes |
| Web exact | 36 requêtes, 345 blocs inspectés | zéro résultat contextuel | indexation et classement non exhaustifs |

Windows PowerShell 5.1 refuse certains JSON OpenAlex dont des clés ne diffèrent
que par la casse. L'analyseur utilise alors un parseur de dictionnaires sensible
à la casse ; les quatre réponses OpenAlex ont été analysées et ne sont pas
classées ambiguës.

La date affichée par FermatSearch reste trop ancienne. La requête POST dynamique
et la table fusionnée confirment que la base répond actuellement et ne retourne
aucun chevauchement pour l'exposant exact. Elles satisfont la règle « autre
confirmation », mais elles appartiennent au même opérateur : cette dépendance
est une limitation explicite, pas une preuve d'absence universelle.

Le contrôle positif historique reste valide : le parseur retrouve la ligne EST
Proth `k=30000..70000`, `max_n=400000`, couvrant le premier déjà connu
`34745*2^33221+1`.

## Chemins numériques

| Chemin | Verdict |
|---|---|
| JSON PowerShell | `PASS` |
| TSV | `PASS` |
| checkpoint `uint32` | `PASS` |
| CLI `uint32` | `PASS` |
| crible PrimeForge | `PASS` |
| adaptateur batch Proth20 | `PASS` |
| domaine documenté Proth20 | `PASS` |
| backend CUDA natif multiprécision | `NOT_IMPLEMENTED`; la campagne utilise Proth20/OpenCL |

## Validation courte de la cible

Le crible exact a produit :

```text
initial_candidates=40001
sieve_bound=65521
eliminated=35989
survivors=4012
survivors_sha256=a6e874f3f2e6c86f2e66a1f4652459037a55db64d027d49b51198f651b214213
```

Les trois premiers survivants ont été comparés avec deux implantations :

| `k` | `n` | Proth20 | témoin | `RES64` | PARI/GP 2.17.4 |
|---:|---:|---|---:|---|---|
| 21909343 | 33326 | `COMPOSITE` | 3 | `32CC4F655E0AAAC4` | `COMPOSITE` |
| 21909357 | 33326 | `COMPOSITE` | 7 | `32160018866CF82C` | `COMPOSITE` |
| 21909367 | 33326 | `COMPOSITE` | 3 | `1B9891A4DDAEDD1E` | `COMPOSITE` |

Accord indépendant : `3/3`.

Le premier essai d'interruption a révélé que les marqueurs Proth20 étaient
tamponnés lorsque stdout était redirigé. Cet essai est rejeté dans
`NEGATIVE_RESULTS.md`. Le patch épinglé force désormais un flush après chaque
`PRIMEFORGE_BATCH_COMPLETE`; le binaire reconstruit a le SHA-256 :

```text
0c97e0e9f61c1e1aedb48ed7b7f5fa1f31343a9cb0cb167f4f4eb0e747dc2dfd
```

Lors du nouvel essai, l'arrêt a été demandé après observation du premier
marqueur. Le watchdog a arrêté Proth20 à la frontière suivante : checkpoint
intermédiaire `2/3`, `next_k=21909367`. La reprise a testé uniquement
`21909367`, puis terminé `3/3`. Le TSV final contient trois clés uniques, aucun
trou et aucun doublon.

Hashes principaux de la validation locale :

```text
campaign.json=f00e835f23b26bb9edb5fc0475d45150bd3aed3655eba5a38307935ab4071015
checkpoint.json=b6344846fca5b5980868e4374e27865eb635bbb9f9cf0ae742ebb05df8f8ed18
results.tsv=d1863abff863dc21f761c89d02fc23c84d3991bbf89cd1bbc7cde9c6123134f2
attempt-001.stdout.log=a459f291ae4e83a5c8a2e1a6f59fa12a2b6a238195a5ed4895f0333f3a8b4315
attempt-002.stdout.log=eca5efc93ec7fb3c65c277ac0412dc88e5b3d10952d11790b54f9a6034e37847
```

Les artefacts complets de validation sont conservés localement sous
`out/discovery/validation-n33326-supported-resume/`.

## Mesures courtes et projection

Sur les 13 instantanés du test arrêt/reprise :

```text
cpu_temperature_max=68.875 C
gpu_temperature_max=47 C
gpu_power_max=83.68 W
ram_available_min=40706715648 bytes
vram_free_min=12488 MiB
throttling_events=0
whea_errors_max=0
gpu_memory_temperature=UNKNOWN
```

Les deux processus observés ont duré `10.371 s` pour deux candidats, puis
`4.700 s` pour le candidat repris. La moyenne descriptive est
`5.0237 s/survivant`, contexte et reprise compris. Une extrapolation linéaire
prudente aux 4 012 survivants donne environ `5.60 h`; l'attente heuristique du
premier, si le modèle s'applique, est environ `1.62 h`. Ce test de trois valeurs
n'est pas un benchmark publiable et ne garantit ni la durée ni la présence d'un
premier.

## Porte opérateur

État prêt à présenter :

```text
PUBLICLY_UNCOVERED_CANDIDATE
PREFLIGHT_PASS
SHORT_VALIDATION_PASS
AWAITING_SASHA_GO
```

Risques restants : calcul antérieur privé ou non indexé, source FermatSearch
affichant une date ancienne malgré sa réponse dynamique, estimation de durée
fondée sur trois candidats, capteur de température mémoire GPU indisponible et
absence de backend CUDA natif pour ces nombres multiprécision.

Validation du jalon :

```text
msvc_debug_build=PASS
ctest_debug=41/41 PASS (54.61 s)
debug_selftest=PASS (MSVC 19.51.36252, C++23)
msvc_release_build=PASS
ctest_release=41/41 PASS (19.76 s)
release_selftest=PASS (MSVC 19.51.36252, C++23)
preflight_capture=79/79 PASS
preflight_manifest_hashes=79/79 PASS
selector_reproduction=PASS
tampered_evidence_closed_gate=PREFLIGHT_FAIL PASS
proth20_pari_agreement=3/3 PASS
interruption_resume_no_gap_no_duplicate=PASS
long_benchmark=NOT_RUN
massive_campaign=NOT_RUN
```

Les lignes MSVC `Remarque : inclusion du fichier` sont la trace `/showIncludes`
utilisée par Ninja, pas des avertissements du code PrimeForge.

## Contribution directe au logiciel final

Ce jalon débloque une vraie cible de découverte que le moteur sait traiter : la
plage est reproductible, compatible avec le crible et Proth20, contrôlée avant
calcul, et sa chaîne arrêt/reprise est testée sur des nombres de taille réelle.
Le préflight et le protocole batch sont stabilisés pour cette tentative. Il
faudra relancer l'audit exact si un premier apparaît, et refaire le preflight
pour toute nouvelle plage. La prochaine action utile est la campagne réelle,
pas une nouvelle couche d'infrastructure.
