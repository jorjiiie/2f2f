#pragma once

#include <cassert>
#include <iostream>
#include <memory_resource>

namespace tftf {
struct alloc_block {
  void *ptr;
  std::size_t size;
};

/// @brief extremely basic fixed size allocator.
/// note that the size is fixed at runtime, maybe template to make it correct
class node_resource : public std::pmr::memory_resource {
public:
  explicit node_resource(std::pmr::memory_resource &upstream)
      : m_upstream(upstream) {}
  auto do_allocate(std::size_t bytes, std::size_t align) -> void * override {
    // check freelist
    if (m_freelist == nullptr) {
      return m_upstream.allocate(bytes, align);
    }

    freenode *node = m_freelist;
    m_freelist = m_freelist->next;
    node->next = m_free_freenodes;
    m_free_freenodes = node;

    assert(node->block.size == bytes);
    // put node on free freenode

    return node->block.ptr;
  }
  void do_deallocate(void *p, std::size_t bytes, size_t align) override {
    freenode *node = get_freenode();
    node->block.ptr = p;
    node->block.size = bytes;
    node->next = m_freelist;
    m_freelist = node;
  }
  auto do_is_equal(const std::pmr::memory_resource &other) const noexcept
      -> bool override {
    return this == &other;
  }

private:
  struct freenode {
    alloc_block block;
    freenode *next{nullptr};
  };
  std::pmr::memory_resource &m_upstream;
  freenode *m_freelist{nullptr};
  freenode *m_free_freenodes{nullptr};

  auto get_freenode() -> freenode * {
    if (m_free_freenodes == nullptr) {
      return static_cast<freenode *>(m_upstream.allocate(sizeof(freenode)));
    }
    freenode *node = m_free_freenodes;
    m_free_freenodes = m_free_freenodes->next;
    return node;
  }
};

} // namespace tftf
