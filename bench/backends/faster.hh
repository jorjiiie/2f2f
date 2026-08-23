#pragma once

#include "../../src/faster.hh"
#include "../../src/allocator.hh"

#include <cstddef>
#include <memory_resource>

namespace bench {

class faster_backend {
public:
  using table_type = tftf::faster<int, int>;

  explicit faster_backend(std::size_t table_size) : table(table_size) {}

  struct worker {
    std::pmr::monotonic_buffer_resource buffer{1'000'000};
    tftf::node_resource<table_type::list_t::alloc_size> resource{buffer};
    tftf::worker_state state{resource};

    explicit worker(faster_backend &owner) { owner.table.register_worker(state); }
  };

  auto get(worker &worker, int key, int &value) -> bool {
    auto result = table.get(worker.state, key);
    if (!result) {
      return false;
    }
    value = *result;
    return true;
  }

  auto put(worker &worker, int key, int value) -> bool {
    return table.put(worker.state, key, value);
  }

  auto erase(worker &worker, int key) -> bool {
    return table.erase(worker.state, key);
  }

private:
  table_type table;
};

} // namespace bench
