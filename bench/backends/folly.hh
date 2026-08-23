#pragma once

#include <folly/concurrency/ConcurrentHashMap.h>

#include <cstddef>

namespace bench {

class folly_backend {
public:
  using table_type = folly::ConcurrentHashMap<int, int>;

  explicit folly_backend(std::size_t bucket_count) : table(bucket_count) {}

  struct worker {
    explicit worker(folly_backend &) {}
  };

  auto get(worker &, int key, int &value) -> bool {
    const auto iterator = table.find(key);
    if (iterator == table.end()) {
      return false;
    }
    value = iterator->second;
    return true;
  }

  auto put(worker &, int key, int value) -> bool {
    return table.insert_or_assign(key, value).second;
  }

  auto erase(worker &, int key) -> bool { return table.erase(key) != 0; }

private:
  table_type table;
};

} // namespace bench
