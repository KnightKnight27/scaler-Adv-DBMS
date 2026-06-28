#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "minidb/buffer/buffer_pool.h"
#include "minidb/common/types.h"
#include "minidb/execution/execution_engine.h"
#include "minidb/index/b_plus_tree.h"
#include "minidb/sql/parser.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/storage/heap_table.h"
#include "minidb/storage/page_manager.h"

namespace {

using namespace minidb;

std::filesystem::path TempDb(const std::string& name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove(path);
  return path;
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw MiniDbError("test failed: " + message);
  }
}

void TestPageManagerAllocatesAndPersistsPages() {
  auto path = TempDb("minidb_m1_pages.db");
  PageId page_id = kInvalidPageId;

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    PageManager pages(buffer);
    page_id = pages.AllocateHeapPage();
    Page& page = pages.FetchPage(page_id);
    auto slot = SlottedPage(page).Insert("hello");
    Require(slot.has_value(), "record should fit in fresh page");
    pages.Unpin(page_id, true);
    pages.FlushAll();
  }

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    Page& page = buffer.FetchPage(page_id);
    auto record = SlottedPage(page).Get(0);
    Require(record.has_value() && *record == "hello", "page record should survive reopen");
    buffer.UnpinPage(page_id, false);
  }
}

void TestHeapTableInsertScanDelete() {
  auto path = TempDb("minidb_m1_heap.db");
  DiskManager disk(path);
  BufferPool buffer(disk, 3);
  PageId first_page = HeapTable::Create(buffer);
  HeapTable table(buffer, first_page);

  Rid alice = table.Insert(EncodeRow({"1", "Ada"}));
  table.Insert(EncodeRow({"2", "Grace"}));
  Require(table.Get(alice).has_value(), "inserted rid should be fetchable");
  Require(DecodeRow(*table.Get(alice))[1] == "Ada", "fetched row should decode");

  auto rows = table.Scan();
  Require(rows.size() == 2, "scan should return two live rows");
  Require(table.Delete(alice), "delete should mark row deleted");
  rows = table.Scan();
  Require(rows.size() == 1, "scan should skip deleted rows");
  Require(DecodeRow(rows[0].second)[1] == "Grace", "remaining row should be Grace");
}

void TestBufferPoolEvictsDirtyPages() {
  auto path = TempDb("minidb_m1_buffer.db");
  PageId first = kInvalidPageId;
  PageId second = kInvalidPageId;
  PageId third = kInvalidPageId;

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 1);
    first = HeapTable::Create(buffer);
    HeapTable first_table(buffer, first);
    first_table.Insert("first");
    buffer.FlushAll();

    second = HeapTable::Create(buffer);
    HeapTable second_table(buffer, second);
    second_table.Insert("second");
    buffer.FlushAll();

    third = HeapTable::Create(buffer);
    HeapTable third_table(buffer, third);
    third_table.Insert("third");
    buffer.FlushAll();
  }

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 1);
    Require(HeapTable(buffer, first).Scan()[0].second == "first", "first dirty page should persist");
    Require(HeapTable(buffer, second).Scan()[0].second == "second", "second dirty page should persist");
    Require(HeapTable(buffer, third).Scan()[0].second == "third", "third dirty page should persist");
  }
}

void TestHeapTableChainsAcrossPages() {
  auto path = TempDb("minidb_m1_heap_chain.db");
  std::vector<Rid> inserted;

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    PageId first_page = HeapTable::Create(buffer);
    HeapTable table(buffer, first_page);

    for (int i = 0; i < 80; ++i) {
      inserted.push_back(table.Insert("row-" + std::to_string(i) + "-" + std::string(180, 'x')));
    }
    buffer.FlushAll();
    Require(disk.PageCount() > 1, "large heap should allocate additional pages");
  }

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    HeapTable table(buffer, inserted.front().page_id);
    auto rows = table.Scan();
    Require(rows.size() == inserted.size(), "scan should traverse every heap page");
    Require(rows.front().second.rfind("row-0-", 0) == 0, "first chained row should persist");
    Require(rows.back().second.rfind("row-79-", 0) == 0, "last chained row should persist");
  }
}

void TestBufferPoolRejectsEvictionWhenAllFramesPinned() {
  auto path = TempDb("minidb_m1_pinned.db");
  DiskManager disk(path);
  BufferPool buffer(disk, 1);

  Page& page = buffer.NewPage();
  bool threw = false;
  try {
    buffer.NewPage();
  } catch (const MiniDbError&) {
    threw = true;
  }

  buffer.UnpinPage(page.page_id(), false);
  Require(threw, "buffer pool should reject eviction when every frame is pinned");
}

void TestBPlusTreeSearchInsertDelete() {
  BPlusTree index(3);
  std::vector<std::string> keys{"05", "02", "09", "01", "07", "03", "08", "04", "06"};

  for (std::size_t i = 0; i < keys.size(); ++i) {
    Require(index.Insert(keys[i], Rid{static_cast<PageId>(i + 1), 0}), "new key should be inserted");
  }

  Require(index.Size() == keys.size(), "index size should track inserted keys");
  auto left = index.Search("01");
  auto right = index.Search("09");
  Require(left.has_value() && left->page_id == 4, "search should find key in left leaf");
  Require(right.has_value() && right->page_id == 3, "search should find key after splits");
  Require(!index.Search("10").has_value(), "missing key should not be found");

  auto rows = index.Scan();
  Require(rows.size() == keys.size(), "index scan should include all keys");
  for (std::size_t i = 1; i < rows.size(); ++i) {
    Require(rows[i - 1].first < rows[i].first, "index scan should be sorted");
  }

  Require(!index.Insert("05", Rid{99, 1}), "duplicate primary key should update existing RID");
  auto updated = index.Search("05");
  Require(updated.has_value() && updated->page_id == 99, "duplicate insert should update RID");
  Require(index.Delete("05"), "delete should remove existing key");
  Require(!index.Search("05").has_value(), "deleted key should not be searchable");
  Require(!index.Delete("05"), "deleting missing key should return false");
}

void TestSqlParserParsesM2Statements() {
  SqlParser parser;

  auto insert = parser.Parse("INSERT INTO users VALUES ('1', 'Ada Lovelace');");
  Require(insert.type == StatementType::Insert, "INSERT should parse as insert statement");
  Require(insert.insert->table == "users", "INSERT table should parse");
  Require(insert.insert->values.size() == 2 && insert.insert->values[1] == "Ada Lovelace",
          "INSERT values should parse and unquote");

  auto select = parser.Parse("SELECT id, name FROM users WHERE id = '1';");
  Require(select.type == StatementType::Select, "SELECT should parse as select statement");
  Require(select.select->columns.size() == 2, "SELECT columns should parse");
  Require(select.select->table == "users", "SELECT table should parse");
  Require(!select.select->where.empty && select.select->where.column == "id" && select.select->where.value == "1",
          "SELECT WHERE predicate should parse");

  auto delete_from = parser.Parse("DELETE FROM users WHERE id = '1';");
  Require(delete_from.type == StatementType::Delete, "DELETE should parse as delete statement");
  Require(delete_from.delete_from->table == "users", "DELETE table should parse");
  Require(delete_from.delete_from->where.op == "=", "DELETE predicate operator should parse");
}

void TestParserConnectsToPrimaryKeyIndex() {
  SqlParser parser;
  BPlusTree primary_index(3);

  auto insert = parser.Parse("INSERT INTO users VALUES ('42', 'Katherine Johnson');");
  Rid rid{7, 2};
  Require(primary_index.Insert(insert.insert->values[0], rid), "parsed INSERT primary key should enter index");

  auto select = parser.Parse("SELECT id, name FROM users WHERE id = '42';");
  auto found = primary_index.Search(select.select->where.value);
  Require(found.has_value() && *found == rid, "parsed SELECT primary key predicate should use index key");

  auto delete_from = parser.Parse("DELETE FROM users WHERE id = '42';");
  Require(primary_index.Delete(delete_from.delete_from->where.value), "parsed DELETE primary key should delete index key");
  Require(!primary_index.Search("42").has_value(), "deleted parsed key should be absent from index");
}

void TestExecutionEngineInsertSelectDelete() {
  auto path = TempDb("minidb_m3_execution.db");
  DiskManager disk(path);
  BufferPool buffer(disk, 4);
  ExecutionEngine engine(buffer);
  engine.CreateTable("users", {Column{"id", ColumnType::Text, true}, Column{"name", ColumnType::Text, false}});

  Require(engine.Execute("INSERT INTO users VALUES ('1', 'Ada');").affected_rows == 1, "INSERT should affect one row");
  Require(engine.Execute("INSERT INTO users VALUES ('2', 'Grace');").affected_rows == 1, "second INSERT should work");

  auto selected = engine.Execute("SELECT id, name FROM users WHERE id = '1';");
  Require(selected.rows.size() == 1, "primary-key SELECT should return one row");
  Require(selected.rows[0][0] == "1" && selected.rows[0][1] == "Ada", "SELECT should project requested columns");

  auto count = engine.Execute("SELECT COUNT(*) FROM users;");
  Require(count.rows.size() == 1 && count.rows[0][0] == "2", "COUNT(*) should count table rows");

  Require(engine.Execute("DELETE FROM users WHERE id = '1';").affected_rows == 1, "DELETE should affect one row");
  auto after_delete = engine.Execute("SELECT COUNT(*) FROM users;");
  Require(after_delete.rows[0][0] == "1", "COUNT(*) should reflect deleted rows");
}

void TestExecutionEngineJoinAndAggregation() {
  auto path = TempDb("minidb_m3_join.db");
  DiskManager disk(path);
  BufferPool buffer(disk, 6);
  ExecutionEngine engine(buffer);
  engine.CreateTable("users", {Column{"id", ColumnType::Text, true}, Column{"name", ColumnType::Text, false}});
  engine.CreateTable("orders", {Column{"id", ColumnType::Text, true}, Column{"user_id", ColumnType::Text, false},
                                Column{"total", ColumnType::Text, false}});

  engine.Execute("INSERT INTO users VALUES ('1', 'Ada');");
  engine.Execute("INSERT INTO users VALUES ('2', 'Grace');");
  engine.Execute("INSERT INTO orders VALUES ('10', '1', '50');");
  engine.Execute("INSERT INTO orders VALUES ('11', '1', '75');");
  engine.Execute("INSERT INTO orders VALUES ('12', '2', '25');");

  auto joined = engine.Execute(
      "SELECT users.name, orders.total FROM users JOIN orders ON users.id = orders.user_id WHERE users.id = '1';");
  Require(joined.rows.size() == 2, "JOIN should return matching rows");
  Require(joined.rows[0][0] == "Ada" && joined.rows[1][0] == "Ada", "JOIN should project left table values");

  auto count = engine.Execute("SELECT COUNT(*) FROM users JOIN orders ON users.id = orders.user_id;");
  Require(count.rows.size() == 1 && count.rows[0][0] == "3", "JOIN COUNT(*) should count joined rows");
}

}  // namespace

int main() {
  try {
    TestPageManagerAllocatesAndPersistsPages();
    TestHeapTableInsertScanDelete();
    TestBufferPoolEvictsDirtyPages();
    TestHeapTableChainsAcrossPages();
    TestBufferPoolRejectsEvictionWhenAllFramesPinned();
    TestBPlusTreeSearchInsertDelete();
    TestSqlParserParsesM2Statements();
    TestParserConnectsToPrimaryKeyIndex();
    TestExecutionEngineInsertSelectDelete();
    TestExecutionEngineJoinAndAggregation();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "M1, M2, and M3 tests passed\n";
  return 0;
}
