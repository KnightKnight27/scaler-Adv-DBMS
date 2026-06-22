#include "catch.hpp"

#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include "lsm/bloom_filter.h"
#include "lsm/lsm_storage_engine.h"

namespace fs = std::filesystem;
using namespace axiomdb;

namespace {
std::string make_dir(const char* tag) {
  std::string d = std::string("/tmp/wdb_lsm_") + tag + "_" + std::to_string(::getpid());
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
std::string key_of(int i) {
  char b[16];
  std::snprintf(b, sizeof(b), "k%08d", i);
  return b;
}
}  // namespace

TEST_CASE("BloomFilter: no false negatives", "[lsm][bloom]") {
  BloomFilter bf(1000);
  for (int i = 0; i < 1000; ++i) bf.add(key_of(i));
  for (int i = 0; i < 1000; ++i) REQUIRE(bf.maybe_contains(key_of(i)));  // never a false negative
  int fp = 0;
  for (int i = 1000; i < 5000; ++i) if (bf.maybe_contains(key_of(i))) ++fp;
  REQUIRE(fp < 400);  // false-positive rate should be modest (~1-5%)
}

TEST_CASE("LSM basic put/get/remove with tombstones", "[lsm]") {
  std::string d = make_dir("basic");
  {
    LsmStorageEngine lsm(d + "/db");
    REQUIRE(lsm.put("apple", "red").ok());
    REQUIRE(lsm.put("banana", "yellow").ok());
    REQUIRE(lsm.get("apple").value() == "red");
    REQUIRE(lsm.put("apple", "green").ok());          // overwrite
    REQUIRE(lsm.get("apple").value() == "green");
    REQUIRE(lsm.remove("banana").ok());
    REQUIRE_FALSE(lsm.get("banana").has_value());     // tombstoned
    REQUIRE_FALSE(lsm.get("missing").has_value());
  }
  fs::remove_all(d);
}

TEST_CASE("LSM flushes to SSTables and survives reopen", "[lsm]") {
  std::string d = make_dir("persist");
  const int N = 2000;
  {
    LsmStorageEngine lsm(d + "/db", /*threshold=*/4096);  // tiny -> forces many flushes
    for (int i = 0; i < N; ++i) lsm.put(key_of(i), "v" + std::to_string(i));
    REQUIRE(lsm.num_sstables() >= 1);
  }
  {
    LsmStorageEngine lsm(d + "/db", 4096);  // reopen: load SSTables + replay WAL
    for (int i = 0; i < N; ++i) {
      auto v = lsm.get(key_of(i));
      REQUIRE(v.has_value());
      REQUIRE(*v == "v" + std::to_string(i));
    }
  }
  fs::remove_all(d);
}

TEST_CASE("LSM newest value wins across memtable and SSTables", "[lsm]") {
  std::string d = make_dir("newest");
  {
    LsmStorageEngine lsm(d + "/db", 4096);
    for (int i = 0; i < 500; ++i) lsm.put(key_of(i), "old");
    lsm.flush();                                   // push to an LsmSSTable
    for (int i = 0; i < 500; ++i) lsm.put(key_of(i), "new");  // shadow in memtable
    for (int i = 0; i < 500; ++i) REQUIRE(lsm.get(key_of(i)).value() == "new");
    lsm.flush();
    for (int i = 0; i < 500; ++i) REQUIRE(lsm.get(key_of(i)).value() == "new");
  }
  fs::remove_all(d);
}

TEST_CASE("LSM compaction reduces LsmSSTable count and reclaims space", "[lsm][compaction]") {
  std::string d = make_dir("compact");
  {
    LsmStorageEngine lsm(d + "/db", 1u << 20);
    lsm.set_compaction_threshold(1000);  // disable auto-compaction for this test
    // Rewrite the same 300 keys many times, flushing each round -> 10 SSTables
    // each holding a full duplicate set (storage amplification).
    for (int round = 0; round < 10; ++round) {
      for (int i = 0; i < 300; ++i) lsm.put(key_of(i), "round" + std::to_string(round));
      lsm.flush();
    }
    REQUIRE(lsm.num_sstables() == 10);
    uint64_t before = lsm.disk_size();

    lsm.compact();
    REQUIRE(lsm.num_sstables() == 1);              // merged to a single table
    uint64_t after = lsm.disk_size();
    REQUIRE(after < before);                       // duplicates reclaimed

    // Correctness preserved: latest version of every key.
    for (int i = 0; i < 300; ++i) REQUIRE(lsm.get(key_of(i)).value() == "round9");
  }
  fs::remove_all(d);
}

TEST_CASE("LSM ordered range scan, deduped, tombstones filtered", "[lsm]") {
  std::string d = make_dir("scan");
  {
    LsmStorageEngine lsm(d + "/db", 4096);
    for (int i = 0; i < 1000; ++i) lsm.put(key_of(i), "v" + std::to_string(i));
    lsm.flush();
    for (int i = 0; i < 1000; i += 3) lsm.remove(key_of(i));  // delete every 3rd

    std::vector<std::string> got;
    for (auto it = lsm.scan(key_of(100), key_of(200)); it->valid(); it->next())
      got.push_back(std::string(it->key()));

    REQUIRE(!got.empty());
    REQUIRE(std::is_sorted(got.begin(), got.end()));
    for (const std::string& k : got) {
      int idx = std::stoi(k.substr(1));
      REQUIRE(idx >= 100);
      REQUIRE(idx < 200);
      REQUIRE(idx % 3 != 0);  // deleted keys excluded
    }
  }
  fs::remove_all(d);
}
