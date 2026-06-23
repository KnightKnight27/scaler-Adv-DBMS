#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>

#include "index/b_plus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

int main() {
  std::remove("test_bptree.db");
  std::remove("test_bptree.db.wal");
  {
    DiskManager dm("test_bptree.db");
    BufferPool bpm(32, &dm);

    page_id_t root = BPlusTree::CreateEmpty(&bpm);
    BPlusTree tree(&bpm, root);

    const int N = 50;
    for (int i = 0; i < N; i++) {
      int64_t key = (i * 37) % N;  // scramble insertion order
      tree.Insert(key, RID{static_cast<page_id_t>(key), 0});
    }

    for (int64_t key = 0; key < N; key++) {
      auto res = tree.Search(key);
      assert(res.has_value());
      assert(res->page_id == key);
    }

    assert(!tree.Search(N + 5).has_value());

    assert(tree.Remove(10));
    assert(!tree.Search(10).has_value());
    assert(!tree.Remove(10));  // already gone

    // Other keys remain intact after deletion.
    assert(tree.Search(9).has_value());
    assert(tree.Search(11).has_value());

    bpm.FlushAllPages();
  }
  std::remove("test_bptree.db");
  std::remove("test_bptree.db.wal");
  std::cout << "bplustree_test PASSED\n";
  return 0;
}
