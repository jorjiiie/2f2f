#pragma once

#include "backend.hh"
#include "workload.hh"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

namespace bench {

struct counters {
  std::uint64_t gets{0};
  std::uint64_t puts{0};
  std::uint64_t erases{0};
  std::uint64_t hits{0};
  std::uint64_t checksum{0};
};

template <class Backend>
auto capacity_for(const config &cfg) -> std::size_t {
  if constexpr (requires {
                  Backend::capacity(cfg.bucket_count, cfg.key_space);
                }) {
    return Backend::capacity(cfg.bucket_count, cfg.key_space);
  } else {
    return cfg.bucket_count;
  }
}

template <map_backend Backend>
void run_backend(std::string_view name, const config &cfg,
                 const thread_counts &counts) {
  std::cout << "\n" << name << "\n";
  std::cout << "threads,seconds,mops,gets,puts,erases,hits,checksum\n";

  for (const std::size_t thread_count : counts.values) {
    if (cfg.total_operations < thread_count) {
      continue;
    }

    double total_seconds = 0.0;
    counters total{};

    for (std::size_t repetition = 0; repetition < cfg.repetitions;
         ++repetition) {
      Backend table{capacity_for<Backend>(cfg)};
      std::vector<counters> local(thread_count);
      std::barrier start_gate{static_cast<std::ptrdiff_t>(thread_count + 1)};
      std::vector<std::thread> workers;
      workers.reserve(thread_count);

      for (std::size_t worker_index = 0; worker_index < thread_count;
           ++worker_index) {
        workers.emplace_back([&, worker_index] {
          typename Backend::worker worker{table};
          const std::size_t preload_begin =
              worker_index * cfg.preload_per_worker;
          for (std::size_t i = 0; i < cfg.preload_per_worker; ++i) {
            const int key = static_cast<int>(
                (preload_begin + i) % cfg.key_space);
            table.put(worker, key, key);
          }

          start_gate.arrive_and_wait();

          auto &stats = local[worker_index];
          std::mt19937 rng{cfg.seed + static_cast<std::uint32_t>(
                                      repetition * 1009 + worker_index)};
          std::uniform_int_distribution<int> key_distribution(
              0, static_cast<int>(cfg.key_space - 1));
          const std::size_t operations = cfg.total_operations / thread_count;

          for (std::size_t i = 0; i < operations; ++i) {
            const int key = key_distribution(rng);
            switch (choose_operation(rng)) {
            case operation::get: {
              int value{};
              ++stats.gets;
              if (table.get(worker, key, value)) {
                ++stats.hits;
                stats.checksum += static_cast<std::uint64_t>(value);
              }
              break;
            }
            case operation::put:
              ++stats.puts;
              table.put(worker, key, key ^ 0x5a5a5a5a);
              break;
            case operation::erase:
              ++stats.erases;
              table.erase(worker, key);
              break;
            }
          }
        });
      }

      const auto start = std::chrono::steady_clock::now();
      start_gate.arrive_and_wait();
      for (auto &worker : workers) {
        worker.join();
      }
      const auto end = std::chrono::steady_clock::now();
      total_seconds += std::chrono::duration<double>(end - start).count();

      for (const auto &stats : local) {
        total.gets += stats.gets;
        total.puts += stats.puts;
        total.erases += stats.erases;
        total.hits += stats.hits;
        total.checksum += stats.checksum;
      }
    }

    const double average_seconds = total_seconds / cfg.repetitions;
    const double operations = static_cast<double>(cfg.total_operations);
    std::cout << thread_count << ',' << average_seconds << ','
              << operations / average_seconds / 1'000'000.0 << ','
              << total.gets / cfg.repetitions << ','
              << total.puts / cfg.repetitions << ','
              << total.erases / cfg.repetitions << ','
              << total.hits / cfg.repetitions << ','
              << total.checksum / cfg.repetitions << '\n';
  }
}

} // namespace bench
