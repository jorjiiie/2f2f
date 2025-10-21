#pragma once

#include "common.hh"

#include <list>
#include <memory_resource>

namespace tftf {

struct alloc_block {
  void *ptr;
  std::uint64_t epoch;
};
/// @brief contains the thread local state
/// to be passed around. Used by faster for local allocators and such
/// freelist is returned to resource periodically after the epoch is deemed safe
struct worker_state {
  std::pmr::memory_resource &resource;
  // this freelist is also going to be tricky: currently, this is the default
  // allocator but that will face contention on alloc/dealloc of the freelist
  // nodes, using the global malloc lock. not great!
  std::list<alloc_block> freelist{};
  std::uint64_t epoch{0};
  std::uint64_t ticks{0};
  size_t index{0};

  void freelist_add(alloc_block block) {
    epoch++;
    // now this has allocation problems...
    freelist.push_back(block);
  }
};

}; // namespace tftf
