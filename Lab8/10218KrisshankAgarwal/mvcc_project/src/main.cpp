// ============================================================
//  MVCC Version Chains Manager — Main Demo & Test Suite
//  Demonstrates NSM, DSM, PAX page layouts with full MVCC
//  transaction semantics (INSERT / SELECT / UPDATE / DELETE)
// ============================================================

#include "mvcc_types.h"
#include "version_chain.h"
#include "transaction_manager.h"
#include "page_layouts.h"
#include "sql_executor.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <cassert>
#include <chrono>

using namespace mvcc;

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
static void banner(const std::string& title) {
    std::string line(60, '=');
    std::cout << "\n" << line << "\n"
              << "  " << title << "\n"
              << line << "\n";
}

static void section(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n";
}

static TableSchema makeEmployeeSchema() {
    TableSchema s;
    s.tableName    = "employees";
    s.primaryKeyCol = 0;   // emp_id
    s.columns = {
        ColumnDef("emp_id",   DataType::INT64),
        ColumnDef("name",     DataType::VARCHAR, 64),
        ColumnDef("dept",     DataType::VARCHAR, 32),
        ColumnDef("salary",   DataType::INT64),
        ColumnDef("active",   DataType::BOOL),
    };
    return s;
}

// Helper to build an employee row
static Tuple emp(int64_t id, const std::string& name,
                 const std::string& dept, int64_t salary, bool active = true) {
    return {
        Value::makeInt64(id),
        Value::makeVarchar(name),
        Value::makeVarchar(dept),
        Value::makeInt64(salary),
        Value::makeBool(active),
    };
}

// ─────────────────────────────────────────────────────────────
//  Demo 1 — Basic CRUD on each page layout
// ─────────────────────────────────────────────────────────────
template<typename ExecT>
void demoCRUD(ExecT& exec, TransactionManager& txnMgr,
              VersionChainIndex& vci, const std::string& layoutName) {
    banner("DEMO 1: Basic CRUD — " + layoutName);

    // INSERT 5 employees
    section("INSERT");
    auto txn1 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    exec.execInsert(txn1, emp(1, "Alice",   "Engineering", 95000));
    exec.execInsert(txn1, emp(2, "Bob",     "Marketing",   72000));
    exec.execInsert(txn1, emp(3, "Carol",   "Engineering", 88000));
    exec.execInsert(txn1, emp(4, "Dave",    "HR",          65000));
    exec.execInsert(txn1, emp(5, "Eve",     "Engineering", 102000));
    txnMgr.commit(txn1, vci);
    std::cout << "Inserted 5 employees (committed)\n";

    // SELECT *
    section("SELECT * FROM employees");
    auto txn2 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    std::vector<ColumnID> allCols = {0,1,2,3,4};
    auto r = exec.execSelect(txn2, allCols);
    r.print();
    txnMgr.commit(txn2, vci);

    // UPDATE salary for Engineering dept
    section("UPDATE salary += 10000 WHERE dept = 'Engineering'");
    auto txn3 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    std::vector<Predicate> whereEng = { Predicate::eq(2, Value::makeVarchar("Engineering")) };
    auto upd = exec.execUpdate(txn3, whereEng, { {3, Value::makeInt64(105000)} });
    std::cout << "Rows updated: " << upd.rowsAffected << "\n";
    txnMgr.commit(txn3, vci);

    // SELECT to verify update
    section("SELECT after UPDATE");
    auto txn4 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    r = exec.execSelect(txn4, {0,1,2,3});
    r.print();
    txnMgr.commit(txn4, vci);

    // DELETE Dave
    section("DELETE WHERE emp_id = 4");
    auto txn5 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    auto del = exec.execDelete(txn5, { Predicate::eq(0, Value::makeInt64(4)) });
    std::cout << "Rows deleted: " << del.rowsAffected << "\n";
    txnMgr.commit(txn5, vci);

    // SELECT final
    section("SELECT * after DELETE");
    auto txn6 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    r = exec.execSelect(txn6, allCols);
    r.print();
    txnMgr.commit(txn6, vci);

    // Aggregate SUM(salary)
    section("SELECT SUM(salary)");
    auto txn7 = txnMgr.begin(IsolationLevel::SNAPSHOT);
    auto agg = exec.execAggSum(txn7, 3);
    agg.print();
    txnMgr.commit(txn7, vci);
}

// ─────────────────────────────────────────────────────────────
//  Demo 2 — Snapshot Isolation: concurrent visibility
// ─────────────────────────────────────────────────────────────
void demoSnapshotIsolation(TransactionManager& txnMgr, VersionChainIndex& vci) {
    banner("DEMO 2: Snapshot Isolation — Concurrent Visibility");

    TableSchema schema = makeEmployeeSchema();
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    // Seed one row
    auto seed = txnMgr.begin();
    exec.execInsert(seed, emp(10, "Mallory", "Security", 80000));
    txnMgr.commit(seed, vci);

    // Txn A starts — takes snapshot
    auto txnA = txnMgr.begin(IsolationLevel::SNAPSHOT);
    std::cout << "TxnA begins (beginTS=" << txnA->beginTS << ")\n";

    // Txn B updates Mallory's salary and commits BEFORE A reads
    auto txnB = txnMgr.begin(IsolationLevel::SNAPSHOT);
    exec.execUpdate(txnB,
        { Predicate::eq(0, Value::makeInt64(10)) },
        { {3, Value::makeInt64(99999)} });
    txnMgr.commit(txnB, vci);
    std::cout << "TxnB committed update (salary→99999)\n";

    // TxnA reads — should still see 80000 (snapshot from its beginTS)
    section("TxnA SELECT (should see old salary=80000)");
    auto r = exec.execSelect(txnA, {0,1,3});
    r.print();
    txnMgr.commit(txnA, vci);

    // New txn reads — sees latest committed (99999)
    section("New TxnC SELECT (should see new salary=99999)");
    auto txnC = txnMgr.begin(IsolationLevel::SNAPSHOT);
    r = exec.execSelect(txnC, {0,1,3});
    r.print();
    txnMgr.commit(txnC, vci);
}

// ─────────────────────────────────────────────────────────────
//  Demo 3 — Write-Write Conflict Detection & Rollback
// ─────────────────────────────────────────────────────────────
void demoConflict(TransactionManager& txnMgr, VersionChainIndex& vci) {
    banner("DEMO 3: Write-Write Conflict Detection & Rollback");

    TableSchema schema = makeEmployeeSchema();
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    auto seed = txnMgr.begin();
    exec.execInsert(seed, emp(20, "Zara", "Finance", 90000));
    txnMgr.commit(seed, vci);

    auto txnX = txnMgr.begin(IsolationLevel::SNAPSHOT);
    auto txnY = txnMgr.begin(IsolationLevel::SNAPSHOT);

    std::cout << "TxnX beginTS=" << txnX->beginTS
              << "  TxnY beginTS=" << txnY->beginTS << "\n";

    // Both update the same row
    exec.execUpdate(txnX,
        { Predicate::eq(0, Value::makeInt64(20)) },
        { {3, Value::makeInt64(95000)} });
    std::cout << "TxnX updated Zara's salary → 95000\n";

    // TxnX commits first
    txnMgr.commit(txnX, vci);
    std::cout << "TxnX committed\n";

    // TxnY tries to update the same row → conflict
    std::cout << "TxnY attempts update on same row…\n";
    try {
        exec.execUpdate(txnY,
            { Predicate::eq(0, Value::makeInt64(20)) },
            { {3, Value::makeInt64(77000)} });
        txnMgr.commit(txnY, vci);
        std::cout << "TxnY committed (unexpected!)\n";
    } catch (const TxnConflictError& e) {
        std::cout << "✓ TxnConflictError caught: " << e.what() << "\n";
        std::cout << "  TxnY aborted successfully\n";
    }

    section("Final state after conflict resolution");
    auto txnZ = txnMgr.begin();
    exec.execSelect(txnZ, {0,1,3}).print();
    txnMgr.commit(txnZ, vci);
}

// ─────────────────────────────────────────────────────────────
//  Demo 4 — Version Chain GC (Garbage Collection)
// ─────────────────────────────────────────────────────────────
void demoGC(TransactionManager& txnMgr, VersionChainIndex& vci) {
    banner("DEMO 4: Version Chain Garbage Collection");

    TableSchema schema = makeEmployeeSchema();
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    auto t1 = txnMgr.begin();
    exec.execInsert(t1, emp(30, "Noah", "Ops", 60000));
    txnMgr.commit(t1, vci);

    // Three successive updates
    for (int i = 1; i <= 3; ++i) {
        auto t = txnMgr.begin();
        exec.execUpdate(t, { Predicate::eq(0, Value::makeInt64(30)) },
                        { {3, Value::makeInt64(60000 + i*5000)} });
        txnMgr.commit(t, vci);
        std::cout << "Update " << i << ": salary → " << (60000 + i*5000) << "\n";
    }

    auto chain = vci.get(30);
    if (chain) {
        std::cout << "\nVersion chain length before GC: " << chain->length() << "\n";
        chain->dump(schema);
    }

    // GC with current horizon (all txns done → all old versions purgeable)
    size_t removed = vci.gcAll(txnMgr.currentTS());
    std::cout << "\nGC removed " << removed << " old version(s)\n";

    if (chain) {
        std::cout << "Version chain length after GC:  " << chain->length() << "\n";
        chain->dump(schema);
    }
}

// ─────────────────────────────────────────────────────────────
//  Demo 5 — Page Layout Comparison
// ─────────────────────────────────────────────────────────────
void demoPageLayoutComparison() {
    banner("DEMO 5: Page Layout Physical Structure Comparison");

    TableSchema schema = makeEmployeeSchema();
    TransactionManager txnMgr;
    VersionChainIndex  vci;

    NSMExecutor nsmExec(PageLayout::NSM, schema, txnMgr, vci);
    DSMExecutor dsmExec(PageLayout::DSM, schema, txnMgr, vci);
    PAXExecutor paxExec(PageLayout::PAX, schema, txnMgr, vci);

    // Insert same 3 rows into each layout
    std::vector<Tuple> rows = {
        emp(100, "Lena",  "Eng",  88000),
        emp(101, "Milo",  "Sales",75000),
        emp(102, "Nina",  "Eng",  92000),
    };

    for (auto& layout_name : std::vector<std::string>{"NSM","DSM","PAX"}) {
        auto txn = txnMgr.begin();
        if (layout_name == "NSM") for (auto& r : rows) nsmExec.execInsert(txn, r);
        if (layout_name == "DSM") for (auto& r : rows) dsmExec.execInsert(txn, r);
        if (layout_name == "PAX") for (auto& r : rows) paxExec.execInsert(txn, r);
        txnMgr.commit(txn, vci);
    }

    section("NSM Physical Page Dump");
    nsmExec.dumpPages();

    section("DSM Physical Page Dump");
    dsmExec.dumpPages();

    section("PAX Physical Page Dump");
    paxExec.dumpPages();

    // Show which layout is best for each query type
    section("Layout Suitability Summary");
    std::cout << std::left;
    std::cout << std::setw(32) << "Query Pattern"
              << std::setw(10) << "NSM"
              << std::setw(10) << "DSM"
              << std::setw(10) << "PAX" << "\n";
    std::cout << std::string(62, '-') << "\n";
    auto row = [](const char* q, const char* a, const char* b, const char* c){
        std::cout << std::setw(32) << q
                  << std::setw(10) << a
                  << std::setw(10) << b
                  << std::setw(10) << c << "\n";
    };
    row("PK Lookup (SELECT by ID)", "★★★", "★", "★★");
    row("Full Row Fetch",           "★★★", "★",  "★★");
    row("Column Scan (AGG/OLAP)",   "★",   "★★★","★★");
    row("Projection (few cols)",    "★",   "★★★","★★★");
    row("Write-heavy OLTP",         "★★★", "★",  "★★");
    row("Mixed HTAP workload",      "★★",  "★★", "★★★");
    row("Cache locality (cols)",    "★",   "★★★","★★★");
    row("Update (full row)",        "★★★", "★",  "★★");
}

// ─────────────────────────────────────────────────────────────
//  Demo 6 — Read Committed vs Snapshot comparison
// ─────────────────────────────────────────────────────────────
void demoIsolationLevels(TransactionManager& txnMgr, VersionChainIndex& vci) {
    banner("DEMO 6: Read Committed vs Snapshot Isolation");

    TableSchema schema = makeEmployeeSchema();
    NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);

    auto seed = txnMgr.begin();
    exec.execInsert(seed, emp(50, "Iris", "Data", 70000));
    txnMgr.commit(seed, vci);

    // Long-running READ COMMITTED txn
    auto rcTxn = txnMgr.begin(IsolationLevel::READ_COMMITTED);
    // Long-running SNAPSHOT txn
    auto siTxn = txnMgr.begin(IsolationLevel::SNAPSHOT);

    std::cout << "rcTxn beginTS=" << rcTxn->beginTS
              << "  siTxn beginTS=" << siTxn->beginTS << "\n";

    // Another txn updates in between
    auto mid = txnMgr.begin();
    exec.execUpdate(mid,
        { Predicate::eq(0, Value::makeInt64(50)) },
        { {3, Value::makeInt64(85000)} });
    txnMgr.commit(mid, vci);
    std::cout << "Intermediate txn committed: salary → 85000\n\n";

    section("READ COMMITTED txn reads (sees 85000 — latest committed)");
    exec.execSelect(rcTxn, {0,1,3}).print();

    section("SNAPSHOT txn reads (sees 70000 — its own snapshot)");
    exec.execSelect(siTxn, {0,1,3}).print();

    txnMgr.commit(rcTxn, vci);
    txnMgr.commit(siTxn, vci);
}

// ─────────────────────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────────────────────
int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║   MVCC Version Chains Manager in C++                    ║\n";
    std::cout << "║   Page Layouts: NSM | DSM | PAX                         ║\n";
    std::cout << "║   For SQL Queries in Database Management Systems        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    // ── Demo 1: CRUD across all 3 layouts ──────────────────────
    {
        TableSchema schema = makeEmployeeSchema();
        {
            TransactionManager txnMgr;
            VersionChainIndex  vci;
            NSMExecutor exec(PageLayout::NSM, schema, txnMgr, vci);
            demoCRUD(exec, txnMgr, vci, "NSM (Row Store)");
        }
        {
            TransactionManager txnMgr;
            VersionChainIndex  vci;
            DSMExecutor exec(PageLayout::DSM, schema, txnMgr, vci);
            demoCRUD(exec, txnMgr, vci, "DSM (Column Store)");
        }
        {
            TransactionManager txnMgr;
            VersionChainIndex  vci;
            PAXExecutor exec(PageLayout::PAX, schema, txnMgr, vci);
            demoCRUD(exec, txnMgr, vci, "PAX (Hybrid)");
        }
    }

    // ── Demo 2: Snapshot Isolation ─────────────────────────────
    {
        TransactionManager txnMgr;
        VersionChainIndex  vci;
        demoSnapshotIsolation(txnMgr, vci);
    }

    // ── Demo 3: Write-Write Conflict ───────────────────────────
    {
        TransactionManager txnMgr;
        VersionChainIndex  vci;
        demoConflict(txnMgr, vci);
    }

    // ── Demo 4: GC ─────────────────────────────────────────────
    {
        TransactionManager txnMgr;
        VersionChainIndex  vci;
        demoGC(txnMgr, vci);
    }

    // ── Demo 5: Layout Comparison ──────────────────────────────
    demoPageLayoutComparison();

    // ── Demo 6: Isolation Levels ───────────────────────────────
    {
        TransactionManager txnMgr;
        VersionChainIndex  vci;
        demoIsolationLevels(txnMgr, vci);
    }

    banner("ALL DEMOS COMPLETED SUCCESSFULLY");
    return 0;
}
