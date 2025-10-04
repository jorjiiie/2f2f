/*
 * bench.cpp
 * the main problem with benching is that faster is thread-aware in the sense
 * that it has central state
 * a way to do this is to give a "task" to a giant worker where a worker is an
 * instance of faster or a generic table that has some work it must do.
 *
 * however, this is not really a fair benchmark. but i guess what can you do?
 *
 * in classic rz faster style, this bench will be a split of read, upsert,
 * read-modify-write, and delete ops defined by a ratio.
 *
 * the benchmark is technically not fair because of different numbers of
 * threads, but it is also not fair because of threading nondeterminism. so we
 * assume the law of large numbers and hope that the benchmark is expected fair.
 * what more can you ask for
 */

#include <atomic>
#include <format>
#include <iostream>

// what's the format of this?
// we have a TABLE and we want to construct it?
// like we have a few setups
//

namespace tftf {

struct BenchResults {};
template <class TableBench> BenchResults benchmark(TableBench bench) {
  typename TableBench::Table table = bench.get_table();
}
} // namespace tftf
