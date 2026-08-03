# PIVOT-09 — Honest Proth reference comparison

**Status:** COMPLETE — INCONCLUSIVE PERFORMANCE  
**Run:** `20260803T005712Z`  
**Compared commit:** `714dac5bd09b13ea586fd815e0f494a20a8639fa`

## Implementation and proof scope

PrimeForge now contains a bounded native `uint64_t` Proth prover. It validates
the Proth form, searches deterministic witnesses and emits a canonical JSON
certificate only after verifying the exact Proth congruence. Exhausting the
witness bound returns `UNTESTED`; it never promotes a candidate to composite.

The complete 160-candidate MVP domain contains 34 known primes and 126
composites. The native path proved all 34 primes, emitted no certificate for any
composite, and reproduced the shared proth20 fixture `43*2^32+1` with witness 3.
The fixture certificate SHA-256 is
`7172a2acdbae79bc90671dacafa01761d5bc632f2650ad4a4674d213efccfd22`.

## Preregistered comparison

The comparison used 16 fixed `n=32` candidates, one fresh process per candidate,
one warm-up and seven deterministic randomized repetitions. Both engines saw the
same decimal candidate inputs and produced the same final prime/composite
classification level. Process startup is deliberately included; checkpointing
is not applicable to a single-candidate invocation.

| Engine | Repetitions | Cases/repetition | Exact agreements | Median total | MAD |
|---|---:|---:|---:|---:|---:|
| PrimeForge | 7 | 16 | 112/112 | 16,171,632,400 ns | 16,473,000 ns |
| pinned proth20 | 7 | 16 | 112/112 | 16,166,107,400 ns | 14,650,600 ns |

The aggregate result is 224/224 exact agreements and zero mismatches. No
performance winner is selected: the common domain is tiny, fresh-process and
OpenCL startup dominate, and CPU temperature/package-power telemetry remains
`UNKNOWN`. Every raw and aggregate timing is therefore marked
`performance_valid=NO` and `performance_claim=NONE`.

## Safety and retained evidence

Fourteen before/after telemetry records observed a maximum GPU temperature of
54 °C, maximum GPU power of 62.27 W, maximum GPU utilization of 7%, minimum
available RAM of 43,317,919,744 bytes and minimum free VRAM of 13,574 MiB. No GPU
throttling was observed. CPU temperature, CPU package power, GPU memory
temperature and energy remain `UNKNOWN`.

The local run retains 224 raw rows, 14 telemetry rows and a 462-entry manifest.
All manifest paths were present and all hashes verified. Compact tracked copies
of metadata, aggregates and the summary live under
`benchmarks/pivot09/results/20260803T005712Z/`; `evidence.tsv` binds them to the
larger ignored local artifacts.

The outer command display timed out after about 184 seconds while its owned
PowerShell worker continued normally. The run was not relaunched. The same
worker finished, wrote the complete manifest and exited; no owned benchmark
process remained afterward.

## Verification gates

- Debug: 34/34 CTest tests passed in 48.20 s.
- Release: 34/34 CTest tests passed in 14.12 s.
- CUDA: 37/37 CTest tests passed in 15.74 s.
- Compute Sanitizer: three runs, zero reported errors.
- Reference protocol validation: 16 cases and both pinned executable hashes
  verified before execution.
- Reference result: 224/224 exact classifications, zero mismatch, zero missing or
  corrupt manifest entries.

## Contribution directe au logiciel final

Ce jalon livre le premier prouveur spécialisé du moteur final et vérifie que ses
certificats concordent exactement avec une implémentation Proth indépendante.
Cette base est indispensable au futur pipeline CPU/GPU : les étages rapides
peuvent maintenant remettre un candidat à une frontière de preuve stricte et
reproductible. La comparaison de performance est définitivement close pour ce
petit protocole comme `INCONCLUSIVE`; il faudra la rouvrir seulement sur une
charge représentative, avec télémétrie CPU complète. Le prochain travail utile
est d'intégrer ce prouveur au pipeline de campagne et à la vérification
indépendante, pas d'ajouter une nouvelle couche de benchmark.
