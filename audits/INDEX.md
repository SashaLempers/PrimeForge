# Stage 2 source-audit index

Audit date: 2026-08-02.

This inventory uses only pinned revisions or identified official archives for components that could be selected. A missing build, unclear license, obsolete toolchain, or unavailable CUDA/WSL environment is stated explicitly. No author-reported performance number is promoted to a PrimeForge benchmark.

| Project | Domain | Evidence | Local execution | Decision |
|---|---|---:|---:|---|
| primesieve 12.15 | segmented prime sieve below 2^64 | REPRODUCED | 34/34 tests PASS | LINK |
| GMP 6.3.0 | multiprecision arithmetic | SOURCE_AUDITED | NOT_RUN | LINK candidate |
| FLINT 3.6.0 | number theory and primality oracle | SOURCE_AUDITED | NOT_RUN | LINK candidate |
| Prime95/gwnum 30.19 | Mersenne and structured PRP/proofs | SOURCE_AUDITED | official archive inspected, computation NOT_RUN | ADAPTER |
| gpuowl PRPLL 0.15 | GPU Mersenne PRP/LL | SOURCE_AUDITED | NOT_RUN, CUDA toolkit absent | ADAPTER |
| Frey-PRPLL | unofficial CUDA PRPLL fork | SOURCE_AUDITED | NOT_RUN | REFERENCE_ONLY |
| Mlucas | independent Mersenne/Fermat verification | SOURCE_AUDITED | NOT_RUN, WSL absent | ADAPTER |
| OpenPFGW mirror | structured-form PRP | SOURCE_AUDITED | NOT_RUN | ADAPTER |
| Genefer22 | generalized Fermat tests/proofs | SOURCE_AUDITED | NOT_RUN | ADAPTER |
| PARI/GP 2.17.4 | rigorous primality and certificates | SOURCE_AUDITED | source archive inspected, NOT_RUN | ADAPTER |
| GMP-ECM 7.0.7 | P-1, P+1 and ECM factoring | SOURCE_AUDITED | NOT_RUN | ADAPTER |
| CGBN | fixed-size CUDA big integers | SOURCE_AUDITED | NOT_RUN | REFERENCE_ONLY |
| PRST 14.0 | Proth/Riesel/general special forms | SOURCE_AUDITED | NOT_RUN | REFERENCE_ONLY |
| LLR2 1.3.3 | legacy LLR/Proth/Riesel | SOURCE_AUDITED | NOT_RUN | REJECTED |
| proth20 | experimental Proth tests | SOURCE_AUDITED | NOT_RUN | REFERENCE_ONLY |
| srsieve 0.6.17 | k*b^n+c sieve | SOURCE_AUDITED | NOT_RUN, historical MinGW build | REFERENCE_ONLY |
| NewPGen | historical Proth/Riesel sieve | DOC_VERIFIED | NOT_RUN | REFERENCE_ONLY |
| sr2sieve | historical Proth/Riesel sieve | DOC_VERIFIED | NOT_RUN | REFERENCE_ONLY |
| mfaktc 0.24.1 | CUDA Mersenne trial factoring | SOURCE_AUDITED | NOT_RUN | ADAPTER |
| mfakto 0.16.0-beta.5 | OpenCL Mersenne trial factoring | SOURCE_AUDITED | NOT_RUN | ADAPTER |
| PSieve-CUDA | historical CUDA Proth sieve | SOURCE_AUDITED | NOT_RUN | REJECTED |
| PrimeGrid formats/service | work formats and coverage | DOC_VERIFIED | no assignment requested | REFERENCE_ONLY |

The missing-tool review for Proth/Riesel is in [PROTH_RIESEL_TOOL_GAPS.md](PROTH_RIESEL_TOOL_GAPS.md). Research papers relevant to proof systems and adaptive engine selection are listed in [RESEARCH_LITERATURE.md](RESEARCH_LITERATURE.md).
