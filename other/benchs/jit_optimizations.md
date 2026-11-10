# JIT optimization benchmarks

Compile the same bytecode once and run it with baseline and optimized Release VMs:

```sh
haxe -cp other/benchs -main JitSmallCalls -hl build/jit-small.hl
haxe -cp other/benchs -main JitLoops -hl build/jit-loops.hl
haxe -cp other/benchs -main JitArithmetic -hl build/jit-arithmetic.hl
hl build/jit-small.hl
hl build/jit-loops.hl
hl build/jit-arithmetic.hl
```

Expected checksums are 40000000, 20000000, and 120000000 respectively.
`@:neverInline` keeps the calls in bytecode so these measure JIT inlining.
The existing `Benchs` runner also discovers these workloads automatically.

Windows x64, MSVC Release, 2026-11-10: median elapsed process time from ten runs after one warmup, using identical bytecode and runtime DLLs. The baseline has these JIT optimizations disabled.

| Workload | Baseline | Optimized | Reduction |
| --- | ---: | ---: | ---: |
| Small calls | 45.481 ms | 35.199 ms | 22.6% |
| Loops | 40.137 ms | 38.265 ms | 4.7% |
| Arithmetic | 48.093 ms | 39.548 ms | 17.8% |

These timings include process startup and JIT compilation; they are local observations, not portable performance guarantees. Check checksums on every run and compare repeated medians on the same machine. For longer measurements, increase loop counts in both versions identically.

The initial optimization scope is deliberately narrow: bounded straight-line integer leaf inlining, block-local expression and constant reuse, 32-bit integer folding with wrapping arithmetic, and transitive removal of unused pure integer values. Division, shifts, floating-point arithmetic, memory reads, calls, and phi inputs are retained by dead-value removal. Instruction positions remain stable through NOPs, which the allocator omits from native output.

Debug mode disables these transformations. Functions with traps or addressable locals retain the original pipeline. Hot reload disables inlining so existing callers continue to use replaceable call targets. No calling-convention or unwind-table generation changes are made.
