// B+ tree tests: insert (with multi-level splits), point search, range scan,
// delete, duplicate rejection, and persistence across reopen.
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>
#include "index/bplus_tree.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "test_util.h"

using namespace minidb;

int main() {
  std::printf("test_index\n");
  std::remove(TmpFile("index.db").c_str());

  const int N = 5000;
  // Insert keys in shuffled order to force splits and root growth.
  std::vector<int> keys(N);
  for (int i = 0; i < N; ++i) keys[i] = i;
  std::mt19937 rng(12345);
  std::shuffle(keys.begin(), keys.end(), rng);

  page_id_t header = INVALID_PAGE_ID;
  {
    DiskManager disk(TmpFile("index.db"));
    BufferPoolManager bpm(&disk, /*pool_size=*/16);  // small pool: exercise eviction
    BPlusTree tree(&bpm, &header);

    for (int k : keys) {
      RID r{static_cast<page_id_t>(k / 100), static_cast<slot_id_t>(k % 100)};
      CHECK(tree.Insert(k, r));
    }
    // Duplicate insert must be rejected.
    CHECK(!tree.Insert(keys[0], RID{0, 0}));

    // Point search every key.
    for (int k = 0; k < N; ++k) {
      RID r;
      CHECK(tree.GetValue(k, &r));
      CHECK_EQ(r.page_id, k / 100);
      CHECK_EQ(r.slot, k % 100);
    }
    RID miss;
    CHECK(!tree.GetValue(N + 10, &miss));

    // Range scan returns keys in sorted order.
    auto rng_res = tree.Range(1000, 1099);
    CHECK_EQ(static_cast<int>(rng_res.size()), 100);
    for (size_t i = 0; i < rng_res.size(); ++i) CHECK_EQ(rng_res[i].first, 1000 + (int)i);

    // Delete the even keys; odd keys remain searchable, evens are gone.
    for (int k = 0; k < N; k += 2) CHECK(tree.Delete(k));
    for (int k = 0; k < N; ++k) {
      RID r;
      bool found = tree.GetValue(k, &r);
      CHECK_EQ(found, (k % 2 == 1));
    }
    bpm.FlushAll();
  }

  // Reopen: tree state persisted via the durable header/root pages.
  {
    DiskManager disk(TmpFile("index.db"));
    BufferPoolManager bpm(&disk, 16);
    BPlusTree tree(&bpm, &header);  // header id reused
    RID r;
    CHECK(tree.GetValue(2501, &r));   // odd key survives
    CHECK(!tree.GetValue(2500, &r));  // even key was deleted
    auto rng_res = tree.Range(0, N);
    CHECK_EQ(static_cast<int>(rng_res.size()), N / 2);  // only odds remain
  }

  TEST_PASS();
}
