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
  remove("/tmp/minidb_test_join_a.tbl");
  remove("/tmp/minidb_test_join_b.tbl");

  DiskManager dma("/tmp/minidb_test_join_a.tbl");
  vector<Column> aCols = {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)};
  TableHeap a(&dma, Schema(aCols));

  RecordId r;
  assert(a.InsertTuple(Tuple({Value(1), Value("alice")}), &r));
  assert(a.InsertTuple(Tuple({Value(2), Value("bob")}), &r));
  assert(a.InsertTuple(Tuple({Value(3), Value("carol")}), &r));

  DiskManager dmb("/tmp/minidb_test_join_b.tbl");
  vector<Column> bCols = {Column("uid", TypeId::INTEGER), Column("dept", TypeId::VARCHAR)};
  TableHeap b(&dmb, Schema(bCols));

  assert(b.InsertTuple(Tuple({Value(1), Value("eng")}), &r));
  assert(b.InsertTuple(Tuple({Value(2), Value("hr")}), &r));
  assert(b.InsertTuple(Tuple({Value(4), Value("ops")}), &r));

  ExecutorContext ctx;
  auto left = make_unique<SeqScanExecutor>(&ctx, &a);
  auto right = make_unique<SeqScanExecutor>(&ctx, &b);
  NestedLoopJoinExecutor join(&ctx, move(left), move(right), (size_t)-1, (size_t)-1);
  join.Init();
  int count = 0;
  Tuple t;
  while (join.Next(&t)) {
    ++count;
  }
  assert(count > 0);
  cout << "test_join passed: " << count << " rows\n";
  return 0;
}
