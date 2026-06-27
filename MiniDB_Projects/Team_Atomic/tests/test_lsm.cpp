// LSM-tree correctness: put/get with flushes, overwrite precedence (newer
// shadows older), tombstone deletes, range scan merge, compaction, and reopen.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include "lsm/lsm_tree.h"

using namespace minidb;

static void cleanup(const std::string& prefix) {
  for (int i = 0; i < 200; i++) std::remove((prefix + "_" + std::to_string(i) + ".sst").c_str());
  std::remove((prefix + ".manifest").c_str());
}

int main() {
  const std::string prefix = "t_lsm";
  cleanup(prefix);

  {
    // Small memtable + low compaction trigger so we exercise both quickly.
    LSMTree lsm(prefix, /*memtable_limit=*/100, /*compaction_trigger=*/3);

    const int N = 1000;
    for (int i = 0; i < N; i++) lsm.Put(i, "v" + std::to_string(i));

    // Point reads across memtable + many flushed SSTables.
    std::string val;
    for (int i = 0; i < N; i++) {
      assert(lsm.Get(i, &val) && val == "v" + std::to_string(i));
    }
    assert(!lsm.Get(N + 5, &val));  // absent

    std::cout << "  after " << N << " puts: flushes=" << lsm.Flushes()
              << " compactions=" << lsm.Compactions()
              << " sstables=" << lsm.NumSSTables() << "\n";
    assert(lsm.Flushes() > 0 && lsm.Compactions() > 0);

    // Overwrite: newer value must win even across flushes.
    lsm.Put(42, "updated");
    assert(lsm.Get(42, &val) && val == "updated");

    // Tombstone delete shadows the old value.
    lsm.Delete(42);
    assert(!lsm.Get(42, &val));
    lsm.Delete(7);
    assert(!lsm.Get(7, &val));

    // Range scan merges all sources, in order, skipping deleted keys.
    auto r = lsm.Scan(0, 9);
    // 0..9 minus deleted 7 = 9 entries, ascending.
    assert(r.size() == 9);
    for (size_t i = 0; i + 1 < r.size(); i++) assert(r[i].first < r[i + 1].first);
    for (auto& [k, v] : r) assert(k != 7);

    // Force a final compaction: deleted keys should be physically dropped.
    lsm.Flush();
    lsm.Compact();
    assert(!lsm.Get(42, &val) && !lsm.Get(7, &val));
    assert(lsm.Get(43, &val) && val == "v43");

    std::cout << "  disk_bytes=" << lsm.DiskBytes()
              << " live_bytes=" << lsm.LiveBytes() << "\n";
  }

  // Reopen from manifest: data must persist.
  {
    LSMTree lsm(prefix, 100, 3);
    std::string val;
    assert(lsm.Get(43, &val) && val == "v43");
    assert(!lsm.Get(7, &val));  // stayed deleted
  }

  cleanup(prefix);
  std::cout << "[lsm] put/get/overwrite/delete/scan/compaction/reopen OK\n";
  return 0;
}
