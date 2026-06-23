// MiniDB test suite. Plain asserts, no framework. `make test` builds & runs.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

#include "database.hpp"
#include "index/bplus_tree.hpp"
#include "lsm/lsm_tree.hpp"
#include "storage/buffer_pool.hpp"
#include "storage/heap_file.hpp"

using namespace minidb;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) { g_pass++; } else { g_fail++; \
    std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static void fresh(const std::string& dir) { fs::remove_all(dir); }

// ---- storage --------------------------------------------------------------
static void test_storage() {
    std::printf("[storage] page + buffer pool + heap file\n");
    fresh("data_test_storage");
    fs::create_directories("data_test_storage");
    DiskManager disk("data_test_storage/t.db");
    BufferPool bp(&disk, 4);              // tiny pool to force evictions
    PageId fp;
    HeapFile hf = HeapFile::create(&bp, &fp);

    std::vector<RID> rids;
    for (int i = 0; i < 2000; ++i) {     // spans many pages -> eviction
        std::string s = "record-" + std::to_string(i);
        rids.push_back(hf.insert(std::vector<uint8_t>(s.begin(), s.end())));
    }
    bool all_ok = true;
    for (int i = 0; i < 2000; ++i) {
        auto rec = hf.get(rids[i]);
        std::string s(rec->begin(), rec->end());
        if (s != "record-" + std::to_string(i)) all_ok = false;
    }
    CHECK(all_ok, "all heap records read back correctly across eviction");
    CHECK(bp.stats().evictions > 0, "buffer pool evicted pages");

    hf.erase(rids[5]);
    CHECK(!hf.get(rids[5]).has_value(), "deleted record is gone");

    int count = 0;
    hf.scan([&](const RID&, const std::vector<uint8_t>&) { count++; });
    CHECK(count == 1999, "scan sees all live records after one delete");
}

// ---- B+ tree --------------------------------------------------------------
static void test_btree() {
    std::printf("[index] B+ tree insert/search/delete/range\n");
    BPlusTree t;
    for (int i = 1; i <= 1000; ++i) t.insert(Value::Int(i), RID{i, 0});
    CHECK(t.size() == 1000, "size after inserts");
    CHECK(t.search(Value::Int(500)).has_value(), "search hit");
    CHECK(!t.search(Value::Int(5000)).has_value(), "search miss");

    long cnt = 0;
    t.range_scan(Value::Int(100), Value::Int(199),
                 [&](const Value&, const RID&) { cnt++; });
    CHECK(cnt == 100, "range scan [100,199] returns 100");

    for (int i = 1; i <= 500; ++i) t.erase(Value::Int(i));
    CHECK(t.size() == 500, "size after deletes");
    CHECK(!t.search(Value::Int(250)).has_value(), "deleted key absent");
    CHECK(t.search(Value::Int(750)).has_value(), "surviving key present");
}

// ---- SQL end to end -------------------------------------------------------
static void test_sql() {
    std::printf("[sql] create/insert/select/where/join/aggregate/update/delete\n");
    fresh("data_test_sql");
    Database db("data_test_sql");
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, name TEXT, age INT);");
    db.execute("INSERT INTO t VALUES (1,'a',20),(2,'b',30),(3,'c',40);");

    auto r1 = db.execute("SELECT id, name FROM t WHERE id = 2;");
    CHECK(r1.rows.size() == 1 && r1.rows[0][1].as_text() == "b", "point query by pk");

    auto r2 = db.execute("SELECT id FROM t WHERE age >= 30;");
    CHECK(r2.rows.size() == 2, "range predicate");

    db.execute("CREATE TABLE o (oid INT PRIMARY KEY, uid INT, amt INT);");
    db.execute("INSERT INTO o VALUES (10,1,5),(11,1,7),(12,3,9);");
    auto r3 = db.execute("SELECT t.name, o.amt FROM t JOIN o ON t.id = o.uid;");
    CHECK(r3.rows.size() == 3, "join cardinality");

    auto r4 = db.execute("SELECT uid, COUNT(*), SUM(amt) FROM o GROUP BY uid;");
    CHECK(r4.rows.size() == 2, "group by produces 2 groups");

    db.execute("UPDATE t SET age = 99 WHERE id = 1;");
    auto r5 = db.execute("SELECT age FROM t WHERE id = 1;");
    CHECK(r5.rows[0][0].as_int() == 99, "update applied");

    db.execute("DELETE FROM t WHERE id = 3;");
    auto r6 = db.execute("SELECT COUNT(*) FROM t;");
    CHECK(r6.rows[0][0].as_int() == 2, "delete reduced row count");

    auto dup = db.execute("INSERT INTO t VALUES (1,'x',1);");
    CHECK(!dup.ok, "duplicate primary key rejected");
}

// ---- transactions: rollback ----------------------------------------------
static void test_txn_rollback() {
    std::printf("[txn] explicit rollback undoes changes\n");
    fresh("data_test_txn");
    Database db("data_test_txn");
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT);");
    db.execute("INSERT INTO t VALUES (1,10);");

    Transaction* tx = db.begin();
    db.execute("INSERT INTO t VALUES (2,20);", tx);
    db.execute("UPDATE t SET v = 999 WHERE id = 1;", tx);
    db.abort(tx);   // rollback

    auto r = db.execute("SELECT id, v FROM t;");
    CHECK(r.rows.size() == 1, "rolled-back insert is gone");
    CHECK(r.rows[0][1].as_int() == 10, "rolled-back update is reverted");
}

// ---- crash recovery (WAL) -------------------------------------------------
static void test_recovery() {
    std::printf("[recovery] crash mid-flight, redo committed / undo uncommitted\n");
    fresh("data_test_recovery");

    pid_t pid = fork();
    if (pid == 0) {
        // Child: write data, then "crash" (skip destructors/checkpoint).
        Database db("data_test_recovery");
        db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT);");
        db.execute("INSERT INTO t VALUES (1,100),(2,200);");   // committed
        Transaction* tx = db.begin();
        db.execute("INSERT INTO t VALUES (3,300);", tx);       // never committed
        // No commit, no checkpoint: simulate a crash.
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);

    // Parent: reopen -> recovery runs in the constructor.
    Database db("data_test_recovery");
    auto r = db.execute("SELECT id, v FROM t ORDER BY id;");
    CHECK(r.rows.size() == 2, "committed rows survived the crash");
    CHECK(r.rows.size() == 2 && r.rows[0][0].as_int() == 1 &&
          r.rows[1][0].as_int() == 2, "exactly the committed rows are present");
    bool has3 = false;
    for (auto& row : r.rows) if (row[0].as_int() == 3) has3 = true;
    CHECK(!has3, "uncommitted row was not recovered");
}

// ---- LSM-tree (extension) -------------------------------------------------
static void test_lsm() {
    std::printf("[lsm] put/get/delete/flush/compaction\n");
    fresh("data_test_lsm");
    LSMOptions opts;
    opts.memtable_bytes = 4096;     // force frequent flushes
    opts.compaction_trigger = 3;
    LSMTree lsm("data_test_lsm", opts);

    for (int i = 0; i < 2000; ++i)
        lsm.put("k" + std::to_string(i), "v" + std::to_string(i));
    CHECK(lsm.get("k0").value_or("") == "v0", "read oldest key after flushes");
    CHECK(lsm.get("k1999").value_or("") == "v1999", "read newest key");
    CHECK(lsm.num_sstables() >= 1, "memtable flushed to sstables");

    lsm.put("k0", "updated");
    CHECK(lsm.get("k0").value_or("") == "updated", "newest value wins");
    lsm.del("k1");
    CHECK(!lsm.get("k1").has_value(), "tombstone hides deleted key");

    lsm.flush();
    lsm.compact();
    CHECK(lsm.get("k0").value_or("") == "updated", "value survives compaction");
    CHECK(!lsm.get("k1").has_value(), "tombstone dropped but key stays deleted");
    CHECK(lsm.get("k500").value_or("") == "v500", "untouched key survives compaction");
}

// ---- concurrency: deadlock detection -------------------------------------
static void test_deadlock() {
    std::printf("[concurrency] strict 2PL detects and breaks a deadlock\n");
    fresh("data_test_deadlock");
    Database db("data_test_deadlock");
    db.execute("CREATE TABLE a (id INT PRIMARY KEY, v INT);");
    db.execute("CREATE TABLE b (id INT PRIMARY KEY, v INT);");
    db.execute("INSERT INTO a VALUES (1,0);");
    db.execute("INSERT INTO b VALUES (1,0);");

    std::atomic<int> deadlocks{0}, ready{0};
    auto worker = [&](const char* first, const char* second) {
        Transaction* tx = db.begin();
        try {
            db.execute(std::string("UPDATE ") + first + " SET v = 1 WHERE id = 1;", tx);
            ready++;
            while (ready.load() < 2) std::this_thread::yield();   // both hold one lock
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            db.execute(std::string("UPDATE ") + second + " SET v = 1 WHERE id = 1;", tx);
            db.commit(tx);
        } catch (const DeadlockError&) {
            deadlocks++;
            db.abort(tx);
        } catch (...) {
            db.abort(tx);
        }
    };
    std::thread t1(worker, "a", "b");
    std::thread t2(worker, "b", "a");
    t1.join();
    t2.join();
    CHECK(deadlocks.load() >= 1, "at least one transaction was aborted as deadlock victim");
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered so progress shows on hang
    std::printf("=== MiniDB test suite ===\n");
    test_storage();
    test_btree();
    test_sql();
    test_txn_rollback();
    test_recovery();
    test_deadlock();
    test_lsm();
    std::printf("=========================\n");
    std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
