#pragma once

#include "common.hh"
#include "logging.hh"
#include "state.hh"

#include <atomic>
#include <optional>
#include <vector>

#include <unordered_set>

namespace tftf {
struct default_faster_traits {
  static constexpr size_t max_workers = 1'024;
  static constexpr size_t minor_ticks_per_major = 10'000;
};
// store K-V
// TODO: enable all warnings for clangd
template <class Key, class Value, class Traits = default_faster_traits>
class faster {
public:
  auto check_reachable_deleted_pointers(worker_state &state) -> size_t {
    // Build a set of all pointers in this worker's freelist
    std::unordered_set<void *> my_deleted_ptrs;
    for (const auto &block : state.freelist) {
      my_deleted_ptrs.insert(block.ptr);
    }

    if (my_deleted_ptrs.empty()) {
      return 0;
    }

    // Walk all buckets and count how many of our deleted pointers are still
    // reachable
    size_t reachable_count = 0;

    for (size_t bucket = 0; bucket < m_table.size(); ++bucket) {
      atomic_ptr &ptr = m_table[bucket];
      list_node *current = ptr.load(std::memory_order_acquire);

      while (current != nullptr) {
        if (my_deleted_ptrs.count(static_cast<void *>(current)) > 0) {
          reachable_count++;
          // Optional: print debug info
          std::printf("Worker %zu: Found reachable deleted pointer in bucket "
                      "%zu, key=%d\n",

                      state.index, bucket, current->key);
        }
        current = current->next.load(std::memory_order_acquire);
      }
    }

    return reachable_count;
  }
  faster(std::size_t table_size = 128)
      : m_table(table_size), m_size(table_size) {}

  // NOTE: all `worker_state` variables are thread local

  /// @brief accessor function. value semantics because we don't expect values
  /// to be large
  auto get(worker_state & /*state*/, const Key &key) -> std::optional<Value> {
    std::size_t index = get_bucket(key);

    return get_internal(index, key);
  }

  /// @brief put/overwrite function. Moves key and value regardless
  /// of whether or not they end up being used. returns if the item was inserted
  template <class Key_, class Value_>
    requires std::is_convertible_v<Key_, Key> &&
             std::is_convertible_v<Value_, Value>
  auto put(worker_state &state, Key_ &&key, Value_ &&value) -> bool {

    auto scope_exit =
        tftf::on_scope_exit([this, &state]() { minor_tick(state); });

    std::size_t index = get_bucket(key);

    // for now, we only epoch here. this just means that the latency for put
    // *can* explode.
    void *node_mem = state.resource.allocate(alloc_size);

    list_node *new_node = new (node_mem)
        list_node(std::forward<Key_>(key), std::forward<Value_>(value));

    bool placed = put_internal(index, new_node);
    if (!placed) {
      state.resource.deallocate(new_node, alloc_size);
    }
    return placed;
  }

  /// @brief update an entry. Returns the old value (if present)
  template <class UpdateFn>
  auto update(worker_state & /*state*/, const Key &key, UpdateFn &&fn)
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

  // not sure if this is a good idea.
  // we operate on the simplifying assumption that no workers leave
  // TODO: might be a good idea to get the worker from here (and so it's private
  // and we can't construct externally, to get the invariant everything is
  // initialized)
  auto register_worker(worker_state &state) -> void {
    state.index = m_workers.fetch_add(1);
  }

private:
  struct list_node;

public:
  static constexpr std::size_t alloc_size = sizeof(list_node);

private:
  // TODO: THINK ABOUT THE ALIGN OF THIS
  struct list_node {
    Key key;
    // Value value;
    std::atomic<Value> value;
    using atomic_ptr = std::atomic<list_node *>;
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
        // we don't CAS because it doesn't matter (non deterministic anyways)
        node->value.store(fn(old_), std::memory_order_release);
        return old_;
      }
      node = node->next.load(std::memory_order_acquire);
    }
    return std::nullopt;
  }

  // erasing causes an epoch event
  auto erase_internal(std::size_t index, worker_state &state, const Key &key)
      -> bool {
    atomic_ptr &ptr = m_table[index];
    list_node *lag = ptr.load(std::memory_order_acquire);

    if (lag == nullptr) {
      return false;
    }
    auto exec = [this, &state] [[nodiscard]] (list_node * ptr) {
      uint64_t epoch = m_epoch.fetch_add(1);
      state.freelist_add(alloc_block{ptr, epoch});
      return true;
    };

    list_node *node = lag->next.load(std::memory_order_acquire);
    // base case: table swap
    if (lag->key == key) {
      // erase this node
      if (ptr.compare_exchange_strong(lag, node)) {
        return exec(lag);
      }
      return erase_internal(index, state, key);
    }

    // standard case: traverse the list
    while (node != nullptr) {
      list_node *next = node->next.load(std::memory_order_acquire);
      if (node->key == key) {
        if (lag->next.compare_exchange_strong(node, next)) {
          return exec(node);
        }
        return erase_internal(index, state, key);
      }
      lag = node;
      node = next;
    }
    // not found
    return false;
  }

  // TODO: (really to benchmark): can consolidate all the free/alloc stuff into
  // a big lock free queue which would better distribute the load (we can only
  // realloc if this worker has a corresponding delete call! so if the load is
  // badly distributed, then this will also be badly distributed) would be a
  // mcmp queue, so will be a "objective" performance hit in tradeoff for better
  // distribution
  void major_tick(worker_state &state) {
    // we are guaranteed at least this thread is registered
    uint64_t safe_epoch = m_epochs[0].load(std::memory_order_acquire);
    uint64_t current_workers = m_workers.load(std::memory_order_acquire);

    // calculate the safe epoch each time we do a gc (we don't need to do it
    // more than this, since this is the only place we use the epoch)
    for (size_t i = 1; i < current_workers; ++i) {
      safe_epoch =
          std::min(safe_epoch, m_epochs[i].load(std::memory_order_acquire));
    }

    auto &delete_list = state.freelist;

    while (!delete_list.empty() && delete_list.front().epoch < safe_epoch) {
      auto front = delete_list.begin();
      state.resource.deallocate(front->ptr, alloc_size);

      delete_list.erase(front);
    }
  }
  void minor_tick(worker_state &state) {
    state.ticks++;
    if (state.ticks == minors_per_major) [[unlikely]] {
      // refresh out ack epoch
      m_epochs[state.index].store(m_epoch.load(std::memory_order_acquire),
                                  std::memory_order_release);
      // major_tick(state);
      state.ticks = 0;
    }
  }

  auto get_bucket(const Key &key) -> std::size_t {
    return std::hash<Key>{}(key) % m_table.size();
  }

  // do not grow though!
  std::vector<atomic_ptr> m_table;
  tftf::atomic<size_t> m_size;
  tftf::atomic<uint64_t> m_epoch;
  static constexpr uint64_t minors_per_major{Traits::minor_ticks_per_major};

  static constexpr size_t max_workers{Traits::max_workers};
  // TODO: investigate cache alignment here
  std::array<tftf::atomic<uint64_t>, max_workers> m_epochs{};
  tftf::atomic<size_t> m_workers{0};
};
} // namespace tftf
