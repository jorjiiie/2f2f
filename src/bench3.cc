/*
 * bench.cpp
 * the main problem with benching is that faster is thread-aware in the sense
 * that it has central state
 * a way to do this is to give a "task" to a giant worker where a worker is an
 * instance of faster or a generic table that has some work it must do.
 *
 * however, this is not really a fair benchmark. but i guess what can you do?
 *
 * in classic rz faster style, this bench will be a split of read, upsert,
 * read-modify-write, and delete ops defined by a ratio.
 *
 * the benchmark is technically not fair because of different numbers of
 * threads, but it is also not fair because of threading nondeterminism. so we
 * assume the law of large numbers and hope that the benchmark is expected fair.
 * what more can you ask for
 */
#include "allocator.hh"
#include "faster.hh"
#include <atomic>
#include <cassert>
#include <chrono>
#include <format>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

void bench() {
  constexpr size_t total_ops = 30'000'000LL; // Fixed total workload
  constexpr double p_del = 0.05;
  constexpr int _N = 1e9;
  constexpr size_t n_iterations = 5;

  std::vector<size_t> thread_counts = {1, 2, 4, 5, 8};

  std::cout << std::format("Benchmark config: {} total ops, {:.1f}% deletes\n",
                           total_ops, p_del * 100);
  std::cout << std::string(80, '=') << "\n\n";

  for (size_t n_threads : thread_counts) {
    size_t ops_per_thread = total_ops / n_threads;

    std::cout << std::format("Testing with {} thread(s), {} ops/thread:\n",
                             n_threads, ops_per_thread);
    std::cout << std::string(70, '-') << "\n";

    double mn = std::numeric_limits<double>::max();
    double mx = 0;
    double total_time = 0;

    for (size_t _k = 0; _k < n_iterations; _k++) {
      std::atomic<size_t> done_count{0};
      std::atomic<bool> done_flag{false};
      tftf::faster<int, int> f{50'000'000};

      auto insert_job = [&f, &done_count, &done_flag,
                         ops_per_thread](uint32_t seed) {
        std::pmr::monotonic_buffer_resource buf{500'000'000};
        tftf::node_resource<tftf::faster<int, int>::list_t::alloc_size>
            resource{buf};
        tftf::worker_state state{resource};
        f.register_worker(state);
        std::mt19937 rng{seed};
        std::mt19937 rng_lag{seed};
        std::mt19937 rng_junk{seed};
        std::uniform_int_distribution<int> dist(0, _N);
        std::uniform_real_distribution<double> dist_p(0, 1);

        for (size_t i = 0; i < ops_per_thread; i++) {
          double p = dist_p(rng_junk);
          if (p > p_del) {
            int k = dist(rng);
            int v = dist(rng);
            f.put(state, k, v);
          } else {
            int k = dist(rng_lag);
            dist(rng_lag);
          }
        }
        done_count.fetch_add(1);
        while (!done_flag.load()) {
          std::this_thread::yield();
        }
      };

      std::vector<std::thread> threads;
      auto start = std::chrono::high_resolution_clock::now();

      for (uint32_t i = 0; i < n_threads; i++) {
        threads.push_back(std::thread{insert_job, i + _k * 100});
      }

      while (done_count.load() < n_threads) {
        std::this_thread::yield();
      }
      done_flag.store(true);
      auto end = std::chrono::high_resolution_clock::now();

      for (auto &t : threads) {
        t.join();
      }

      // Calculate metrics
      auto duration =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start);
      double seconds = duration.count() / 1'000'000.0;
      double ops_per_sec = total_ops / seconds;

      // Update stats
      mn = std::min(mn, seconds);
      mx = std::max(mx, seconds);
      total_time += seconds;

      // Print iteration results
      std::cout << std::format(
          "  Iteration {:2d}: {:8.3f}s  |  {:.2f} Mops/s\n", _k + 1, seconds,
          ops_per_sec / 1'000'000);
    }

    // Print summary for this thread count
    double avg_time = total_time / n_iterations;
    double avg_throughput = total_ops / avg_time / 1'000'000;

    std::cout << std::format(
        "  Average: {:.3f}s  |  {:.2f} Mops/s  |  Speedup: {:.2f}x\n", avg_time,
        avg_throughput,
        (n_threads == 1)
            ? 1.0
            : avg_throughput / (total_ops / total_time * n_threads));
    std::cout << "\n";
  }
}

int main() { bench(); }
