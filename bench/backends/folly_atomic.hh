#pragma once

#include <folly/AtomicHashMap.h>

#include <atomic>
#include <cstddef>
#include <limits>

namespace bench {

// AtomicHashMap is intentionally an optional reference backend. Its values
// require external synchronization after insertion; atomic_ref supplies that
// synchronization for the benchmark's int values.
class folly_atomic_backend {
public:
  using table_type = folly::AtomicHashMap<int, int>;

  explicit folly_atomic_backend(std::size_t bucket_count) : table(bucket_count) {}

  static auto capacity(std::size_t, std::size_t key_space) -> std::size_t {
    // AtomicHashMap never reclaims erased slots, so size for the complete key
    // universe rather than the active bucket count. Leave headroom for the
    // implementation's load factor and reserved slots.
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    return key_space > max / 2 ? max : key_space * 2;
  }

  struct worker {
    explicit worker(folly_atomic_backend &) {}
  };

  auto get(worker &, int key, int &value) -> bool {
    const auto iterator = table.find(key);
    if (iterator == table.end()) {
      return false;
    }
    value = folly::atomic_ref<int>(iterator->second).load(
        std::memory_order_relaxed);
    return true;
  }

  auto put(worker &, int key, int value) -> bool {
    auto iterator = table.find(key);
    if (iterator != table.end()) {
      folly::atomic_ref<int>(iterator->second).store(value,
                                             std::memory_order_relaxed);
      return false;
    }

    auto result = table.insert(key, value);
    if (!result.second) {
      folly::atomic_ref<int>(result.first->second).store(
          value, std::memory_order_relaxed);
    }
    return result.second;
  }

  auto erase(worker &, int key) -> bool { return table.erase(key) != 0; }

private:
  table_type table;
};

} // namespace bench
