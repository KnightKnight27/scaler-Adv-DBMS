// Unit tests for the storage engine, B+ Tree, and write-ahead log.
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "minidb/db.h"
#include "minidb/index/btree.h"
#include "minidb/recovery/wal.h"
#include "minidb/storage/buffer_pool.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/storage/heap_file.h"
#include "minidb/txn/lock_manager.h"

using namespace minidb;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            ++g_failed;                                                  \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                \
    } while (0)

static void test_heap_and_buffer() {
    std::remove("test_heap.db");
    DiskManager dm("test_heap.db");
    BufferPool bp(dm, 8);  // tiny pool to force eviction
    std::vector<int> pages;
    HeapFile heap(bp, pages);

    Schema schema = {{"id", Type::Int}, {"name", Type::Text}};
    std::vector<RID> rids;
    for (int k = 0; k < 500; ++k) {
        Tuple t = {Value::make_int(k), Value::make_text("row-" + std::to_string(k))};
        rids.push_back(heap.insert(serialize_tuple(schema, t)));
    }
    bp.flush_all();

    int seen = 0;
    heap.scan([&](RID, const std::vector<uint8_t>& b) {
        Tuple t = deserialize_tuple(schema, b.data(), static_cast<int>(b.size()));
        CHECK(t[1].s == "row-" + std::to_string(t[0].i));
        ++seen;
    });
    CHECK(seen == 500);

    std::vector<uint8_t> out;
    CHECK(heap.get(rids[123], out));
    Tuple t = deserialize_tuple(schema, out.data(), static_cast<int>(out.size()));
    CHECK(t[0].i == 123);

    heap.mark_delete(rids[123]);
    CHECK(!heap.get(rids[123], out));
    std::remove("test_heap.db");
}

static void test_btree() {
    BPlusTree tree(4);  // small order to exercise splits and merges
    const int N = 2000;
    for (int k = 0; k < N; ++k) tree.insert(k, RID{k, k});

    RID r;
    for (int k = 0; k < N; ++k) {
        CHECK(tree.search(k, r));
        CHECK(r.page_id == k);
    }
    CHECK(!tree.search(N + 1, r));

    // delete every third key, then verify what remains
    std::set<int> erased;
    for (int k = 0; k < N; k += 3) {
        CHECK(tree.erase(k));
        erased.insert(k);
    }
    for (int k = 0; k < N; ++k) {
        bool found = tree.search(k, r);
        CHECK(found == (erased.count(k) == 0));
    }

    // delete the rest; tree should end up empty and searchable without crashing
    for (int k = 0; k < N; ++k)
        if (!erased.count(k)) CHECK(tree.erase(k));
    CHECK(!tree.search(0, r));
}

static void test_wal_roundtrip() {
    std::remove("test.wal");
    {
        LogManager log("test.wal");
        LogRecord b;
        b.txn_id = 1;
        b.type = LogType::Begin;
        log.append(b);
        LogRecord ins;
        ins.txn_id = 1;
        ins.type = LogType::Insert;
        ins.table = "t";
        ins.rid = {2, 5};
        ins.image = {1, 2, 3, 4};
        log.append(ins);
        LogRecord c;
        c.txn_id = 1;
        c.type = LogType::Commit;
        log.append(c);
        log.flush();
    }
    {
        LogManager log("test.wal");
        const auto& recs = log.records();
        CHECK(recs.size() == 3);
        CHECK(recs[1].type == LogType::Insert);
        CHECK(recs[1].table == "t");
        CHECK(recs[1].rid.slot_id == 5);
        CHECK(recs[1].image.size() == 4);
        CHECK(recs[2].type == LogType::Commit);
    }
    std::remove("test.wal");
}

static void test_wound_wait() {
    LockManager lm;
    // shared locks are compatible across transactions
    CHECK(lm.acquire(5, "B", LockMode::Shared) == LockResult::Granted);
    CHECK(lm.acquire(6, "B", LockMode::Shared) == LockResult::Granted);

    // txn 2 holds an exclusive lock; older txn 1 wounds it and takes over
    CHECK(lm.acquire(2, "A", LockMode::Exclusive) == LockResult::Granted);
    CHECK(lm.acquire(1, "A", LockMode::Exclusive) == LockResult::Granted);
    CHECK(lm.is_wounded(2));

    // younger txn 3 wanting txn 1's lock aborts itself instead of waiting
    CHECK(lm.acquire(3, "A", LockMode::Exclusive) == LockResult::Aborted);
    CHECK(lm.is_wounded(3));
}

static long long count_rows(Database& db, const std::string& table) {
    return static_cast<long long>(db.execute("SELECT * FROM " + table).rows.size());
}

// Sum an integer column (by position) over all rows of a table.
static long long sum_col(Database& db, const std::string& table, int col) {
    Result r = db.execute("SELECT * FROM " + table);
    long long s = 0;
    for (const auto& row : r.rows) s += std::atoll(row[col].c_str());
    return s;
}

static void test_recovery() {
    std::remove("rectest.data");
    std::remove("rectest.wal");
    std::remove("rectest.meta");

    {
        Database db("rectest");
        db.execute("CREATE TABLE t (id INT, v INT)");
        for (int i = 0; i < 50; ++i)
            db.execute("INSERT INTO t VALUES (" + std::to_string(i) + ", " + std::to_string(i * 2) + ")");
        db.execute("DELETE FROM t WHERE id = 10");
        CHECK(count_rows(db, "t") == 49);

        // discard the buffer pool (lose everything not on disk), then recover
        db.execute("CRASH");
        CHECK(count_rows(db, "t") == 49);
        CHECK(sum_col(db, "t", 1) == 2430);  // sum of 0..98 even, minus v(10)=20

        // an aborted transaction leaves no trace after recovery
        db.execute("BEGIN");
        db.execute("INSERT INTO t VALUES (999, 999)");
        db.execute("ABORT");
        CHECK(count_rows(db, "t") == 49);
    }  // db destroyed here, so the files below are not re-created on shutdown

    std::remove("rectest.data");
    std::remove("rectest.wal");
    std::remove("rectest.meta");
}

int main() {
    std::printf("running storage / index / wal / txn / recovery tests\n");
    test_heap_and_buffer();
    test_btree();
    test_wal_roundtrip();
    test_wound_wait();
    test_recovery();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
