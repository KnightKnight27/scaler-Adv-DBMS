// Storage-engine tests: disk manager, buffer pool + LRU eviction, slotted
// pages, and the table heap (including a scan that survives eviction/reload).
#include <cstdio>
#include <string>
#include <vector>
#include "record/schema.h"
#include "record/tuple.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"
#include "test_util.h"

using namespace minidb;

static Schema MakeSchema() {
  return Schema({{"id", TypeId::INTEGER, true}, {"name", TypeId::VARCHAR, false}});
}

int main() {
  std::printf("test_storage\n");
  std::remove(TmpFile("storage.db").c_str());

  Schema schema = MakeSchema();

  // Use a deliberately tiny buffer pool so a large insert/scan forces eviction
  // and reload through the disk manager.
  {
    DiskManager disk(TmpFile("storage.db"));
    BufferPoolManager bpm(&disk, /*pool_size=*/4);

    page_id_t first = INVALID_PAGE_ID;
    TableHeap heap(&bpm, &first);

    const int N = 2000;  // many pages; far exceeds 4 frames
    std::vector<RID> rids;
    for (int i = 0; i < N; ++i) {
      Tuple t({Value::Int(i), Value::Str("row-" + std::to_string(i))});
      rids.push_back(heap.InsertTuple(t.Serialize(schema)));
    }

    // Point reads by RID after eviction churn.
    for (int i = 0; i < N; i += 137) {
      std::string bytes;
      CHECK(heap.GetTuple(rids[i], &bytes));
      Tuple t = Tuple::Deserialize(schema, bytes);
      CHECK_EQ(t.value(0).i, i);
      CHECK_EQ(t.value(1).s, std::string("row-" + std::to_string(i)));
    }

    // Full sequential scan returns every row exactly once.
    int count = 0;
    for (auto it = heap.Begin(); !it.AtEnd(); it.Advance()) {
      Tuple t = Tuple::Deserialize(schema, it.value());
      CHECK(t.value(0).i >= 0 && t.value(0).i < N);
      ++count;
    }
    CHECK_EQ(count, N);

    // Delete half and confirm a re-scan sees exactly the survivors.
    for (int i = 0; i < N; i += 2) CHECK(heap.DeleteTuple(rids[i]));
    int after = 0;
    for (auto it = heap.Begin(); !it.AtEnd(); it.Advance()) ++after;
    CHECK_EQ(after, N / 2);

    bpm.FlushAll();
  }

  // Reopen the file: persistence across a fresh disk/buffer-pool instance.
  {
    DiskManager disk(TmpFile("storage.db"));
    BufferPoolManager bpm(&disk, 4);
    page_id_t first = 0;  // heap rooted at page 0
    TableHeap heap(&bpm, &first);
    int live = 0;
    for (auto it = heap.Begin(); !it.AtEnd(); it.Advance()) ++live;
    CHECK_EQ(live, 1000);  // the surviving odd-indexed rows
  }

  TEST_PASS();
}
