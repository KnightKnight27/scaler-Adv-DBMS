// Track A: the vectorized engine must produce exactly the same result as the
// row-at-a-time baseline (and the known analytical answer).
#include <cstdio>
#include <string>

#include "engine/database.h"
#include "tests/test_util.h"
#include "vectorized/vectorized_engine.h"

using namespace minidb;

int main() {
  const std::string f = "test_vec.db";
  std::remove(f.c_str());
  std::remove((f + ".wal").c_str());

  Database db(f);
  db.Execute("CREATE TABLE t (id INT, v INT)");
  for (int base = 0; base < 2000; base += 100) {
    std::string sql = "INSERT INTO t VALUES ";
    for (int i = 0; i < 100; i++) {
      int id = base + i;
      sql += "(" + std::to_string(id) + "," + std::to_string(id * 2) + ")";
      if (i != 99) sql += ",";
    }
    db.Execute(sql);
  }

  // Expected: SUM(v) WHERE id < 1000, with v = 2*id  ->  2 * sum(0..999).
  long expected = 0;
  for (int id = 0; id < 1000; id++) expected += id * 2;

  TableHeap *heap = db.GetCatalog()->GetTableHeap("t");
  const Schema &schema = db.GetCatalog()->GetTable("t")->schema;
  VectorizedEngine ve(heap, db.GetBufferPool(), &schema);

  long vec = ve.FilterSum(/*filter_col=*/0, /*threshold=*/1000, /*sum_col=*/1);
  long row = ve.RowAtATimeFilterSum(0, 1000, 1);

  CHECK_EQ(vec, expected);
  CHECK_EQ(row, expected);
  CHECK_EQ(vec, row);

  std::remove(f.c_str());
  std::remove((f + ".wal").c_str());
  return minidb::test::summary("vectorized");
}
