// Aggregate executor test for MiniDB.
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
  remove("/tmp/minidb_test_agg.tbl");
  DiskManager dm("/tmp/minidb_test_agg.tbl");
  vector<Column> cols = {Column("x", TypeId::INTEGER)};
  TableHeap table(&dm, Schema(cols));
  for (int32_t i = 1; i <= 10; ++i) {
    RecordId r;
    table.InsertTuple(Tuple({Value(i)}), &r);
  }

  // SUM
  {
    auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
    AggregateExecutor agg(std::move(scan), AggregateExecutor::AggType::SUM, 0);
    agg.Init();
    Tuple out;
    assert(agg.Next(&out));
    assert(out.GetValue(0).GetTypeId() == TypeId::BIGINT);
    int64_t sum = out.GetValue(0).GetAsBigInt();
    assert(sum == 55);
  }

  // COUNT
  {
    auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
    AggregateExecutor agg(std::move(scan), AggregateExecutor::AggType::COUNT, 0);
    agg.Init();
    Tuple out;
    assert(agg.Next(&out));
    int32_t count = out.GetValue(0).GetAsInteger();
    assert(count == 10);
  }

  // MIN
  {
    auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
    AggregateExecutor agg(std::move(scan), AggregateExecutor::AggType::MIN, 0);
    agg.Init();
    Tuple out;
    assert(agg.Next(&out));
    int32_t m = out.GetValue(0).GetAsInteger();
    assert(m == 1);
  }

  // MAX
  {
    auto scan = std::make_unique<SeqScanExecutor>(nullptr, &table);
    AggregateExecutor agg(std::move(scan), AggregateExecutor::AggType::MAX, 0);
    agg.Init();
    Tuple out;
    assert(agg.Next(&out));
    int32_t m = out.GetValue(0).GetAsInteger();
    assert(m == 10);
  }

  remove("/tmp/minidb_test_agg.tbl");
  cout << "ALL AGGREGATE TESTS PASSED" << endl;
  return 0;
}
