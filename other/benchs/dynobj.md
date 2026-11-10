# Dynamic object benchmarks

Build with `BUILD_TESTING=ON`:

```sh
cmake --build build --config Release --target dynobj_bench
build/bin/dynobj_bench
ctest --test-dir build -C Release -R '^dynobj$' --output-on-failure
```

On Windows, run `build/bin/dynobj_bench.exe`. Pass `--test` for correctness checks
only. The harness uses the public runtime API and keeps assertions enabled.

Each workload creates 20,000 objects with 5, 20, or 100 fields. Scalar fields are
32-bit integers; pointer fields contain newly allocated dynamic integers; mixed
objects alternate between the two. Timing includes object creation, field
insertion, value allocation, and automatic GC. Field hashes are prepared before
timing. A full GC runs before each workload. Storage allocation counts are
measured separately by counting changes to lookup, scalar, and pointer buffers;
they exclude object and value allocations.

Correctness checks cover insertion order, mixed scalar alignment, forced GC,
virtual references across growth and compaction, deletion/reinsertion, scalar
and pointer type changes, virtual type mismatch, copying, and materialization of
virtual objects. The checks also run in Debug with allocator guard words enabled.

## Local comparison

Windows x64, MSVC Release, November 10, 2026. Representative runs with the same
workload before and after the optimization, in nanoseconds per object:

| Fields | Storage | Before | After | Storage allocations before / after |
| ---: | --- | ---: | ---: | ---: |
| 5 | Scalar | 570.0 | 492.0 | 10 / 4 |
| 20 | Scalar | 5866.4 | 2187.1 | 40 / 8 |
| 100 | Scalar | 120478.9 | 7045.6 | 200 / 12 |
| 5 | Pointer | 688.7 | 699.9 | 10 / 4 |
| 20 | Pointer | 6157.8 | 3008.8 | 40 / 8 |
| 100 | Pointer | 132272.7 | 10117.0 | 200 / 12 |
| 5 | Mixed | 686.5 | 497.8 | 10 / 4 |
| 20 | Mixed | 5727.8 | 2227.9 | 40 / 10 |
| 100 | Mixed | 106560.7 | 7168.3 | 200 / 16 |

Baseline storage allocations are two per insertion. Larger objects improve
substantially; the five-pointer case is slightly slower in this sample. Timings
vary with GC and system load and are not application throughput measurements.
Geometric growth retains spare capacity in exchange for fewer allocations and
copies. Capacity comes from the GC allocation size, preserving the object ABI;
unused pointer slots are zeroed. Scalar holes are compacted when growth is
needed, and compaction alone does not double capacity.

Validation: the new correctness checks passed in Debug and Release. The initial
full Release CTest run passed 12 of 14 tests. The existing UV bytecode sample
failed with `Eof`, and the native UV sample executable was missing, matching the
limitations already recorded in `maps.md` for this `WITH_UV=OFF` build.
