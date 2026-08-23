# Shared benchmark

The benchmark runner is templated over a small backend concept. Every backend
provides a table type, a per-thread `worker`, and `get`, `put`, and `erase`
operations. The workload and timing code live in `include/bench/` and are
shared by every implementation.

Build and run the built-in, sharded-`std::unordered_map`, and locally detected
oneTBB backends with:

```sh
make -C bench
./bin/bench --ops 1000000 --keys 100000 --buckets 5000 --repetitions 3
```

The benchmark uses a 60% get / 30% overwrite-put / 10% erase mix. Each run
preloads keys before timing and reports CSV rows for 1, 2, 4, and 8 threads.

Optional backends can be enabled without changing the runner:

```sh
make -C bench LIBCUCKOO_INCLUDE=/path/to/libcuckoo
make -C bench FOLLY_CFLAGS='...' FOLLY_LIBS='...'
```

Folly's `AtomicHashMap` is included with the Folly option. It is a special
reference backend: values are updated through `std::atomic_ref`, and its
fixed-size/tombstone behavior should be reported separately from erase-friendly
maps.
