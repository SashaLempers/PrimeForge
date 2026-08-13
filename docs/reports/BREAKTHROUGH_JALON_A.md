# Feuille de route de rupture — jalon A

**Statut :** `PASS`

**Date de mesure :** 2026-08-13

**Révision de départ PrimeForge :** `4b8ba257044015a2f44d2d5a8ace1b11f967f7bd`

**Révision Proth20 épinglée :** `6771325939a7ceef2c75644c79981c7df4a61882`

**SHA-256 de la directive :** `8233222851779bdf9fd18622b8e18ea90bac19286589ccb869f1070bbc91674e`

## Arrêt préalable de la campagne

La campagne `n=66411`, `k=75939069..76077027` était déjà inactive lorsque
l'ordre d'arrêt a été reçu. Aucun processus contrôleur, Proth20 ou watchdog ne
restait actif et aucun doublon n'a été lancé. Le checkpoint global est conservé
à `1300/3563`, état `ERROR`, sans premier trouvé. Les deux workers avaient
chacun durablement terminé 650 candidats.

La cause terminale est un arrêt demandé par les watchdogs sur
`GPU_THROTTLING:SW_POWER_CAP`. Les dernières températures observées étaient
58–61 °C pour le GPU et 72–74 °C pour le CPU, sans WHEA. `SW_POWER_CAP` est un
état de limitation logicielle NVIDIA et non une surchauffe. La campagne n'a pas
été reprise : tout le travail du présent jalon a été réalisé dans le worktree
séparé `PrimeForge-breakthrough`.

## Instrumentation livrée

Le patch versionné `patches/proth20-phase-profile.patch`, appliqué après le
patch batch existant sur la révision Proth20 épinglée, ajoute deux modes
explicitement optionnels :

- `--phase-profile` : timestamps CPU monotones et compteurs structurés ;
- `--kernel-profile` : événements OpenCL synchronisés et nombre/durée de
  chaque type de kernel.

Sans ces options, le chemin de production et son protocole de sortie restent
inchangés. Les phases couvrent notamment la découverte OpenCL, la création du
contexte, la génération et compilation du programme, les kernels, buffers,
racines, tables dépendant de `k`, l'autotuning, le témoin, `a^k`, la boucle de
carrés, les contrôles Gerbicz, la normalisation, la lecture du résidu, le
checkpoint et le nettoyage. Les octets de source, tables et résultat transférés
sont également comptés.

Le mode kernel utilise un exécutable séparé compilé avec `quick_bench`. Il
s'arrête après un préfixe borné de 10 000 carrés et ne produit aucun verdict de
primalité utilisable. Il sert uniquement à mesurer les lancements OpenCL sans
transformer un essai court en campagne.

## Protocole reproductible

Le corpus contient les 20 premiers survivants complets de la campagne arrêtée,
tous à `n=66411`. Le protocole exécute exactement les mêmes candidats selon
l'ordre :

1. `A1` — un worker ;
2. `B1` — deux workers ;
3. `B2` — deux workers ;
4. `A2` — un worker.

Cela représente 80 exécutions de taille réelle. Trois candidats ont aussi été
exécutés dans trois processus directs distincts afin de comparer le lancement
direct et le batch persistant. Le moniteur matériel a fonctionné pendant chaque
variante. Une variante A1 démarrée à 55 °C et une variante B1 interrompue à
19/20 par une propriété `ExitCode` Windows indisponible ont été exclues et
archivées localement ; elles ne sont mélangées à aucune mesure retenue.

Commande principale :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_proth20_phase_profile.ps1 `
  -OutputDirectory out\benchmarks\proth20-phase-profile-20260813-r2
```

Le script refuse une RTX déjà chargée, attend un état thermique comparable,
écrit la télémétrie et peut réutiliser uniquement une variante entièrement
terminée avec `-Resume`.

## Exactitude

- 80/80 classifications identiques entre A1, B1, B2 et A2 ;
- 80/80 témoins et `RES64` identiques aux résultats archivés de la campagne ;
- aucun doublon et aucun candidat manquant ;
- aucun WHEA, throttling ou dépassement thermique pendant les mesures retenues ;
- stderr vide pour chaque worker retenu.

Le corpus attendu est versionné dans
`benchmarks/profiles/proth20_n66411_profile20_expected.tsv`. Un PRP n'est pas
présenté comme un premier prouvé et ce corpus ne contient aucun premier.

## Résultats de phase

La couverture top-level minimale est **99,996629 %** et la moyenne est
**99,998327 %**, au-dessus de la porte de 95 %. Les phases imbriquées ne sont
pas additionnées une deuxième fois.

| Mesure | Valeur moyenne | Part du temps candidat |
|---|---:|---:|
| Candidat complet | 11,293583 s | 100 % |
| Construction `gpmp` | 0,315272 s | 2,791607 % |
| Boucle principale de carrés | 10,885345 s | 96,385219 % |
| Autotuning, imbriqué dans `gpmp` | 0,283630 s | 2,511 % environ |
| Compilation du programme, par événement | 0,011039 s | imbriqué |

La comparaison directe chaude contre batch persistant donne seulement
**1,010426×** par candidat. Après remplissage des caches du pilote, la création
de processus et de contexte n'est donc pas le principal coût de cette charge.
Le tout premier essai à froid avait montré une compilation beaucoup plus longue
et reste un coût de démarrage, mais pas le goulot d'une campagne chaude.

| Variante | Débit effectif |
|---|---:|
| A1, 1 worker | 410,733 candidats/h |
| A2, 1 worker | 416,737 candidats/h |
| B1, 2 workers | 517,531 candidats/h |
| B2, 2 workers | 519,526 candidats/h |

La moyenne deux-workers dépasse la moyenne un-worker de **25,3287 %**, mais
reste très loin d'un doublement. Chaque candidat ralentit sous contention ; la
bonne cible est donc un partage natif du travail NTT, pas davantage de processus
indépendants.

Les températures de départ A1/B1/B2/A2 étaient 42/43/43/43 °C. Le maximum
retenu est 55 °C pour le GPU et 75,75 °C pour le CPU. La puissance GPU maximale
observée est 84,44 W. Ces valeurs sont des mesures de télémétrie, pas des valeurs
déduites du TDP.

## Événements OpenCL et Nsight

Le quick probe a enregistré 121 053 lancements appartenant à 23 types de
kernels sur 10 000 carrés. La somme des événements OpenCL vaut 47,518331 % du
temps mural profilé. Comme ce mode attend chaque événement, le résidu de
52,481669 % mélange attente hôte et surcoût d'instrumentation ; il ne doit pas
être présenté comme le taux d'inactivité du chemin de production.

Nsight Systems 2026.1.3 a produit un rapport borné. Sur cette session Windows :

- le jeu de rapports Nsight ne trace pas l'activité OpenCL de Proth20 ;
- l'échantillonnage CPU et les changements de contexte demandent des droits
  administrateur ;
- les compteurs GPU renvoient `ERR_NVGPUCTRPERM`.

Aucun réglage de droits, pilote, BIOS, tension, puissance ou ventilation n'a été
modifié. Les événements OpenCL et la télémétrie `nvidia-smi` restent les preuves
utilisées ; les compteurs indisponibles sont explicitement `UNAVAILABLE`.

## Décision

Le jalon A de 20 à 100 candidats est terminé et validé. La porte P0 plus large
de 100 survivants identiques reste `NOT_RUN` afin d'éviter un benchmark plus
long sans décision à prendre. Elle sera requise pour accepter une réécriture
lourde du moteur.

Le profil réfute l'idée que la reconstruction `gpmp` soit actuellement le
goulot dominant après échauffement des caches. La boucle NTT principale domine
à plus de 96 %. Le jalon B peut conserver les gains simples du crible et du
cache, mais aucune réécriture importante ne sera justifiée par le seul coût de
construction. La cible structurelle reste le batch NTT natif et le partage du
contexte GPU.

Les preuves compactes et leurs SHA-256 sont dans
`benchmarks/evidence/breakthrough-jalon-a/`. Les journaux bruts restent sous
`out/benchmarks/proth20-phase-profile-20260813-r2/` et leurs hashes sont
enregistrés dans `raw-artifacts.sha256`.

## Contribution directe au logiciel final

Ce jalon identifie quantitativement le vrai coût du moteur de preuve 20 000
chiffres : la boucle NTT, et non l'infrastructure de lancement. Il empêche donc
PrimeForge d'investir plusieurs jalons dans une optimisation secondaire et
fournit une porte d'exactitude pour les futures transformations GPU.

L'instrumentation du jalon est terminée pour le diagnostic courant. Elle devra
être réutilisée, sans l'activer en production, pour comparer les jalons C et D.
La porte de 100 survivants sera alors exécutée uniquement si une modification
structurelle franchit d'abord les essais courts.
