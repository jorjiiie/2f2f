#pragma once

#include <oneapi/tbb/concurrent_hash_map.h>

#include <cstddef>

namespace bench {

class tbb_backend {
public:
  using table_type = oneapi::tbb::concurrent_hash_map<int, int>;

  explicit tbb_backend(std::size_t bucket_count) : table(bucket_count) {}

  struct worker {
    explicit worker(tbb_backend &) {}
  };

  auto get(worker &, int key, int &value) -> bool {
    table_type::const_accessor accessor;
    if (!table.find(accessor, key)) {
      return false;
    }
    value = accessor->second;
    return true;
  }

  auto put(worker &, int key, int value) -> bool {
    table_type::accessor accessor;
    const bool inserted = table.insert(accessor, key);
    accessor->second = value;
    return inserted;
  }

  auto erase(worker &, int key) -> bool { return table.erase(key); }

private:
  table_type table;
};

} // namespace bench
