# Map microbenchmarks

Build with `BUILD_TESTING=ON`, then run:

```sh
cmake --build build --config Release --target maps_bench
build/bin/maps_bench
ctest --test-dir build -C Release -R '^maps$' --output-on-failure
```

On Windows, use `build/bin/maps_bench.exe`. `--test` runs only correctness checks.
The executable includes the production map implementation to exercise internal
pointer lookup maps and inspect probe distances without exposing new APIs.

Each workload inserts and removes 4,096 keys over 32 rounds, with 16 successful
lookup passes per round. Insert timing includes growth and allocation. Values
and removal results are checked during timing. Pointer keys are spaced eight
dynamic objects apart. Collision keys are `i * 8192 - 1`, forcing the same home
bucket and wraparound through the tested capacities. Integer keys are sequential.

Correctness checks cover randomized updates/deletions, exact probe distances,
growth, wraparound, null keys/values, clear, enumeration sizes, byte-string
content equality, 64-bit keys, and both pointer map implementations.

## Local comparison

Windows x64, MSVC Release, November 10, 2026. Representative consecutive baseline
and optimized runs of the same harness; times are nanoseconds per operation.

| Workload | Operation | Before | After |
| --- | --- | ---: | ---: |
| Sequential integers | Insert | 57.87 | 59.99 |
| Sequential integers | Lookup | 1.40 | 1.25 |
| Sequential integers | Remove | 3.46 | 2.65 |
| Aligned pointers | Insert | 340.11 | 73.52 |
| Aligned pointers | Lookup | 76.41 | 4.44 |
| Aligned pointers | Remove | 159.03 | 8.72 |
| Colliding integers | Insert | 10297.86 | 2534.62 |
| Colliding integers | Lookup | 5183.46 | 1340.07 |
| Colliding integers | Remove | 9834.33 | 3168.78 |

These are microbenchmarks, not application throughput measurements. Allocation
and GC affect insertion timings; the small integer insertion difference varies
between runs. Deliberately colliding integers still have linear probe chains;
masking reduces their per-probe cost without changing the integer hash.

Validation: all 11 non-UV CTest cases passed. The full build could not link the
UV sample because `uv.hdll.lib` is unavailable; its bytecode test also failed
with `Eof`. The enabled configuration has `WITH_UV=OFF`.
