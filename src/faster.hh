#pragma once

#include "common.hh"
#include "logging.hh"
#include "state.hh"

#include <atomic>
#include <format>
#include <optional>
#include <vector>

namespace tftf {
// store K-V
// TODO: enable all warnings for clangd
template <class Key, class Value> class faster {
public:
  faster(std::size_t table_size = 128)
      : m_table(table_size), m_size(table_size) {}

  // NOTE: all `worker_state` variables are thread local

  /// @brief accessor function. value semantics because we don't expect values
  /// to be large
  auto get(worker_state &state, const Key &key) -> std::optional<Value> {
    std::size_t index = get_bucket(key);

    return get_internal(index, key);
  }

  /// @brief put/overwrite function. Moves key and value regardless
  /// of whether or not they end up being used. returns if the item was inserted
  template <class Key_, class Value_>
    requires std::is_convertible_v<Key_, Key> &&
             std::is_convertible_v<Value_, Value>
  auto put(worker_state &state, Key_ &&key, Value_ &&value) -> bool {

    std::size_t index = get_bucket(key);

    // for now, we only epoch here. this just means that the latency for put
    // *can* explode.
    void *node_mem = state.resource.allocate(sizeof(list_node));

    list_node *new_node = new (node_mem)
        list_node(std::forward<Key_>(key), std::forward<Value_>(value));

    bool placed = put_internal(index, new_node);
    if (!placed) {
      state.resource.deallocate(new_node, sizeof(list_node));
    }
    return placed;
  }

  /// @brief update an entry. Returns the old value (if present)
  template <class UpdateFn>
  auto update(worker_state &state, const Key &key, UpdateFn &&fn)
      -> std::optional<Value> {
    std::size_t index = get_bucket(key);

    return update_internal(index, key, std::forward<UpdateFn>(fn));
  }

  // what is the allocator behavior here? we put it into our freelist
  /// @brief erase kv pair. Returns whether or not the erase was sucessful or
  /// not
  auto erase(worker_state &state, const Key &key) -> bool {
    std::size_t index = get_bucket(key);

    return erase_internal(index, state, key);
  }

private:
  // TODO: THINK ABOUT THE ALIGN OF THIS
  struct list_node {
    Key key;
    // Value value;
    tftf::atomic<Value> value;
    using atomic_ptr = tftf::atomic<list_node *>;
    atomic_ptr next;
  };
  using atomic_ptr = list_node::atomic_ptr;

  auto get_internal(std::size_t index, const Key &key) -> std::optional<Value> {
    const atomic_ptr &ptr = m_table[index];

    list_node *node = ptr.load(std::memory_order_acquire);
    if (node == nullptr) {
      return std::nullopt;
    }

    while (node != nullptr) {
      // compare key: key is constant
      if (node->key == key) {
        return node->value.load(std::memory_order_acquire);
      }
      node = node->next.load(std::memory_order_acquire);
    }

    return std::nullopt;
  }

  auto put_internal(std::size_t index, list_node *new_node) -> bool {
    atomic_ptr &ptr = m_table[index];

    list_node *lag = ptr.load(std::memory_order_acquire);

    // case where nullptr
    if (lag == nullptr) {
      list_node *_null = nullptr;
      if (ptr.compare_exchange_strong(_null, new_node)) {
        return true;
      }
      // if someone beat us here, just try again
      return put_internal(index, new_node);
    }

    // not null: start walking
    // TODO: think about memory orders in general
    // precondition: all list noders here are valid
    // for the duration of the function call.
    // ALSO CHECK IF IT's std::memory_order_acquire or
    // std::memory_order::acquire
    list_node *node = lag->next.load(std::memory_order_acquire);

    // base case: check lag
    // return if we should break
    auto compare_set = [new_node](list_node *node) -> bool {
      if (node->key == new_node->key) {
        node->value.store(std::move(new_node->value),
                          std::memory_order_release);
        return true;
      }
      return false;
    };

    if (compare_set(lag)) {
      return false;
    }

    while (node != nullptr) {
      if (compare_set(node)) {
        return false;
      }

      lag = node;
      node = node->next.load(std::memory_order_acquire);
    }
    // splice onto end
    if (lag->next.compare_exchange_strong(node, new_node)) {
      return true;
    }
    // try again
    return put_internal(index, new_node);
  }

  auto update_internal(std::size_t index, const Key &key, auto &&fn)
      -> std::optional<Value> {
    const atomic_ptr &ptr = m_table[index];
    list_node *node = ptr.load(std::memory_order_acquire);
    if (node == nullptr) {
      return std::nullopt;
    }
    while (node != nullptr) {
      if (node->key == key) {
        const Value old_ = node->value.load(std::memory_order_acquire);
        node->value.store(fn(old_), std::memory_order_release);
        return old_;
      }
      node = node->next.load(std::memory_order_acquire);
    }
    return std::nullopt;
  }
  auto erase_internal(std::size_t index, worker_state &state, const Key &key)
      -> bool {
    atomic_ptr &ptr = m_table[index];
    list_node *lag = ptr.load(std::memory_order_release);

    if (lag == nullptr) {
      return false;
    }

    list_node *node = lag->next.load(std::memory_order_acquire);
    // base case: table swap
    if (lag->key == key) {
      // erase this node
      if (ptr.compare_exchange_strong(lag, node) == false) {
        // try again: the only case this happens is a concurrent read.
        // but I think it's possible for things to get alientated here
        // TODO: think about this operation
        return erase_internal(index, state, key);
      }
      // spliced this node out
      state.freelist_add(alloc_block{lag, sizeof(list_node)});
      return true;
    }

    while (node != nullptr) {
      list_node *next = node->next.load(std::memory_order_acquire);
      if (node->key == key) {
        if (lag->next.compare_exchange_strong(node, next)) {
          state.freelist_add(alloc_block{node, sizeof(list_node)});
          return true;
        }
        // try again (think)
        return erase_internal(index, state, key);
      }
      lag = node;
      node = next;
    }
    return false;
  }

  auto get_bucket(const Key &key) -> std::size_t {
    return std::hash<Key>{}(key) % m_table.size();
  }

  // do not grow though!
  std::vector<atomic_ptr> m_table;
  std::atomic<std::size_t> m_size;
};
} // namespace tftf
