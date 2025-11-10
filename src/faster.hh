#pragma once

#include "common.hh"
#include "list.hh"
#include "state.hh"

#include <atomic>
#include <optional>
#include <vector>

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
  using list_t = tftf::list<Key, Value, std::less<Key>>;
  faster(std::size_t table_size = 128)
      : m_lists(table_size), m_size(table_size) {}

  // NOTE: all `worker_state` variables are thread local

  /// @brief accessor function. value semantics because we don't expect values
  /// to be large
  auto get(worker_state &state, const Key &key) -> std::optional<Value> {
    list_t &l = get_list(key);

    return l.find(state, key);
  }

  /// @brief put/overwrite function. Moves key and value regardless
  /// of whether or not they end up being used. returns if the item was inserted
  template <class Key_, class Value_>
    requires std::is_convertible_v<Key_, Key> &&
             std::is_convertible_v<Value_, Value>
  auto put(worker_state &state, Key_ &&key, Value_ &&value) -> bool {

    list_t &l = get_list(key);

    auto scope_exit =
        tftf::on_scope_exit([this, &state]() { minor_tick(state); });

    return l.put(state, std::forward<Key_>(key), std::forward<Value_>(value));
  }

  /// @brief update an entry. Returns the old value (if present)
  template <class UpdateFn>
  auto update(worker_state &state, const Key &key, UpdateFn &&fn)
      -> std::optional<Value> {
    list_t &l = get_list(key);
    return l.update(state, key, std::forward<UpdateFn>(fn));
  }

  // what is the allocator behavior here? we put it into our freelist
  /// @brief erase kv pair. Returns whether or not the erase was sucessful or
  /// not
  auto erase(worker_state &state, const Key &key) -> bool {
    list_t &l = get_list(key);
    return l.erase(state, key);
  }

  // not sure if this is a good idea.
  // we operate on the simplifying assumption that no workers leave
  // TODO: might be a good idea to get the worker from here (and so it's private
  // and we can't construct externally, to get the invariant everything is
  // initialized)
  auto register_worker(worker_state &state) -> void {
    state.index = m_workers.fetch_add(1);
    state.epoch_counter = &m_epoch;
  }

private:
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
      state.resource.deallocate(front->ptr, list_t::alloc_size);

      delete_list.erase(front);
    }
  }
  void minor_tick(worker_state &state) {
    state.ticks++;
    if (state.ticks == minors_per_major) [[unlikely]] {
      // refresh out ack epoch
      m_epochs[state.index].store(m_epoch.load(std::memory_order_acquire),
                                  std::memory_order_release);
      major_tick(state);
      state.ticks = 0;
    }
  }

  auto get_list(const Key &key) -> list_t & {
    uint64_t index = std::hash<Key>{}(key) % m_lists.size();
    return m_lists[index];
  }

  // do not grow though!
  std::vector<list_t> m_lists;
  tftf::atomic<size_t> m_size;
  tftf::atomic<uint64_t> m_epoch;
  static constexpr uint64_t minors_per_major{Traits::minor_ticks_per_major};

  static constexpr size_t max_workers{Traits::max_workers};
  // TODO: investigate cache alignment here
  std::array<tftf::atomic<uint64_t>, max_workers> m_epochs{};
  tftf::atomic<size_t> m_workers{0};
};
} // namespace tftf
