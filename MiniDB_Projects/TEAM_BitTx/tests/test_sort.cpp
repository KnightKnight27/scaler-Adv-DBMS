// Sort executor test for MiniDB.
#include <cassert>
#include <iostream>
#include <memory>

#include "catalog/table_heap.h"
#include "common/types.h"
#include "execution/executor.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_sort.tbl");
  DiskManager dm("/tmp/minidb_test_sort.tbl");
  vector<Column> cols = {Column("x", TypeId::INTEGER)};
  TableHeap table(&dm, Schema(cols));
  int32_t vals[] = {7, 1, 4, 9, 2, 8, 3, 5, 6, 0};
  for (int32_t v : vals) {
    RecordId r;
    table.InsertTuple(Tuple({Value(v)}), &r);
  }

  // ASC
  {
    auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
    SortExecutor sort(std::move(scan), 0, false);
    sort.Init();
    int32_t prev = -1;
    Tuple t;
    while (sort.Next(&t)) {
      int32_t v = t.GetValue(0).GetAsInteger();
      assert(v >= prev);
      prev = v;
    }
    assert(prev == 9);
  }

  // DESC
  {
    auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
    SortExecutor sort(std::move(scan), 0, true);
    sort.Init();
    int32_t prev = 100;
    Tuple t;
    while (sort.Next(&t)) {
      int32_t v = t.GetValue(0).GetAsInteger();
      assert(v <= prev);
      prev = v;
    }
    assert(prev == 0);
  }

  remove("/tmp/minidb_test_sort.tbl");
  cout << "ALL SORT TESTS PASSED" << endl;
  return 0;
}
