# PrimeForge state-of-the-art map

Status date: 2026-08-02. Every factual capability statement below is tied to a pinned audit. “Supports” means documented or found in source; it does not mean locally benchmarked.

## Prime enumeration and small-prime production

primesieve 12.15 is the selected 64-bit segmented-sieve reference. The source implements segmented Eratosthenes, wheel-210 presieving/sieving, cache-sized segments, bucket structures for large sieving primes, and multithreaded interval partitioning. The local MSVC Release build passed all 34 supplied tests. This establishes functional reproduction only, not a comparative performance claim.

## General multiprecision and independent oracles

GMP 6.3.0 is a candidate linked arithmetic backend under its per-component dual license. FLINT 3.6.0 is a candidate linked mathematical oracle, and PARI/GP 2.17.4 is reserved as an isolated executable adapter for rigorous primality and certificate workflows. Their source and licenses were audited, but installation is deferred until the corpus/oracle stage makes it necessary.

## Proth, Riesel, and general structured forms

PRST 14.0 is the most capable modern candidate found for Proth, Riesel, Fermat PRP, LLR, and Proth/Pocklington-style proof workflows. Its repository has no project-wide license declaration, so it cannot be integrated or redistributed and remains REFERENCE_ONLY. LLR2 is explicitly deprecated by its author in favor of PRST and also contains components with separate terms. proth20 is a smaller MIT experimental reference.

Historical sieves NewPGen, srsieve, sr2sieve, and PSieve-CUDA remain useful for file formats, algorithms, and test vectors. They are not selected as production dependencies. srsieve documents baby-step/giant-step sieving for k*b^n+c, checkpoints, factor checking, and NewPGen/ABC output. Its last archived source is old and expects GCC/MinGW-era build paths.

## Mersenne

Prime95/gwnum is the external reference for highly optimized FFT-based Mersenne and supported structured-form work. Its official EULA and bundled third-party components require isolation and an individual review; it is ADAPTER-only. gpuowl PRPLL and Mlucas provide open independent paths. Mlucas is the preferred independent CPU verifier candidate under WSL; no WSL distribution is installed on the audited host. GPU tools were source-audited but not run because the CUDA toolkit is absent.

Prime95 documentation distinguishes Fermat PRP from Lucas-Lehmer primality and describes proof files as verification that a PRP computation was performed correctly. PrimeForge continues to classify the mathematical result as PROBABLE_PRIME unless a valid primality proof is independently established.

## Generalized Fermat

Genefer22 is the preferred external CPU/OpenCL adapter candidate for b^(2^n)+1. Its source documents CPU and OpenCL execution, deterministic and probable-prime modes, checkpoints/error checks, and Gerbicz-Li/Pietrzak proof support. The pinned source is MIT. OpenCL platforms are present locally, but Genefer is deferred until an adapter and corpus are available.

## Factoring before expensive tests

GMP-ECM 7.0.7 provides P-1, P+1, and ECM factoring and is not a primality test. mfaktc and mfakto are specialized Mersenne trial-factor engines. None is automatically inserted into a pipeline: later use requires a reproducible cost-benefit model, exact work provenance, and an explicit decision about any external assignment.

## CUDA big integers

CGBN is a research reference for cooperative fixed-size big integers using groups of 4, 8, 16, or 32 CUDA threads over sizes from 32 through 32,768 bits. Its own repository labels it beta. It is not a solution for giant FFT/NTT arithmetic and remains conditional on the later CUDA gate.

## Algorithm selection

The algorithm-selection literature establishes a framework for per-instance selection and portfolio methods, but it does not prove PrimeForge hypothesis H3. PrimeForge must train and evaluate on disjoint workloads, compare to the virtual and single best baselines, record selection overhead, and avoid leakage. H3 remains HYPOTHESIS.

## Conclusions that are not claims

- No audited tool is called “best.”
- No source-level capability is treated as reproduced unless its tests ran locally.
- No performance statement is accepted without the stage 5 benchmark protocol.
- No PRP is promoted to PROVEN_PRIME.
- No public distributed work or assignment was requested.
- Missing power data remains UNKNOWN.
