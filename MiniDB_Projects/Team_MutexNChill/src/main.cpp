#include <iostream>
#include <cstring>
#include <cstdio>
#include "storage/page.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "catalog/catalog.h"
#include "query/lexer.h"
#include "query/parser.h"
#include "query/executor.h"
#include "optimizer/optimizer.h"
#include "txn/transaction_manager.h"
#include "wal/wal.h"

// ---- Helpers ----

static void printSep(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

static void runSQL(const std::string& sql, Executor& exec, Optimizer& opt) {
    std::cout << "\n> " << sql << "\n";
    Lexer lexer(sql);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    ParseResult* result = parser.parse();

    if (result->type == StmtType::SELECT) {
        // Ask the optimizer for a plan and print the reasoning.
        QueryPlan plan = opt.plan(result->select);
        std::cout << "  Optimizer: " << plan.reason << "\n";

        auto rows = exec.executeSelect(result->select);
        if (rows.empty()) {
            std::cout << "  (no results)\n";
        } else {
            for (auto& r : rows) {
                std::cout << "  ";
                for (size_t i = 0; i < r.values.size(); i++) {
                    if (i) std::cout << " | ";
                    std::cout << r.col_names[i] << "=" << r.values[i];
                }
                std::cout << "\n";
            }
        }
    } else if (result->type == StmtType::INSERT) {
        Row row = exec.executeInsert(result->insert);
        std::cout << "  Inserted: id=" << row.id << " name=" << row.name << "\n";
    } else if (result->type == StmtType::DELETE) {
        int n = exec.executeDelete(result->del);
        std::cout << "  Deleted " << n << " row(s)\n";
    }
    delete result;
}

// ---- Demo sections ----

static void demoStorage() {
    printSep("1. Storage Engine");

    // Clean up any leftover files from a previous run.
    remove("demo.db");

    HeapFile hf("demo.db");
    std::cout << "Allocated page 0 for demo.db\n";

    // Insert 5 rows directly (bypassing SQL).
    const char* names[] = {"Alice", "Bob", "Charlie", "Diana", "Eve"};
    int ages[] = {20, 17, 22, 25, 28};
    for (int i = 0; i < 5; i++) {
        Row r;
        r.id = i + 1; r.age = ages[i]; r.extra = 0; r.is_valid = true;
        strncpy(r.name, names[i], 31); r.name[31] = '\0';
        RID rid = hf.insertRow(r);
        std::cout << "  Inserted row id=" << r.id << " at page=" << rid.page_id
                  << " slot=" << rid.slot << "\n";
    }

    std::cout << "Full scan:\n";
    for (auto& r : hf.scanAll()) {
        std::cout << "  id=" << r.id << " name=" << r.name << " age=" << r.age << "\n";
    }

    // Delete row id=2 (Bob). We need its RID; for this demo we know it's {0, 1}.
    hf.deleteRow({0, 1});
    std::cout << "After deleting Bob (slot 1):\n";
    for (auto& r : hf.scanAll()) {
        std::cout << "  id=" << r.id << " name=" << r.name << "\n";
    }
}

static void demoBPlusTree() {
    printSep("2. B+ Tree Index");

    BPlusTree tree;

    // Insert keys 1–7 with fake RIDs.
    std::cout << "Inserting keys 1-7...\n";
    for (int k = 1; k <= 7; k++) {
        tree.insert(k, {0, k - 1});
    }

    // Point search
    RID found;
    if (tree.search(4, found)) {
        std::cout << "  search(4) -> page=" << found.page_id << " slot=" << found.slot << "\n";
    }
    if (!tree.search(99, found)) {
        std::cout << "  search(99) -> not found\n";
    }

    // Range search
    std::vector<RID> range = tree.rangeSearch(3, 6);
    std::cout << "  rangeSearch(3,6) -> " << range.size() << " results: ";
    for (auto& r : range) std::cout << "(p=" << r.page_id << " s=" << r.slot << ") ";
    std::cout << "\n";

    // Delete
    tree.remove(4);
    if (!tree.search(4, found)) {
        std::cout << "  After remove(4): search(4) -> not found\n";
    }
}

static void demoSQL() {
    printSep("3. SQL Query Engine + Optimizer");

    // Clean up old db files.
    remove("students.db");
    remove("departments.db");

    Catalog cat;
    cat.createTable("students");
    cat.createTable("departments");

    Executor  exec(cat);
    Optimizer opt(cat);

    // Insert departments
    runSQL("INSERT INTO departments VALUES (1, 'CS', 0, 0)",    exec, opt);
    runSQL("INSERT INTO departments VALUES (2, 'EE', 0, 0)",    exec, opt);
    runSQL("INSERT INTO departments VALUES (3, 'Math', 0, 0)",  exec, opt);

    // Insert students
    runSQL("INSERT INTO students VALUES (1, 'Alice', 20, 1)",   exec, opt);
    runSQL("INSERT INTO students VALUES (2, 'Bob', 17, 2)",     exec, opt);
    runSQL("INSERT INTO students VALUES (3, 'Charlie', 22, 1)", exec, opt);
    runSQL("INSERT INTO students VALUES (4, 'Diana', 25, 2)",   exec, opt);
    runSQL("INSERT INTO students VALUES (5, 'Eve', 28, 3)",     exec, opt);

    // SeqScan: age predicate, not on indexed column
    runSQL("SELECT name FROM students WHERE age > 20", exec, opt);

    // IndexScan: equality on primary key id
    runSQL("SELECT name FROM students WHERE id = 3", exec, opt);

    // DELETE
    runSQL("DELETE FROM students WHERE id = 2", exec, opt);
    runSQL("SELECT name FROM students WHERE age > 15", exec, opt);

    // JOIN
    runSQL("SELECT students.name, departments.name FROM students JOIN departments ON students.extra = departments.id",
           exec, opt);
}

static void demoTransactions() {
    printSep("4. Transaction Manager — 2PL + Deadlock Detection");

    TransactionManager tm;

    // Scenario 1: Basic lock acquisition
    std::cout << "\n-- Scenario A: Basic locking --\n";
    int t1 = tm.begin();
    int t2 = tm.begin();
    std::cout << "TxID " << t1 << ": BEGIN\n";
    std::cout << "TxID " << t2 << ": BEGIN\n";

    tm.acquireExclusive(t1, "students", 1);
    std::cout << "TxID " << t1 << ": acquired EXCLUSIVE lock on students:1\n";

    tm.acquireExclusive(t2, "students", 2);
    std::cout << "TxID " << t2 << ": acquired EXCLUSIVE lock on students:2\n";

    // Scenario 2: Deadlock
    std::cout << "\n-- Scenario B: Deadlock detection --\n";
    // T1 wants students:2 (held by T2) -> T1 now waits for T2
    tm.registerWait(t1, t2);
    std::cout << "TxID " << t1 << ": wants students:2 (held by TxID " << t2 << ") -> waiting\n";

    // T2 wants students:1 (held by T1) -> cycle: T2 -> T1 -> T2
    std::cout << "TxID " << t2 << ": wants students:1 (held by TxID " << t1 << ") -> ";
    try {
        tm.acquireExclusive(t2, "students", 1);
        std::cout << "no conflict (unexpected)\n";
    } catch (DeadlockException& e) {
        std::cout << "DEADLOCK DETECTED! " << e.what() << "\n";
        tm.abort(t2);
        std::cout << "TxID " << t2 << ": aborted, all locks released\n";
    } catch (LockConflictException& e) {
        std::cout << "lock conflict: " << e.what() << "\n";
    }

    tm.commit(t1);
    std::cout << "TxID " << t1 << ": committed, all locks released\n";
}

static void demoWAL() {
    printSep("5. WAL + Crash Recovery");

    // Clean up from previous runs.
    remove("wal_demo.db");
    remove("minidb.wal");

    HeapFile hf("wal_demo.db");
    BPlusTree idx;
    WalManager wal("minidb.wal");

    // Transaction 1: insert Grace — COMMITS successfully
    std::cout << "\nTransaction 1: inserting Grace (will COMMIT)\n";
    int t1 = 1;
    wal.logBegin(t1);
    Row grace; grace.id=7; grace.age=21; grace.extra=2; grace.is_valid=true;
    strncpy(grace.name, "Grace", 31); grace.name[31] = '\0';
    wal.logInsert(t1, "wal_demo", grace);
    wal.logCommit(t1);
    RID rid = hf.insertRow(grace);
    idx.insert(grace.id, rid);

    // Transaction 2: insert Hank — CRASH before COMMIT
    std::cout << "Transaction 2: inserting Hank (will CRASH before commit)\n";
    int t2 = 2;
    wal.logBegin(t2);
    Row hank; hank.id=8; hank.age=22; hank.extra=1; hank.is_valid=true;
    strncpy(hank.name, "Hank", 31); hank.name[31] = '\0';
    wal.logInsert(t2, "wal_demo", hank);
    // << CRASH here — no COMMIT logged for T2 >>

    std::cout << "\nWAL log before crash:\n";
    wal.printLog();

    std::cout << "\nSimulating crash (truncating last log record)...\n";
    wal.simulateCrash();

    std::cout << "WAL log after crash simulation:\n";
    wal.printLog();

    // Wipe the db file to simulate data loss (only WAL survives).
    hf.flush();
    remove("wal_demo.db");

    // Recovery: rebuild from WAL.
    std::cout << "\nRunning recovery from WAL...\n";
    HeapFile hf2("wal_demo.db");
    BPlusTree idx2;
    std::map<std::string, HeapFile*>  heaps   = {{"wal_demo", &hf2}};
    std::map<std::string, BPlusTree*> indexes = {{"wal_demo", &idx2}};
    int redone = wal.recover(heaps, indexes);
    std::cout << "Redone " << redone << " operation(s)\n";

    // Check results
    RID r;
    std::cout << "Grace (id=7) in recovered DB: " << (idx2.search(7, r) ? "YES" : "NO") << "\n";
    std::cout << "Hank  (id=8) in recovered DB: " << (idx2.search(8, r) ? "YES (wrong!)" : "NO (correct — was not committed)") << "\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "         MiniDB — Capstone Demo         \n";
    std::cout << "========================================\n";

    demoStorage();
    demoBPlusTree();
    demoSQL();
    demoTransactions();
    demoWAL();

    std::cout << "\n========================================\n";
    std::cout << "            Demo complete.\n";
    std::cout << "  See benchmarks/ for the MVCC benchmark.\n";
    std::cout << "========================================\n";
    return 0;
}
