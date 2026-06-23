/**
 * ═══════════════════════════════════════════════════════════════════════
 * MiniDB — Full System Demonstration
 * ═══════════════════════════════════════════════════════════════════════
 *
 * This file exercises ALL components of the MiniDB database system:
 *
 *   1. SQL Parsing:       Lexer → Parser → AST
 *   2. Cost-Based Optimizer: Access path & join order selection
 *   3. Volcano Execution:  TableScan, IndexScan, Filter, NLJ
 *   4. Transactions:       Strict 2PL with lock acquisition
 *   5. Deadlock Detection: Wait-For Graph cycle detection
 *   6. WAL + Recovery:     Crash simulation with REDO/UNDO
 *   7. Track A Benchmark:  Row-at-a-time vs batch throughput
 *
 * Each section prints clear output suitable for a live demo.
 */

#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <random>
#include <unordered_set>

#include "Record.h"
#include "DiskManager.h"
#include "BufferPool.h"
#include "BPlusTree.h"
#include "Table.h"
#include "PlanNode.h"
#include "TableScanNode.h"
#include "IndexScanNode.h"
#include "FilterNode.h"
#include "Lexer.h"
#include "Parser.h"
#include "Catalog.h"
#include "CatalogManager.h"
#include "LockManager.h"
#include "LogRecord.h"
#include "LogManager.h"
#include "DeadlockDetector.h"
#include "RecoveryManager.h"
#include "Transaction.h"
#include "Optimizer.h"

// ═══════════════════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════════════════

static void printSeparator(const std::string &title) {
  std::cout << "\n╔══════════════════════════════════════════════════════════╗"
            << std::endl;
  std::cout << "║  " << title;
  for (size_t i = title.size(); i < 55; i++)
    std::cout << " ";
  std::cout << "║" << std::endl;
  std::cout << "╚══════════════════════════════════════════════════════════╝"
            << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 1: SQL Parsing (Lexer → Parser → AST)
// ═══════════════════════════════════════════════════════════════════════

static void demoSQLParsing() {
  printSeparator("DEMO 1: SQL PARSING (Lexer -> Parser -> AST)");

  std::vector<std::string> queries = {
      "SELECT * FROM employees WHERE id = 42",
      "INSERT INTO employees VALUES (100, 5000)",
      "DELETE FROM employees WHERE id = 99",
      "SELECT * FROM employees JOIN departments ON employees.id = departments.id",
      "SELECT * FROM orders WHERE val > 500"};

  for (const auto &sql : queries) {
    std::cout << "\nSQL: " << sql << std::endl;

    // Lexer
    Lexer lexer(sql);
    auto tokens = lexer.tokenize();
    std::cout << "  Tokens: " << tokens.size() - 1 << " (excluding EOF)"
              << std::endl;

    // Parser
    Parser parser(tokens);
    auto ast = parser.parse();
    std::cout << "  AST:    " << ast->toString() << std::endl;
  }
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 2: Insert + Select via Execution Engine
// ═══════════════════════════════════════════════════════════════════════

static void demoInsertAndSelect() {
  printSeparator("DEMO 2: INSERT + SELECT via Volcano Engine");

  // Clean up any leftover files
  std::remove("demo_employees.db");

  Table employees("employees", "demo_employees.db");
  Catalog catalog;
  catalog.addTable("employees", &employees);

  // Insert records via parsed SQL
  std::cout << "\n--- Inserting 20 records via SQL ---" << std::endl;
  for (int i = 1; i <= 20; i++) {
    std::string sql = "INSERT INTO employees VALUES (" + std::to_string(i) +
                      ", " + std::to_string(i * 100) + ")";

    Lexer lexer(sql);
    Parser parser(lexer.tokenize());
    auto ast = parser.parse();

    auto *insert = dynamic_cast<InsertNode *>(ast.get());
    if (insert && insert->values.size() >= 2) {
      Record r(insert->values[0], insert->values[1]);
      employees.insertRecord(r);
    }
  }
  std::cout << "  Inserted " << employees.getNumRows() << " records."
            << std::endl;

  // SELECT * FROM employees (TableScan)
  std::cout << "\n--- SELECT * FROM employees (first 5 rows) ---" << std::endl;
  {
    TableScanNode scan(&employees);
    scan.open();
    int count = 0;
    while (scan.hasNext() && count < 5) {
      Record r = scan.next();
      std::cout << "  " << r.toString() << std::endl;
      count++;
    }
    scan.close();
    std::cout << "  ... (" << employees.getNumRows() << " total rows)"
              << std::endl;
  }

  // SELECT * FROM employees WHERE id = 10 (IndexScan)
  std::cout << "\n--- SELECT * FROM employees WHERE id = 10 (IndexScan) ---"
            << std::endl;
  {
    IndexScanNode iscan(&employees, 10);
    iscan.open();
    if (iscan.hasNext()) {
      Record r = iscan.next();
      std::cout << "  Found: " << r.toString() << std::endl;
    }
    iscan.close();
  }

  // SELECT * FROM employees WHERE val > 1500 (TableScan + Filter)
  std::cout << "\n--- SELECT * FROM employees WHERE val > 1500 ---"
            << std::endl;
  {
    auto scan = std::make_unique<TableScanNode>(&employees);
    FilterNode filter(std::move(scan), "val", CompOp::GREATER, 1500);
    filter.open();
    while (filter.hasNext()) {
      Record r = filter.next();
      std::cout << "  " << r.toString() << std::endl;
    }
    filter.close();
  }

  employees.flush();
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 3: Cost-Based Optimizer
// ═══════════════════════════════════════════════════════════════════════

static void demoCostBasedOptimizer() {
  printSeparator("DEMO 3: COST-BASED OPTIMIZER");

  std::remove("cbo_employees.db");
  std::remove("cbo_departments.db");

  Table employees("employees", "cbo_employees.db");
  Table departments("departments", "cbo_departments.db");

  // Insert data: 1000 employees, 10 departments
  for (int i = 1; i <= 1000; i++) {
    employees.insertRecord(Record(i, i * 10));
  }
  for (int i = 1; i <= 10; i++) {
    departments.insertRecord(Record(i, i * 100));
  }

  Catalog catalog;
  catalog.addTable("employees", &employees);
  catalog.addTable("departments", &departments);

  Optimizer optimizer(catalog);

  // Test 1: Equality on indexed column → IndexScan
  std::cout << "\n--- Query 1: Equality on indexed column ---" << std::endl;
  {
    std::string sql = "SELECT * FROM employees WHERE id = 500";
    Lexer lexer(sql);
    Parser parser(lexer.tokenize());
    auto ast = parser.parse();

    auto plan = optimizer.optimize(ast.get());
    std::cout << optimizer.getExplanation() << std::endl;

    if (plan) {
      plan->open();
      while (plan->hasNext()) {
        Record r = plan->next();
        std::cout << "  Result: " << r.toString() << std::endl;
      }
      plan->close();
    }
  }

  // Test 2: Non-indexed column → TableScan + Filter
  std::cout << "\n--- Query 2: Range on non-indexed column ---" << std::endl;
  {
    std::string sql = "SELECT * FROM employees WHERE val > 9950";
    Lexer lexer(sql);
    Parser parser(lexer.tokenize());
    auto ast = parser.parse();

    auto plan = optimizer.optimize(ast.get());
    std::cout << optimizer.getExplanation() << std::endl;

    if (plan) {
      plan->open();
      int count = 0;
      while (plan->hasNext()) {
        Record r = plan->next();
        std::cout << "  Result: " << r.toString() << std::endl;
        count++;
      }
      plan->close();
      std::cout << "  (" << count << " rows matched)" << std::endl;
    }
  }

  // Test 3: Join with join order optimization
  std::cout << "\n--- Query 3: JOIN with order optimization ---" << std::endl;
  {
    std::string sql = "SELECT * FROM employees JOIN departments "
                      "ON employees.id = departments.id";
    Lexer lexer(sql);
    Parser parser(lexer.tokenize());
    auto ast = parser.parse();

    auto plan = optimizer.optimize(ast.get());
    std::cout << optimizer.getExplanation() << std::endl;

    if (plan) {
      plan->open();
      int count = 0;
      while (plan->hasNext()) {
        Record r = plan->next();
        if (count < 5) {
          std::cout << "  Result: " << r.toString() << std::endl;
        }
        count++;
      }
      plan->close();
      std::cout << "  (" << count << " join matches total)" << std::endl;
    }
  }

  employees.flush();
  departments.flush();
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 4: Transaction Management (S2PL)
// ═══════════════════════════════════════════════════════════════════════

static void demoTransactions() {
  printSeparator("DEMO 4: STRICT 2PL TRANSACTIONS");

  LockManager lockMgr;

  // Simulate two transactions accessing records
  Transaction txn1(1);
  Transaction txn2(2);

  int64_t rid1 = LockManager::encodeRid(0, 100); // Table 0, Record 100
  int64_t rid2 = LockManager::encodeRid(0, 200); // Table 0, Record 200

  // Txn1 acquires S lock on record 100
  std::cout << "\nTxn1: Requesting S lock on record 100..." << std::endl;
  bool granted = lockMgr.lock(&txn1, rid1, LockMode::SHARED);
  std::cout << "  " << (granted ? "GRANTED" : "DENIED") << std::endl;

  // Txn2 also acquires S lock on record 100 (compatible!)
  std::cout << "Txn2: Requesting S lock on record 100..." << std::endl;
  granted = lockMgr.lock(&txn2, rid1, LockMode::SHARED);
  std::cout << "  " << (granted ? "GRANTED (S+S compatible)" : "DENIED")
            << std::endl;

  // Txn1 tries to upgrade to X lock (blocked by Txn2's S lock)
  std::cout << "Txn1: Requesting X lock on record 100 (upgrade)..."
            << std::endl;
  granted = lockMgr.lock(&txn1, rid1, LockMode::EXCLUSIVE);
  std::cout << "  " << (granted ? "GRANTED" : "DENIED (Txn2 holds S lock)")
            << std::endl;

  // Txn2 releases its S lock
  std::cout << "Txn2: Releasing S lock on record 100..." << std::endl;
  lockMgr.unlock(&txn2, rid1);

  // Now Txn1 can get X lock
  std::cout << "Txn1: Retrying X lock on record 100..." << std::endl;
  granted = lockMgr.lock(&txn1, rid1, LockMode::EXCLUSIVE);
  std::cout << "  " << (granted ? "GRANTED (no other holders)" : "DENIED")
            << std::endl;

  // Txn1 acquires X lock on record 200
  std::cout << "Txn1: Requesting X lock on record 200..." << std::endl;
  granted = lockMgr.lock(&txn1, rid2, LockMode::EXCLUSIVE);
  std::cout << "  " << (granted ? "GRANTED" : "DENIED") << std::endl;

  // Commit Txn1 → release all locks
  std::cout << "\nTxn1: COMMIT (releasing all locks)..." << std::endl;
  txn1.setState(TxnState::COMMITTED);
  lockMgr.unlockAll(&txn1);
  std::cout << "  State: " << txn1.stateToString() << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 5: Deadlock Detection
// ═══════════════════════════════════════════════════════════════════════

static void demoDeadlockDetection() {
  printSeparator("DEMO 5: DEADLOCK DETECTION (Wait-For Graph)");

  DeadlockDetector detector;

  std::cout << "\nScenario: Txn1 holds r1, wants r2. Txn2 holds r2, wants r1."
            << std::endl;

  // Txn1 waits for Txn2
  std::cout << "Adding edge: Txn1 → Txn2 (Txn1 waits for Txn2's lock)"
            << std::endl;
  detector.addEdge(1, 2);

  // Check if Txn2 waiting for Txn1 would cause a cycle
  bool wouldDeadlock = detector.wouldCauseCycle(2, 1);
  std::cout << "Would Txn2 → Txn1 cause deadlock? "
            << (wouldDeadlock ? "YES — DEADLOCK DETECTED!" : "No") << std::endl;

  // Add the edge anyway for victim selection demo
  detector.addEdge(2, 1);
  int victim = detector.detectAndChooseVictim();
  std::cout << "Victim for abort: Txn " << victim << " (youngest transaction)"
            << std::endl;

  // Three-way deadlock
  std::cout << "\nScenario: Three-way deadlock (Txn3 → Txn4 → Txn5 → Txn3)"
            << std::endl;
  DeadlockDetector detector2;
  detector2.addEdge(3, 4);
  detector2.addEdge(4, 5);
  wouldDeadlock = detector2.wouldCauseCycle(5, 3);
  std::cout << "Would Txn5 → Txn3 cause deadlock? "
            << (wouldDeadlock ? "YES — 3-WAY CYCLE!" : "No") << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 6: WAL + Crash Recovery
// ═══════════════════════════════════════════════════════════════════════

static void demoRecovery() {
  printSeparator("DEMO 6: WAL + CRASH RECOVERY");

  std::remove("recovery_table.db");
  std::remove("recovery_wal.log");

  // Phase A: Normal operations with WAL logging
  std::cout << "\n--- Phase A: Normal Operations (with WAL) ---" << std::endl;
  {
    Table table("test_table", "recovery_table.db");
    LogManager logMgr("recovery_wal.log");

    // BEGIN Txn 1
    LogRecord beginLog;
    beginLog.txnId = 1;
    beginLog.type = LogType::BEGIN;
    logMgr.appendLog(beginLog);
    std::cout << "  " << beginLog.toString() << std::endl;

    // Txn 1: INSERT record (1, 100) — this WILL be committed
    Record r1(1, 100);
    int rid1 = table.insertRecord(r1);

    LogRecord insertLog1;
    insertLog1.txnId = 1;
    insertLog1.type = LogType::INSERT;
    insertLog1.tableId = 0;
    insertLog1.recordId = rid1;
    insertLog1.newId = 1;
    insertLog1.newVal = 100;
    logMgr.appendLog(insertLog1);
    std::cout << "  " << insertLog1.toString() << std::endl;

    // Txn 1: INSERT record (2, 200) — this WILL be committed
    Record r2(2, 200);
    int rid2 = table.insertRecord(r2);

    LogRecord insertLog2;
    insertLog2.txnId = 1;
    insertLog2.type = LogType::INSERT;
    insertLog2.tableId = 0;
    insertLog2.recordId = rid2;
    insertLog2.newId = 2;
    insertLog2.newVal = 200;
    logMgr.appendLog(insertLog2);
    std::cout << "  " << insertLog2.toString() << std::endl;

    // COMMIT Txn 1
    LogRecord commitLog;
    commitLog.txnId = 1;
    commitLog.type = LogType::COMMIT;
    logMgr.appendLog(commitLog);
    logMgr.flush(); // Force WAL to disk on commit
    std::cout << "  " << commitLog.toString() << std::endl;

    // BEGIN Txn 2 (will NOT commit — simulates crash)
    LogRecord beginLog2;
    beginLog2.txnId = 2;
    beginLog2.type = LogType::BEGIN;
    logMgr.appendLog(beginLog2);
    std::cout << "  " << beginLog2.toString() << std::endl;

    // Txn 2: INSERT record (3, 300) — will be UNDONE
    Record r3(3, 300);
    int rid3 = table.insertRecord(r3);

    LogRecord insertLog3;
    insertLog3.txnId = 2;
    insertLog3.type = LogType::INSERT;
    insertLog3.tableId = 0;
    insertLog3.recordId = rid3;
    insertLog3.newId = 3;
    insertLog3.newVal = 300;
    logMgr.appendLog(insertLog3);
    std::cout << "  " << insertLog3.toString() << std::endl;

    logMgr.flush();
    table.flush();

    std::cout << "\n  *** SIMULATING CRASH *** (Txn 2 never committed!)"
              << std::endl;
    // Destructor runs, but Txn 2 has no COMMIT record in WAL
  }

  // Phase B: Recovery
  std::cout << "\n--- Phase B: Database Recovery ---" << std::endl;
  {
    Table table("test_table", "recovery_table.db");
    LogManager logMgr("recovery_wal.log");

    std::unordered_map<int, Table *> tables;
    tables[0] = &table;

    RecoveryManager recovery(&logMgr, tables);
    int undone = recovery.recover();

    // Verify: only committed records survive
    std::cout << "\n--- Verification After Recovery ---" << std::endl;

    // Record 1 (committed by Txn1) should exist
    int rid = table.searchByKey(1);
    if (rid != -1) {
      Record r = table.getRecord(rid);
      std::cout << "  Record with id=1: " << r.toString()
                << (r.isDeleted() ? " (DELETED - ERROR!)"
                                  : " (ALIVE - CORRECT!)")
                << std::endl;
    }

    // Record 2 (committed by Txn1) should exist
    rid = table.searchByKey(2);
    if (rid != -1) {
      Record r = table.getRecord(rid);
      std::cout << "  Record with id=2: " << r.toString()
                << (r.isDeleted() ? " (DELETED - ERROR!)"
                                  : " (ALIVE - CORRECT!)")
                << std::endl;
    }

    // Record 3 (uncommitted Txn2) should be deleted by UNDO
    rid = table.searchByKey(3);
    if (rid != -1) {
      Record r = table.getRecord(rid);
      std::cout << "  Record with id=3: " << r.toString()
                << (r.isDeleted() ? " (DELETED - CORRECT! UNDO worked!)"
                                  : " (ALIVE - ERROR! UNDO failed!)")
                << std::endl;
    } else {
      std::cout << "  Record with id=3: not found in index (CORRECT!)"
                << std::endl;
    }

    table.flush();
  }
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 7: Track A — Batch Execution Benchmark
// ═══════════════════════════════════════════════════════════════════════

static void demoBatchBenchmark() {
  printSeparator("DEMO 7: TRACK A — BATCH vs ROW BENCHMARK");

  std::remove("bench_table.db");

  Table table("benchmark", "bench_table.db");

  // Insert 10,000 records for benchmarking
  std::cout << "\nInserting 10,000 records for benchmark..." << std::endl;
  for (int i = 1; i <= 10000; i++) {
    table.insertRecord(Record(i, i * 10));
  }
  std::cout << "  Done. " << table.getNumRows() << " records inserted."
            << std::endl;

  // Benchmark 1: Row-at-a-time scan
  std::cout << "\n--- Row-at-a-Time Scan ---" << std::endl;
  {
    TableScanNode scan(&table);
    scan.open();

    auto start = std::chrono::high_resolution_clock::now();

    int rowCount = 0;
    while (scan.hasNext()) {
      Record r = scan.next();
      rowCount++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    scan.close();
    std::cout << "  Rows processed:  " << rowCount << std::endl;
    std::cout << "  Time:            " << duration.count() << " μs"
              << std::endl;
    std::cout << "  Throughput:      "
              << (rowCount * 1000000LL /
                  std::max(1LL, (long long)duration.count()))
              << " rows/sec" << std::endl;
  }

  // Benchmark 2: Batch scan (100 records per batch)
  std::cout << "\n--- Batch Scan (100 records/batch) ---" << std::endl;
  {
    TableScanNode scan(&table);
    scan.open();

    auto start = std::chrono::high_resolution_clock::now();

    int rowCount = 0;
    int batchCount = 0;
    while (true) {
      auto batch = scan.nextBatch(100);
      if (batch.empty())
        break;
      rowCount += static_cast<int>(batch.size());
      batchCount++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    scan.close();
    std::cout << "  Rows processed:  " << rowCount << std::endl;
    std::cout << "  Batches:         " << batchCount << std::endl;
    std::cout << "  Time:            " << duration.count() << " μs"
              << std::endl;
    std::cout << "  Throughput:      "
              << (rowCount * 1000000LL /
                  std::max(1LL, (long long)duration.count()))
              << " rows/sec" << std::endl;
  }

  // Benchmark 3: Batch scan with filter
  std::cout << "\n--- Batch Scan + Filter (val > 5000) ---" << std::endl;
  {
    auto scan = std::make_unique<TableScanNode>(&table);
    FilterNode filter(std::move(scan), "val", CompOp::GREATER, 50000);
    filter.open();

    auto start = std::chrono::high_resolution_clock::now();

    int rowCount = 0;
    while (true) {
      auto batch = filter.nextBatch(100);
      if (batch.empty())
        break;
      rowCount += static_cast<int>(batch.size());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    filter.close();
    std::cout << "  Matching rows:   " << rowCount << std::endl;
    std::cout << "  Time:            " << duration.count() << " μs"
              << std::endl;
  }

  std::cout << "\n--- Batch Execution Conclusion ---" << std::endl;
  std::cout << "  Batching reduces per-row virtual dispatch overhead by"
            << std::endl;
  std::cout << "  amortizing one hasNext()/next() virtual call over 100 rows."
            << std::endl;
  std::cout << "  This is the core idea behind vectorized execution engines"
            << std::endl;
  std::cout << "  like DuckDB and MonetDB/X100." << std::endl;

  table.flush();
}

// ═══════════════════════════════════════════════════════════════════════
// DEMO 0: Original B+ Tree Stress Test (preserved from previous work)
// ═══════════════════════════════════════════════════════════════════════

static void testBPlusTree() {
  printSeparator("DEMO 0: B+ TREE STRESS TEST (10,000 keys)");

  std::remove("index_test.db");

  DiskManager dm("index_test.db");
  BufferPool pool(&dm, 50);
  BPlusTree tree(&pool);

  std::map<int, int> oracle;
  std::unordered_set<int> usedKeys;
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, 999999);

  std::cout << "\nInserting 10,000 unique random keys..." << std::endl;
  for (int i = 0; i < 10000; i++) {
    int key;
    do {
      key = dist(rng);
    } while (usedKeys.count(key));
    usedKeys.insert(key);
    oracle[key] = i;
    tree.insert(key, i);
  }

  std::cout << "Verifying all keys..." << std::endl;
  for (const auto &[key, expected] : oracle) {
    int actual = tree.search(key);
    if (expected != actual) {
      std::cout << "FAIL at key " << key << std::endl;
      return;
    }
  }
  std::cout << "SUCCESS: All 10,000 keys verified!" << std::endl;
}

// ═══════════════════════════════════════════════════════════════════════
// Interactive Shell (REPL)
// ═══════════════════════════════════════════════════════════════════════

#include <cctype>

static void runInteractiveShell() {
  Table employees("employees", "employees.db");
  Table departments("departments", "departments.db");

  // Add some default records for testing
  employees.insertRecord(Record(1, 100));
  employees.insertRecord(Record(2, 200));
  employees.insertRecord(Record(3, 300));
  employees.insertRecord(Record(4, 400));
  employees.insertRecord(Record(5, 500));

  departments.insertRecord(Record(1, 10));
  departments.insertRecord(Record(2, 20));
  departments.insertRecord(Record(3, 30));
  departments.insertRecord(Record(4, 40));
  departments.insertRecord(Record(5, 50));

  Catalog catalog;
  catalog.addTable("employees", &employees);
  catalog.addTable("departments", &departments);

  Optimizer optimizer(catalog);
  CatalogManager catalogManager("catalog.txt");
  try {
    catalogManager.loadState(optimizer);
  } catch (const std::exception& e) {
    std::cerr << "Warning: could not load catalog.txt: " << e.what() << std::endl;
  }

  std::cout << "\n============================================================"
            << std::endl;
  std::cout << "             MiniDB Interactive SQL Shell (REPL)" << std::endl;
  std::cout << "============================================================"
            << std::endl;
  std::cout << "Exposed tables:\n";
  std::cout << "  - employees (id INT, val INT)\n";
  std::cout << "  - departments (id INT, val INT)\n";
  std::cout << "\nSupported SQL grammar examples:\n";
  std::cout << "  - SELECT * FROM employees\n";
  std::cout << "  - SELECT * FROM employees WHERE id = 3\n";
  std::cout << "  - SELECT * FROM employees JOIN departments ON employees.id = "
               "departments.id\n";
  std::cout << "  - CREATE TABLE users (id INT, name VARCHAR)\n";
  std::cout << "  - INSERT INTO users VALUES (1, 'Alice')\n";
  std::cout << "  - INSERT INTO employees VALUES (6, 600)\n";
  std::cout << "  - SHOW TABLES\n";
  std::cout << "  - DELETE FROM employees WHERE id = 3\n";
  std::cout
      << "\nType 'help' to see this prompt again, or 'exit'/'quit' to exit.\n";
  std::cout << "============================================================"
            << std::endl;

  while (true) {
    std::cout << "\nminidb> ";
    std::string sql;
    if (!std::getline(std::cin, sql)) {
      break;
    }

    // Trim leading/trailing whitespace
    while (!sql.empty() &&
           std::isspace(static_cast<unsigned char>(sql.front())))
      sql.erase(sql.begin());
    while (!sql.empty() && std::isspace(static_cast<unsigned char>(sql.back())))
      sql.pop_back();

    if (sql.empty())
      continue;
    if (sql == "exit" || sql == "quit") {
      try {
        catalogManager.saveState(optimizer);
      } catch (const std::exception& e) {
        std::cerr << "Warning: could not save catalog.txt: " << e.what() << std::endl;
      }
      break;
    }
    if (sql == "help") {
      std::cout << "Supported statements: CREATE TABLE, INSERT, SELECT, DELETE, SHOW TABLES" << std::endl;
      continue;
    }

    try {
      Lexer lexer(sql);
      Parser parser(lexer.tokenize());
      auto ast = parser.parse();

      if (auto *select = dynamic_cast<SelectNode *>(ast.get())) {
        auto plan = optimizer.optimize(select);
        std::cout << "\n[Cost-Based Optimizer Decision]" << std::endl;
        std::cout << optimizer.getExplanation() << std::endl;

        if (plan) {
          plan->open();
          int count = 0;
          std::cout << "\n[Query Results]" << std::endl;
          while (plan->hasNext()) {
            Record r = plan->next();
            std::cout << "  " << r.toString() << std::endl;
            count++;
          }
          plan->close();
          std::cout << "(" << count << " rows)" << std::endl;
        }
      } else if (auto *create = dynamic_cast<CreateAST *>(ast.get())) {
        optimizer.executeCreateTable(create);
        catalogManager.saveState(optimizer);
        std::cout << "Success: CREATE TABLE " << create->tableName << std::endl;
      } else if (auto *show = dynamic_cast<ShowTablesAST *>(ast.get())) {
        (void)show;
        optimizer.executeShowTables();
      } else if (auto *insert = dynamic_cast<InsertNode *>(ast.get())) {
        int rid = optimizer.executeInsert(insert);
        Table *table = catalog.getTable(insert->tableName);
        table->flush();
        catalogManager.saveState(optimizer);
        std::cout << "Success: INSERT 1 record (recordId: " << rid << ")"
                  << std::endl;
      } else if (auto *del = dynamic_cast<DeleteNode *>(ast.get())) {
        Table *table = catalog.getTable(del->tableName);
        const auto& columns = table->getSchema().getColumns();
        if (columns.empty() || !del->hasWhere ||
            del->whereCol != columns.front().name ||
            del->whereOp != CompOp::EQUALS) {
          throw std::runtime_error("DELETE only supports equality on the primary key column");
        }
        int rid = table->searchByKey(del->whereVal);
        if (rid != -1) {
          Record r = table->getRecord(rid);
          if (r.isDeleted()) {
            std::cout << "DELETE 0 records (already deleted)" << std::endl;
          } else {
            table->deleteRecord(rid);
            table->flush();
            catalog.refreshStats(del->tableName);
            catalogManager.saveState(optimizer);
            std::cout << "Success: DELETE 1 record" << std::endl;
          }
        } else {
          std::cout << "DELETE 0 records (key not found)" << std::endl;
        }
      } else {
        std::cout << "Error: Unknown AST node type" << std::endl;
      }
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << std::endl;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════

int main(int argc, char *argv[]) {
  bool run_shell = false;
  bool run_demo = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-i" || arg == "--interactive" || arg == "-s" ||
        arg == "--shell") {
      run_shell = true;
    } else if (arg == "-d" || arg == "--demo") {
      run_demo = true;
    }
  }

  // If no command-line flags are specified, ask the user interactively
  if (!run_shell && !run_demo) {
    std::cout << "============================================================"
              << std::endl;
    std::cout << "                    Welcome to MiniDB!" << std::endl;
    std::cout << "============================================================"
              << std::endl;
    std::cout << "Select execution mode:\n";
    std::cout << "  [1] Run the full system demo suite (automated tests)\n";
    std::cout << "  [2] Launch the interactive SQL shell (REPL)\n";
    std::cout << "Enter choice (1 or 2): ";

    std::string choice;
    if (std::getline(std::cin, choice)) {
      while (!choice.empty() &&
             std::isspace(static_cast<unsigned char>(choice.front())))
        choice.erase(choice.begin());
      while (!choice.empty() &&
             std::isspace(static_cast<unsigned char>(choice.back())))
        choice.pop_back();

      if (choice == "2") {
        run_shell = true;
      } else {
        run_demo = true;
      }
    } else {
      // Default to demo if stdin is closed/pipe
      run_demo = true;
    }
  }

  if (run_shell) {
    runInteractiveShell();
    return 0;
  }

  std::cout << "╔══════════════════════════════════════════════════════════╗"
            << std::endl;
  std::cout << "║              MiniDB — Full System Demo                  ║"
            << std::endl;
  std::cout << "╚══════════════════════════════════════════════════════════╝"
            << std::endl;

  try {
    testBPlusTree();
    demoSQLParsing();
    demoInsertAndSelect();
    demoCostBasedOptimizer();
    demoTransactions();
    demoDeadlockDetection();
    demoRecovery();
    demoBatchBenchmark();

    std::cout
        << "\n╔══════════════════════════════════════════════════════════╗"
        << std::endl;
    std::cout << "║          ALL DEMOS COMPLETED SUCCESSFULLY!              ║"
              << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════╝"
              << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "\nFATAL ERROR: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
