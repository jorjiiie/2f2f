
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
  tftf::node_resource<tftf::faster<int, int>::list_t::alloc_size> resource{buf};

  tftf::worker_state state{resource};
  f.register_worker(state);

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
  tftf::node_resource<tftf::faster<int, int>::list_t::alloc_size> resource{buf};

  tftf::worker_state state{resource};
  f.register_worker(state);

  std::cerr << "passed delete test!\n";
}

void basic_multithread_test() {

  double mn = 10000;

  constexpr size_t n_threads = 5;
  constexpr size_t n_inserts = 10000;

  for (size_t _k = 0; _k < 100; _k++) {

    tftf::faster<int, int> f{5000};

    constexpr int _M = n_threads * n_inserts;

    // as long as _M < 1e6 this is okay
    assert(_M < 1'000'000 && "probably too many inserts being tried");

    constexpr int _N = _M * 1000;

    std::atomic<size_t> done_count;
    std::atomic<bool> done_flag{false};

    auto insert_job = [&f, &done_count, &done_flag](uint32_t seed) {
      std::pmr::monotonic_buffer_resource buf{100000};
      tftf::node_resource<tftf::faster<int, int>::list_t::alloc_size> resource{
          buf};

      tftf::worker_state state{resource};
      std::mt19937 rng{seed};
      std::uniform_int_distribution<int> dist(0, _N);

      for (size_t i = 0; i < n_inserts; i++) {
        int k = dist(rng);
        int v = dist(rng);

        f.put(state, k, v);
      }

      done_count.fetch_add(1);
      while (!done_flag.load()) {
        std::this_thread::yield();
      }
    };

    // apple doesn't have jthread? 1984
    std::vector<std::thread> threads;

    for (uint32_t i = 0; i < n_threads; i++) {
      threads.push_back(std::thread{insert_job, i + _k * n_threads});
    }

    while (done_count.load() < n_threads) {
      std::this_thread::yield();
    }

    uint64_t correct_count{};
    for (uint32_t i = 0; i < n_threads; i++) {
      tftf::worker_state state{*std::pmr::get_default_resource()};
      std::uniform_int_distribution<int> dist(0, _N);
      std::mt19937 rng2{(uint32_t)(i + _k * n_threads)};
      for (size_t i = 0; i < n_inserts; i++) {
        int k = dist(rng2);
        int v = dist(rng2);

        if (auto x = f.get(state, k); x && *x == v) {
          correct_count++;
        }
      }
    }

    done_flag.store(true);

    for (auto &t : threads) {
      t.join();
    }

    // see blog post for an explaination
    assert(correct_count * 1.0 / n_inserts / n_threads > 0.999);
    mn = std::min(mn, correct_count * 1.0 / n_inserts / n_threads);
    // std::cerr << correct_count * 1.0 / n_inserts / n_threads << "\n";
  }

  std::cerr << "passed multi test 1!: for reference, min was " << mn << "\n";
}

struct eager_delete {

  static constexpr size_t max_workers = 1'024;
  static constexpr size_t minor_ticks_per_major = 1'000;
};
void basic_multithread_mixed_test() {
  // i don't know the proportion for this yet

  constexpr size_t n_threads = 5;
  constexpr size_t n_inserts = 10000;

  constexpr int _M = n_threads * n_inserts;

  constexpr double p_del = 0.05;

  // as long as _M < 1e6 this is okay
  assert(_M < 1'000'000 && "probably too many inserts being tried");

  constexpr int _N = _M * 1000;

  double mn = 1;
  double mx = 0;

  for (size_t _k = 0; _k < 100; _k++) {
    std::atomic<size_t> done_count;
    std::atomic<bool> done_flag{false};
    std::atomic<size_t> delete_good{0};
    std::atomic<size_t> delete_total{0};

    tftf::faster<int, int, eager_delete> f{5000};

    auto insert_job = [&f, &done_count, &done_flag, &delete_good,
                       &delete_total](uint32_t seed) {
      std::pmr::monotonic_buffer_resource buf{100000};
      tftf::node_resource<tftf::faster<int, int>::list_t::alloc_size> resource{
          buf};

      tftf::worker_state state{resource};
      f.register_worker(state);
      std::mt19937 rng{seed};
      std::mt19937 rng_lag{seed};
      std::mt19937 rng_junk{seed};
      std::uniform_int_distribution<int> dist(0, _N);
      std::uniform_real_distribution<double> dist_p(0, 1);

      for (size_t i = 0; i < n_inserts; i++) {
        double p = dist_p(rng_junk);

        if (p > p_del) {

          int k = dist(rng);
          int v = dist(rng);

          f.put(state, k, v);
        } else {
          int k = dist(rng_lag);
          dist(rng_lag);
          delete_total++;

          delete_good += f.erase(state, k);
        }
      }

      done_count.fetch_add(1);
      while (!done_flag.load()) {
        std::this_thread::yield();
      }
    };

    // apple doesn't have jthread? 1984
    std::vector<std::thread> threads;

    for (uint32_t i = 0; i < n_threads; i++) {
      threads.push_back(std::thread{insert_job, i + _k * n_threads});
    }

    while (done_count.load() < n_threads) {
      std::this_thread::yield();
    }

    uint64_t correct_count{};
    for (uint32_t i = 0; i < n_threads; i++) {
      tftf::worker_state state{*std::pmr::get_default_resource()};
      std::uniform_int_distribution<int> dist(0, _N);
      std::mt19937 rng2{(uint32_t)(i + _k * n_threads)};
      for (size_t i = 0; i < n_inserts; i++) {
        int k = dist(rng2);
        int v = dist(rng2);

        if (auto x = f.get(state, k); x && *x == v) {
          correct_count++;
        }
      }
    }

    done_flag.store(true);

    for (auto &t : threads) {
      t.join();
    }

    // see blog post for an explaination
    double acc = correct_count * 1.0 / n_inserts / n_threads;
    assert(acc > .89 && acc < .91);
    mn = std::min(mn, acc);
    mx = std::max(mx, acc);
  }
  std::cerr << "passed multi test 2! for reference, min was " << mn
            << ", max was " << mx << "\n";
}

auto main() -> int {
  alloc_test();
  integration_test();
  delete_heavy_test();
  basic_multithread_test();
  basic_multithread_mixed_test();

  std::cerr << "all tests passed!\n";
}
