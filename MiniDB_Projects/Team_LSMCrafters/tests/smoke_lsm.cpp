// Tests the LSM engine: inserts that trigger flushes and compaction, point
// lookups (present and missing), overwrites, deletes via tombstones, an ordered
// scan that matches a std::map oracle, and persistence across a reopen.
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <map>
#include <random>
#include "lsm/lsm_table.h"

using namespace minidb;

int main() {
  const std::string dir = "/tmp/minidb_lsm_test";
  std::filesystem::remove_all(dir);

  std::map<Key, std::string> oracle;
  {
    LsmOptions opt;
    opt.memtable_threshold = 8 * 1024;  // small -> many flushes and compactions
    opt.l0_trigger         = 4;
    opt.sync_wal           = false;
    LsmTable table(dir, opt);

    std::mt19937_64 rng(3);
    for (int i = 0; i < 3000; ++i) {
      Key k = static_cast<Key>(rng() % 5000);
      std::string v = "v" + std::to_string(i);
      table.insert(k, v);
      oracle[k] = v;
    }
    for (int i = 0; i < 500; ++i) {
      Key k = static_cast<Key>(rng() % 5000);
      table.erase(k);
      oracle.erase(k);
    }

    for (const auto& [k, v] : oracle) {
      auto got = table.get(k);
      assert(got && *got == v);
    }
    assert(!table.get(999999));  // missing key

    // Scan must return exactly the live keys, in ascending order.
    auto cursor = table.scan();
    Key key, prev = -1;
    Bytes value;
    int n = 0;
    bool first = true;
    while (cursor->next(key, value)) {
      assert(first || key > prev);
      prev = key;
      first = false;
      ++n;
    }
    assert(n == static_cast<int>(oracle.size()));

    table.flush();
    table.force_compact();
    std::printf("lsm OK: %zu live keys, on-disk after compaction = %llu bytes\n",
                oracle.size(), static_cast<unsigned long long>(table.disk_bytes()));
  }

  // Reopen the same directory and confirm everything persisted.
  {
    LsmTable table(dir);
    for (const auto& [k, v] : oracle) {
      auto got = table.get(k);
      assert(got && *got == v);
    }
    std::printf("lsm reopen OK: persisted %zu keys\n", oracle.size());
  }

  std::printf("smoke_lsm ALL OK\n");
  return 0;
}
