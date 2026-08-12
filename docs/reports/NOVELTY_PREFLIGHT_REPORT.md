# PrimeForge — preflight de nouveauté avant campagne

Audit live : 2026-08-12 20:20:27Z à 20:21:31Z

Branche : `audit/novelty-preflight-2026-08-12`

Base auditée : `547ee8566eac612756ceff9e50fea4c56c225d96`

Processus de découverte actif au départ : `NO_ACTIVE_DISCOVERY_PROCESS`

## Verdict exécutable

```text
coverage_verdict=NO_DOCUMENTED_OVERLAP_FOUND_IN_CAPTURED_RESPONSES
preflight_status=PREFLIGHT_FAIL
launch_status=MASSIVE_RUN_BLOCKED
```

La collecte publique n'a révélé aucun chevauchement documenté pour la plage proposée. Cela ne suffit pas à ouvrir la campagne : la porte échoue de façon fermée pour des raisons indépendantes et objectives.

```text
FERMATSEARCH_DONE_AND_RUNNING_STALE_OVER_90_DAYS
PRIMEFORGE_SIEVE_REJECTS_K_ABOVE_99999999
PINNED_PROTH20_BATCH_ADAPTER_REJECTS_K_ABOVE_99999999
UPSTREAM_PROTH20_DOCUMENTS_K_BELOW_100000000_ONLY
```

Le statut `PUBLICLY_UNCOVERED_CANDIDATE`, `PREFLIGHT_PASS` ou `AWAITING_SASHA_GO` n'est donc pas attribué. Aucune campagne massive n'a été lancée.

## Cible auditée

```text
family=k*2^n+1
n=33326
k_min=1227250535
k_max=1227330535
k_step=2
candidate_count=40001
digits_min=10042
digits_max=10042
```

Les deux bornes sont impaires, tiennent dans un entier signé 32 bits et satisfont très largement `k < 2^n`. Les 16 valeurs intérieures sont déterminées par `index=floor(j*40000/17)`, `k=k_min+2*index`, pour `j=1..16`; elles sont enregistrées dans `target-and-queries.json`.

L'espérance heuristique recalculée est `3.46018171949567` premiers et le modèle de Poisson donne `0.968575948863230` pour au moins un premier. Ce sont uniquement des heuristiques.

La graine enregistrée est :

```text
PrimeForge|SashaLempers|2026-08-05|547ee8566eac612756ceff9e50fea4c56c225d96
sha256=b2343fc91164eee5843d57f26e6dd3e5434fb1213d9a599c76909fbb19ec2cf8
```

Le dépôt ne contient pas l'algorithme ayant transformé cette graine en plage. La provenance est donc classée honnêtement `EXTERNALLY_SELECTED`; aucune dérivation rétrospective n'est inventée.

## Collecte et reproductibilité

Le script `scripts/run_novelty_preflight.ps1` a capturé 77 réponses sur 77 avec un code HTTP réussi. Avec le manifeste, la définition de cible et les deux corps POST conservés, le dossier contient 81 fichiers et 12 365 462 octets.

```text
docs/reports/novelty_sources/2026-08-05-or-later/
source-manifest.sha256=13fe463fe42d12506f8803e645bacbba614c376b42dbc8293bbf28fc25e7b7ab
```

Pour chaque réponse, `source-manifest.json` enregistre l'URL, la requête exacte, l'heure UTC, le code HTTP, la date affichée ou `NOT_DISPLAYED`, la taille, le SHA-256, l'état de capture et la limite de la source. `scripts/analyze_novelty_preflight.ps1` refait les comparaisons numériques et génère `NOVELTY_PREFLIGHT_MACHINE.json`.

## Résultats par source

| Source | Contrôle live | Résultat pour la plage | Limite décisive |
|---|---|---|---|
| EST Proth | table complète, mise à jour affichée le 2026-06-21 | 0 intervalle ou `k` individuel couvrant la cible | n'observe pas les calculs privés |
| FermatSearch terminé | table complète, mise à jour affichée le 2026-03-31 | 0 chevauchement numérique | page vieille de 134 jours |
| FermatSearch réservé | 26 réservations affichées, mise à jour le 2026-03-31 | 0 chevauchement numérique | page vieille de 134 jours, donc réservation actuelle non garantie |
| PrimeGrid PPS | table officielle courante par `k` | 0 `k` cible | historique public non universel |
| PrimeGrid PPSE | table officielle courante par `k` | 0 `k` cible | historique public non universel |
| PrimePages/T5K | POST exact sur les deux bornes et `n=33326` | `0 primes` | base non exhaustive à cette taille |
| GitHub code | 4 requêtes REST authentifiées | 0 résultat | dépôts privés et gros fichiers non indexés |
| GitHub commits | 4 requêtes REST authentifiées | 0 résultat | historique supprimé/non indexé invisible |
| GitHub issues | 198 faux positifs sur le numéro `33326`, aucun `k` cible | 0 résultat pertinent | aucune recherche globale des contenus d'assets de releases |
| Zenodo | 4 appels officiels `GET /api/records` | 0 résultat exact ou contextuel | dépôts non publiés invisibles |
| Crossref | 4 appels, résultats généraux inspectés par objet | 0 résultat contenant la plage en contexte | index bibliographique, pas registre de calcul |
| DataCite | 4 appels | 0 résultat | même limite |
| OpenAlex | 4 appels | 0 résultat exact ou contextuel | un JSON possède des clés différant seulement par la casse, conservé brut |
| OEIS, arXiv, Figshare, GitLab, Archive.org | API publiques | 0 résultat exact ou contextuel | indexations incomplètes |
| Web exact | 36 requêtes incluant bornes, 16 valeurs intérieures, formes et séparateurs; 361 blocs inspectés | 0 bloc contenant une valeur cible dans un contexte Proth/`33326` | indexation et classement non exhaustifs |

Le contrôle positif historique fonctionne : l'analyse numérique retrouve bien dans EST Proth la ligne `k=30000..70000`, `max n=400000`, qui couvre `34745*2^33221+1`. Cela confirme que le parseur n'obtient pas un zéro systématique.

## Audit des chemins numériques

| Chemin | Verdict | Détail |
|---|---|---|
| JSON PowerShell | `PASS` | aller-retour exact des deux bornes |
| TSV | `PASS` | aller-retour exact |
| checkpoint | `PASS` | stockage `[uint32]`, valeur exacte |
| CLI PrimeForge | `PASS` | parseur `uint32_t`, valeur exacte |
| crible PrimeForge | `FAIL_LIMIT_99999999` | rejet explicite de `k_stop > 99'999'999` |
| adaptateur batch Proth20 | `FAIL_LIMIT_99999999` | rejet explicite de `batchK > 99999999` |
| domaine officiel Proth20 | `FAIL` | l'auteur documente seulement `3 <= k < 100,000,000` |
| CUDA de découverte | `NOT_IMPLEMENTED` | la campagne multiprécision actuelle utilise Proth20/OpenCL, pas le backend CUDA 64 bits |

Il n'y a pas de troncature silencieuse : la valeur est transportable, puis correctement refusée. Retirer la garde ne constituerait pas une validation scientifique. Le code interne de Proth20 emploie des `uint32_t`, mais cela ne prouve pas la correction de son arithmétique au-delà du domaine publié.

Un ancien exécutable Release local a d'abord accepté la plage, en contradiction avec le source. Son objet généré était plus récent que le fichier versionné et Ninja l'avait considéré à tort à jour après un changement de contenu antérieur. Après recompilation forcée de `proth_sieve.cpp` dans l'environnement développeur MSVC, l'exécutable reconstruit a rendu le code `1`, affiché `Proth discovery k bound exceeds the pinned engine limit` et n'a créé aucun fichier de sortie. Le résultat du binaire périmé est rejeté et consigné dans `NEGATIVE_RESULTS.md`.

## Durée et ressources

La campagne connue a traité 1 251 survivants en environ 7 412,050 secondes entre le premier et le dernier journal, soit `5,9249 s/candidat` sur ce chemin. Une extrapolation purement indicative à 40 001 candidats donne environ `237 002 s`, `65,83 h` ou `2,74 jours` pour parcourir toute la plage.

Ce chiffre n'est pas un benchmark de la nouvelle cible : celle-ci est hors domaine du moteur et n'a pas été exécutée. Une campagne de cette durée franchirait de toute façon l'arrêt obligatoire des 24 heures.

État matériel ponctuel du 2026-08-12T20:21:13Z, sans charge de benchmark :

```text
disk_C_free=3329.64 GiB
cpu_temperature=65.25 C
cpu_power=115.371 W
gpu_temperature=57 C
gpu_power=55.85 W
gpu_vram_free=13199 MiB
gpu_throttling=NONE
whea_errors_last_120_seconds=0
gpu_memory_temperature=UNKNOWN
```

## Vérification locale du jalon

```text
source_capture=77/77 PASS
source_hash_manifest=77/77 PASS
machine_json_parse=35/35 PASS
analyzer_exit_code=2 EXPECTED_PREFLIGHT_FAIL
msvc_debug_build=PASS
msvc_release_build=PASS
ctest_debug=41/41 PASS (55.62 s)
ctest_release=41/41 PASS (20.55 s)
rebuilt_target_range_guard=PASS (exit 1, no output created)
long_benchmark=NOT_RUN
massive_campaign=NOT_RUN
```

Les compilations n'ont produit aucune erreur ni avertissement PrimeForge. Les lignes `Remarque : inclusion du fichier` du compilateur Debug sont la trace `/showIncludes`, pas des avertissements.

## Décision conservatrice

1. Ne pas lancer cette cible avec le chemin actuel.
2. Ne pas supprimer les gardes Proth20 sans validation différentielle indépendante.
3. Pour rester sur le moteur éprouvé, dériver une plage reproductible entièrement sous `k < 100000000`, puis refaire tout le preflight.
4. Obtenir une confirmation réellement actuelle des réservations FermatSearch; contacter un tiers nécessitera l'autorisation explicite de Sasha.
5. Seulement après `PREFLIGHT_PASS`, demander le GO pour la validation courte de trois candidats, puis pour toute campagne dépassant 24 heures.

## Jalon

- **Objectif :** empêcher une seconde redécouverte ou une campagne inexécutable.
- **Avant :** plage publiquement plausible mais non revalidée et hors domaine du moteur épinglé.
- **Modification :** archivage de l'ancien premier, collecte live reproductible, analyse numérique et contrôle de tous les chemins de valeurs.
- **Après :** zéro chevauchement trouvé, mais lancement bloqué par fraîcheur et compatibilité moteur.
- **Gain/validation :** une campagne d'environ 2,74 jours potentiellement invalide a été évitée avant consommation significative.
- **Commit :** commit de jalon portant ce rapport et les preuves; son hash est donné dans le compte rendu de session.
- **Étape suivante :** cible compatible et nouvel audit; aucune campagne automatique.

## Contribution directe au logiciel final

Ce jalon protège directement le moteur de découverte : il refuse une plage que le backend de preuve ne sait pas officiellement traiter et démontre que la chaîne d'audit détecte bien une région connue. Les scripts sont terminés pour cette version de la porte, mais il faudra relancer la collecte pour toute nouvelle cible et remplacer la source FermatSearch périmée par une confirmation actuelle avant un lancement réel.
