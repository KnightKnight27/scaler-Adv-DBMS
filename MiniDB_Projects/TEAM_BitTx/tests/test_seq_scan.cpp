#include "catalog/table_heap.h"
#include "common/types.h"
#include "execution/executor.h"
#include "storage/disk_manager.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_scan.tbl");

  DiskManager dm("/tmp/minidb_test_scan.tbl");
  vector<Column> cols = {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)};
  TableHeap table(&dm, Schema(cols));

  RecordId r1, r2, r3;
  assert(table.InsertTuple(Tuple({Value(1), Value("alice")}), &r1));
  assert(table.InsertTuple(Tuple({Value(2), Value("bob")}), &r3));
  assert(table.InsertTuple(Tuple({Value(3), Value("carol")}), &r2));

  ExecutorContext ctx;
  SeqScanExecutor scan(&ctx, &table);
  scan.Init();
  int count = 0;
  Tuple t;
  while (scan.Next(&t)) {
    ++count;
  }
  assert(count == 3);

  // Filter by id == 2
  FilterExecutor filter(make_unique<SeqScanExecutor>(&ctx, &table),
                        [](const Tuple& t) { return t.GetValue(0).GetAsInteger() == 2; });
  filter.Init();
  count = 0;
  while (filter.Next(&t)) {
    assert(t.GetValue(1).GetAsVarchar() == "bob");
    ++count;
  }
  assert(count == 1);

  remove("/tmp/minidb_test_scan.tbl");
  cout << "ALL SEQ SCAN TESTS PASSED" << endl;
  return 0;
}