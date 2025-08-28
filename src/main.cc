#include "faster.hh"

#include <iostream>
#include <memory_resource>

auto main() -> int {
  tftf::faster<int, int> f;
  std::pmr::monotonic_buffer_resource buf{1000};

  tftf::worker_state state{buf};

  f.put(state, 1, 2);

  auto y = f.get(state, 1);
  if (y) {
    std::cerr << "found " << *y << "\n";
  } else {
    std::cerr << "why\n";
  }
}
