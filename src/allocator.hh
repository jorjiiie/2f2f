#pragma once
/// august 28, 2025
/// ryan z
/// simple fixed-size allocator. intended to be thread-local, so no
/// synchronization or thread safety is allowed. Another way to do this would be
/// to use std::hive (c++26), but not 100% sure how that's implemented yet
#include <cassert>
#include <memory_resource>

namespace tftf {

// if we really care about measuring distribution we can store a vector here to
// observe the load factor, but honestly the vector wouldn't be independent of
// the distribution because of resizing nonsense (measurement would cause side
// effects is what I'm saying)
struct stats {
  uint64_t alloc_count{0};
  uint64_t dealloc_count{0};
  uint64_t freenodes_allocated{0};
};
/// @brief extremely basic fixed size bucket-freelist allocator
/// note that the size is fixed at runtime, maybe template to make it correct
template <std::size_t alloc_size>
class node_resource : public std::pmr::memory_resource {
public:
  explicit node_resource(std::pmr::memory_resource &upstream)
      : m_upstream(upstream) {}

  /// @brief allocation strategy: try using the freelist,
  /// otherwise use upstream
  auto do_allocate(std::size_t bytes, std::size_t align) -> void * override {
    // check freelist
    assert(bytes == alloc_size);
    m_stats.alloc_count++;
    if (m_freelist == nullptr) {
      return m_upstream.allocate(bytes, align);
    }

    // grab off freelist
    freenode *node = m_freelist;
    m_freelist = m_freelist->next;

    // put on freenode freelist
    node->next = m_free_freenodes;
    m_free_freenodes = node;

    return node->ptr;
  }
  /// @brief deallocate and put into freenode: may actually allocate due to
  /// freelist nodes being required
  void do_deallocate(void *p, std::size_t /* bytes */,
                     size_t /* align*/) override {
    m_stats.dealloc_count++;
    freenode *node = get_freenode();
    node->ptr = p;
    node->next = m_freelist;
    m_freelist = node;
  }
  auto do_is_equal(const std::pmr::memory_resource &other) const noexcept
      -> bool override {
    return this == &other;
  }

  // we observe this publicly
  stats m_stats{};

private:
  struct freenode {
    void *ptr;
    freenode *next{nullptr};
  };
  std::pmr::memory_resource &m_upstream;
  freenode *m_freelist{nullptr};
  freenode *m_free_freenodes{nullptr};

  /// @brief get a freelist node, either through allocation or the freenode
  /// freelist
  auto get_freenode() -> freenode * {
    if (m_free_freenodes == nullptr) {
      m_stats.freenodes_allocated++;
      return static_cast<freenode *>(m_upstream.allocate(sizeof(freenode)));
    }
    freenode *node = m_free_freenodes;
    m_free_freenodes = m_free_freenodes->next;
    return node;
  }
};
template <> class node_resource<24>;

} // namespace tftf
