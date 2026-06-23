// =============================================================================
// main.cpp – MiniDB Interactive REPL
//
// Integrates all subsystems:
//   Storage    : DiskManager → BufferPool → HeapFile
//   Indexing   : BPlusTree (primary key)
//   Query      : Parser → Optimizer → Executor
//   Concurrency: LockManager → TxManager (2PL)
//   Recovery   : WAL + Recovery (crash-recovery demo)
//   Replication: Primary + Replica (Track D)
//
// Supported commands:
//   INSERT <id> <value>    DELETE <id>
//   SELECT <id>            SELECT *    SHOW
//   BEGIN   COMMIT   ABORT
//   DEMO    CRASH    RECOVER    REPLICA
//   HELP    QUIT / EXIT
// =============================================================================

#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <filesystem>

#include "common/config.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "query/parser.h"
#include "query/optimizer.h"
#include "query/executor.h"
#include "transaction/lock_manager.h"
#include "transaction/tx_manager.h"
#include "recovery/wal.h"
#include "recovery/recovery.h"
#include "replication/primary.h"
#include "replication/replica.h"

using namespace minidb;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

static void printRow(const Record& r) {
    std::cout << "    | id = " << r.id << "  value = " << r.value << " |\n";
}

static void printHelp() {
    std::cout << R"(
Commands
────────
  INSERT <id> <value>   Insert a row
  SELECT <id>           Point lookup (uses B+ Tree index)
  SELECT *              Full table scan
  DELETE <id>           Delete a row (tombstone)
  SHOW                  Alias for SELECT *
  BEGIN                 Start an explicit transaction (2PL)
  COMMIT                Commit the current transaction
  ABORT                 Abort (rollback) the current transaction
  DEMO                  Run automated demo sequence
  CRASH                 Simulate crash (deletes primary DB, keeps WAL)
  RECOVER               Replay WAL and rebuild primary state
  REPLICA               Show current replica state
  HELP                  Show this message
  QUIT / EXIT           Shutdown

)";
}

// ─── MiniDB subsystem bundle ─────────────────────────────────────────────────
struct DB {
    // Primary storage
    std::unique_ptr<DiskManager>  dm;
    std::unique_ptr<BufferPool>   bp;
    std::unique_ptr<HeapFile>     heap;
    std::unique_ptr<BPlusTree>    tree;

    // Replica storage
    std::unique_ptr<DiskManager>  rdm;
    std::unique_ptr<BufferPool>   rbp;
    std::unique_ptr<HeapFile>     rheap;
    std::unique_ptr<BPlusTree>    rtree;
    std::unique_ptr<Replica>      replica;

    // Shared subsystems
    std::unique_ptr<WAL>          wal;
    std::unique_ptr<LockManager>  lm;
    std::unique_ptr<TxManager>    txm;
    std::unique_ptr<Primary>      primary;

    // Active explicit transaction (one at a time for demo)
    txn_id_t active_txn = -1;
    Optimizer opt;
    Executor* exec() { return &primary_exec_; }

private:
    Executor primary_exec_{nullptr, nullptr}; // placeholder; real exec is in Primary
};

static std::unique_ptr<DB> MakeDB() {
    auto db = std::make_unique<DB>();

    db->dm   = std::make_unique<DiskManager>(DB_FILE);
    db->bp   = std::make_unique<BufferPool>(DEFAULT_POOL_SIZE, db->dm.get());
    db->heap = std::make_unique<HeapFile>(db->bp.get(), INVALID_PAGE_ID);
    db->tree = std::make_unique<BPlusTree>();

    db->rdm   = std::make_unique<DiskManager>(REPLICA_FILE);
    db->rbp   = std::make_unique<BufferPool>(32, db->rdm.get());
    db->rheap = std::make_unique<HeapFile>(db->rbp.get(), INVALID_PAGE_ID);
    db->rtree = std::make_unique<BPlusTree>();

    db->wal  = std::make_unique<WAL>(WAL_FILE);
    db->lm   = std::make_unique<LockManager>();
    db->txm  = std::make_unique<TxManager>(db->lm.get(), db->wal.get());

    // Run crash-recovery on startup.
    Recovery rec(WAL_FILE, db->heap.get(), db->tree.get());
    rec.Run();

    db->primary = std::make_unique<Primary>(
        db->heap.get(), db->tree.get(),
        db->wal.get(), db->lm.get(), db->txm.get());

    db->replica = std::make_unique<Replica>(
        WAL_FILE, db->rheap.get(), db->rtree.get());
    db->replica->StartReplication();

    return db;
}

// ─── Automated demo ──────────────────────────────────────────────────────────
static void RunDemo(DB& db) {
    auto run = [&](const std::string& sql){
        std::cout << "  CMD: " << sql << "\n";
        auto stmt = Parser::Parse(sql);
        auto sample_data = db.heap->ScanAll();
        size_t total_records = sample_data.size();
        std::string plan = db.opt.SelectPlan(stmt, total_records, sample_data);
        std::cout << "  PLAN: " << plan << "\n";
        auto r = db.primary->Execute(stmt);
        std::cout << "  " << (r.success ? "OK" : "FAIL") << ": " << r.message << "\n";
        if (stmt.type == StmtType::SELECT_JOIN) {
            for (const auto& pair : r.joined_rows) {
                std::cout << "    | A.id = " << pair.first.id << " A.value = " << pair.first.value 
                          << " | JOIN | B.id = " << pair.second.id << " B.value = " << pair.second.value << " |\n";
            }
        } else {
            for (const auto& row : r.rows) printRow(row);
        }
        std::cout << "\n";
    };

    std::cout << "\n══════ MiniDB Automated Demo ══════\n\n";

    std::cout << "─── Phase 1: INSERT ───\n";
    run("INSERT 10 1000"); run("INSERT 5 500"); run("INSERT 20 2000");
    run("INSERT 1 100");   run("INSERT 15 1500");

    std::cout << "─── Phase 2: SELECT (index scan) ───\n";
    run("SELECT 10"); run("SELECT 5"); run("SELECT 99");

    std::cout << "─── Phase 3: WHERE filters ───\n";
    run("SELECT * WHERE VALUE > 800");
    run("SELECT * WHERE ID > 10");

    std::cout << "─── Phase 4: JOIN (self-join A.value = B.id) ───\n";
    run("INSERT 2 10"); // value = 10 matches ID 10
    run("SELECT JOIN");

    std::cout << "─── Phase 5: DELETE ───\n";
    run("DELETE 5"); run("SELECT *");

    std::cout << "─── Phase 6: 2PL locking and deadlock timeout demo ───\n";
    txn_id_t t1 = db.txm->Begin();
    txn_id_t t2 = db.txm->Begin();
    std::cout << "  TXN-" << t1 << " acquires X-lock on row 10...\n";
    db.lm->LockExclusive(t1, 10);
    std::cout << "  TXN-" << t2 << " tries X-lock on row 10 → BLOCKS (timeout is 800ms).\n";
    std::thread releaser([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        std::cout << "  TXN-" << t1 << " commits (too late for TXN-2).\n";
        db.txm->Commit(t1);
    });
    bool got_lock = db.lm->LockExclusive(t2, 10);
    if (!got_lock) {
        std::cout << "  TXN-" << t2 << " lock request TIMED OUT (deadlock prevented).\n";
        db.txm->Abort(t2);
    } else {
        std::cout << "  TXN-" << t2 << " acquired lock.\n";
        db.txm->Commit(t2);
    }
    releaser.join();

    std::cout << "\n─── Phase 7: Wait for replica to sync (1s) ───\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "  Replica rows:\n";
    for (const auto& r : db.replica->ScanAll()) printRow(r);

    std::cout << "\n══════ Demo complete ══════\n\n";
}

// ─── Main REPL ───────────────────────────────────────────────────────────────
int main() {
    std::cout << "╔══════════════════════════════════════╗\n"
              << "║        MiniDB — DBMS Capstone        ║\n"
              << "║   Type HELP for available commands   ║\n"
              << "╚══════════════════════════════════════╝\n\n";

    auto db = MakeDB();
    std::string line;

    while (true) {
        if (db->active_txn != -1)
            std::cout << "[TXN-" << db->active_txn << "] > ";
        else
            std::cout << "minidb> ";

        if (!std::getline(std::cin, line)) break;
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty()) continue;

        std::string cmd = toUpper(line.substr(0, line.find(' ')));

        if (cmd == "HELP")              { printHelp(); continue; }
        if (cmd == "QUIT" || cmd == "EXIT") {
            db->replica->StopReplication();
            std::cout << "Goodbye.\n"; break;
        }
        if (cmd == "DEMO")   { RunDemo(*db); continue; }
        if (cmd == "REPLICA") {
            std::cout << "─── Replica State ───\n";
            auto rows = db->replica->ScanAll();
            if (rows.empty()) std::cout << "  (empty or not yet synced)\n";
            else for (const auto& r : rows) printRow(r);
            std::cout << "  Status: " << (db->replica->IsRunning() ? "RUNNING" : "STOPPED") << "\n";
            continue;
        }

        if (cmd == "BEGIN") {
            if (db->active_txn != -1) { std::cout << "  ERROR: transaction already active.\n"; continue; }
            db->active_txn = db->txm->Begin();
            std::cout << "  BEGIN TXN-" << db->active_txn << "\n"; continue;
        }
        if (cmd == "COMMIT") {
            if (db->active_txn == -1) { std::cout << "  ERROR: no active transaction.\n"; continue; }
            txn_id_t tid = db->active_txn; db->active_txn = -1;
            db->txm->Commit(tid);
            std::cout << "  TXN-" << tid << " COMMITTED.\n"; continue;
        }
        if (cmd == "ABORT") {
            if (db->active_txn == -1) { std::cout << "  ERROR: no active transaction.\n"; continue; }
            txn_id_t tid = db->active_txn; db->active_txn = -1;
            db->txm->Abort(tid);
            std::cout << "  TXN-" << tid << " ABORTED.\n"; continue;
        }

        if (cmd == "CRASH") {
            std::cout << "  [CRASH] Simulating crash – removing " << DB_FILE << " but keeping WAL.\n";
            db->replica->StopReplication();
            db->bp->FlushAll(); db.reset();
            std::filesystem::remove(DB_FILE);
            // Rebuild skeleton for RECOVER.
            db = std::make_unique<DB>();
            db->dm   = std::make_unique<DiskManager>(DB_FILE);
            db->bp   = std::make_unique<BufferPool>(DEFAULT_POOL_SIZE, db->dm.get());
            db->heap = std::make_unique<HeapFile>(db->bp.get(), INVALID_PAGE_ID);
            db->tree = std::make_unique<BPlusTree>();
            db->rdm   = std::make_unique<DiskManager>(REPLICA_FILE);
            db->rbp   = std::make_unique<BufferPool>(32, db->rdm.get());
            db->rheap = std::make_unique<HeapFile>(db->rbp.get(), INVALID_PAGE_ID);
            db->rtree = std::make_unique<BPlusTree>();
            db->wal  = std::make_unique<WAL>(WAL_FILE);
            db->lm   = std::make_unique<LockManager>();
            db->txm  = std::make_unique<TxManager>(db->lm.get(), db->wal.get());
            db->primary = std::make_unique<Primary>(db->heap.get(), db->tree.get(),
                                                    db->wal.get(), db->lm.get(), db->txm.get());
            db->replica = std::make_unique<Replica>(WAL_FILE, db->rheap.get(), db->rtree.get());
            std::cout << "  Crashed. Type RECOVER to replay WAL.\n"; continue;
        }
        if (cmd == "RECOVER") {
            Recovery rec(WAL_FILE, db->heap.get(), db->tree.get());
            rec.Run();
            db->replica->StartReplication();
            std::cout << "  Recovery done. Replica restarted.\n"; continue;
        }

        // ── Normal SQL-like query ─────────────────────────────────────────
        Statement stmt = Parser::Parse(line);
        if (stmt.type == StmtType::INVALID) {
            std::cout << "  ERROR: unknown command. Type HELP.\n"; continue;
        }
        auto sample_data = db->heap->ScanAll();
        size_t total_records = sample_data.size();
        std::string plan = db->opt.SelectPlan(stmt, total_records, sample_data);
        std::cout << "  [Optimizer] plan = " << plan << "\n";

        ExecResult r = db->primary->Execute(stmt, db->active_txn);
        std::cout << "  " << (r.success ? "OK" : "FAIL") << ": " << r.message << "\n";
        if (stmt.type == StmtType::SELECT_JOIN) {
            for (const auto& pair : r.joined_rows) {
                std::cout << "    | A.id = " << pair.first.id << " A.value = " << pair.first.value 
                          << " | JOIN | B.id = " << pair.second.id << " B.value = " << pair.second.value << " |\n";
            }
        } else {
            for (const auto& row : r.rows) printRow(row);
        }

        if (!r.success && db->active_txn != -1) {
            db->active_txn = -1; // transaction aborted due to timeout
        }
    }

    return 0;
}
