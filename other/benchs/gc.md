# GC allocation benchmark

Build `gc_bench` with `cmake --build build --config Release --target gc_bench`.
Run `build/bin/gc_bench 4 10000000 24 1`: workers, allocations per worker,
object size, and whether to retain the last 4096 objects per worker. Retained
objects are explicit roots and remain live during the 20 final major collections.
Run each configuration in a fresh process. Report medians over at least three runs.
The throughput interval includes worker creation, allocation, automatic collections,
and worker unregistration. Root registration and explicit collection sampling are
outside that interval. `heap` is GC-managed memory, not process RSS or peak memory.

Set `HL_GC_PROFILE=1` for automatic collection durations and allocation slow-path
measurements. Times in this output are seconds. `lock-time` includes context saving,
lock acquisition, and scheduling delay; it is not a count of contended mutexes.
`slow-allocations` counts global allocation-lock entries, including slice refills.
Profiling adds timestamp and output overhead, so measure throughput separately.
Linux uses `clock_gettime(CLOCK_MONOTONIC)`; Windows uses performance counters.
The Linux clock API is documented in the [Linux manual](https://www.man7.org/linux/man-pages/man2/clock_gettime.2.html).

Override `GC_SLICE_BYTES` with a compiler definition when comparing slice sizes.
The default is 4096 bytes. All allocator synchronization and slice invalidation
remain in place. Fixed-size free-list rebuilding skips fully live bitmap bytes and
coalesces fully dead bytes, respecting the next existing free run and page bounds.
Variable-size metadata and interior-pointer lookup are unchanged.

## Measurements

Windows x64, MSVC Release, November 10, 2026. Local synthetic measurements;
Linux execution was unavailable (WSL access denied). No Linux performance claim.

Initial slice sweep, four workers, 2 million 24-byte allocations per worker,
4096 retained objects per worker, before the bitmap optimization:

| Slice bytes | Median million allocations/s | GC heap MiB |
| --- | ---: | ---: |
| 2048 | 37.820 | 0.562 |
| 4096 | 39.505 | 0.562 |
| 8192 | 39.641 | 0.562 |
| 16384 | 36.175 | 0.562 |

The 8192-byte runs varied from 31.568 to 39.898 million allocations/s;
4096 was the more consistent smaller choice. Longer measurements showed that
slice sizing alone did not establish a throughput improvement.

Final comparison: four workers, 10 million 24-byte allocations per worker,
4096 retained objects per worker. Baseline uses 2048-byte slices and the original
allocator; both builds include the same timing instrumentation. Three fresh
processes each:

| Metric | Baseline | Final |
| --- | ---: | ---: |
| Median million allocations/s | 38.100 | 48.282 |
| Reported GC heap MiB | 0.562 | 0.562 |
| Explicit collection median ms, 60 samples | 0.0676 | 0.0670 |
| Explicit collection p95 ms | 0.0682 | 0.0705 |
| Explicit collection max ms | 0.0725 | 0.0712 |

Throughput improved 26.7%. Explicit pause latency was essentially unchanged.
A separate profiled run with 1 million allocations per worker measured:

| Metric | Baseline | Final |
| --- | ---: | ---: |
| Slow-path entries | 66835 | 40463 |
| Aggregate allocation lock-path seconds | 0.34385 | 0.24770 |
| Automatic collection median ms | 0.0707 | 0.0684 |
| Automatic collection p95 ms | 0.0895 | 0.0846 |
| Automatic collection max ms | 0.249 | 0.282 |

Automatic collection time includes waiting for the world to stop and marking;
lazy free-list rebuilding happens in subsequent allocation slow paths. Tail
outliers did not uniformly improve. These measurements do not establish behavior
under large live heaps, process RSS, long-lived fragmentation, or Linux contention.

Additional final allocation runs completed for four workers with 8-, 40-, and
128-byte objects (including the variable-size allocation path), and one worker
with 24-byte objects. Existing Release `dynobj` and `maps` CTest checks passed.
The broader thread CTest selection could not run because its executables were
absent from this build. No new correctness tests were added.
