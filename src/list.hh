#include <atomic>
#include <optional>
#include <string>
#include <utility>

#include "state.hh"

namespace tftf {

template <class Key, class Value> struct node {
public:
  template <class _Key, class _Value>
  node(_Key &&_key, _Value &&_value)
      : m_key(std::forward<_Key>(_key)), m_value(std::forward<_Value>(_value)) {
  }
  const Key &key() const { return m_key; }
  std::atomic<Value> &value() { return m_value; }
  bool is_marked() const { return m_next.load(std::memory_order_acquire) & 1; }
  node *next() const {
    return reinterpret_cast<node *>(m_next.load(std::memory_order_acquire) &
                                    ~uintptr_t{1});
  }
  void set_next(node *n) {
    m_next.store(reinterpret_cast<uintptr_t>(n), std::memory_order_release);
  }
  void mark() { m_next.fetch_or(1); }
  bool cas_next(node *expected_next, node *new_next) {
    uintptr_t exp = reinterpret_cast<uintptr_t>(expected_next);
    uintptr_t nxt = reinterpret_cast<uintptr_t>(new_next);
    return m_next.compare_exchange_strong(exp, nxt, std::memory_order_release,
                                          std::memory_order_acquire);
  }
  bool cas_mark(node *expected_next) {
    uintptr_t exp = reinterpret_cast<uintptr_t>(expected_next);
    uintptr_t nxt = exp | 1;
    return m_next.compare_exchange_strong(exp, nxt, std::memory_order_release,
                                          std::memory_order_acquire);
  }
  std::pair<node *, bool> get_next_and_is_marked() const {
    uintptr_t nxt = m_next.load(std::memory_order_acquire);
    return std::make_pair(reinterpret_cast<node *>(nxt & ~uintptr_t{1}),
                          nxt & 1);
  }

private:
  template <class K, class V, class C> friend class list;
  Key m_key;
  std::atomic<Value> m_value;
  std::atomic<uintptr_t> m_next{0};
};

template <class Key, class Value, class Compare> class list {
public:
  using node_t = node<Key, Value>;
  static constexpr size_t alloc_size = sizeof(node_t);

  list() {
    head = static_cast<node_t *>(std::malloc(sizeof(node_t)));
    tail = static_cast<node_t *>(std::malloc(sizeof(node_t)));

    new (&(head->m_next)) std::atomic<uintptr_t>(0);
    new (&(tail->m_next)) std::atomic<uintptr_t>(0);

    head->set_next(tail);
  }

  ~list() {
    head->m_next.~atomic();
    tail->m_next.~atomic();

    std::free(head);
    std::free(tail);

    // TODO: walk list and delete real nodes with proper delete
  }

  template <class Key_, class Value_>
  auto put(worker_state &state, Key_ &&key, Value_ &&value) -> bool {
    void *new_mem = state.resource.allocate(alloc_size);

    node_t *new_node = new (new_mem)
        node_t(std::forward<Key_>(key), std::forward<Value_>(value));

    node_t *left, *right;

    do {
      right = search(state, new_node->key(), left);
      if ((right != tail) && (right->key() == new_node->key())) {
        right->value().store(
            std::move(new_node->value().load(std::memory_order_acquire)),
            std::memory_order_release);
        new_node->~node_t();
        state.resource.deallocate(new_mem, alloc_size);
        return false;
      }
      new_node->set_next(right);
      if (left->cas_next(right, new_node)) {
        return true;
      }
    } while (true);
  }

  template <class Key_, class Fn>
  auto update(worker_state &state, Key_ &&key, Fn &&f) -> std::optional<Value> {

    node_t *left, *right;

    do {
      right = search(state, key, left);
      if ((right != tail) && (right->key() == key)) {
        const Value old = right->value().load(std::memory_order_acquire);
        right->value().store(f(old), std::memory_order_release);
        return old;
      }
      return std::nullopt;
    } while (true);
  }
  auto erase(worker_state &state, const Key &key) -> bool {
    node_t *right, *right_next, *left;
    do {
      right = search(state, key, left);
      if ((right == tail) || (right->key() != key)) {
        return false;
      }
      right_next = right->next();
      // if right isn't already erased and we succcessfully cas it
      if (!right->is_marked() && right->cas_mark(right_next)) {
        break;
      }
    } while (true);
    // no idea what this does? seems like a compaction step
    if (!left->cas_next(right, right_next)) {
      right = search(state, right->key(), left);
    } else {
      // doesn't this just get leaked?
      uint64_t epoch =
          state.epoch_counter->fetch_add(1, std::memory_order_acquire);
      state.freelist_add({right, epoch});
    }
    return true;
  }
  auto find(worker_state &state, const Key &key) -> std::optional<Value> {
    node_t *left, *right;
    right = search(state, key, left);
    if ((right != tail) && (right->key() == key)) {
      return right->value();
    }
    return std::nullopt;
  }

private:
  // this kind of relies on the sentinel-ness here.
  node_t *head, *tail;

  node_t *search(worker_state &state, const Key &key, node_t *&left) {
    node_t *left_next;
    node_t *right;

    Compare cmp;

  again:
    do {
      node_t *t = head;
      bool t_is_marked;
      node_t *t_next;

      std::tie(t_next, t_is_marked) = t->get_next_and_is_marked();

      do {
        if (!t_is_marked) {
          left = t;
          left_next = t_next;
        }
        t = t_next;
        if (t == tail)
          break;
        std::tie(t_next, t_is_marked) = t->get_next_and_is_marked();
      } while (t_is_marked || cmp(t->key(), key));

      right = t;

      if (left_next == right) {
        if ((right != tail) && right->is_marked()) {
          goto again;
        }
        return right;
      }
      // cas all the dead things
      if (left->cas_next(left_next, right)) {
        if ((right != tail) && right->is_marked()) {
          goto again;
        }

        uint64_t epoch =
            state.epoch_counter->fetch_add(1, std::memory_order_acquire);
        while (left_next != right) {

          left = left_next->next();
          state.freelist_add({left_next, epoch});
          left_next = left;
        }
        return right;
      }

    } while (true);
  }
};

template class list<int, int, std::less<int>>;
} // namespace tftf
