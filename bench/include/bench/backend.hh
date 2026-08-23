#pragma once

#include <concepts>
#include <cstddef>

namespace bench {

template <class Backend>
concept map_backend = requires(Backend &backend,
                               typename Backend::worker &worker, int key,
                               int value) {
  typename Backend::worker;
  { typename Backend::worker{backend} };
  { backend.get(worker, key, value) } -> std::same_as<bool>;
  { backend.put(worker, key, value) } -> std::same_as<bool>;
  { backend.erase(worker, key) } -> std::same_as<bool>;
};

} // namespace bench
