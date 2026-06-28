// ============================================================
//  Unit Tests — MVCC Version Chains Manager
//  Tests: VersionChain, TransactionManager, Page Layouts,
//         SQL Executor, Snapshot Isolation, Conflict Detection
// ============================================================

#include "../include/mvcc_types.h"
#include "../include/version_chain.h"
#include "../include/transaction_manager.h"
#include "../include/page_layouts.h"
#include "../include/sql_executor.h"

#include <iostream>
#include <cassert>
#include <stdexcept>
#include <string>

using namespace mvcc;

// ─────────────────────────────────────────────────────────────
//  Micro test-runner
// ─────────────────────────────────────────────────────────────
static int passed = 0, failed = 0;

#define TEST(name, expr) \
    do { \
        if (expr) { \
            std::cout << "  [PASS] " << name << "\n"; \
            ++passed; \
        } else { \
            std::cout << "  [FAIL] " << name << "\n"; \
            ++failed; \
        } \
    } while(0)

#define TEST_THROWS(name, expr, ExcType) \
    do { \
        bool caught = false; \
        try { expr; } catch (const ExcType&) { caught = true; } \
        TEST(name, caught); \
    } while(0)

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
static TableSchema testSchema() {
    TableSchema s;
    s.tableName = "test";
    s.primaryKeyCol = 0;
    s.columns = {
        ColumnDef("id",    DataType::INT64),
        ColumnDef("value", DataType::INT64),
        ColumnDef("name",  DataType::VARCHAR, 32),
    };
    return s;
}

static Tuple makeRow(int64_t id, int64_t val, const std::string& name) {
    return { Value::makeInt64(id), Value::makeInt64(val), Value::makeVarchar(name) };
}

// ─────────────────────────────────────────────────────────────
//  Test Groups
// ─────────────────────────────────────────────────────────────
void testVersionChain() {
    std::cout << "\n=== VersionChain Tests ===\n";

    VersionChain chain(42);
    TEST("initial length is 0", chain.length() == 0);

    // Create a committed version visible at TS=10
    auto rec = std::make_shared<VersionRecord>(1, 5, makeRow(42,100,"A"), RID{0,0});
    rec->status = VersionStatus::COMMITTED;
    rec->endTS  = INF_TS;
    chain.append(rec);

    TEST("length after append = 1", chain.length() == 1);
    TEST("visible at TS=10", chain.readVersion(10).has_value());
    TEST("not visible before beginTS", !chain.readVersion(4).has_value());

    // Append a newer version that supersedes it at TS=20
    rec->endTS = 20;
    auto rec2 = std::make_shared<VersionRecord>(2, 20, makeRow(42,200,"B"), RID{0,1});
    rec2->status = VersionStatus::COMMITTED;
    rec2->endTS  = INF_TS;
    chain.append(rec2);

    TEST("length after second append = 2", chain.length() == 2);
    TEST("old version visible at TS=15",  chain.readVersion(15).has_value());
    TEST("new version visible at TS=25",  chain.readVersion(25).has_value());
    TEST("old value at TS=15 is 100",
         chain.readVersion(15).value()[1] == Value::makeInt64(100));
    TEST("new value at TS=25 is 200",
         chain.readVersion(25).value()[1] == Value::makeInt64(200));
}

void testTransactionManager() {
    std::cout << "\n=== TransactionManager Tests ===\n";

    TransactionManager mgr;
    VersionChainIndex  vci;

    auto t1 = mgr.begin(IsolationLevel::SNAPSHOT);
    TEST("txn is active after begin", t1->isActive());
    TEST("active count = 1", mgr.activeCount() == 1);

    auto t2 = mgr.begin(IsolationLevel::SNAPSHOT);
    TEST("active count = 2", mgr.activeCount() == 2);

    mgr.commit(t1, vci);
    TEST("t1 committed", t1->isCommitted());
    TEST("active count = 1 after commit", mgr.activeCount() == 1);

    mgr.abort(t2, vci);
    TEST("t2 aborted", t2->isAborted());
    TEST("active count = 0 after abort", mgr.activeCount() == 0);

    // beginTS ordering
    auto t3 = mgr.begin();
    auto t4 = mgr.begin();
    TEST("t4.beginTS > t3.beginTS", t4->beginTS > t3->beginTS);
    mgr.commit(t3, vci);
    mgr.commit(t4, vci);
}

void testNSMPage() {
    std::cout << "\n=== NSMPage Tests ===\n";

    auto schema = testSchema();
    NSMPage page(0, schema);

    // Insert
    SlotID s0 = page.insert(makeRow(1, 100, "Alice"), 1, 5);
    SlotID s1 = page.insert(makeRow(2, 200, "Bob"),   1, 5);
    TEST("slot 0 is 0", s0 == 0);
    TEST("slot 1 is 1", s1 == 1);
    TEST("numSlots = 2", page.numSlots() == 2);

    // Read — committed (mark beginTS=5, endTS=INF, so set status manually)
    // We simulate commit by marking the slot's hdr in a real system;
    // here we verify by checking visibility at TS=10
    auto r = page.read(0, 10);  // beginTS=5 <= 10 < INF → visible
    TEST("slot 0 visible at TS=10", r.has_value());
    TEST("slot 0 value[1]=100", r.value()[1] == Value::makeInt64(100));

    TEST("slot 0 not visible at TS=3", !page.read(0, 3).has_value());

    // Scan
    auto rows = page.scan(10);
    TEST("scan returns 2 rows", rows.size() == 2);

    // Project
    auto proj = page.project({0,2}, 10);
    TEST("project returns 2 rows", proj.size() == 2);
    TEST("projected cols = 2", proj[0].second.size() == 2);

    // Update (seal old, insert new)
    SlotID s2 = page.update(0, makeRow(1,150,"Alice"), 2, 15, 15);
    TEST("update returns new slot", s2 == 2);
    TEST("old slot no longer visible at TS=20", !page.read(0, 20).has_value());
    TEST("new slot visible at TS=20", page.read(s2, 20).has_value());

    // Delete
    page.remove(1, 20);
    TEST("deleted slot not visible at TS=25", !page.read(1, 25).has_value());
}

void testDSMPage() {
    std::cout << "\n=== DSMPage Tests ===\n";

    auto schema = testSchema();
    DSMPage page(1, schema);

    page.insert(makeRow(1, 100, "X"), 1, 5);
    page.insert(makeRow(2, 200, "Y"), 1, 5);

    TEST("numRows = 2", page.numRows() == 2);

    auto v = page.readColumn(0, 1, 10);  // col=1 (value), slot=0
    TEST("readColumn(0,1) at TS=10 is 100", v.has_value() && v->num.i64 == 100);

    auto scanVals = page.scanColumn(1, 10);
    TEST("scanColumn returns 2 values", scanVals.size() == 2);
    TEST("first scanned value = 100", scanVals[0].num.i64 == 100);

    int64_t sum = page.aggregateSum(1, 10);
    TEST("SUM(value) = 300", sum == 300);

    auto row = page.readRow(1, 10);
    TEST("readRow reconstructs full tuple", row.has_value() && row->size() == 3);
}

void testPAXPage() {
    std::cout << "\n=== PAXPage Tests ===\n";

    auto schema = testSchema();
    PAXPage page(2, schema);

    page.insert(makeRow(1, 50,  "P"), 1, 5);
    page.insert(makeRow(2, 75,  "Q"), 1, 5);
    page.insert(makeRow(3, 100, "R"), 1, 5);

    TEST("numRows = 3", page.numRows() == 3);

    auto colVals = page.scanMiniPage(1, 10);
    TEST("scanMiniPage col=1 returns 3", colVals.size() == 3);
    TEST("first minipage value = 50", colVals[0].second.num.i64 == 50);

    auto row = page.readRow(2, 10);
    TEST("readRow slot=2 at TS=10", row.has_value());
    TEST("readRow name = R", row.value()[2].str == "R");

    auto allRows = page.scan(10);
    TEST("scan returns 3 rows", allRows.size() == 3);
}

void testSQLExecutor() {
    std::cout << "\n=== SQLExecutor Tests (NSM) ===\n";

    auto schema = testSchema();
    TransactionManager txnMgr;
    VersionChainIndex  vci;
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    // Insert
    auto t1 = txnMgr.begin();
    auto r1 = exec.execInsert(t1, makeRow(1, 10, "A"));
    auto r2 = exec.execInsert(t1, makeRow(2, 20, "B"));
    auto r3 = exec.execInsert(t1, makeRow(3, 30, "C"));
    TEST("insert returns success", r1.success && r2.success && r3.success);
    txnMgr.commit(t1, vci);

    // Select all
    auto t2 = txnMgr.begin();
    auto sel = exec.execSelect(t2, {0,1,2});
    TEST("select returns 3 rows", sel.rows.size() == 3);
    txnMgr.commit(t2, vci);

    // Select with predicate
    auto t3 = txnMgr.begin();
    auto selPred = exec.execSelect(t3, {0,1},
        { Predicate::gt(1, Value::makeInt64(15)) });
    TEST("select with GT predicate returns 2 rows", selPred.rows.size() == 2);
    txnMgr.commit(t3, vci);

    // Update
    auto t4 = txnMgr.begin();
    auto upd = exec.execUpdate(t4,
        { Predicate::eq(0, Value::makeInt64(2)) },
        { {1, Value::makeInt64(999)} });
    TEST("update affects 1 row", upd.rowsAffected == 1);
    txnMgr.commit(t4, vci);

    // Verify update
    auto t5 = txnMgr.begin();
    auto afterUpd = exec.execSelect(t5, {0,1},
        { Predicate::eq(0, Value::makeInt64(2)) });
    TEST("updated value = 999",
         afterUpd.rows.size() == 1 &&
         afterUpd.rows[0][1] == Value::makeInt64(999));
    txnMgr.commit(t5, vci);

    // Delete
    auto t6 = txnMgr.begin();
    auto del = exec.execDelete(t6, { Predicate::eq(0, Value::makeInt64(3)) });
    TEST("delete affects 1 row", del.rowsAffected == 1);
    txnMgr.commit(t6, vci);

    auto t7 = txnMgr.begin();
    auto afterDel = exec.execSelect(t7, {0});
    TEST("after delete: 2 rows remain", afterDel.rows.size() == 2);
    txnMgr.commit(t7, vci);

    // Aggregate
    auto t8 = txnMgr.begin();
    auto agg = exec.execAggSum(t8, 1);
    TEST("SUM(value) after update/delete = 999+10 = 1009",
         !agg.rows.empty() && agg.rows[0][0] == Value::makeInt64(1009));
    txnMgr.commit(t8, vci);
}

void testSnapshotIsolation() {
    std::cout << "\n=== Snapshot Isolation Tests ===\n";

    auto schema = testSchema();
    TransactionManager txnMgr;
    VersionChainIndex  vci;
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    auto seed = txnMgr.begin();
    exec.execInsert(seed, makeRow(1, 100, "orig"));
    txnMgr.commit(seed, vci);

    // Long-running snapshot txn
    auto snapTxn = txnMgr.begin(IsolationLevel::SNAPSHOT);

    // Another txn updates and commits
    auto updTxn = txnMgr.begin(IsolationLevel::SNAPSHOT);
    exec.execUpdate(updTxn,
        { Predicate::eq(0, Value::makeInt64(1)) },
        { {1, Value::makeInt64(999)} });
    txnMgr.commit(updTxn, vci);

    // snapTxn should still see 100
    auto r = exec.execSelect(snapTxn, {0,1},
        { Predicate::eq(0, Value::makeInt64(1)) });
    TEST("snapshot txn sees old value 100",
         r.rows.size() == 1 && r.rows[0][1] == Value::makeInt64(100));
    txnMgr.commit(snapTxn, vci);

    // New txn sees 999
    auto newTxn = txnMgr.begin(IsolationLevel::SNAPSHOT);
    r = exec.execSelect(newTxn, {0,1},
        { Predicate::eq(0, Value::makeInt64(1)) });
    TEST("new txn sees updated value 999",
         r.rows.size() == 1 && r.rows[0][1] == Value::makeInt64(999));
    txnMgr.commit(newTxn, vci);
}

void testConflictDetection() {
    std::cout << "\n=== Conflict Detection Tests ===\n";

    auto schema = testSchema();
    TransactionManager txnMgr;
    VersionChainIndex  vci;
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    auto seed = txnMgr.begin();
    exec.execInsert(seed, makeRow(99, 1, "conflict_test"));
    txnMgr.commit(seed, vci);

    auto txA = txnMgr.begin(IsolationLevel::SNAPSHOT);
    auto txB = txnMgr.begin(IsolationLevel::SNAPSHOT);

    exec.execUpdate(txA, { Predicate::eq(0, Value::makeInt64(99)) },
                    { {1, Value::makeInt64(42)} });
    txnMgr.commit(txA, vci);
    TEST("TxnA committed without conflict", txA->isCommitted());

    bool conflictCaught = false;
    try {
        exec.execUpdate(txB, { Predicate::eq(0, Value::makeInt64(99)) },
                        { {1, Value::makeInt64(77)} });
        txnMgr.commit(txB, vci);
    } catch (const TxnConflictError&) {
        conflictCaught = true;
    }
    TEST("TxnB detects write-write conflict", conflictCaught);
    TEST("TxnB is aborted after conflict", txB->isAborted());
}

void testGarbageCollection() {
    std::cout << "\n=== Garbage Collection Tests ===\n";

    auto schema = testSchema();
    TransactionManager txnMgr;
    VersionChainIndex  vci;
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    auto t1 = txnMgr.begin();
    exec.execInsert(t1, makeRow(77, 1, "gc_test"));
    txnMgr.commit(t1, vci);

    // Two more updates
    auto t2 = txnMgr.begin();
    exec.execUpdate(t2, { Predicate::eq(0, Value::makeInt64(77)) },
                    { {1, Value::makeInt64(2)} });
    txnMgr.commit(t2, vci);

    auto t3 = txnMgr.begin();
    exec.execUpdate(t3, { Predicate::eq(0, Value::makeInt64(77)) },
                    { {1, Value::makeInt64(3)} });
    txnMgr.commit(t3, vci);

    auto chain = vci.get(77);
    TEST("chain exists", chain != nullptr);
    size_t lenBefore = chain->length();
    TEST("chain has >= 2 versions", lenBefore >= 2);

    size_t removed = vci.gcAll(txnMgr.currentTS());
    TEST("GC removed at least 1 old version", removed >= 1);
    TEST("chain shorter after GC", chain->length() < lenBefore);
}

// ─────────────────────────────────────────────────────────────
//  main
// ─────────────────────────────────────────────────────────────
int main() {
    std::cout << "\n╔══════════════════════════════════════════╗\n";
    std::cout << "║  MVCC Version Chains Manager — Unit Tests ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    testVersionChain();
    testTransactionManager();
    testNSMPage();
    testDSMPage();
    testPAXPage();
    testSQLExecutor();
    testSnapshotIsolation();
    testConflictDetection();
    testGarbageCollection();

    std::cout << "\n═══════════════════════════════════════════\n";
    std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "═══════════════════════════════════════════\n\n";

    return failed == 0 ? 0 : 1;
}
