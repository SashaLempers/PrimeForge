# primesieve

- Name: primesieve
- Primary URL: https://github.com/kimwalisch/primesieve
- Revision: tag v12.15, commit 4f85384851da23c36c01ec01ef85b5d9d246e556
- Source/archive SHA-256: 40CFE3DDFE2659D7056BCC193C743C7B106EAB16F2DD7CC3891BB4139650982D
- License at revision: BSD-2-Clause
- License-file SHA-256: E76D77E55AC0F0AA7F01F0543995A8FB3DD8CBE6D394F08CD2AB471B1FDBF946
- Evidence level: REPRODUCED
- Mathematical domain: enumeration and counting of primes in 64-bit intervals
- Main algorithms: segmented Eratosthenes, wheel-210, presieving, bucket sieve
- Supported sizes/forms: start and stop values below 2^64; interval length constraints documented by the API
- CPU/GPU: CPU
- Vectorization/assembly/FFT/NTT: portable C++; architecture-specific bit operations selected by compiler; no GPU/FFT path
- Threading: interval subdivision using standard C++ asynchronous workers
- Memory structures and block sizes: cache-sized segments; separate small/medium/big sieving-prime paths; bucket lists for large primes; runtime sieve-size option
- Checkpoints and error controls: no long-running scientific checkpoint protocol; argument validation and deterministic counts
- Proofs/certificates: none
- Input/output formats: C and C++ APIs; command-line text counts/lists
- Platform status: Windows MSVC reproduced; upstream also documents Unix-like builds
- Supplied test status: 34/34 CTest tests passed locally
- Simple reproduced case: pi(1,000,000) = 78,498; interval [10^12, 10^12+10^6] returned 36,249 primes
- Raw performance: three diagnostic process launches for pi(1,000,000) took 9.362 ms, 5.920 ms, and 5.947 ms wall-clock including process startup; not a stage 5 benchmark and not a comparative claim
- Author-declared limits/TODO: 64-bit interval domain; not a large-integer primality or proof engine
- Integration risks: ABI and dependency pinning; avoid treating enumeration as proof for arbitrary big integers
- Redistribution decision: permitted with BSD notice
- PrimeForge decision: LINK after the stage 3 dependency gate

Reproduction:

    cmake -S out/audit_sources/primesieve -B out/audit_builds/primesieve-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
    cmake --build out/audit_builds/primesieve-release --parallel
    ctest --test-dir out/audit_builds/primesieve-release --output-on-failure
    out/audit_builds/primesieve-release/primesieve.exe 1000000
    out/audit_builds/primesieve-release/primesieve.exe 1000000000000 -d 1000000

Built executable SHA-256: 0C35F0776C84380DED58214631337C53E8C2C2BE0BC0F91D89421475799C661E.
