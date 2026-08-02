# Distribution license manifest

The distribution manifest is the machine-checked view of how every audited software component relates to PrimeForge:

- ORIGINAL: authored for PrimeForge and covered by the top-level Apache-2.0 grant;
- LINKED: a library linked or considered for linking;
- EXTERNAL: invoked through an isolated process adapter;
- REFERENCE: studied but neither linked nor invoked as a supported dependency;
- REJECTED: explicitly excluded from new integration;
- CI_ONLY or DEVELOPMENT_TOOL: used to build/test but not shipped.

Redistributed is an independent field. An EXTERNAL process is not automatically license-safe. A component may change to YES only in a milestone that adds its exact license text/hash, notices, source offer or other required materials, and a reviewed redistribution decision.

The portable CMake gate validates every redistributed license file and its SHA-256:

    cmake --build --preset msvc-debug --target primeforge-distribution-check

The CTest negative fixture proves that a missing required license makes the gate fail.
