#include "catch.hpp"

#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "engine/heap_btree_engine.h"
#include "engine/storage_engine.h"
#include "lsm/lsm_engine.h"

namespace fs = std::filesystem;
using namespace walterdb;

namespace {
std::string key_of(int i) {
  char b[16];
  std::snprintf(b, sizeof(b), "k%08d", i);
  return b;
}

// The same workload, driven entirely through the abstract StorageEngine API --
// this is exactly what makes the Track-C benchmark a clean A/B swap.
void exercise(StorageEngine& eng) {
  const int N = 2000;
  for (int i = 0; i < N; ++i) REQUIRE(eng.put(key_of(i), "val" + std::to_string(i)).ok());

  for (int i = 0; i < N; ++i) {
    auto v = eng.get(key_of(i));
    REQUIRE(v.has_value());
    REQUIRE(*v == "val" + std::to_string(i));
  }
  REQUIRE_FALSE(eng.get("nope").has_value());

  // Overwrite + delete.
  REQUIRE(eng.put(key_of(5), "updated").ok());
  REQUIRE(eng.get(key_of(5)).value() == "updated");
  REQUIRE(eng.remove(key_of(7)).ok());
  REQUIRE_FALSE(eng.get(key_of(7)).has_value());

  // Ordered range scan over [k100, k110).
  std::vector<std::string> keys;
  for (auto it = eng.scan(key_of(100), key_of(110)); it->valid(); it->next())
    keys.emplace_back(it->key());
  REQUIRE(keys.size() == 10);
  REQUIRE(std::is_sorted(keys.begin(), keys.end()));
  REQUIRE(keys.front() == key_of(100));
  REQUIRE(keys.back() == key_of(109));

  eng.flush();
}
}  // namespace

TEST_CASE("HeapBTreeEngine satisfies the StorageEngine contract", "[engine][kv]") {
  std::string dir = "/tmp/wdb_kv_heap_" + std::to_string(::getpid());
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    HeapBTreeEngine eng(dir + "/db.wdb");
    REQUIRE(eng.name() == "HeapBTree");
    exercise(eng);
  }
  fs::remove_all(dir);
}

TEST_CASE("LSMEngine satisfies the StorageEngine contract", "[engine][kv]") {
  std::string dir = "/tmp/wdb_kv_lsm_" + std::to_string(::getpid());
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    LSMEngine eng(dir + "/db", 8192);
    REQUIRE(eng.name() == "LSM");
    exercise(eng);
  }
  fs::remove_all(dir);
}
