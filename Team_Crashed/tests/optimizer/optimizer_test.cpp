// =============================================================================
// tests/optimizer/optimizer_test.cpp
// -----------------------------------------------------------------------------
// Verifies the cost-based optimizer actually chooses between a sequential
// scan and an index scan based on COST, not on mere index existence.
//
//   * PK equality on a large table  -> INDEX_SCAN (tree traversal + one heap
//     fetch beats a full scan).
//   * Low-selectivity range on a small (few-page) table -> SEQ_SCAN (a range
//     that hits many rows costs more as random heap fetches than as one
//     sequential pass).
//
// We drive the optimizer directly (Optimizer::optimize) so we can inspect the
// PhysicalPlan kind, which the QueryEngine does not expose.
// =============================================================================
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "catalog/catalog_manager.h"
#include "executor/query_engine.h"
#include "index/index_manager.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/optimizer.h"
#include "planner/physical_plan.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;
namespace fs = std::filesystem;

// Descend through wrapper nodes (PROJECT / FILTER / SORT / LIMIT) to the
// first SCAN node and return its physical kind.
static planner::PhysicalKind scanKind(const planner::PhysicalPlan* p) {
    while (p != nullptr &&
           p->kind != planner::PhysicalKind::SEQ_SCAN &&
           p->kind != planner::PhysicalKind::INDEX_SCAN) {
        if (p->children.empty() || !p->children[0]) return p->kind;
        p = p->children[0].get();
    }
    return p ? p->kind : planner::PhysicalKind::SEQ_SCAN;
}

// Parse one SQL statement and run it through the optimizer.
static std::unique_ptr<planner::PhysicalPlan>
optimize(planner::Optimizer& opt, const std::string& sql) {
    parser::Lexer lex(sql);
    auto toks = lex.tokenize();
    parser::Parser p(std::move(toks));
    parser::Stmt stmt = p.parse();
    return opt.optimize(stmt);
}

int main() {
    fs::path tmp = fs::temp_directory_path() / "minidb_optimizer_test";
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
    executor::QueryEngine engine(&bp, &cat, &idx, &txns, &rec, &wal);

    assert(engine.executeUpdate(
        "CREATE TABLE t (id INT PRIMARY KEY, v INT)") == Status::OK);
    assert(engine.executeUpdate(
        "CREATE INDEX t_v_idx ON t (v)") == Status::OK);

    // Load ~600 rows so the table spans a couple of pages and the optimizer
    // has a real cardinality (set by InsertExecutor) to cost against.
    for (int batch = 0; batch < 12; ++batch) {
        std::string sql = "INSERT INTO t VALUES ";
        for (int k = 0; k < 50; ++k) {
            int i = batch * 50 + k + 1;
            sql += "(" + std::to_string(i) + "," + std::to_string(i) + ")";
            if (k + 1 < 50) sql += ",";
        }
        assert(engine.executeUpdate(sql) == Status::OK);
    }
    assert(cat.cardinality("t") == 600);

    planner::Optimizer opt(&cat, &idx, &txns);

    // PK equality on a large table: index scan should win.
    auto p1 = optimize(opt, "SELECT * FROM t WHERE id = 50");
    assert(p1 != nullptr);
    assert(scanKind(p1.get()) == planner::PhysicalKind::INDEX_SCAN);

    // Low-selectivity range on a small (few-page) table: the index path
    // (one random heap fetch per matching RID) costs more than one sequential
    // pass, so the optimizer must keep the sequential scan.
    auto p2 = optimize(opt, "SELECT * FROM t WHERE v >= 0");
    assert(p2 != nullptr);
    assert(scanKind(p2.get()) == planner::PhysicalKind::SEQ_SCAN);

    std::printf("[OK] cost-based scan selection (index vs seq)\n");
    return 0;
}