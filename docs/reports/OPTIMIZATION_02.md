# Optimisation 02 — vague FLINT parallèle bornée

Date : 2026-08-03

Baseline précédente : `c140b36a8645ab97e38ffee602d0391888160aaf`

Code optimisé mesuré : `bbc5bebfe568049c520e1a61bbb7ac438b252cd3`

## Hypothèse

Après la mise en cache des hashes, 34 processus FLINT séquentiels occupaient
encore environ 643 à 647 ms. La RTX n'était pas le goulet de ce profil : le coût
était surtout la répétition des démarrages Windows. La variante testée soumet les
classifications indépendantes dans une vague bornée à huit processus, valeur
conservatrice pour les 16 cœurs/32 threads du Ryzen 9 9950X3D.

Toutes les classifications de la vague sont jointes avant la preuve et avant
toute écriture durable. Les résultats sont ensuite consommés dans l'ordre exact
des candidats. Le parallélisme ne change donc ni le JSONL, ni les certificats,
ni les checkpoints.

## Variante rejetée

La première implémentation laissait les lancements de processus se chevaucher
avec les écritures durables. Deux essais consécutifs ont échoué avec
`cannot append search result`. Ces mesures sont invalides et la variante a été
supprimée. `NR-0055` conserve le diagnostic et la condition de nouvel essai.

## Protocole

- Même profil `FULL_U64_KNOWN_160`.
- Trois échauffements, sept répétitions retenues par backend.
- Comparaison directe avec l'Optimisation 01.
- Résultats et reprise exigés byte-identiques.

## Avant et après

| Backend | Médiane avant | Médiane après | Différence absolue | Amélioration |
|---|---:|---:|---:|---:|
| CPU | 897 725 200 ns | 452 597 800 ns | -445 127 400 ns | 49,584 % |
| CUDA | 887 138 100 ns | 478 568 700 ns | -408 569 400 ns | 46,055 % |
| auto → CPU | 889 527 900 ns | 446 626 200 ns | -442 901 700 ns | 49,791 % |

Les MAD après optimisation sont respectivement 5 987 500 ns, 27 416 400 ns et
4 593 200 ns. La dispersion CUDA est plus élevée, mais même son échantillon le
plus conservateur reste très séparé de la baseline précédente. Le résultat
logique reste
`f2286a3ee8de22ad750eb8d6dc1d845c457658fc23340327854ea6f98a683fd6`.

Le gain cumulé par rapport au Commit A est de 92,821 % sur CPU, 92,383 % sur
CUDA et 92,922 % en routage automatique.

La vérification FLINT reste le premier poste à environ 207–210 ms. Les autres
postes importants sont désormais les checkpoints (~93–104 ms), les I/O de
résultats (~75–78 ms) et la preuve (~67–70 ms). Une vraie invocation FLINT par
lot reste une piste, mais elle exige un protocole d'oracle versionné et ne doit
pas être confondue avec cette vague de processus indépendants.

## Validation

- Debug et Release : 37/37 tests.
- Le test MVP force le chemin parallèle, les résultats déterministes, l'arrêt,
  la reprise et la détection d'un désaccord.
- CUDA Release : 41/41 tests.
- Compute Sanitizer : quatre validations, zéro erreur.
- Arrêt au candidat 37, reprise et vérification du manifeste : `PASS`.
- Reprise contre exécution ininterrompue : SHA-256 identique
  `4f2d2d24cc94632177de9f0c73917ab8f49a981414c4bf40b4410c12f00ad221`.

Les preuves brutes sont conservées sous
`benchmarks/baselines/optimization-02/`.

## Contribution directe au logiciel final

Cette optimisation réduit directement la latence de la pipeline de recherche
et exploite les cœurs du 9950X3D sans toucher aux décisions mathématiques. Elle
est retenue avec une limite de huit processus. Il faudra mesurer d'autres
densités de survivants avant de rendre cette limite adaptative.
