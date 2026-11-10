# JIT allocation elimination

`jit_alloc_test` is both a bytecode-level correctness test and an allocation-heavy
microbenchmark. Build it with CMake and run `ctest -R jit_allocations -C Release`.
It compares identical bodies with an added label forcing the original pipeline,
checks zero-initialized fields and captured boxing values after source mutation,
and verifies that aliasing retains allocations. It prints timings and GC
slow-path allocation counts for 200,000 calls per body. These counters exclude
slice fast paths; zero is asserted only for eliminated bodies, and fallback
bodies must record a positive count.

For an end-to-end workload:

```sh
haxe -cp other/benchs -main JitAllocations -hl build/jit-allocations.hl
build/bin/hl build/jit-allocations.hl
haxe -cp other/tests -main JitAllocationSafety -hl build/jit-allocation-safety.hl
build/bin/hl build/jit-allocation-safety.hl
```

Expected output is `20000000` and `allocation safety passed`, respectively.
Compare the same benchmark bytecode against a baseline VM on the same machine.

The pass replaces primitive fields of unaliased objects with SSA values and
captures primitive boxing operands for exact-type unboxing. It admits bounded,
straight-line functions only, with a fixed opcode whitelist. Any unsupported
operation (including calls, branches, traps, address-taking and identity
comparisons) keeps the original function pipeline. Every occurrence of each
allocation register is checked, including writes and uses before allocation.
Copies, escaping returns, changed box types, inheritance, bindings, reference
fields, debug mode and hot reload prevent replacement. No partial object is
passed to native code, and no reference field is removed from GC-visible storage.
Allocation failure and allocation statistics naturally differ when an allocation
is eliminated; no program-visible cast or null-access exception is removed
unless the source allocation proves the check succeeds.
