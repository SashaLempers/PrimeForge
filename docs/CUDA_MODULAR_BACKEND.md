# CUDA modular batch backend

PIVOT-05 introduces the first GPU arithmetic component that can later serve the
prime-search pipeline. It is optional, target-specific and absent from ordinary
CPU builds and release packages.

## Contract

`ModularMultiplyTask` contains exactly three unsigned 64-bit values: `left`,
`right` and a nonzero `modulus`. `ModularBatchBackend::multiply_mod` writes one
fully reduced residue for every task. Input/output lengths must match and the
batch must not exceed the fixed construction-time capacity. The v1 stable id is
`primeforge.cuda.modular-u64.v1`.

The CUDA implementation selects the target RTX 5080, allocates task and result
buffers once, and creates one nonblocking stream. Each synchronous call validates
the complete request, submits H2D copy, kernel and D2H copy in that stream, checks
launch status and synchronizes before returning. No CUDA type escapes the public
header. Empty batches are valid no-ops; zero modulus, invalid capacity/device,
length mismatch and capacity overflow fail before accepting a result.

## Exact arithmetic

The kernel first reduces both operands and then uses overflow-safe modular
addition/doubling. For reduced `a,b < m`, it computes `a+b mod m` as either
`a-(m-b)` or `a+b`, avoiding an overflowing intermediate. This simple fixed-width
algorithm is not presented as fast; its role is to establish the trusted batch,
buffer, stream and differential-testing boundary before optimization.

The test matrix contains the Cartesian product of ten operand boundaries and ten
modulus boundaries (1,000 tasks), then 100,000 SplitMix64 tasks from a fixed seed.
Every result must equal both `multiply_mod_portable_reference` and the optimized
CPU `multiply_mod`, be reduced, and repeat identically. Contract-failure cases run
in the same executable. The complete executable is repeated under Compute
Sanitizer memcheck.

## Local gate

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_cuda_validation.ps1 -Clean
```

This is a correctness gate, not a benchmark. GPU output remains untrusted in the
search engine until PIVOT-06 supplies per-batch identity, CPU verification,
durable completion and interruption recovery.
