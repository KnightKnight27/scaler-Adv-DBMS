#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "minidb/db/database.h"
#include "minidb/index/b_plus_tree.h"
#include "minidb/lsm/lsm_tree.h"
#include "minidb/query/optimizer.h"
#include "minidb/query/parser.h"
#include "minidb/recovery/log_manager.h"
#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/storage/heap_file.h"

namespace {

std::filesystem::path TempDir(const std::string &name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
  std::filesystem::create_directories(path);
  return path;
}

void TestParser() {
  minidb::Parser parser;
  auto insert = parser.Parse("INSERT users 1 Sushant");
  assert(insert.type == minidb::QueryType::Insert);
  assert(insert.table == "users");
  assert(insert.key == 1);
  assert(insert.value == "Sushant");

  auto select = parser.Parse("SELECT users WHERE id=1");
  assert(select.type == minidb::QueryType::Select);
  assert(select.key == 1);

  auto join = parser.Parse("SELECT users JOIN profiles WHERE id=1");
  assert(join.type == minidb::QueryType::Join);
  assert(join.table == "users");
  assert(join.join_table == "profiles");

  auto real_join = parser.Parse("SELECT users JOIN profiles ON users.id=profiles.id");
  assert(real_join.type == minidb::QueryType::Join);
  assert(real_join.table == "users");
  assert(real_join.join_table == "profiles");
  assert(real_join.join_all_on_id);

  assert(parser.Parse("BEGIN").type == minidb::QueryType::Begin);
  assert(parser.Parse("COMMIT").type == minidb::QueryType::Commit);
  assert(parser.Parse("ABORT").type == minidb::QueryType::Abort);
}

void TestHeapPersistence() {
  const auto dir = TempDir("minidb_heap_test");
  minidb::RID rid;
  {
    minidb::DiskManager disk(dir / "heap.tbl");
    minidb::BufferPoolManager buffer(disk, 2);
    minidb::HeapFile heap(buffer);
    rid = heap.Insert({7, "seven"});
    buffer.FlushAll();
  }
  {
    minidb::DiskManager disk(dir / "heap.tbl");
    minidb::BufferPoolManager buffer(disk, 2);
    minidb::HeapFile heap(buffer);
    auto record = heap.Read(rid);
    assert(record);
    assert(record->key == 7);
    assert(record->value == "seven");
    assert(heap.Delete(rid));
    assert(!heap.Read(rid));
  }
}

void TestBTree() {
  minidb::BPlusTree tree(4);
  for (int i = 0; i < 100; ++i) {
    assert(tree.Insert(i, minidb::RID{i, 0}));
  }
  assert(tree.Validate());
  for (int i = 0; i < 100; ++i) {
    auto rid = tree.Search(i);
    assert(rid);
    assert(rid->page_id == i);
  }
  assert(!tree.Insert(5, minidb::RID{5, 1}));
  for (int i = 0; i < 50; i += 2) assert(tree.Delete(i));
  assert(tree.Validate());
  assert(!tree.Search(10));
  assert(tree.Search(11));
}

void TestBasicInsertSelectDelete() {
  const auto dir = TempDir("minidb_basic_dml_test");
  minidb::Database db(dir);
  auto inserted = db.Execute("INSERT users 42 answer");
  assert(inserted.ok);
  auto selected = db.Execute("SELECT users WHERE id=42");
  assert(selected.record);
  assert(selected.record->value == "answer");
  auto deleted = db.Execute("DELETE users WHERE id=42");
  assert(deleted.ok);
  auto missing = db.Execute("SELECT users WHERE id=42");
  assert(!missing.record);
}

void TestOptimizerChoosesIndex() {
  minidb::Optimizer optimizer;
  minidb::Query query{minidb::QueryType::Select, "users", {}, 500, {}, false};
  auto plan = optimizer.Optimize(query, 100, 1000, 3);
  assert(plan.access_path == minidb::AccessPath::IndexScan);
}

void TestDatabaseRecovery() {
  const auto dir = TempDir("minidb_db_test");
  {
    minidb::Database db(dir);
    auto r1 = db.Execute("INSERT users 1 Sushant");
    assert(r1.ok);
    auto r2 = db.Execute("SELECT users WHERE id=1");
    assert(r2.record);
    assert(r2.record->value == "Sushant");
    assert(db.Execute("INSERT profiles 1 student").ok);
    auto joined = db.Execute("SELECT users JOIN profiles WHERE id=1");
    assert(joined.record);
    assert(joined.record->value == "Sushant|student");
    db.Flush();
  }
  {
    minidb::Database db(dir);
    auto found = db.Execute("SELECT users WHERE id=1");
    assert(found.record);
    assert(found.record->value == "Sushant");
    auto deleted = db.Execute("DELETE users WHERE id=1");
    assert(deleted.ok);
    assert(db.RowCount("users") == 0);
    db.Flush();
  }
  {
    minidb::Database db(dir);
    auto found = db.Execute("SELECT users WHERE id=1");
    assert(!found.record);
  }
}

void TestRecoveryUndoIncompleteInsert() {
  const auto dir = TempDir("minidb_recovery_undo_test");
  {
    std::ofstream catalog(dir / "catalog.txt");
    catalog << "users\n";
  }
  {
    minidb::LogManager log(dir / "minidb.wal");
    log.Append(100, minidb::LogType::Begin);
    log.Append(100, minidb::LogType::Insert, "users", 77, {}, "ghost");
    log.Flush();

    minidb::DiskManager disk(dir / "users.tbl");
    minidb::BufferPoolManager buffer(disk, 2);
    minidb::HeapFile heap(buffer);
    heap.Insert({77, "ghost"});
    buffer.FlushAll();
  }
  {
    minidb::Database db(dir);
    auto found = db.Execute("SELECT users WHERE id=77");
    assert(!found.record);
    assert(db.RowCount("users") == 0);
  }
}

void TestTransactionRollback() {
  const auto dir = TempDir("minidb_txn_rollback_test");
  minidb::Database db(dir);
  assert(db.Execute("INSERT users 10 keep").ok);

  assert(db.Execute("BEGIN").ok);
  assert(db.Execute("INSERT users 11 temp").ok);
  assert(db.Execute("DELETE users WHERE id=10").ok);
  auto before_abort_deleted = db.Execute("SELECT users WHERE id=10");
  assert(!before_abort_deleted.record);
  auto before_abort_inserted = db.Execute("SELECT users WHERE id=11");
  assert(before_abort_inserted.record);
  assert(db.Execute("ABORT").ok);

  auto restored = db.Execute("SELECT users WHERE id=10");
  assert(restored.record);
  assert(restored.record->value == "keep");
  auto removed = db.Execute("SELECT users WHERE id=11");
  assert(!removed.record);
}

void TestRealJoinAndOrdering() {
  const auto dir = TempDir("minidb_join_test");
  minidb::Database db(dir);
  const std::string payload(200, 'x');
  for (int i = 1; i <= 300; ++i) {
    assert(db.Execute("INSERT big " + std::to_string(i) + " b" +
                      std::to_string(i) + payload).ok);
  }
  assert(db.Execute("INSERT small 2 s2").ok);
  assert(db.Execute("INSERT small 5 s5").ok);

  auto result = db.Execute("SELECT big JOIN small ON big.id=small.id");
  assert(result.ok);
  assert(result.records.size() == 2);
  assert(result.plan.outer_table == "small");
  assert(result.plan.inner_table == "big");
  assert(result.plan.access_path == minidb::AccessPath::IndexNestedLoopJoin);
}

void TestLsm() {
  const auto dir = TempDir("minidb_lsm_test");
  minidb::LsmTree lsm(dir, 3);
  lsm.Put(1, "one");
  lsm.Put(2, "two");
  assert(lsm.Get(1) == "one");
  lsm.Put(3, "three");
  assert(lsm.SSTableCount() == 1);
  assert(lsm.Get(2) == "two");
  lsm.Delete(2);
  assert(!lsm.Get(2));
  lsm.Flush();
  lsm.Compact();
  assert(lsm.SSTableCount() == 1);
  assert(lsm.Get(1) == "one");
  assert(!lsm.Get(2));
}

}  // namespace

int main() {
  TestParser();
  TestHeapPersistence();
  TestBTree();
  TestBasicInsertSelectDelete();
  TestOptimizerChoosesIndex();
  TestDatabaseRecovery();
  TestRecoveryUndoIncompleteInsert();
  TestTransactionRollback();
  TestRealJoinAndOrdering();
  TestLsm();
  std::cout << "all MiniDB tests passed\n";
  return 0;
}
