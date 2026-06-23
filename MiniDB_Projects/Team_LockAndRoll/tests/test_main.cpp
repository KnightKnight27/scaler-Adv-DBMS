// MiniDB unit + integration tests (no external deps).
#include <filesystem>
#include <iostream>
#include <string>

#include "engine.h"
#include "index.h"
#include "parser.h"
#include "storage.h"

using namespace minidb;
namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                                \
  do {                                                                             \
    g_checks++;                                                                    \
    if (!(cond)) {                                                                 \
      g_failures++;                                                                \
      std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << ": " << #cond << "\n"; \
    }                                                                              \
  } while (0)

static std::string fresh_dir(const std::string& name) {
  std::string d = "testdata_" + name;
  std::error_code ec;
  fs::remove_all(d, ec);
  return d;
}

// run a select and return its rows
static std::vector<std::vector<Value>> query(Database& db, const std::string& sql) {
  return db.execute(sql).rows;
}

static void test_tuple_serialization() {
  std::cout << "test_tuple_serialization\n";
  Schema s(std::vector<Column>{{"id", TypeId::INTEGER}, {"name", TypeId::VARCHAR},
                               {"ok", TypeId::BOOLEAN}});
  Tuple t(std::vector<Value>{Value((int64_t)42), Value(std::string("hello")), Value(true)});
  auto bytes = t.serialize();
  Tuple back = Tuple::deserialize(bytes.data(), bytes.size(), s);
  CHECK(back.value(0).as_int() == 42);
  CHECK(back.value(1).as_string() == "hello");
  CHECK(back.value(2).as_bool() == true);

  Tuple withnull(std::vector<Value>{Value((int64_t)1), Value::null(), Value(false)});
  auto b2 = withnull.serialize();
  Tuple r2 = Tuple::deserialize(b2.data(), b2.size(), s);
  CHECK(r2.value(1).is_null());
  CHECK(r2.value(0).as_int() == 1);
}

static void test_slotted_page() {
  std::cout << "test_slotted_page\n";
  std::vector<char> page(PAGE_SIZE);
  slotted::init(page.data());
  CHECK(slotted::num_slots(page.data()) == 0);
  uint8_t a[] = {1, 2, 3, 4};
  uint8_t b[] = {9, 9};
  int s0 = slotted::insert(page.data(), a, 4);
  int s1 = slotted::insert(page.data(), b, 2);
  CHECK(s0 == 0 && s1 == 1);
  uint16_t off, len;
  slotted::get_slot(page.data(), s0, off, len);
  CHECK(len == 4 && std::memcmp(page.data() + off, a, 4) == 0);
  // delete then reuse the tombstone
  slotted::set_slot(page.data(), s0, 0, 0);
  uint8_t c[] = {7};
  int s2 = slotted::insert(page.data(), c, 1);
  CHECK(s2 == 0);  // reused slot 0

  // recovery applying a slot to an all-zero page must keep it insertable (free_ptr stays valid)
  std::vector<char> zero(PAGE_SIZE, 0);
  uint8_t img[] = {5, 6, 7};
  slotted::apply_slot(zero.data(), 0, PAGE_SIZE - 3, 3, img);
  CHECK(slotted::free_ptr(zero.data()) == PAGE_SIZE - 3);
  int s3 = slotted::insert(zero.data(), img, 3);
  CHECK(s3 == 1);  // not wrongly treated as full
}

static void test_bplustree() {
  std::cout << "test_bplustree\n";
  BPlusTree tree;
  const int N = 5000;
  for (int i = 0; i < N; i++) CHECK(tree.insert(i, RID{i, i}));
  CHECK(tree.size() == (size_t)N);
  CHECK(!tree.insert(100, RID{0, 0}));  // duplicate rejected
  for (int i = 0; i < N; i += 137) {
    auto r = tree.search(i);
    CHECK(r && r->page_id == i);
  }
  CHECK(!tree.search(N + 1).has_value());
  // range scan
  int seen = 0;
  tree.range_scan(10, 19, [&](int64_t k, RID) {
    CHECK(k >= 10 && k <= 19);
    seen++;
  });
  CHECK(seen == 10);
  // delete half (forces merges/borrows), the rest must survive
  for (int i = 0; i < N; i += 2) CHECK(tree.erase(i));
  CHECK(tree.size() == (size_t)(N / 2));
  for (int i = 1; i < N; i += 2) CHECK(tree.search(i).has_value());
  for (int i = 0; i < N; i += 2) CHECK(!tree.search(i).has_value());
}

static void test_buffer_pool_disk() {
  std::cout << "test_buffer_pool_disk\n";
  std::string d = fresh_dir("bpool");
  {
    DiskManager dm(d);
    BufferPool bp(&dm, 4);
    file_id_t f = dm.open_file("x.dat");
    page_id_t pid;
    Page* p = bp.new_page(f, &pid);
    slotted::init(p->data());
    uint8_t data[] = {42, 43};
    slotted::insert(p->data(), data, 2);
    bp.unpin_page(f, pid, true);
    bp.flush_all();
  }
  {  // reopen; data must persist
    DiskManager dm(d);
    BufferPool bp(&dm, 4);
    file_id_t f = dm.open_file("x.dat");
    Page* p = bp.fetch_page(f, 0);
    uint16_t off, len;
    slotted::get_slot(p->data(), 0, off, len);
    CHECK(len == 2 && (uint8_t)p->data()[off] == 42);
    bp.unpin_page(f, 0, false);
  }
}

static void test_sql_basic() {
  std::cout << "test_sql_basic\n";
  std::string d = fresh_dir("sql_basic");
  Database db(d);
  db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER)");
  db.execute("INSERT INTO u VALUES (1,'a',30),(2,'b',25),(3,'c',40)");
  auto rows = query(db, "SELECT id, name FROM u");
  CHECK(rows.size() == 3);
  auto one = query(db, "SELECT name FROM u WHERE id = 2");
  CHECK(one.size() == 1 && one[0][0].as_string() == "b");
  // duplicate key rejected
  bool threw = false;
  try { db.execute("INSERT INTO u VALUES (1,'x',1)"); } catch (...) { threw = true; }
  CHECK(threw);
}

static void test_where_ops() {
  std::cout << "test_where_ops\n";
  std::string d = fresh_dir("where");
  Database db(d);
  db.execute("CREATE TABLE n (id INTEGER PRIMARY KEY, v INTEGER)");
  db.execute("INSERT INTO n VALUES (1,10),(2,20),(3,30),(4,40),(5,50)");
  CHECK(query(db, "SELECT id FROM n WHERE v > 25").size() == 3);
  CHECK(query(db, "SELECT id FROM n WHERE v >= 20 AND v <= 40").size() == 3);
  CHECK(query(db, "SELECT id FROM n WHERE v = 30 OR v = 50").size() == 2);
  CHECK(query(db, "SELECT id FROM n WHERE v != 30").size() == 4);
  auto r = query(db, "SELECT id FROM n WHERE id = 3 ORDER BY id");
  CHECK(r.size() == 1 && r[0][0].as_int() == 3);
}

static void test_join() {
  std::cout << "test_join\n";
  std::string d = fresh_dir("join");
  Database db(d);
  db.execute("CREATE TABLE u (id INTEGER PRIMARY KEY, name VARCHAR)");
  db.execute("CREATE TABLE o (id INTEGER PRIMARY KEY, uid INTEGER, amt INTEGER)");
  db.execute("INSERT INTO u VALUES (1,'a'),(2,'b')");
  db.execute("INSERT INTO o VALUES (10,1,100),(11,1,200),(12,2,50)");
  auto r = query(db, "SELECT u.name, o.amt FROM u JOIN o ON u.id = o.uid ORDER BY o.amt");
  CHECK(r.size() == 3);
  CHECK(r[0][0].as_string() == "b" && r[0][1].as_int() == 50);
  CHECK(r[2][1].as_int() == 200);
}

static void test_aggregation() {
  std::cout << "test_aggregation\n";
  std::string d = fresh_dir("agg");
  Database db(d);
  db.execute("CREATE TABLE s (id INTEGER PRIMARY KEY, g INTEGER, v INTEGER)");
  db.execute("INSERT INTO s VALUES (1,1,10),(2,1,20),(3,2,30),(4,2,40),(5,2,50)");
  auto total = query(db, "SELECT COUNT(*), SUM(v), MIN(v), MAX(v), AVG(v) FROM s");
  CHECK(total.size() == 1);
  CHECK(total[0][0].as_int() == 5);
  CHECK(total[0][1].as_int() == 150);
  CHECK(total[0][2].as_int() == 10);
  CHECK(total[0][3].as_int() == 50);
  CHECK(total[0][4].as_int() == 30);
  auto grouped = query(db, "SELECT g, COUNT(*), SUM(v) FROM s GROUP BY g ORDER BY g");
  CHECK(grouped.size() == 2);
  CHECK(grouped[0][1].as_int() == 2 && grouped[0][2].as_int() == 30);
  CHECK(grouped[1][1].as_int() == 3 && grouped[1][2].as_int() == 120);
}

static void test_delete() {
  std::cout << "test_delete\n";
  std::string d = fresh_dir("del");
  Database db(d);
  db.execute("CREATE TABLE n (id INTEGER PRIMARY KEY, v INTEGER)");
  db.execute("INSERT INTO n VALUES (1,10),(2,20),(3,10),(4,40)");
  auto res = db.execute("DELETE FROM n WHERE v = 10");
  CHECK(res.affected == 2);
  CHECK(query(db, "SELECT id FROM n").size() == 2);
  CHECK(query(db, "SELECT id FROM n WHERE id = 1").empty());
}

static void test_optimizer_explain() {
  std::cout << "test_optimizer_explain\n";
  std::string d = fresh_dir("explain");
  Database db(d);
  db.execute("CREATE TABLE n (id INTEGER PRIMARY KEY, v INTEGER)");
  db.execute("INSERT INTO n VALUES (1,10),(2,20),(3,30)");
  std::string pk_plan = db.execute("EXPLAIN SELECT * FROM n WHERE id = 2").message;
  CHECK(pk_plan.find("IndexScan") != std::string::npos);
  std::string scan_plan = db.execute("EXPLAIN SELECT * FROM n WHERE v > 5").message;
  CHECK(scan_plan.find("SeqScan") != std::string::npos);
}

static void test_persistence() {
  std::cout << "test_persistence\n";
  std::string d = fresh_dir("persist");
  {
    Database db(d);
    db.execute("CREATE TABLE k (id INTEGER PRIMARY KEY, v INTEGER)");
    db.execute("INSERT INTO k VALUES (1,100),(2,200),(3,300)");
  }  // clean shutdown flushes
  {
    Database db(d);  // reopen
    auto r = query(db, "SELECT v FROM k WHERE id = 2");
    CHECK(r.size() == 1 && r[0][0].as_int() == 200);
    CHECK(query(db, "SELECT id FROM k").size() == 3);
  }
}

static void test_crash_recovery() {
  std::cout << "test_crash_recovery\n";
  std::string d = fresh_dir("crash");
  Database db(d);
  db.execute("CREATE TABLE acct (id INTEGER PRIMARY KEY, bal INTEGER)");
  db.execute("INSERT INTO acct VALUES (1,100),(2,200)");  // committed
  db.checkpoint();
  db.execute("BEGIN");
  db.execute("INSERT INTO acct VALUES (3,300)");  // uncommitted
  db.simulate_crash_and_recover();
  auto rows = query(db, "SELECT id FROM acct ORDER BY id");
  CHECK(rows.size() == 2);  // committed survive, uncommitted gone
  CHECK(query(db, "SELECT id FROM acct WHERE id = 3").empty());
  CHECK(query(db, "SELECT bal FROM acct WHERE id = 1")[0][0].as_int() == 100);
}

static void test_crash_recovery_no_checkpoint() {
  std::cout << "test_crash_recovery_no_checkpoint\n";
  std::string d = fresh_dir("crash2");
  Database db(d);
  db.execute("CREATE TABLE acct (id INTEGER PRIMARY KEY, bal INTEGER)");
  // no explicit checkpoint: the WAL must still preserve the rows
  db.execute("INSERT INTO acct VALUES (1,11),(2,22),(3,33)");
  db.simulate_crash_and_recover();
  CHECK(query(db, "SELECT id FROM acct").size() == 3);
  CHECK(query(db, "SELECT bal FROM acct WHERE id = 3")[0][0].as_int() == 33);
}

static void test_2pl_abort_rollback() {
  std::cout << "test_2pl_abort_rollback\n";
  std::string d = fresh_dir("2pl_rb");
  Database db(d, CCMode::TWO_PL);
  db.execute("CREATE TABLE n (id INTEGER PRIMARY KEY, v INTEGER)");
  db.execute("INSERT INTO n VALUES (1,10)");
  db.execute("BEGIN");
  db.execute("INSERT INTO n VALUES (2,20)");
  db.execute("DELETE FROM n WHERE id = 1");
  db.execute("ROLLBACK");
  // rollback restores the pre-transaction state
  CHECK(query(db, "SELECT id FROM n").size() == 1);
  CHECK(query(db, "SELECT v FROM n WHERE id = 1")[0][0].as_int() == 10);
  CHECK(query(db, "SELECT id FROM n WHERE id = 2").empty());
}

static void test_mvcc_snapshot() {
  std::cout << "test_mvcc_snapshot\n";
  std::string d = fresh_dir("mvcc_snap");
  Database db(d, CCMode::MVCC);
  db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)");
  db.execute("INSERT INTO t VALUES (1,10),(2,20)");
  TableInfo* t = db.catalog().get_table("t");

  Transaction* reader = db.begin();  // snapshot taken now

  // another txn inserts a row and commits
  Transaction* writer = db.begin();
  db.insert_row(writer, t, Tuple({Value((int64_t)3), Value((int64_t)30)}));
  db.commit(writer);

  // reader's snapshot predates the commit, so it shouldn't see row 3
  int count = 0;
  db.scan_table(reader, t, [&](RID, const Tuple&) { count++; return true; });
  CHECK(count == 2);
  Tuple out;
  CHECK(!db.read_key(reader, t, 3, &out));
  db.commit(reader);

  // a fresh txn sees all three rows
  Transaction* later = db.begin();
  int count2 = 0;
  db.scan_table(later, t, [&](RID, const Tuple&) { count2++; return true; });
  CHECK(count2 == 3);
  db.commit(later);
}

static void test_mvcc_write_conflict() {
  std::cout << "test_mvcc_write_conflict\n";
  std::string d = fresh_dir("mvcc_conf");
  Database db(d, CCMode::MVCC);
  db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER)");
  db.execute("INSERT INTO t VALUES (1,10)");
  TableInfo* t = db.catalog().get_table("t");

  Transaction* t1 = db.begin();
  Transaction* t2 = db.begin();
  db.delete_row(t1, t, 1);  // t1 stages a delete of row 1
  bool conflict = false;
  try {
    db.delete_row(t2, t, 1);  // t2 touches the same row -> conflict
  } catch (const AbortException&) {
    conflict = true;
  }
  CHECK(conflict);
  db.abort(t2);
  db.commit(t1);

  Transaction* t3 = db.begin();
  Tuple out;
  CHECK(!db.read_key(t3, t, 1, &out));  // row 1 is gone after t1 committed
  db.commit(t3);
}

// true if executing sql throws (a clean error, not a crash)
static bool throws(Database& db, const std::string& sql) {
  try {
    db.execute(sql);
    return false;
  } catch (const std::exception&) {
    return true;
  }
}

static void test_error_handling() {
  std::cout << "test_error_handling\n";
  std::string d = fresh_dir("errors");
  Database db(d);
  db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, s VARCHAR, n INTEGER)");
  db.execute("INSERT INTO t VALUES (1,'a',10)");

  // null / missing primary key
  CHECK(throws(db, "INSERT INTO t VALUES (NULL,'b',20)"));
  CHECK(throws(db, "INSERT INTO t (s,n) VALUES ('b',20)"));
  // integer literal overflow
  CHECK(throws(db, "INSERT INTO t VALUES (99999999999999999999,'b',1)"));
  CHECK(throws(db, "SELECT * FROM t LIMIT 99999999999999999999"));
  // arithmetic on a non-integer column
  CHECK(throws(db, "SELECT s + 1 FROM t"));
  // non-boolean operand to AND
  CHECK(throws(db, "SELECT * FROM t WHERE n AND id = 1"));
  // unterminated string literal
  CHECK(throws(db, "INSERT INTO t VALUES (2,'unterminated, 3)"));
  // EXPLAIN with no statement
  CHECK(throws(db, "EXPLAIN"));
  // type mismatch
  CHECK(throws(db, "INSERT INTO t VALUES ('notint','b',1)"));
  // division by zero
  CHECK(throws(db, "SELECT n / 0 FROM t"));

  // after all those failures the table is still intact and usable
  CHECK(query(db, "SELECT id FROM t").size() == 1);
  db.execute("INSERT INTO t VALUES (2,'b',20)");
  CHECK(query(db, "SELECT id FROM t").size() == 2);
}

// a corrupt tuple buffer must be rejected, not read out of bounds
static void test_corrupt_tuple_rejected() {
  std::cout << "test_corrupt_tuple_rejected\n";
  Schema s(std::vector<Column>{{"id", TypeId::INTEGER}, {"name", TypeId::VARCHAR}});
  // claims 2 values but the buffer is way too short for them
  std::vector<uint8_t> bad = {2, 0, 0};  // nvalues=2, 1 bitmap byte, then nothing
  bool threw = false;
  try {
    Tuple::deserialize(bad.data(), bad.size(), s);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);
}

int main() {
  std::cout << std::unitbuf;  // flush each write so a hang shows the last test
  std::cout << "=== MiniDB test suite ===\n";
  test_tuple_serialization();
  test_slotted_page();
  test_bplustree();
  test_buffer_pool_disk();
  test_sql_basic();
  test_where_ops();
  test_join();
  test_aggregation();
  test_delete();
  test_optimizer_explain();
  test_persistence();
  test_crash_recovery();
  test_crash_recovery_no_checkpoint();
  test_2pl_abort_rollback();
  test_mvcc_snapshot();
  test_mvcc_write_conflict();
  test_error_handling();
  test_corrupt_tuple_rejected();

  std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
  if (g_failures) {
    std::cout << g_failures << " FAILURES\n";
    return 1;
  }
  std::cout << "ALL TESTS PASSED\n";
  return 0;
}
