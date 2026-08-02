# Research-literature audit

Primary or publisher-hosted sources reviewed on 2026-08-02:

| Topic | Primary source | Verified use in PrimeForge |
|---|---|---|
| Proth theorem | François Proth, 1878, Comptes rendus de l’Académie des sciences, original scan | Specialized proof premise for k*2^n+1; implementation still requires independently checked preconditions |
| APR primality | Adleman, Pomerance, Rumely, Annals of Mathematics 117 (1983), DOI 10.2307/2006975 | Background for rigorous general primality and APRCL implementations |
| ECPP | Atkin and Morain, Mathematics of Computation 61 (1993), DOI 10.1090/S0025-5718-1993-1199989-X | Background for PARI/GP certificate route |
| Algorithm selection | Rice, Advances in Computers 15 (1976), DOI 10.1016/S0065-2458(08)60520-3 | Defines the feature/performance selection problem; does not validate H3 |
| Portfolio selection | Xu, Hutter, Hoos, Leyton-Brown, JAIR 32 (2008), DOI 10.1613/JAIR.2490 | Experimental precedent for per-instance portfolios; PrimeForge still needs held-out validation |
| Automatically configured selector | Lindauer et al., IJCAI 2017, DOI 10.24963/ijcai.2017/715 | Reference design only; no dependency selected |
| Proofs of exponentiation | Pietrzak, IACR ePrint 2018/627 | Basis for recursive proof concepts used by current generalized-Fermat tooling |
| Gerbicz-Li proof construction | Li and Gallot, arXiv:2209.15623 | Primary algorithm paper linked by Genefer; implementation is audited separately |

These papers are SOURCE_AUDITED at the bibliographic/algorithm level. No paper’s experimental claim is reproduced here, and none establishes a PrimeForge performance or novelty claim.
