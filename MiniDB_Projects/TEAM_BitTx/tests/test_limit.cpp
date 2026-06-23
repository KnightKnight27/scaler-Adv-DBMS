// Limit executor test for MiniDB.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>

#include "catalog/table_heap.h"
#include "common/types.h"
#include "execution/executor.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_limit.tbl");
  DiskManager dm("/tmp/minidb_test_limit.tbl");
  vector<Column> cols = {Column("x", TypeId::INTEGER)};
  TableHeap table(&dm, Schema(cols));
  for (int32_t i = 1; i <= 100; ++i) {
    RecordId r;
    table.InsertTuple(Tuple({Value(i)}), &r);
  }

  auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
  LimitExecutor lim(std::move(scan), 5);
  lim.Init();
  int32_t count = 0;
  int32_t last = 0;
  Tuple t;
  while (lim.Next(&t)) {
    ++count;
    last = t.GetValue(0).GetAsInteger();
  }
  assert(count == 5);
  assert(last == 5);

  auto scan2 = std::make_unique<SeqScanExecutor>(nullptr, &table);
  LimitExecutor lim0(std::move(scan2), 0);
  lim0.Init();
  Tuple t0;
  assert(!lim0.Next(&t0));

  auto scan3 = std::make_unique<SeqScanExecutor>(nullptr, &table);
  LimitExecutor limBig(std::move(scan3), 1000);
  limBig.Init();
  int32_t c2 = 0;
  Tuple t2;
  while (limBig.Next(&t2))
    ++c2;
  assert(c2 == 100);

  remove("/tmp/minidb_test_limit.tbl");
  cout << "ALL LIMIT TESTS PASSED" << endl;
  return 0;
}
