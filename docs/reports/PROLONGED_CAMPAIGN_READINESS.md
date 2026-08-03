# Prolonged campaign readiness

**Status:** SHORT VALIDATION PASSED — 24-HOUR AUTHORIZATION PENDING

**Date:** 2026-08-03

PIVOT-00 through PIVOT-11 have passed their scoped correctness gates. The former
CPU-sensor blocker is closed on the target: the already-installed L-Connect 3
service provides fresh CPU package temperature, package power and clock through
a local loopback response. PrimeForge accepts those fields only with strict
freshness and range validation and otherwise returns `UNKNOWN`.

Three consecutive Release samples reported CPU temperatures from 63.0 to
64.125 °C and CPU package power from 98.671146947560786 to
116.89694076029312 W. GPU temperature was 50–51 °C, GPU board power was
45.10–45.94 W, RAM availability stayed above 42.9 GB, VRAM availability stayed
above 13.0 GiB and NVIDIA reported no throttling reason. These are short
readiness observations, not benchmark or energy claims.

The final Debug and Release builds each passed 34/34 tests in 50.65 s and
17.31 s. The optional CUDA build passed 37/37 tests in 17.98 s; all three CUDA
Compute Sanitizer runs reported zero errors. The runtime fault
suite covers valid, missing, stale and invalid local data, the required-sensor
loss paths and the 92 °C stop decision. The canonical hardware-profile gate also
passes both detected and deliberately disabled L-Connect paths. The post-CUDA
sample reported CPU 62 °C, GPU 51 °C, no NVIDIA throttling and zero WHEA events
in the preceding ten minutes.

PrimeForge does not load, link, copy or redistribute L-Connect/HWiNFO. The local
provider is restricted to this private target-machine workflow; public product
redistribution would require a separate component review. No driver was installed,
no privilege was elevated and no BIOS, voltage, power limit or fan curve changed.

The watchdog now also stops on WHEA presence, loss of the WHEA query, configurable
minimum available RAM/VRAM and nonzero worker exit; Windows retains the process
handle so the exit code is logged after termination. A propagated CUDA failure
therefore invalidates the campaign; the real-process gate retained exact exit
code 7 and `WORKER_EXIT_NONZERO`. The provisional conservative memory floors
are 8 GiB RAM and 2 GiB VRAM; they are safety floors, not performance settings.

One mandatory condition still prevents PIVOT-12 launch: the owner has not yet
given the separate explicit authorization required immediately before a real
campaign of 24 hours or more.

## Contribution directe au logiciel final

Ce contrôle permet au moteur d'appliquer réellement l'arrêt thermique à 92 °C,
de journaliser la puissance CPU mesurée et de s'arrêter si la source disparaît.
Il ferme le blocage matériel sans ajouter de moteur externe ni détourner le
projet vers l'infrastructure. La télémétrie cible est terminée pour les essais
courts ; il faudra seulement la revalider si L-Connect est mis à jour. La campagne
prolongée reste en attente de l'autorisation explicite du propriétaire. Il faudra
revalider cette source si L-Connect est mis à jour.
