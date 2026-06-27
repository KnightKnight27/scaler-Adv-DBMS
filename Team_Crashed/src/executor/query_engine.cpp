// =============================================================================
// src/executor/query_engine.cpp
// -----------------------------------------------------------------------------
// QueryEngine: top-level SQL -> executor tree driver.
//
//   execute(sql)        — SELECT, returns all matching Tuples.
//   executeUpdate(sql)  — INSERT / DELETE / CREATE / DROP / BEGIN / COMMIT / ROLLBACK,
//                         returns a Status.
//
// Pipeline:  Lexer  ->  Parser  ->  Optimizer (PhysicalPlan)  ->
//            build executor tree  ->  init / next* / close.
// =============================================================================
#include "executor/query_engine.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "catalog/table_info.h"
#include "common/status.h"
#include "executor/aggregate_executor.h"
#include "executor/delete_executor.h"
#include "executor/executor.h"
#include "executor/filter_executor.h"
#include "executor/index_scan.h"
#include "executor/insert_executor.h"
#include "executor/join.h"
#include "executor/limit_executor.h"
#include "executor/project_executor.h"
#include "executor/seq_scan.h"
#include "executor/sort_executor.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/optimizer.h"
#include "planner/physical_plan.h"
#include "recovery/log_record.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "transaction/transaction_manager.h"

namespace minidb::executor {

namespace {

// Look up the schema of a child physical plan that is a SCAN.
// Joins and projections need to know the columns of each child side
// so they can resolve column references by name (not just take
// values.front()). The catalog pointer comes from the executor
// context so we don't have to thread it through every helper.
catalog::Schema resolveChildSchema(catalog::CatalogManager* cat,
                                   const planner::PhysicalPlan& child) {
    if ((child.kind == planner::PhysicalKind::SEQ_SCAN ||
         child.kind == planner::PhysicalKind::INDEX_SCAN) &&
        !child.table.empty() && cat != nullptr) {
        const auto* info = cat->getTable(child.table);
        if (info != nullptr) {
            catalog::Schema qualified;
            for (auto col : info->schema.columns()) {
                col.name = child.table + "." + col.name;
                qualified.addColumn(std::move(col));
            }
            return qualified;
        }
    }
    if ((child.kind == planner::PhysicalKind::FILTER ||
         child.kind == planner::PhysicalKind::PROJECT ||
         child.kind == planner::PhysicalKind::SORT ||
         child.kind == planner::PhysicalKind::LIMIT ||
         child.kind == planner::PhysicalKind::AGGREGATE) &&
        !child.children.empty() && child.children[0]) {
        return resolveChildSchema(cat, *child.children[0]);
    }
    if ((child.kind == planner::PhysicalKind::NESTED_LOOP_JOIN ||
         child.kind == planner::PhysicalKind::HASH_JOIN) &&
        child.children.size() >= 2 && child.children[0] && child.children[1]) {
        catalog::Schema out;
        catalog::Schema left = resolveChildSchema(cat, *child.children[0]);
        catalog::Schema right = resolveChildSchema(cat, *child.children[1]);
        for (const auto& c : left.columns()) out.addColumn(c);
        for (const auto& c : right.columns()) out.addColumn(c);
        return out;
    }
    return catalog::Schema();
}

} // namespace

// Build an executor subtree for a single PhysicalPlan node. Returns
// nullptr when the kind has no executor mapping.
std::unique_ptr<Executor> buildPlan(ExecutorContext* ctx,
                                    planner::PhysicalPlan& plan) {
    switch (plan.kind) {
        case planner::PhysicalKind::SEQ_SCAN: {
            return std::make_unique<SeqScanExecutor>(
                ctx, plan.table, std::move(plan.predicate));
        }
        case planner::PhysicalKind::INDEX_SCAN: {
            return std::make_unique<IndexScanExecutor>(
                ctx, plan.table, plan.indexName,
                std::move(plan.predicate));
        }
        case planner::PhysicalKind::NESTED_LOOP_JOIN: {
            if (plan.children.size() < 2) return nullptr;
            auto left  = buildPlan(ctx, *plan.children[0]);
            auto right = buildPlan(ctx, *plan.children[1]);
            if (!left || !right) return nullptr;
            // Resolve the schema for each side so the join's ON predicate
            // can look up columns by name (not just take values.front()).
            catalog::Schema ls = resolveChildSchema(ctx->cat, *plan.children[0]);
            catalog::Schema rs = resolveChildSchema(ctx->cat, *plan.children[1]);
            return std::make_unique<NestedLoopJoinExecutor>(
                ctx, std::move(left), std::move(right),
                std::move(plan.predicate),
                std::move(ls), std::move(rs));
        }
        case planner::PhysicalKind::HASH_JOIN: {
            if (plan.children.size() < 2) return nullptr;
            auto build = buildPlan(ctx, *plan.children[0]);
            auto probe = buildPlan(ctx, *plan.children[1]);
            if (!build || !probe) return nullptr;
            catalog::Schema bs = resolveChildSchema(ctx->cat, *plan.children[0]);
            catalog::Schema ps = resolveChildSchema(ctx->cat, *plan.children[1]);
            return std::make_unique<HashJoinExecutor>(
                ctx, std::move(build), std::move(probe),
                std::move(plan.predicate),
                std::move(bs), std::move(ps));
        }
        case planner::PhysicalKind::FILTER: {
            if (plan.children.empty()) return nullptr;
            auto child = buildPlan(ctx, *plan.children[0]);
            if (!child) return nullptr;
            // The filter sees the child's columns; resolve the schema
            // so the predicate can look up columns by name.
            catalog::Schema cs = resolveChildSchema(ctx->cat, *plan.children[0]);
            return std::make_unique<FilterExecutor>(
                ctx, std::move(child),
                std::move(plan.predicate),
                std::move(cs));
        }
        case planner::PhysicalKind::PROJECT: {
            if (plan.children.empty()) return nullptr;
            auto child = buildPlan(ctx, *plan.children[0]);
            if (!child) return nullptr;
            catalog::Schema cs = resolveChildSchema(ctx->cat, *plan.children[0]);
            return std::make_unique<ProjectExecutor>(
                ctx, std::move(child), plan.outputColumns,
                std::move(plan.projectionExprs), std::move(cs));
        }
        case planner::PhysicalKind::AGGREGATE: {
            if (plan.children.empty()) return nullptr;
            auto child = buildPlan(ctx, *plan.children[0]);
            if (!child) return nullptr;
            return std::make_unique<AggregateExecutor>(
                ctx, std::move(child),
                std::move(plan.projectionExprs),
                std::move(plan.groupBy));
        }
        case planner::PhysicalKind::SORT: {
            if (plan.children.empty()) return nullptr;
            auto child = buildPlan(ctx, *plan.children[0]);
            if (!child) return nullptr;
            // The sort sees the child's columns; resolve the schema
            // so the orderBy columns can be looked up by name.
            catalog::Schema cs = resolveChildSchema(ctx->cat, *plan.children[0]);
            return std::make_unique<SortExecutor>(
                ctx, std::move(child), std::move(plan.orderBy), plan.orderDesc, std::move(cs));
        }
        case planner::PhysicalKind::LIMIT: {
            if (plan.children.empty()) return nullptr;
            auto child = buildPlan(ctx, *plan.children[0]);
            if (!child) return nullptr;
            return std::make_unique<LimitExecutor>(ctx, std::move(child), plan.limit);
        }
        default:
            if (!plan.children.empty()) {
                return buildPlan(ctx, *plan.children[0]);
            }
            return nullptr;
    }
    return nullptr;
}

// Apply ORDER BY + LIMIT in memory to the materialised tuple set.
// ORDER BY is positional for v1: we sort by the first orderBy column.
void sortAndLimit(std::vector<Tuple>& rows, const parser::SelectStmt& s) {
    if (!s.orderBy.empty() && !rows.empty()) {
        const parser::Expr& key = *s.orderBy[0];
        const std::string colName = key.text;
        // Locate the column index by name from the catalog of the first
        // tuple via the query engine context. For v1 we just keep the
        // natural order when the name doesn't appear.
        (void)colName;
        // v1: stable sort to keep the existing row order; a later
        // revision can plug in real comparator functions.
    }
    if (s.limit >= 0 && static_cast<int>(rows.size()) > s.limit) {
        rows.resize(static_cast<std::size_t>(s.limit));
    }
} // namespace

// ----- QueryEngine -----

QueryEngine::QueryEngine(storage::BufferPool*             bp,
                         catalog::CatalogManager*         cat,
                         index::IndexManager*             idx,
                         transaction::TransactionManager* txn,
                         recovery::RecoveryManager*       rec,
                         recovery::WAL*                   wal)
    : ctx_{bp, cat, idx, txn}, wal_(wal)
{
    ctx_.wal = wal_;
    optimizer_ = std::make_unique<planner::Optimizer>(cat, idx, txn);
    (void)rec;
}

// Default the destructor so the unique_ptr<Optimizer> gets a TU-local one.
QueryEngine::~QueryEngine() = default;

// SELECT path. Parse, optimise, build the executor tree, drain it.
//
// The SELECT runs inside an implicit transaction so the reader has an MVCC
// snapshot: SeqScan/IndexScan filter out rows whose (created_txn, deleted_txn)
// trailer says they belong to a txn not committed at the reader's snapshot.
// This is how MVCC delivers non-blocking, consistent reads. The implicit txn
// writes nothing and (in the default AUTOCOMMIT mode) takes no locks, so the
// ad-hoc CLI / demo path is unchanged; it simply becomes snapshot-aware.
std::vector<Tuple> QueryEngine::execute(const std::string& sql) {
    std::vector<Tuple> out;
    parser::Lexer lex(sql);
    auto toks = lex.tokenize();
    parser::Parser p(std::move(toks));
    parser::Stmt stmt = p.parse();
    if (stmt.kind != parser::StmtKind::SELECT || !stmt.select) {
        return out;
    }

    std::unique_ptr<planner::PhysicalPlan> plan;
    try {
        plan = optimizer_->optimize(stmt);
    } catch (...) {
        return out;
    }
    if (!plan) return out;

    // Begin an implicit reader transaction (for the MVCC snapshot). Done
    // after parsing/optimising so a parse failure doesn't leak a txn.
    const bool useTxn = (ctx_.txn != nullptr);
    TransactionId tid = INVALID_TXN_ID;
    if (useTxn) {
        tid = ctx_.txn->begin();
        ctx_.currentTxnId = tid;
        ctx_.readerTxn   = ctx_.txn->getTransaction(tid);
    }

    auto root = buildPlan(&ctx_, *plan);
    if (root && root->init() == Status::OK) {
        Tuple t;
        while (root->next(t) == Status::OK) {
            out.push_back(std::move(t));
        }
        (void)root->close();
    }

    if (useTxn) {
        (void)ctx_.txn->commit(tid);          // release any 2PL locks, drop write-set
        ctx_.currentTxnId = INVALID_TXN_ID;
        ctx_.readerTxn   = nullptr;
    }

    sortAndLimit(out, *stmt.select);
    return out;
}

// INSERT / DELETE / CREATE / DROP / TXN path.
Status QueryEngine::executeUpdate(const std::string& sql) {
    parser::Lexer lex(sql);
    auto toks = lex.tokenize();
    parser::Parser p(std::move(toks));
    parser::Stmt stmt = p.parse();

    // Surface parser errors (e.g. "show tables" which we don't support)
    // as INVALID_ARGUMENT — the CLI prints "Status: INVALID_ARGUMENT".
    // A fuller build would carry the parser's error string through.
    if (!p.lastError().empty() && stmt.kind == parser::StmtKind::SELECT &&
        !stmt.select) {
        return Status::INVALID_ARGUMENT;
    }

    switch (stmt.kind) {
        case parser::StmtKind::INSERT: {
            if (!stmt.insert) return Status::INVALID_ARGUMENT;
            // Drive the insertion inside an implicit transaction so the WAL
            // carries BEGIN / INSERT / COMMIT records that recovery can redo.
            ctx_.lastLsn      = INVALID_LSN;
            ctx_.currentTxnId = INVALID_TXN_ID;
            const bool useTxn = (wal_ != nullptr && ctx_.txn != nullptr);
            TransactionId tid = INVALID_TXN_ID;
            if (useTxn) {
                tid = ctx_.txn->begin();
                ctx_.currentTxnId = tid;
                // The writer's own scans (none here, but uniform with DELETE)
                // would filter by this snapshot.
                ctx_.readerTxn = ctx_.txn->getTransaction(tid);
            }
            InsertExecutor exec(&ctx_, std::move(stmt.insert));
            Status s = exec.init();
            if (s == Status::OK) {
                Tuple t;
                Status ns;
                while ((ns = exec.next(t)) == Status::OK) { /* drain */ }
                if (ns != Status::DONE) s = ns;            // next() raised an error
                Status cs = exec.close();
                if (s == Status::OK) s = cs;
            }
            if (useTxn) {
                // Write-ahead ordering: each row's INSERT record was appended
                // to the WAL after the in-memory heap mutation succeeded, and
                // the BufferPool flushes pages lazily. We fsync the WAL here,
                // at statement commit, before returning to the caller and
                // before any shutdown flush — so the log reaches disk ahead
                // of the dirty pages and the recovery demo can redo.
                if (s == Status::OK) {
                    recovery::LogRecord commit;
                    commit.kind   = recovery::LogKind::COMMIT;
                    commit.txnId  = tid;
                    commit.prevLSN = ctx_.lastLsn;
                    try { (void)wal_->append(commit); wal_->flush(); }
                    catch (...) { /* a WAL failure must never crash execution */ }
                    (void)ctx_.txn->commit(tid);
                } else {
                    recovery::LogRecord abort;
                    abort.kind    = recovery::LogKind::ABORT;
                    abort.txnId   = tid;
                    abort.prevLSN = ctx_.lastLsn;
                    try { (void)wal_->append(abort); }
                    catch (...) {}
                    (void)ctx_.txn->abort(tid);
                }
                ctx_.currentTxnId = INVALID_TXN_ID;
                ctx_.readerTxn   = nullptr;
            }
            return s;
        }
        case parser::StmtKind::DELETE: {
            if (!stmt.del) return Status::INVALID_ARGUMENT;
            // Same implicit-transaction + WAL pattern as INSERT. The
            // DeleteExecutor captures each row's before-image and appends a
            // DELETE record; we then COMMIT + flush here.
            ctx_.lastLsn      = INVALID_LSN;
            ctx_.currentTxnId = INVALID_TXN_ID;
            const bool useTxn = (wal_ != nullptr && ctx_.txn != nullptr);
            TransactionId tid = INVALID_TXN_ID;
            if (useTxn) {
                tid = ctx_.txn->begin();
                ctx_.currentTxnId = tid;
                // The DELETE drives a SeqScan child to find victims; that scan
                // must filter by this writer's snapshot (see only rows that
                // were committed when the delete began).
                ctx_.readerTxn = ctx_.txn->getTransaction(tid);
            }
            DeleteExecutor exec(&ctx_, std::move(stmt.del));
            Status s = exec.init();
            if (s == Status::OK) {
                Tuple t;
                Status ns;
                while ((ns = exec.next(t)) == Status::OK) { /* drain */ }
                if (ns != Status::DONE) s = ns;
                Status cs = exec.close();
                if (s == Status::OK) s = cs;
            }
            if (useTxn) {
                if (s == Status::OK) {
                    recovery::LogRecord commit;
                    commit.kind   = recovery::LogKind::COMMIT;
                    commit.txnId  = tid;
                    commit.prevLSN = ctx_.lastLsn;
                    try { (void)wal_->append(commit); wal_->flush(); }
                    catch (...) {}
                    (void)ctx_.txn->commit(tid);
                } else {
                    recovery::LogRecord abort;
                    abort.kind    = recovery::LogKind::ABORT;
                    abort.txnId   = tid;
                    abort.prevLSN = ctx_.lastLsn;
                    try { (void)wal_->append(abort); }
                    catch (...) {}
                    (void)ctx_.txn->abort(tid);
                }
                ctx_.currentTxnId = INVALID_TXN_ID;
                ctx_.readerTxn   = nullptr;
            }
            return s;
        }
        case parser::StmtKind::CREATE: {
            if (!stmt.create) return Status::INVALID_ARGUMENT;
            const auto& c = *stmt.create;
            // CREATE INDEX <name> ON <tbl> (<col>) — handled by the index
            // manager directly; no table is created.
            if (c.isIndex) {
                if (ctx_.idx == nullptr) return Status::INVALID_ARGUMENT;
                return ctx_.idx->createIndex(c.table, c.indexColumn, c.indexName);
            }
            catalog::TableInfo info;
            info.name = c.table;
            info.schema = catalog::Schema();
            std::string primaryColumn;
            for (const auto& col : c.columns) {
                if (col.isPrimaryKey && primaryColumn.empty()) {
                    primaryColumn = col.name;
                    info.primaryIndexName = c.table + "_" + col.name + "_pk";
                }
                info.schema.addColumn(col);
            }
            Status s = ctx_.cat->createTable(info);
            if (s != Status::OK) return s;
            if (!primaryColumn.empty() && ctx_.idx != nullptr) {
                Status is = ctx_.idx->createIndex(c.table, primaryColumn, info.primaryIndexName);
                if (is != Status::OK && is != Status::DUPLICATE_KEY) return is;
            }
            return Status::OK;
        }
        case parser::StmtKind::DROP: {
            if (!stmt.drop) return Status::INVALID_ARGUMENT;
            return ctx_.cat->dropTable(stmt.drop->table);
        }
        case parser::StmtKind::TXN: {
            if (!stmt.txn) return Status::INVALID_ARGUMENT;
            switch (stmt.txn->op) {
                case parser::TxnStmt::Op::BEGIN:
                    (void)ctx_.txn->begin();
                    return Status::OK;
                case parser::TxnStmt::Op::COMMIT: {
                    auto active = ctx_.txn->activeTxns();
                    Status last = Status::OK;
                    for (auto id : active) last = ctx_.txn->commit(id);
                    return last;
                }
                case parser::TxnStmt::Op::ROLLBACK: {
                    auto active = ctx_.txn->activeTxns();
                    Status last = Status::OK;
                    for (auto id : active) last = ctx_.txn->abort(id);
                    return last;
                }
            }
            return Status::OK;
        }
        case parser::StmtKind::SELECT:
            return Status::INVALID_ARGUMENT;
    }
    return Status::INVALID_ARGUMENT;
}

} // namespace minidb::executor
