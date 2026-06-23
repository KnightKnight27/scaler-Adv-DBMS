// Tests the M2 layer: the B+Tree (against a std::map oracle) and HeapTable used
// purely through the StorageEngine interface (insert/get/erase/scan/index_range).
#include <cassert>
#include <cstdio>
#include <map>
#include <random>
#include "index/bplus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_table.h"

using namespace minidb;

static void test_bplus_tree_against_map() {
  BPlusTree tree;
  std::map<Key, RID> oracle;
  std::mt19937_64 rng(7);
  for (int i = 0; i < 2000; ++i) {
    Key k = static_cast<Key>(rng() % 5000);
    RID rid{static_cast<PageId>(i), 0};
    tree.insert(k, rid);
    oracle[k] = rid;
  }
  for (const auto& [k, rid] : oracle) {
    auto found = tree.search(k);
    assert(found && found->page_id == rid.page_id);
  }
  // Range [1000, 2000] must match the oracle exactly, in order.
  auto r = tree.range(1000, 2000);
  std::vector<Key> expect;
  for (const auto& [k, _] : oracle)
    if (k >= 1000 && k <= 2000) expect.push_back(k);
  assert(r.size() == expect.size());
  for (std::size_t i = 0; i < r.size(); ++i) assert(r[i].first == expect[i]);
  std::printf("bplus_tree oracle OK: %zu keys\n", oracle.size());
}

static Bytes row_bytes(Key k) { return "row#" + std::to_string(k); }

static void test_heap_table() {
  const std::string path = "/tmp/minidb_table.db";
  std::remove(path.c_str());
  DiskManager disk(path);
  BufferPool pool(disk);

  std::unique_ptr<HeapTable> table = make_heap_table(pool);
  StorageEngine& store = *table;  // use only the abstract interface below

  const int kRows = 1000;
  for (int i = 0; i < kRows; ++i) store.insert(i, row_bytes(i));
  assert(store.stats().row_count == kRows);

  // Point lookups via the B+Tree index.
  assert(store.get(42) && *store.get(42) == row_bytes(42));
  assert(!store.get(999999));

  // Overwrite (upsert) keeps the row count stable.
  store.insert(42, "updated");
  assert(*store.get(42) == "updated");
  assert(store.stats().row_count == kRows);

  // Range scan over the index.
  auto cur = store.index_range(100, 109);
  int range_seen = 0;
  Key k; Bytes v;
  while (cur->next(k, v)) { assert(k >= 100 && k <= 109); ++range_seen; }
  assert(range_seen == 10);

  // Delete then confirm absence and full-scan count.
  store.erase(42);
  assert(!store.get(42));
  auto full = store.scan();
  int seen = 0;
  while (full->next(k, v)) ++seen;
  assert(seen == kRows - 1);

  std::printf("heap_table OK: %d rows, range=%d, after delete=%d\n", kRows, range_seen, seen);
}

int main() {
  test_bplus_tree_against_map();
  test_heap_table();
  std::printf("smoke_table ALL OK\n");
  return 0;
}
