# Fast-prime benchmark summary

Date : `2026-08-13`

Décision : `2 000 000 000` pour la borne du crible, `2` workers Proth20 sur la
RTX 5080, plage recommandée `fp-20000-c0-a210bbba7489`.

## Données autoritatives

`raw.jsonl` contient 42 enregistrements JSON indépendants :

| Schéma | Nombre |
|---|---:|
| crible multi-bandes et profondeurs | 26 |
| concurrence GPU | 7 |
| sondes Proth20 à taille réelle | 6 |
| arrêt/reprise | 1 |
| temps PARI/GP indépendant | 1 |
| mémoire du crible retenu | 1 |

```text
raw.jsonl SHA-256=a14080bfc4c09dd85c4c4f9dc0eafd66bd79fe2970981a8165a1ec75868f2a2a
```

Chaque enveloppe conserve le chemin de l'artefact local source. Les données
brutes sous `out/` restent ignorées par Git ; ce JSONL compact contient les
métriques nécessaires au contrôle du rapport.

La reconstruction propre finale a passé `41/41` tests Debug en `54,00 s`,
`41/41` tests Release en `20,20 s` et les deux self-tests C++23.

## Résultats retenus

| Mesure | Résultat |
|---|---:|
| crible 2 milliards, binaire Release final | 3 563 survivants en 13,625 s |
| pic mémoire du même crible | 1 697 804 288 octets |
| deux workers, passage A | 527,598 candidats/h |
| deux workers, passage B | 520,638 candidats/h |
| médiane retenue | 524,118 candidats/h |
| trois workers | 500,279 candidats/h, rejeté |
| PARI/GP indépendant | 5/5 accords, 52,139 s sur la porte finale |
| arrêt/reprise | arrêt 2/5, reprise 5/5, zéro trou/doublon |

## Températures et erreurs

Pendant le passage de concurrence le plus chargé : CPU `70,5 °C` au maximum,
GPU `62 °C`, puissance GPU `91,44 W`, RAM disponible au minimum
`47 656 476 672` octets et VRAM libre au minimum `13 061 MiB`. Aucun
throttling ni événement WHEA récent n'a été enregistré. La température mémoire
GPU reste `UNKNOWN`, car le pilote ne l'expose pas dans ces captures.

## Règles d'interprétation

- la chauffe est exclue des comparaisons ;
- les variantes classifient les mêmes candidats ;
- deux passages sont conservés pour un et deux workers ;
- trois workers étant plus lents que deux, quatre n'est pas testé ;
- quatre milliards de profondeur est conservé comme expérience rejetée, pas
  comme configuration recommandée ;
- les probabilités sont heuristiques et aucun résultat de campagne n'est
  contenu dans ce benchmark.
