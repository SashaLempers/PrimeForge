# Proth/Riesel tool-gap review

The mandatory search covered current and historical categories rather than assuming the initial specification list was exhaustive.

| Need | Tools found | Current conclusion |
|---|---|---|
| Fast k*2^n±1 candidate sieving | NewPGen, sr2sieve, srsieve, PSieve-CUDA | Historical formats/algorithms are valuable, but no audited modern dependency has both a clear current maintenance path and a completed license/build gate |
| Current PRP and specialized proof | PRST 14.0, LLR2, proth20 | PRST has the broadest documented modern capability, but its absent project-wide license blocks integration; LLR2 is deprecated; proth20 is an experimental reference |
| General structured-form fallback | OpenPFGW, Prime95/gwnum | External adapters only due composite/custom terms |
| Rigorous general proof | PARI/GP 2.17.4, FLINT 3.6.0 | Selected as later independent adapter/oracle candidates |
| Factor prefilter | GMP-ECM 7.0.7 | Conditional external adapter; never treated as primality testing |
| Work/coverage formats | NewPGen, ABC/PFGW, PrimeGrid | Parsers must be independently implemented and fuzzed; external coverage must be checked at the time of real search |

Remaining gaps are explicit:

1. A maintained, clearly licensed, locally reproducible Proth/Riesel sieve has not yet been selected.
2. PRST cannot be distributed or linked until a project-wide license grant is found.
3. Historical NewPGen/sr2sieve provenance and exact source hashes remain UNKNOWN.
4. No PrimeGrid/GIMPS assignment or coverage claim has been requested.
5. No GPU candidate has passed the later CUDA stability/correctness gate.

These gaps do not block Stage 2 because none of the affected programs is selected as an integrated component. They must be resolved before production use.
