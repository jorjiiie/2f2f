#include "bench/runner.hh"
#include "backends/faster.hh"
#include "backends/sharded_std.hh"

#ifdef BENCH_ENABLE_TBB
#include "backends/tbb.hh"
#endif

#ifdef BENCH_ENABLE_LIBCUCKOO
#include "backends/libcuckoo.hh"
#endif

#ifdef BENCH_ENABLE_FOLLY
#include "backends/folly.hh"
#endif

#ifdef BENCH_ENABLE_FOLLY_ATOMIC
#include "backends/folly_atomic.hh"
#endif

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

auto parse_size(int argc, char **argv, std::string_view option,
                std::size_t fallback) -> std::size_t {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == option) {
      return std::strtoull(argv[i + 1], nullptr, 10);
    }
  }
  return fallback;
}

} // namespace

int main(int argc, char **argv) {
  bench::config config;
  config.total_operations =
      parse_size(argc, argv, "--ops", config.total_operations);
  config.key_space = parse_size(argc, argv, "--keys", config.key_space);
  config.bucket_count =
      parse_size(argc, argv, "--buckets", config.bucket_count);
  config.repetitions =
      parse_size(argc, argv, "--repetitions", config.repetitions);

  if (config.total_operations == 0 || config.key_space == 0 ||
      config.bucket_count == 0 || config.repetitions == 0) {
    std::cerr << "--ops, --keys, --buckets, and --repetitions must be positive\n";
    return 2;
  }

  const bench::thread_counts counts;
  bench::run_backend<bench::faster_backend>("faster", config, counts);
  bench::run_backend<bench::sharded_std_backend>("sharded-std", config,
                                                 counts);

#ifdef BENCH_ENABLE_TBB
  bench::run_backend<bench::tbb_backend>("oneTBB", config, counts);
#endif

#ifdef BENCH_ENABLE_LIBCUCKOO
  bench::run_backend<bench::libcuckoo_backend>("libcuckoo", config, counts);
#endif

#ifdef BENCH_ENABLE_FOLLY
  bench::run_backend<bench::folly_backend>("folly", config, counts);
#endif

#ifdef BENCH_ENABLE_FOLLY_ATOMIC
  bench::run_backend<bench::folly_atomic_backend>("folly-atomic", config,
                                                  counts);
#endif
}
