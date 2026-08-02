# Pre-pivot backup

This checkpoint preserves work that was in progress when the hardware-specialized pivot superseded the stage 0–20 roadmap.

The external-process adapter scaffold provides isolated working directories, executable SHA-256 checks, raw-output retention, timeout and memory-limit mechanisms, versioned conservative parsers, and a CLI. Its unit tests pass, but the former stage 12 was not completed and this commit is not a stage-12 scientific gate. `validate_stage12_adapters.ps1` is retained as reference material and has not been used to claim engine equivalence.

The complete local checkpoint passed 17/17 tests in Debug (33.13 s) and Release (3.67 s), with zero PrimeForge warnings. The new external-adapter test passed in 0.11 s Debug and 0.08 s Release.

The specialized pivot roadmap decides whether this scaffold is kept, adapted, or moved to reference status. No CUDA installation, external campaign, publication, or long computation occurred here.
