# Microbenchmark host. Google Benchmark is fetched on demand once we have
# real workloads to measure.
add_custom_target(noted_bench_placeholder
    COMMENT "bench: placeholder — wires google/benchmark in feat/bench")
