# Phase 2: Performance

## Method

`mml_bench` generates one message stream and replays it, unchanged, into all four book variants:

- **Workload.** About 10,000 resting orders at steady state. Mix: 52% passive adds a geometric distance from the touch, 39% cancels of live orders, 9% aggressive market orders of 1 to 150 units. Cancels are generated against a reference book so they always target live orders.
- **Throughput pass.** The whole stream is timed once; ns/msg is total time divided by messages.
- **Latency pass.** Each message is timed individually with `steady_clock`. The clock's own overhead (about 27 ns on the test machine) is reported and included in the figures.
- **Allocation analysis.** The benchmark binary replaces global `operator new` with a counting version, so allocations and bytes per message are measured, not estimated.
- **Scaling.** The throughput pass is repeated with 1k to 1M resting orders. Larger books are also spread over more price levels.
- **Consistency.** A checksum of fills must agree across variants or the benchmark exits with an error.

## Results

Measured on an Intel Xeon @ 2.10 GHz (cloud VM, 2 vCPU), GCC 13.3, `-O3`, Release build. Numbers on your machine will differ; the relative ordering is the interesting part. Raw output: [`results/bench_output.txt`](../results/bench_output.txt).

### Throughput and allocations (10k resting orders, 1M messages)

| Variant | ns/msg | M msg/s | allocs/msg | bytes/msg |
|---|---:|---:|---:|---:|
| map + hash | 116.4 | 8.59 | 0.659 | 23.6 |
| map + dense | 80.2 | 12.47 | 0.138 | 27.6 |
| ladder + hash | 55.3 | 18.08 | 0.521 | 12.5 |
| ladder + dense | 55.4 | 18.05 | 0.000 | 16.5 |

### Latency (ns): p50 / p99 / p99.9

| Variant | add | cancel | aggressive |
|---|---|---|---|
| map + hash | 137 / 290 / 484 | 118 / 360 / 500 | 177 / 594 / 870 |
| map + dense | 109 / 225 / 399 | 62 / 284 / 430 | 130 / 417 / 620 |
| ladder + hash | 72 / 170 / 330 | 117 / 243 / 384 | 138 / 421 / 621 |
| ladder + dense | 74 / 148 / 328 | 90 / 229 / 429 | 126 / 299 / 424 |

### Scaling with book size (ns/msg)

| Resting orders | map + hash | map + dense | ladder + hash | ladder + dense |
|---:|---:|---:|---:|---:|
| 1,000 | 72.2 | 48.7 | 43.6 | 27.7 |
| 10,000 | 108.0 | 72.5 | 52.1 | 34.5 |
| 100,000 | 183.8 | 114.0 | 100.7 | 52.1 |
| 1,000,000 | 299.1 | 154.3 | 219.5 | 76.9 |

## Interpretation

**Allocation is the first-order cost.** `std::unordered_map` allocates a node on every insert (about 0.5 allocations per message here, since about half the messages are adds). `std::map` allocates whenever a new price level appears. Replacing both brings steady-state allocations to zero: `ladder + dense` reports 0.000 allocs/msg. Its non-zero bytes/msg comes from the dense index doubling a few times as ids grow, which is a handful of large allocations, not per-message churn.

**Order-id lookup.** Swapping the hash index for a direct-mapped vector cuts cancel p50 from 118 to 62 ns on the map book. The dense index requires ids to be small and roughly sequential, which is true when the gateway assigns them, as it does here.

**Price levels.** The ladder turns level lookup into an array index. The cost is memory proportional to the tick range (100k ticks by default, about 4 MB per side) and a linear scan to find the next best level when the best one empties. The scan is cheap because liquidity is dense near the touch.

**Cache behaviour.** Everything slows as the book grows because the working set leaves cache. The hash-based variants degrade fastest (for ladder + hash, 52 to 220 ns from 10k to 1M orders) because hash buckets and nodes are scattered across memory. `ladder + dense` degrades most gracefully (35 to 77 ns): both of its lookups are single indexed loads into contiguous arrays.

## Reproduce

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMML_NATIVE=ON
cmake --build build -j
./build/mml_bench              # full run, writes results/bench_*.csv
./build/mml_bench --quick      # smaller run
perf stat -e cache-misses,cache-references,instructions,cycles ./build/mml_bench --quick
```

For stable numbers, pin the process to one core (`taskset -c 2`) and set the CPU governor to `performance`.

## Next steps

- Hardware cycle counters (rdtsc or `perf_event_open`) instead of `steady_clock`, to remove clock overhead.
- An intrusive open-addressing hash index for sparse id spaces.
- A bitmap over the ladder to find the next non-empty level in O(L/64).
- Latency under bursty traffic replayed from real exchange feeds (for example ITCH).
