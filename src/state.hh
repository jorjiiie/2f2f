#pragma once

#include "allocator.hh"
#include "common.hh"

#include <memory_resource>
#include <vector>

namespace tftf {
/// @brief contains the thread local state
/// to be passed around. Used by faster for local allocators and such
/// freelist is returned to resource periodically after the epoch is deemed safe
struct worker_state {
  std::pmr::memory_resource &resource;
  std::vector<alloc_block> freelist;
  tftf::atomic<std::uint64_t> epoch;

  void freelist_add(alloc_block block) {
    epoch++;
    freelist.push_back(block);
  }
};

}; // namespace tftf
