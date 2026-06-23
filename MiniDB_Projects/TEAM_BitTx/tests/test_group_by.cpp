// GROUP BY executor test for MiniDB.
#include <cassert>
#include <iostream>
#include <memory>
#include <unordered_map>

#include "catalog/table_heap.h"
#include "common/types.h"
#include "execution/executor.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_gb.tbl");
  DiskManager dm("/tmp/minidb_test_gb.tbl");
  vector<Column> cols = {Column("dept", TypeId::INTEGER), Column("sal", TypeId::INTEGER)};
  TableHeap table(&dm, Schema(cols));
  // 2 in dept 1, 3 in dept 2
  table.InsertTuple(Tuple({Value(1), Value(10)}), nullptr);
  table.InsertTuple(Tuple({Value(1), Value(20)}), nullptr);
  table.InsertTuple(Tuple({Value(2), Value(30)}), nullptr);
  table.InsertTuple(Tuple({Value(2), Value(40)}), nullptr);
  table.InsertTuple(Tuple({Value(2), Value(50)}), nullptr);

  auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
  GroupByExecutor gb(std::move(scan), {0}, {1}, {AggregateExecutor::AggType::SUM});
  gb.Init();
  unordered_map<int32_t, int64_t> sums;
  Tuple t;
  while (gb.Next(&t)) {
    int32_t dept = t.GetValue(0).GetAsInteger();
    int64_t s = t.GetValue(1).GetAsBigInt();
    sums[dept] = s;
  }
  assert(sums[1] == 30);
  assert(sums[2] == 120);
  assert(sums.size() == 2);

  // COUNT
  auto scan2 = std::make_unique<SeqScanExecutor>(nullptr, &table);
  GroupByExecutor gb2(std::move(scan2), {0}, {0}, {AggregateExecutor::AggType::COUNT});
  gb2.Init();
  unordered_map<int32_t, int32_t> counts;
  Tuple t2;
  while (gb2.Next(&t2)) {
    counts[t2.GetValue(0).GetAsInteger()] = t2.GetValue(1).GetAsInteger();
  }
  assert(counts[1] == 2);
  assert(counts[2] == 3);

  remove("/tmp/minidb_test_gb.tbl");
  cout << "ALL GROUP BY TESTS PASSED" << endl;
  return 0;
}
