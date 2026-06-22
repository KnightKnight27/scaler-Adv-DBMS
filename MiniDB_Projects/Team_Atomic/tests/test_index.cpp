// Tests the B+ tree index (search/insert/delete/range, multi-level splits)
// and the table heap (insert/scan/delete) over the buffer pool.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <set>
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/table_heap.h"
#include "index/bplus_tree.h"
#include "record/schema.h"
#include "record/tuple.h"

using namespace minidb;

static void TestBPlusTree(BufferPoolManager& bpm) {
  page_id_t hdr = BPlusTree::Create(&bpm);
  BPlusTree tree(&bpm, hdr);

  const int N = 5000;
  // Insert keys in a scrambled order to exercise splits at all positions.
  for (int i = 0; i < N; i++) {
    int k = (i * 2654435761u) % N;  // pseudo-shuffle, distinct mod N? not quite
    (void)k;
  }
  // Use a guaranteed-distinct shuffle: odd stride coprime with N.
  std::set<int> inserted;
  for (int i = 0; i < N; i++) {
    int k = static_cast<int>((static_cast<long long>(i) * 7919) % N);
    while (inserted.count(k)) k = (k + 1) % N;
    inserted.insert(k);
    tree.Insert(k, RID{k + 100, k % 50});
  }

  // Point search every key.
  for (int k = 0; k < N; k++) {
    RID rid;
    bool ok = tree.Search(k, &rid);
    assert(ok && rid.page_id == k + 100 && rid.slot_id == k % 50);
  }
  // Missing keys.
  RID dummy;
  assert(!tree.Search(N + 1, &dummy));

  // Range scan [1000, 1099] -> 100 keys in order.
  auto r = tree.Range(1000, 1099);
  assert(r.size() == 100);
  for (size_t i = 0; i + 1 < r.size(); i++) assert(r[i].first < r[i + 1].first);
  assert(r.front().first == 1000 && r.back().first == 1099);

  // Delete half the keys; survivors still found, deleted ones gone.
  for (int k = 0; k < N; k += 2) assert(tree.Delete(k));
  for (int k = 0; k < N; k++) {
    RID rid;
    bool found = tree.Search(k, &rid);
    assert(found == (k % 2 == 1));
  }
  std::cout << "[index] B+ tree: " << N
            << " keys insert/search/range/delete OK\n";
}

static void TestTableHeap(BufferPoolManager& bpm) {
  Schema schema({{"id", TypeId::INTEGER}, {"name", TypeId::VARCHAR}}, 0);
  page_id_t first = TableHeap::Create(&bpm);
  TableHeap heap(&bpm, first);

  std::vector<RID> rids;
  const int M = 2000;  // enough rows to span many pages
  for (int i = 0; i < M; i++) {
    Tuple t({Value::Int(i), Value::Str("name_" + std::to_string(i))});
    rids.push_back(heap.InsertTuple(t.Serialize(schema)));
  }
  // Random-access read back.
  for (int i = 0; i < M; i++) {
    std::string bytes;
    assert(heap.GetTuple(rids[i], &bytes));
    Tuple t = Tuple::Deserialize(bytes.data(), schema);
    assert(t.GetValue(0).i == i);
    assert(t.GetValue(1).s == "name_" + std::to_string(i));
  }
  // Full scan counts all live rows.
  int count = 0;
  for (auto it = heap.Begin(); !it.IsEnd(); it.Next()) count++;
  assert(count == M);

  // Delete evens; scan should see M/2.
  for (int i = 0; i < M; i += 2) assert(heap.DeleteTuple(rids[i]));
  count = 0;
  for (auto it = heap.Begin(); !it.IsEnd(); it.Next()) count++;
  assert(count == M / 2);
  std::cout << "[index] table heap: " << M
            << " rows insert/get/scan/delete OK\n";
}

int main() {
  const char* path = "test_index.db";
  std::remove(path);
  DiskManager dm(path);
  BufferPoolManager bpm(64, &dm);
  TestBPlusTree(bpm);
  TestTableHeap(bpm);
  bpm.FlushAll();
  std::remove(path);
  std::cout << "[index] OK\n";
  return 0;
}
