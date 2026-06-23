#include <cassert>
#include <cstdio>
#include <iostream>

#include "catalog/catalog.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

int main() {
  std::remove("test_catalog.db");
  std::remove("test_catalog.db.wal");
  std::remove("test_catalog.meta");
  {
    DiskManager dm("test_catalog.db");
    BufferPool bpm(16, &dm);
    Catalog cat(&bpm, "test_catalog.meta");

    std::vector<Column> cols = {
        {"id", TypeId::INTEGER, true},
        {"name", TypeId::VARCHAR, false},
    };
    TableInfo *t = cat.CreateTable("users", cols);
    assert(t != nullptr);
    assert(t->index != nullptr);

    Tuple tup({Value::Int(1), Value::Str("alice")});
    RID rid = t->heap->InsertTuple(tup.Serialize());
    t->index->Insert(1, rid);
    cat.NotifyRowInserted("users");

    auto found = t->index->Search(1);
    assert(found.has_value());
    std::string raw;
    t->heap->GetTuple(*found, &raw);
    Tuple decoded = Tuple::Deserialize(raw, t->schema);
    assert(decoded.Get(1).AsStr() == "alice");

    bpm.FlushAllPages();
  }
  {
    // Simulate restart: reopen catalog + db file, verify schema and data.
    DiskManager dm("test_catalog.db");
    BufferPool bpm(16, &dm);
    Catalog cat(&bpm, "test_catalog.meta");
    TableInfo *t = cat.GetTable("users");
    assert(t != nullptr);
    assert(t->schema.PrimaryKeyIndex() == 0);
    assert(t->num_rows == 1);

    auto found = t->index->Search(1);
    assert(found.has_value());
    std::string raw;
    t->heap->GetTuple(*found, &raw);
    Tuple decoded = Tuple::Deserialize(raw, t->schema);
    assert(decoded.Get(0).AsInt() == 1);
    assert(decoded.Get(1).AsStr() == "alice");
  }
  std::remove("test_catalog.db");
  std::remove("test_catalog.db.wal");
  std::remove("test_catalog.meta");
  std::cout << "catalog_test PASSED\n";
  return 0;
}
