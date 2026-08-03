# Optimisation 06 — SHA-256 produit accéléré sous Windows

Date : 2026-08-03

Baseline précédente : `18822512d1431428b7cc405ddd45e864ee0bfaa8`

Code optimisé mesuré : `76019df1f24af9cd2c34e9d05d089e186f7d01a3`

## Diagnostic et modification

Après le lot FLINT, la vérification prenait encore environ 118 ms alors qu'une
invocation directe chaude de l'oracle prenait environ 5 à 6 ms. Le profil a
localisé l'écart dans la vérification SHA-256 des binaires et leur lecture.

`PlatformSha256Provider` utilise désormais l'API cryptographique native CNG
(`bcrypt`) sous Windows et garde le SHA-256 PrimeForge portable sur les autres
systèmes. L'interface injectable et les octets calculés ne changent pas. Les
fichiers d'installation sont lus en une opération binaire dimensionnée plutôt
qu'au travers d'un itérateur caractère par caractère. Les changements de taille
pendant la lecture échouent explicitement.

Il ne s'agit ni d'OpenSSL, ni d'une dépendance redistribuée : `bcrypt` est une
API du système Windows. Les vecteurs connus et un message multi-blocs exigent
l'identité exacte entre fournisseurs natif et portable.

## Avant et après

| Backend | Médiane avant | Médiane après | Différence absolue | Amélioration |
|---|---:|---:|---:|---:|
| CPU | 274 626 300 ns | 209 748 200 ns | -64 878 100 ns | 23,624 % |
| CUDA | 274 229 200 ns | 214 354 500 ns | -59 874 700 ns | 21,834 % |
| auto → CPU | 269 488 900 ns | 218 873 800 ns | -50 615 100 ns | 18,782 % |

Les MAD finales sont 3 227 900 ns, 1 762 500 ns et 6 492 300 ns. Le hash de
résultat reste :
`0a6300a9b67205bad62779ebab241119e5f4df5ac45d7dd516c81eb43f64d776`.

Le gain cumulé depuis Commit A atteint 96,670 % sur CPU, 96,581 % sur CUDA et
96,527 % en routage automatique. La médiane de vérification tombe à environ
51–54 ms ; le checkpoint (~72–75 ms) devient le premier coût non recouvert.

## Validation

- Vecteur SHA-256 `abc` connu et message de 4 096 octets : fournisseurs CNG et
  portable identiques.
- Debug et Release : 37/37 tests.
- CUDA Release : 41/41 tests.
- Compute Sanitizer : quatre validations, zéro erreur.
- Arrêt au candidat 37, reprise et vérification finale : `PASS`.
- Résultats repris et ininterrompus byte-identiques :
  `f8d8ef759c6b2bb0ce514b3101c02039f9706a52fb0d5db10c36b340b643ecab`.
- 42 mesures finales, six groupes, aucune divergence.

Les données brutes et leur manifeste sont conservés sous
`benchmarks/baselines/optimization-06/`.

## Contribution directe au logiciel final

Cette optimisation accélère le contrôle de provenance réellement exécuté avant
les preuves externes, sans supprimer ce contrôle ni modifier un seul hash. Elle
est terminée pour Windows et reste portable via le repli existant. Il faudra
mesurer séparément un fournisseur natif Linux seulement si ce système devient
une cible de campagne, pas pour la CI actuelle.
