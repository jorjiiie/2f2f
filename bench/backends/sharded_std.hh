#pragma once

#include "../include/bench/backend.hh"

#include <cstddef>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace bench {

class sharded_std_backend {
public:
  explicit sharded_std_backend(std::size_t shard_count)
      : shards(shard_count) {}

  struct worker {
    explicit worker(sharded_std_backend &) {}
  };

  auto get(worker &, int key, int &value) -> bool {
    auto &shard = get_shard(key);
    std::shared_lock lock(shard.mutex);
    const auto it = shard.values.find(key);
    if (it == shard.values.end()) {
      return false;
    }
    value = it->second;
    return true;
  }

  auto put(worker &, int key, int value) -> bool {
    auto &shard = get_shard(key);
    std::unique_lock lock(shard.mutex);
    return shard.values.insert_or_assign(key, value).second;
  }

  auto erase(worker &, int key) -> bool {
    auto &shard = get_shard(key);
    std::unique_lock lock(shard.mutex);
    return shard.values.erase(key) != 0;
  }

private:
  struct shard {
    std::shared_mutex mutex;
    std::unordered_map<int, int> values;
  };

  auto get_shard(int key) -> shard & {
    return shards[std::hash<int>{}(key) % shards.size()];
  }

  std::vector<shard> shards;
};

} // namespace bench
