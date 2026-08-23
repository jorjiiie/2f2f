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
// what's the format of this?
// we have a TABLE and we want to construct it?
// like we have a few setups
//
void bench() {
  constexpr size_t n_threads = 1;
  constexpr size_t n_inserts = 10'000'000LL;
  constexpr double p_del = 0.05;
  constexpr int _N = 1e9;
  double mn = std::numeric_limits<double>::max();
  double mx = 0;
  double total_time = 0;

  std::cout << std::format(
      "Benchmark config: {} threads, {} ops/thread, {:.1f}% deletes\n",
      n_threads, n_inserts, p_del * 100);
  std::cout << std::string(70, '-') << "\n";

  for (size_t _k = 0; _k < 10; _k++) {
    std::atomic<size_t> done_count;
    std::atomic<bool> done_flag{false};
    tftf::faster<int, int> f{50'000'000};
    auto insert_job = [&f, &done_count, &done_flag](uint32_t seed) {
      std::pmr::monotonic_buffer_resource buf{50'000'000};
      tftf::node_resource<tftf::faster<int, int>::list_t::alloc_size> resource{
          buf};
      tftf::worker_state state{resource};
      f.register_worker(state);

      std::mt19937 rng{seed};
      std::mt19937 rng_lag{seed};
      std::mt19937 rng_junk{seed};
      std::uniform_int_distribution<int> dist(0, _N);
      std::uniform_real_distribution<double> dist_p(0, 1);

      for (size_t i = 0; i < n_inserts; i++) {
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
    // apple doesn't have jthread? 1984
    std::vector<std::thread> threads;
    auto start = std::chrono::system_clock::now();
    for (uint32_t i = 0; i < n_threads; i++) {
      threads.push_back(std::thread{insert_job, i + _k * n_threads});
    }
    while (done_count.load() < n_threads) {
      std::this_thread::yield();
    }
    done_flag.store(true);
    auto end = std::chrono::system_clock::now();
    for (auto &t : threads) {
      t.join();
    }

    // Calculate metrics
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    double seconds = duration.count() / 1000.0;
    size_t total_ops = n_threads * n_inserts;
    double ops_per_sec = total_ops / seconds;

    // Update stats
    mn = std::min(mn, seconds);
    mx = std::max(mx, seconds);
    total_time += seconds;

    // Print iteration results
    std::cout << std::format("Iteration {:2d}: {:8.3f}s  |  {:.2f} Mops/s\n",
                             _k + 1, seconds, ops_per_sec / 1'000'000);
  }

  // Print summary statistics
  std::cout << std::string(70, '-') << "\n";
  std::cout << std::format("Summary:\n");
  std::cout << std::format("  Average time:    {:.3f}s\n", total_time / 10);
  std::cout << std::format("  Min time:        {:.3f}s\n", mn);
  std::cout << std::format("  Max time:        {:.3f}s\n", mx);
  size_t total_ops = n_threads * n_inserts;
  std::cout << std::format("  Average throughput: {:.2f} Mops/s\n",
                           (total_ops / (total_time / 10)) / 1'000'000);
}
int main() { bench(); }
