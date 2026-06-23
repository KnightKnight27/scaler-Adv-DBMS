#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include "catalog/catalog.h"
#include "common/tuple.h"
#include "execution/executor.h"
#include "execution/vectorized.h"
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
  string dbPath = "/tmp/minidb_benchmark.tbl";
  remove(dbPath.c_str());

  DiskManager dm(dbPath);
  CatalogManager cat(&dm);

  // Define table schema
  vector<Column> cols = {
      Column("id", TypeId::INTEGER),
      Column("val", TypeId::INTEGER),
      Column("big_val", TypeId::BIGINT),
      Column("flag", TypeId::BOOLEAN)
  };
  Schema schema(cols);

  cout << "=== MiniDB Vectorization and Columnar Scan Benchmark ===" << endl;
  cout << "Creating table with 100,000 rows..." << endl;

  assert(cat.CreateTable("benchmark_table", schema));
  TableHeap* table = cat.GetTable("benchmark_table");

  // Populate table
  for (int i = 0; i < 100000; ++i) {
    RecordId r;
    Tuple t({
        Value(i),
        Value(i * 3),
        Value((int64_t)i * 1000),
        Value(i % 2 == 0)
    });
    table->InsertTuple(t, &r);
  }

  cout << "Table created. Running row-store sequential scan (SeqScanExecutor)..." << endl;

  // 1. Benchmark SeqScanExecutor
  double start = NowMs();
  int64_t rowSum = 0;
  size_t rowCount = 0;
  {
    SeqScanExecutor scan(nullptr, table);
    scan.Init();
    Tuple tup;
    while (scan.Next(&tup)) {
      rowSum += tup.GetValue(1).GetAsInteger();
      rowCount++;
    }
  }
  double seqTime = NowMs() - start;
  cout << "Row scan complete: " << rowCount << " rows processed. Time: " << seqTime << " ms" << endl;

  // 2. Benchmark VectorizedSeqScanExecutor with different batch sizes
  vector<size_t> batchSizes = {128, 512, 1024, 4096};
  vector<double> vecTimes;

  for (size_t batchSize : batchSizes) {
    cout << "Running vectorized columnar scan (batch size = " << batchSize << ")..." << endl;
    start = NowMs();
    int64_t vecSum = 0;
    size_t vecCount = 0;
    {
      VectorizedSeqScanExecutor vscan(table, batchSize);
      vscan.Init();
      ColumnBatch batch;
      while (vscan.NextBatch(&batch) > 0) {
        vecCount += batch.Size();
        for (size_t i = 0; i < batch.Size(); ++i) {
          vecSum += batch.Row(i).GetValue(1).GetAsInteger();
        }
      }
    }
    double vecTime = NowMs() - start;
    vecTimes.push_back(vecTime);
    assert(vecCount == rowCount);
    assert(vecSum == rowSum);
    cout << "Vectorized scan complete. Time: " << vecTime << " ms (Speedup: " << (seqTime / vecTime) << "x)" << endl;
  }

  // Print results in markdown format
  cout << "\n### Benchmark Results (100k Rows)\n" << endl;
  cout << "| Scan Type | Batch Size | Execution Time (ms) | Speedup vs Row-Store |" << endl;
  cout << "|-----------|------------|---------------------|----------------------|" << endl;
  printf("| Row-Store (SeqScan) | - | %.2f ms | 1.00x |\n", seqTime);
  for (size_t i = 0; i < batchSizes.size(); ++i) {
    printf("| Columnar (Vectorized) | %zu | %.2f ms | %.2fx |\n", batchSizes[i], vecTimes[i], seqTime / vecTimes[i]);
  }

  remove(dbPath.c_str());
  return 0;
}
