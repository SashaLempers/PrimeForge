# Prolonged campaign readiness

**Status:** BLOCKED SAFELY — CAMPAIGN NOT STARTED  
**Date:** 2026-08-03

PIVOT-00 through PIVOT-11 have passed their scoped correctness gates. The next
roadmap milestone is a prolonged campaign, but two mandatory conditions prevent
launch:

1. the owner has not yet given the separate explicit authorization required for
   a real campaign of 24 hours or more;
2. CPU temperature and package power remain `UNKNOWN`, so the required 92 °C
   stop rule cannot be enforced.

The host exposes no usable `MSAcpi_ThermalZoneTemperature` value and no supported
hardware monitor is already running. An official LibreHardwareMonitor v0.9.6
probe was downloaded from GitHub, verified against archive SHA-256
`086D9F1B5A99E643EDC2CFAAAC16051685B551E4C5AC0B32A57C58C0E529C001`,
and loaded only from ignored local storage. It enumerated the Ryzen package
temperature and power sensors but returned zero values, which PrimeForge rejects
as invalid. The probe is not integrated or redistributed.

The next technical attempt would require privileged hardware access and possibly
a driver. PrimeForge did not elevate, install a driver, change BIOS/voltage/power
limits/fan curves, or start any campaign. GPU telemetry remains available, but it
cannot substitute for CPU thermal safety.

## Contribution directe au logiciel final

Ce contrôle protège directement le moteur contre une campagne dont la règle
d'arrêt thermique CPU serait impossible à appliquer. Il ne rajoute aucun système
générique : il clôt une seule vérification bloquante et conserve `UNKNOWN` au lieu
d'inventer une mesure. Le moteur borné, ses preuves et sa reprise sont prêts ; la
campagne prolongée attend l'autorisation du propriétaire et une source CPU
fiable.
