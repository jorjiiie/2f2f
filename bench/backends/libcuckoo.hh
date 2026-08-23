#pragma once

#include <libcuckoo/cuckoohash_map.hh>

#include <cstddef>

namespace bench {

class libcuckoo_backend {
public:
  using table_type = libcuckoo::cuckoohash_map<int, int>;

  explicit libcuckoo_backend(std::size_t bucket_count) : table(bucket_count) {}

  struct worker {
    explicit worker(libcuckoo_backend &) {}
  };

  auto get(worker &, int key, int &value) -> bool {
    return table.find(key, value);
  }

  auto put(worker &, int key, int value) -> bool {
    return table.insert_or_assign(key, value);
  }

  auto erase(worker &, int key) -> bool { return table.erase(key); }

private:
  table_type table;
};

} // namespace bench
