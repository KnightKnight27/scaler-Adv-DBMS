#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>

#include "catalog/catalog.h"
#include "common/tuple.h"
#include "execution/executor.h"
#include "execution/vectorized.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"
#include "storage/columnar.h"
#include "storage/disk_manager.h"

using namespace minidb;
using namespace std;
using namespace std::chrono;

static double NowMs() {
  return duration_cast<duration<double, milli>>(high_resolution_clock::now().time_since_epoch())
      .count();
}

int main() {
  // Columnar storage roundtrip.
  remove("/tmp/minidb_test_col.col");
  vector<Column> cols = {Column("a", TypeId::INTEGER), Column("b", TypeId::INTEGER),
                         Column("c", TypeId::BIGINT), Column("d", TypeId::BOOLEAN)};
  Schema schema(cols);
  {
    ColumnarFile cf("/tmp/minidb_test_col.col", schema);
    for (int i = 0; i < 100; ++i) {
      cf.AppendRow(Tuple({Value(i), Value(i * 2), Value((int64_t)i * 1000), Value(i % 2 == 0)}));
    }
  }
  ColumnarFile cf2("/tmp/minidb_test_col.col", schema);
  assert(cf2.NumRows() == 100);
  auto rows = cf2.Scan();
  assert(rows.size() == 100);
  assert(rows[7].GetValue(0).GetAsInteger() == 7);
  assert(rows[7].GetValue(1).GetAsInteger() == 14);
  assert(rows[7].GetValue(3).GetAsBoolean() == false);
  remove("/tmp/minidb_test_col.col");

  // Vectorized vs row-store scan on a TableHeap.
  remove("/tmp/minidb_test_vec.tbl");
  DiskManager dm("/tmp/minidb_test_vec.tbl");
  CatalogManager cat(&dm);
  assert(cat.CreateTable("v", Schema({Column("x", TypeId::INTEGER)})));
  TableHeap* t = cat.GetTable("v");
  for (int i = 0; i < 5000; ++i) {
    RecordId r;
    t->InsertTuple(Tuple({Value(i)}), &r);
  }
  assert(t->GetNumTuples() == 5000);

  int64_t rowSum = 0;
  int64_t rowCnt = 0;
  {
    SeqScanExecutor scan(nullptr, t);
    scan.Init();
    Tuple tup;
    while (scan.Next(&tup)) {
      rowSum += tup.GetValue(0).GetAsInteger();
      ++rowCnt;
    }
  }
  assert(rowCnt == 5000);
  assert(rowSum == 12497500);

  int64_t vecSum = 0;
  int64_t vecCnt = 0;
  {
    VectorizedSeqScanExecutor vscan(t, /*batchSize=*/1024);
    vscan.Init();
    ColumnBatch batch;
    while (vscan.NextBatch(&batch) > 0) {
      vecCnt += batch.Size();
      for (size_t i = 0; i < batch.Size(); ++i) {
        vecSum += batch.Row(i).GetValue(0).GetAsInteger();
      }
    }
  }
  assert(vecCnt == 5000);
  assert(vecSum == 12497500);

  cout << "test_vectorized passed\n";
  remove("/tmp/minidb_test_vec.tbl");
  return 0;
}