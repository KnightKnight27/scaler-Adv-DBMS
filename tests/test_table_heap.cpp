// Verifies tuple (de)serialization, slotted-page insert/get/delete via the
// table heap + iterator, and that the catalog persists schemas across a reopen.

#include <cstdio>
#include <set>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "storage/disk_manager.h"
#include "storage/table_heap.h"
#include "storage/tuple.h"
#include "tests/test_util.h"

using namespace minidb;

static Schema MakeSchema() {
  return Schema({Column("id", TypeId::INTEGER), Column("name", TypeId::VARCHAR, 32),
                 Column("year", TypeId::INTEGER)});
}

static Tuple MakeRow(const Schema &s, int id, const std::string &name, int year) {
  return Tuple({Value(id), Value(name), Value(year)}, s);
}

int main() {
  const std::string file = "test_heap.db";
  std::remove(file.c_str());
  Schema schema = MakeSchema();

  // --- tuple round-trip -----------------------------------------------------
  {
    Tuple t = MakeRow(schema, 42, "neo", 1999);
    CHECK_EQ(t.GetValue(schema, 0).GetInt(), 42);
    CHECK(t.GetValue(schema, 1).GetString() == "neo");
    CHECK_EQ(t.GetValue(schema, 2).GetInt(), 1999);
  }

  // --- heap insert / scan / delete, spanning many pages ---------------------
  std::set<int> expected;
  {
    DiskManager dm(file);
    BufferPoolManager bpm(16, &dm);
    Catalog catalog(&bpm);  // claims page 0
    TableMeta *meta = catalog.CreateTable("books", schema);
    CHECK(meta != nullptr);
    CHECK(catalog.CreateTable("books", schema) == nullptr);  // dup rejected
    TableHeap *heap = catalog.GetTableHeap("books");
    CHECK(heap != nullptr);

    const int N = 500;  // enough to force several pages
    for (int i = 0; i < N; i++) {
      RID rid;
      CHECK(heap->InsertTuple(MakeRow(schema, i, "row-" + std::to_string(i), 2000 + (i % 25)), &rid));
      expected.insert(i);
    }

    // Delete every 5th row.
    int deleted = 0;
    for (auto it = heap->Begin(); it != heap->End(); ++it) {
      Tuple t = *it;
      int id = t.GetValue(schema, 0).GetInt();
      if (id % 5 == 0) {
        CHECK(heap->MarkDelete(it.GetRID()));
        expected.erase(id);
        deleted++;
      }
    }
    CHECK_EQ(deleted, 100);

    // Scan must now yield exactly the survivors.
    std::set<int> seen;
    for (auto it = heap->Begin(); it != heap->End(); ++it) {
      seen.insert((*it).GetValue(schema, 0).GetInt());
    }
    CHECK(seen == expected);
    CHECK_EQ(static_cast<int>(seen.size()), 400);
  }

  // --- reopen: catalog + data must survive ----------------------------------
  {
    DiskManager dm(file);
    BufferPoolManager bpm(16, &dm);
    Catalog catalog(&bpm);  // loads page 0
    TableMeta *meta = catalog.GetTable("books");
    CHECK(meta != nullptr);
    CHECK_EQ(static_cast<int>(meta->schema.GetColumnCount()), 3);
    CHECK(meta->schema.GetColumn(1).name == "name");

    TableHeap *heap = catalog.GetTableHeap("books");
    std::set<int> seen;
    for (auto it = heap->Begin(); it != heap->End(); ++it) {
      seen.insert((*it).GetValue(schema, 0).GetInt());
    }
    CHECK(seen == expected);  // same survivors after restart
  }

  std::remove(file.c_str());
  return minidb::test::summary("table_heap");
}
