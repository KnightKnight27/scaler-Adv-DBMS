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
  remove("/tmp/minidb_test_exec.tbl");

  DiskManager dm("/tmp/minidb_test_exec.tbl");
  vector<Column> cols = {Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR)};
  TableHeap table(&dm, Schema(cols));

  ExecutorContext ctx;
  vector<Value> row = {Value(1), Value("alice")};
  InsertExecutor ins(&ctx, &table, row);
  ins.Init();
  Tuple out;
  assert(ins.Next(&out));
  assert(out.GetValue(0).GetAsInteger() == 1);
  assert(!ins.Next(&out));

  remove("/tmp/minidb_test_exec.tbl");
  cout << "ALL INSERT EXECUTOR TESTS PASSED" << endl;
  return 0;
}