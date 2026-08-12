# PrimeForge — redécouverte connue `34745 × 2^33221 + 1`

Date de clôture de l'audit : 2026-08-12 UTC

Commit audité : `547ee8566eac612756ceff9e50fea4c56c225d96`

État des processus au début de l'audit : `NO_ACTIVE_DISCOVERY_PROCESS`

## Verdict

```text
primality_status=PROVEN_PRIME
verification_status=SELF_VERIFIED
discovery_classification=INDEPENDENT_REDISCOVERY_CANDIDATE
coverage_status=KNOWN_SEARCH_REGION
novelty_status=NOT_NOVEL
submission_status=DO_NOT_SUBMIT_AS_NEW
```

PrimeForge a bien reproduit une preuve de primalité par le théorème de Proth, mais ce résultat n'est pas une découverte. La table publique EST Proth, datée du 21 juin 2026 lors du contrôle, couvre exhaustivement `k = 30000..70000` jusqu'à `n = 400000`. Le couple `(34745, 33221)` est strictement inclus dans cette couverture.

## Preuve primaire relue dans les sorties brutes

Le fichier `logs/unit-0013-attempt-001.stdout.log` contient exactement :

```text
Testing 34745 * 2^33221 + 1, 10006 digits, size = 2^12 x 21 bits, plan: 256_8 sq_16 p2i_4_32
34745 * 2^33221 + 1 is prime, a = 3, time = 00:00:03
PRIMEFORGE_BATCH_COMPLETE	51	34745	33221	PROVEN_PRIME
PRIMEFORGE_BATCH_PRIME_FOUND
```

Le témoin de Proth enregistré est `a = 3`. Aucun résidu numérique n'est émis pour ce résultat premier dans les sorties conservées : `residue=UNKNOWN`. Aucun résidu ne doit être inventé à partir des sorties composites voisines.

`results.tsv` confirme :

```text
34745	33221	PROVEN_PRIME	unit-0013-attempt-001.stdout.log
```

`presults.txt` confirme indépendamment la même ligne de verdict du moteur :

```text
34745 * 2^33221 + 1 is prime, a = 3, time = 00:00:03
```

## Progression et identité immuable

Le checkpoint arrêté au premier résultat prouvé indique :

```text
campaign_status=PRIME_FOUND
total_candidates=4008
completed_candidates=1251
next_k=34755
proven_prime_k=34745
updated_utc=2026-08-05T12:07:24Z
```

La progression est donc exactement `1251/4008`. L'identité de campagne est `sha256:396b53212dcdfe98636f2c12db35b7487c8a000f86ac55738554f41cdc554c4b`. Le binaire Proth20 enregistré dans `campaign.json` a le SHA-256 `283dcb968da9de4671138cb31cee8cf2557b3abdcb5c55643a7608f281036a02`.

## Empreintes des artefacts décisifs

| Artefact | SHA-256 |
|---|---|
| `campaign.json` | `03d99962636f44e646fff5677ff3b39f552d06d826968826ae731ba77f459a15` |
| `checkpoint.json` | `b23f6b819cf19a34ba209e6fc634ca824d257ab8cad077cdd625d2e7f67834f8` |
| `results.tsv` | `b13190c1a670f90be2c61c70726a3907486e3609ea287836fb872dd95cdf7025` |
| `survivors.txt` | `f4e60c6cbd66030d2034976a8b8aa663899e1dd61ebb995cd053c58abf13c5e5` |
| `logs/unit-0013-attempt-001.stdout.log` | `b2f15093c1380e1c9190406aedac633b39bd144933c416b11c3329d565a423bb` |
| `presults.txt` | `ccb61f535fa5cda74d26674daaf0d16c7937daca3b72825792f943ecf0fb2d8f` |

## Conservation

Les 85 fichiers d'origine (13 886 952 octets) restent intacts dans `out/discovery/proth-n33221-k10001-90001/`. Une copie vérifiée fichier par fichier par SHA-256 se trouve dans :

```text
out/discovery/known-rediscoveries/proth-n33221-k10001-90001-NOT_NOVEL/
```

Cette copie est un artefact local ignoré par Git. Elle ne constitue pas une revendication publique.

## Contribution directe au logiciel final

Cette clôture valide en conditions réelles la détection d'un premier, l'arrêt au premier résultat, l'écriture du checkpoint et la reprise. Elle établit surtout que l'audit de couverture doit précéder toute campagne coûteuse. Le résultat mathématique est clos définitivement comme redécouverte connue; il ne faut revenir à ces artefacts qu'en cas d'audit de reproductibilité.
