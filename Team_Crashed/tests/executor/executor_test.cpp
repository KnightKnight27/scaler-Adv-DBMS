#include <cassert>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <string>

#include "catalog/catalog_manager.h"
#include "executor/executor.h"
#include "executor/insert_executor.h"
#include "executor/query_engine.h"
#include "executor/seq_scan.h"
#include "index/index_manager.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;
namespace fs = std::filesystem;

// Parse a single INSERT statement and hand back its AST node.
static std::unique_ptr<parser::InsertStmt> parseInsertStmt(const std::string& sql) {
    parser::Lexer lex(sql);
    auto toks = lex.tokenize();
    parser::Parser p(std::move(toks));
    parser::Stmt s = p.parse();
    return std::move(s.insert);
}

// A fresh executor context bound to the given stack, running inside txn `tid`.
static executor::ExecutorContext makeCtx(storage::BufferPool* bp,
                                          catalog::CatalogManager* cat,
                                          index::IndexManager* idx,
                                          transaction::TransactionManager* txn,
                                          TransactionId tid,
                                          executor::IsoMode mode) {
    executor::ExecutorContext ctx;
    ctx.bp = bp;
    ctx.cat = cat;
    ctx.idx = idx;
    ctx.txn = txn;
    ctx.wal = nullptr;            // no WAL for these concurrency tests
    ctx.currentTxnId = tid;
    ctx.readerTxn   = (txn != nullptr && tid != INVALID_TXN_ID)
                          ? txn->getTransaction(tid) : nullptr;
    ctx.isoMode = mode;
    return ctx;
}

int main() {
    fs::path tmp = fs::temp_directory_path() / "minidb_executor_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    storage::DiskManager dm((tmp / "minidb.db").string());
    storage::BufferPool bp(&dm, 32);
    catalog::CatalogManager cat(&dm);
    assert(cat.load() == Status::OK);
    index::IndexManager idx(&bp, &cat);
    transaction::TransactionManager txns;
    recovery::WAL wal((tmp / "wal.log").string());
    recovery::RecoveryManager rec(&wal, &bp, &cat, &idx, &txns);
    executor::QueryEngine engine(&bp, &cat, &idx, &txns, &rec);

    assert(engine.executeUpdate(
        "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(16), age INT)") == Status::OK);
    assert(engine.executeUpdate("INSERT INTO users VALUES (1, 'alice', 30), (2, 'bob', 25)") == Status::OK);

    auto rows = engine.execute("SELECT name FROM users WHERE id = 2");
    assert(rows.size() == 1);
    assert(rows[0].values.size() == 1);
    assert(rows[0].values[0].tag == executor::Value::Tag::STRING);
    assert(rows[0].values[0].s == "bob");

    assert(engine.executeUpdate("DELETE FROM users WHERE id = 1") == Status::OK);
    rows = engine.execute("SELECT id FROM users WHERE id = 1");
    assert(rows.empty());

    // -----------------------------------------------------------------
    // MVCC snapshot isolation, exercised THROUGH the executor path.
    //
    // Txn A inserts a row but does NOT commit. Txn B begins and scans: its
    // snapshot was taken before A committed, so A's row must be invisible
    // (snapshot isolation). Then A commits; a fresh txn C must see the row.
    // This proves isVisible() is actually wired into SeqScan.
    //
    // These checks are explicit (not assert) so they run in a Release build
    // where NDEBUG would compile assert() away.
    // -----------------------------------------------------------------
    if (engine.executeUpdate(
            "CREATE TABLE mvcc_t (id INT PRIMARY KEY, name VARCHAR(16))") != Status::OK) {
        std::printf("[FAIL] create mvcc_t\n"); return 1;
    }

    TransactionId A = txns.begin();
    {
        auto ctxA = makeCtx(&bp, &cat, &idx, &txns, A, executor::IsoMode::MVCC);
        auto ins = std::make_unique<executor::InsertExecutor>(
            &ctxA, parseInsertStmt("INSERT INTO mvcc_t VALUES (5, 'x')"));
        if (ins->init() != Status::OK) { std::printf("[FAIL] mvcc insert init\n"); return 1; }
        executor::Tuple tmp;
        if (ins->next(tmp) != Status::OK) { std::printf("[FAIL] mvcc insert next\n"); return 1; }
        if (ins->close() != Status::OK)  { std::printf("[FAIL] mvcc insert close\n"); return 1; }
    }
    // A is intentionally left uncommitted.

    TransactionId B = txns.begin();
    {
        auto ctxB = makeCtx(&bp, &cat, &idx, &txns, B, executor::IsoMode::MVCC);
        executor::SeqScanExecutor scan(&ctxB, "mvcc_t", nullptr);
        if (scan.init() != Status::OK) { std::printf("[FAIL] mvcc scan B init\n"); return 1; }
        int seen = 0;
        executor::Tuple r;
        while (scan.next(r) == Status::OK) ++seen;
        (void)scan.close();
        if (seen != 0) {   // A's uncommitted insert must be invisible to B
            std::printf("[FAIL] MVCC snapshot isolation: saw %d rows, expected 0\n", seen);
            return 1;
        }
    }
    (void)txns.abort(B);   // B was read-only; just discard it

    if (txns.commit(A) != Status::OK) { std::printf("[FAIL] commit A\n"); return 1; }

    TransactionId C = txns.begin();
    {
        auto ctxC = makeCtx(&bp, &cat, &idx, &txns, C, executor::IsoMode::MVCC);
        executor::SeqScanExecutor scan(&ctxC, "mvcc_t", nullptr);
        if (scan.init() != Status::OK) { std::printf("[FAIL] mvcc scan C init\n"); return 1; }
        int seen = 0;
        executor::Tuple r;
        while (scan.next(r) == Status::OK) ++seen;
        (void)scan.close();
        if (seen != 1) {   // A committed => the row must be visible to C
            std::printf("[FAIL] MVCC visibility after commit: saw %d rows, expected 1\n", seen);
            return 1;
        }
    }
    (void)txns.commit(C);

    // -----------------------------------------------------------------
    // 2PL wiring: a TWO_PL insert acquires an X lock on its row, held until
    // commit (releaseAll). Proven deterministically via holdsLock().
    // -----------------------------------------------------------------
    if (engine.executeUpdate(
            "CREATE TABLE two_t (id INT PRIMARY KEY, v INT)") != Status::OK) {
        std::printf("[FAIL] create two_t\n"); return 1;
    }
    TransactionId T = txns.begin();
    RecordId lockedRid = INVALID_RID;
    {
        auto ctxT = makeCtx(&bp, &cat, &idx, &txns, T, executor::IsoMode::TWO_PL);
        auto ins = std::make_unique<executor::InsertExecutor>(
            &ctxT, parseInsertStmt("INSERT INTO two_t VALUES (7, 100)"));
        if (ins->init() != Status::OK) { std::printf("[FAIL] 2pl insert init\n"); return 1; }
        executor::Tuple tmp;
        if (ins->next(tmp) != Status::OK) { std::printf("[FAIL] 2pl insert next\n"); return 1; }
        lockedRid = ins->lastRid();
        if (lockedRid == INVALID_RID) { std::printf("[FAIL] 2pl no rid\n"); return 1; }
        if (!txns.lockManager().holdsLock(T, lockedRid)) {
            std::printf("[FAIL] 2PL: X lock not held after insert\n"); return 1;
        }
    }
    if (txns.commit(T) != Status::OK) { std::printf("[FAIL] 2pl commit\n"); return 1; }
    if (txns.lockManager().holdsLock(T, lockedRid)) {
        std::printf("[FAIL] 2PL: X lock not released after commit\n"); return 1;
    }

    std::printf("[OK] executor SQL create/insert/select/delete + MVCC snapshot isolation + 2PL lock wiring\n");
    return 0;
}
