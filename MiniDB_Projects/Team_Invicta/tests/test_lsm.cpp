// LSM-tree tests: put/get/overwrite/delete with tombstones, MemTable flushing,
// size-tiered compaction, merged range scans, and persistence across reopen.
#include <cstdio>
#include <filesystem>
#include <string>
#include "lsm/lsm_tree.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace minidb;

static std::string V(int i) { return "value-" + std::to_string(i); }

int main() {
  std::printf("test_lsm\n");
  std::string dir = "/tmp/minidb_lsm_dir";
  fs::remove_all(dir);

  const int N = 2000;
  {
    // Small MemTable limit + low compaction trigger to exercise flush/compact.
    LSMTree tree(dir, /*mem_limit=*/512, /*compaction_trigger=*/4);

    for (int i = 0; i < N; ++i) tree.Put(i, V(i));
    CHECK(tree.num_sstables() >= 1);  // flushing happened

    // Point reads across MemTable + many SSTables.
    std::string v;
    for (int i = 0; i < N; ++i) {
      CHECK(tree.Get(i, &v));
      CHECK_EQ(v, V(i));
    }
    CHECK(!tree.Get(N + 1, &v));  // absent (Bloom filters should skip files)

    // Overwrite: newest version wins.
    tree.Put(100, "updated");
    CHECK(tree.Get(100, &v));
    CHECK_EQ(v, std::string("updated"));

    // Delete: tombstone hides older versions, even across SSTables.
    for (int i = 0; i < N; i += 2) tree.Delete(i);
    for (int i = 0; i < N; ++i) {
      bool live = tree.Get(i, &v);
      CHECK_EQ(live, (i % 2 == 1));
    }

    // Merged scan returns only live keys, in sorted order.
    auto all = tree.ScanAll();
    CHECK_EQ(static_cast<int>(all.size()), N / 2);
    for (size_t i = 0; i + 1 < all.size(); ++i) CHECK(all[i].first < all[i + 1].first);

    auto rng = tree.Range(101, 199);
    CHECK_EQ(static_cast<int>(rng.size()), 50);  // odd keys 101..199

    CHECK_EQ(tree.LiveCount(), static_cast<size_t>(N / 2));
    int64_t mn, mx;
    CHECK(tree.KeyRange(&mn, &mx));
    CHECK_EQ(mn, 1);
    CHECK_EQ(mx, N - 1);

    tree.Flush();  // persist remaining MemTable
  }

  // Reopen: SSTables on disk are reloaded; data + deletes survive.
  {
    LSMTree tree(dir, 512, 4);
    std::string v;
    CHECK(tree.Get(101, &v));
    CHECK_EQ(v, std::string("value-101"));
    CHECK(!tree.Get(100, &v));   // 100 is even -> was deleted before shutdown
    CHECK_EQ(tree.LiveCount(), static_cast<size_t>(N / 2));
  }

  TEST_PASS();
}
