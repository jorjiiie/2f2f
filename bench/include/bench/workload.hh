#pragma once

#include <cstddef>
#include <cstdint>
#include <random>

namespace bench {

struct config {
  std::size_t total_operations{1'000'000};
  std::size_t key_space{100'000};
  std::size_t bucket_count{5'000};
  std::size_t repetitions{3};
  std::size_t preload_per_worker{2'000};
  std::uint32_t seed{0x2f2f};
};

struct thread_counts {
  std::size_t values[4]{1, 2, 4, 8};
};

enum class operation : std::uint8_t { get, put, erase };

inline auto choose_operation(std::mt19937 &rng) -> operation {
  const auto bucket = std::uniform_int_distribution<int>{0, 99}(rng);
  if (bucket < 60) {
    return operation::get;
  }
  if (bucket < 90) {
    return operation::put;
  }
  return operation::erase;
}

} // namespace bench
