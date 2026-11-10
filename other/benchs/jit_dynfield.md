# JIT dynamic field cache benchmark

Build with `BUILD_TESTING=ON` and `WITH_VM=ON`, then run `jit_dynfield_test` from the build output directory. The same executable is registered as the `jit_dynamic_fields` CTest correctness check.

The benchmark compares five million emitted `ODynGet` operations with direct runtime getters, and emitted `ODynSet`/`ODynGet` pairs with runtime setter/getter pairs. It prints elapsed CPU time and checks results before measuring. Use a Release build and compare repeated runs on the same machine.

Caches specialize once per instruction. Exact field types use generated loads/stores; conversions, methods, virtuals, inherited fields, and unsupported representations retain the runtime path. Dynamic objects validate the current lookup slot, field hash, type, and packed offset, so deletion, retyping, table growth, and storage relocation cannot reuse stale addresses. Class caches guard both type and object metadata identity. No dynamic object or backing storage pointers are retained in cache records.

Timing uses a fresh dynamic object with the original stable layout after the correctness workload exercises mutation and receiver changes. Both measured instruction sites can hit their original specialization.
