
#include "allocator.hh"
#include "faster.hh"

#include <cassert>
#include <iostream>
#include <memory_resource>
#include <random>
#include <thread>

void alloc_test() {
  // extra for the freelist....
  void *buffer = malloc(1300);
  std::pmr::monotonic_buffer_resource buf{buffer, 1300,
                                          std::pmr::null_memory_resource()};

  tftf::node_resource<500> resource{buf};

  void *p1 = resource.allocate(500);
  void *p2 = resource.allocate(500);

  // we don't go more...
  try {
    void *p3 = resource.allocate(500);
    std::cerr << p3 << " " << p1 << "\n";
    assert(false && "should have failed");
  } catch (...) {
  }

  for (int i = 0; i < 100; i++) {
    resource.deallocate(p1, 500);
    resource.deallocate(p2, 500);
    void *p4 = resource.allocate(500);
    void *p3 = resource.allocate(500);
    assert(p1 == p3 && p2 == p4); // should be reusing the memory!
  }
  std::cerr << "passed all alloc tests!\n";
}

void integration_test() {
  tftf::faster<int, int> f;
  std::pmr::monotonic_buffer_resource buf{1000};
  tftf::node_resource<tftf::faster<int, int>::alloc_size> resource{buf};

  tftf::worker_state state{resource};

  f.put(state, 1, 2);

  {
    auto y = f.get(state, 1);
    if (y) {
      assert(2 == *y && "huh\n");
    } else {
      assert(false && "should have been equal");
    }
  }

  f.put(state, 1, 5);
  {

    auto y = f.get(state, 1);
    if (y) {
      assert(5 == *y && "huh\n");
    } else {
      assert(false && "should have been equal");
    }
  }

  for (int i = 0; i < 100; i++) {
    f.put(state, i, i);
  }
  for (int i = 99; i >= 0; i--) {
    auto y = f.get(state, i);
    assert(y);
    assert(*y == i);
  }

  auto square = [](const int i) { return i * i; };

  for (int i = 0; i < 100; i++) {
    auto z = f.update(state, i, square);
    assert(z);
    assert(*z == i);
  }

  for (int i = 99; i >= 0; i--) {
    auto y = f.get(state, i);
    assert(y);
    assert(*y == i * i);
  }

  for (int i = 0; i < 100; i++) {
    assert(f.erase(state, i));
  }
  for (int i = 0; i < 100; i++) {
    assert(!f.get(state, i));
  }
  std::cerr << "passed all integration tests!\n";
}

void delete_heavy_test() {

  tftf::faster<int, int> f;
  std::pmr::monotonic_buffer_resource buf{1000};
  tftf::node_resource<tftf::faster<int, int>::alloc_size> resource{buf};

  tftf::worker_state state{resource};

  std::cerr << "passed delete test!\n";
}

void basic_multithread_test() {

  tftf::faster<int, int> f;
  const size_t n_threads = 5;
  const size_t n_inserts = 1000;

  std::atomic<uint64_t> correct_count;

  auto insert_job = [&correct_count, &f](uint32_t seed) {
    std::pmr::monotonic_buffer_resource buf{100000};
    tftf::node_resource<tftf::faster<int, int>::alloc_size> resource{buf};

    tftf::worker_state state{resource};
    std::mt19937 rng{seed};
    std::uniform_int_distribution<int> dist(0, 10000000);

    for (size_t i = 0; i < n_inserts; i++) {
      int k = dist(rng);
      int v = dist(rng);

      f.put(state, k, v);
    }

    std::mt19937 rng2{seed};
    for (size_t i = 0; i < n_inserts; i++) {
      int k = dist(rng2);
      int v = dist(rng2);

      if (auto x = f.get(state, k); x && *x == v) {
        correct_count++;
      }
    }
  };

  {
    // apple doesn't have jthread? 1984
    std::vector<std::thread> threads;

    for (uint32_t i = 0; i < n_threads; i++) {
      threads.push_back(std::thread{insert_job, i});
    }

    for (auto &t : threads) {
      t.join();
    }

    // this seems reasonable to assume 90% of the things won't be contended (we
    // can probably get some guarantee...)
    assert(correct_count * 1.0 / n_inserts / n_threads > 0.9);
  }
  std::cerr << "passed multi test 1!\n";
}

auto main() -> int {
  alloc_test();
  integration_test();
  delete_heavy_test();
  basic_multithread_test();

  std::cerr << "all tests passed!\n";
}
