// Distinct executor test for MiniDB.
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
  remove("/tmp/minidb_test_dist.tbl");
  DiskManager dm("/tmp/minidb_test_dist.tbl");
  vector<Column> cols = {Column("x", TypeId::INTEGER)};
  TableHeap table(&dm, Schema(cols));
  int32_t vals[] = {1, 2, 2, 3, 1, 4, 3, 5, 5, 5};
  for (int32_t v : vals) {
    RecordId r;
    table.InsertTuple(Tuple({Value(v)}), &r);
  }

  auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
  DistinctExecutor dist(std::move(scan));
  dist.Init();
  int32_t count = 0;
  Tuple t;
  while (dist.Next(&t)) {
    ++count;
  }
  assert(count == 5);

  remove("/tmp/minidb_test_dist.tbl");
  cout << "ALL DISTINCT TESTS PASSED" << endl;
  return 0;
}
