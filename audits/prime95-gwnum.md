# Prime95 / gwnum 30.19

- Name: Prime95 and gwnum
- Primary URL: https://www.mersenne.org/download/
- Revision: Windows binary 30.19 build 20; source archive 30.19 build 21
- Source/archive SHA-256: source BDC843A547A6F91DC67004A3EFBCD99858AF7DB075ECD77B7188B23E5AC2CE2A; Windows binary D9475F2FF3F4A6A701ABC49A86A66126CB48ABD10BDA6FA87039D98FA8756BCA
- License at revision: GIMPS End User License Agreement plus individually licensed bundled components
- License-file SHA-256: 9D8F6CE09E546117B64FADA383346ADFD0F0C40B3F0E4F8D5C3ED2B2A67DF6D8
- Evidence level: SOURCE_AUDITED
- Mathematical domain: Mersenne LL/PRP, structured-form PRP, P-1/P+1/ECM, proof certification
- Main algorithms: FFT-based modular arithmetic, Fermat PRP, Lucas-Lehmer, P-1/P+1, ECM
- Supported sizes/forms: Mersenne-centered plus documented k*b^n+c PRP work format within gwnum limits
- CPU/GPU: CPU
- Vectorization/assembly/FFT/NTT: extensive x86 assembly, SSE2/AVX/FMA3/AVX-512 FFT variants
- Threading: worker threads and multithreaded FFT/support operations
- Memory structures and block sizes: FFT-length-specific buffers and runtime tuning data
- Checkpoints and error controls: save files, roundoff checking, rollback, Gerbicz checks, residues
- Proofs/certificates: PRP proof files and certification; Lucas-Lehmer is the deterministic Mersenne primality path
- Input/output formats: worktodo.txt, results.txt, proof/residue/save files and PrimeNet protocol
- Platform status: official Windows archive installed only into ignored audit storage; source inspected
- Supplied test status: computation NOT_RUN to avoid network assignment, long work, and unscoped stress testing
- Simple reproduced case: NOT_RUN
- Raw performance: NOT_MEASURED
- Author-declared limits/TODO: architecture specialization; source described as non-portable; licensing and award/assignment rules apply
- Integration risks: custom EULA, embedded third-party terms, network behavior, assignment provenance, large proof files, and PRP/proof semantic confusion
- Redistribution decision: no redistribution
- PrimeForge decision: isolated ADAPTER only

The audit deliberately did not start Prime95. Merely unpacking an official binary does not authorize assignment requests, stress testing, or redistribution.
