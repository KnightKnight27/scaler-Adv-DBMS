#include "database.h"

#include <unordered_set>

#include "index/bplus_tree.h"
#include "parser/parser.h"
#include "planner/planner.h"

namespace minidb {

Database::Database(const std::string& db_file, size_t pool_frames)
    : db_file_(db_file), pool_frames_(pool_frames) {
    disk_ = std::make_unique<DiskManager>(db_file);
    bpool_ = std::make_unique<BufferPool>(disk_.get(), pool_frames);
    catalog_ = std::make_unique<Catalog>(bpool_.get());
    txn_mgr_ = std::make_unique<TransactionManager>(&lock_mgr_);
    log_ = std::make_unique<LogManager>(db_file + ".wal");
    if (!log_->Empty()) Recover();  // a non-empty WAL means we are reopening after a crash
}

Transaction* Database::BeginTxn() {
    Transaction* t = txn_mgr_->Begin();
    if (!recovering_) log_->Append(LogRecord::Begin(t->id()));
    return t;
}

void Database::CommitTxn(Transaction* t) {
    txn_mgr_->Commit(t);
    if (!recovering_) { log_->Append(LogRecord::Commit(t->id())); log_->Flush(); }  // durable at commit
}

// Undo a transaction's inserts (delete the rows it added and remove their index entries).
// This is the M4 rollback for the deadlock demo; the MVCC build (M5) supersedes it with
// version-chain discard + WAL undo, which also rolls back deletes.
void Database::Rollback(Transaction* txn) {
    const auto& undo = txn->undo();
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
        TableInfo* t = catalog_->GetTable(it->table);
        if (!t) continue;
        std::string bytes;
        if (t->heap->Get(it->rid, &bytes)) {
            Tuple row = Tuple::Deserialize(bytes, t->schema);
            for (auto& ix : t->indexes) ix.tree->Remove(row.GetValue(ix.col_idx), it->rid);
            t->heap->Delete(it->rid);
            if (t->num_rows > 0) t->num_rows--;
        }
    }
}

void Database::AbortTxn(Transaction* txn) {
    Rollback(txn);
    txn_mgr_->Abort(txn);
    if (!recovering_) { log_->Append(LogRecord::Abort(txn->id())); log_->Flush(); }
}

// Replay helpers used by recovery (mutate catalog/heap/index directly, no logging).
void Database::ApplyInsert(const std::string& table, const std::vector<Value>& row) {
    TableInfo* t = catalog_->GetTable(table);
    if (!t) return;
    Tuple tuple(row);
    RID rid = t->heap->Insert(tuple.Serialize(t->schema));
    for (auto& ix : t->indexes) ix.tree->Insert(tuple.GetValue(ix.col_idx), rid);
    t->num_rows++;
}

void Database::ApplyDeleteByValues(const std::string& table, const std::vector<Value>& row) {
    TableInfo* t = catalog_->GetTable(table);
    if (!t) return;
    Tuple target(row);
    for (auto it = t->heap->begin(); it != t->heap->end(); ++it) {
        Tuple cur = Tuple::Deserialize(it.GetRecord(), t->schema);
        bool eq = cur.Count() == target.Count();
        for (size_t i = 0; eq && i < cur.Count(); ++i)
            if (!(cur.GetValue(i) == target.GetValue(i))) eq = false;
        if (eq) {
            RID rid = it.GetRID();
            for (auto& ix : t->indexes) ix.tree->Remove(cur.GetValue(ix.col_idx), rid);
            t->heap->Delete(rid);
            if (t->num_rows > 0) t->num_rows--;
            return;  // remove a single matching row
        }
    }
}

void Database::Recover() {
    std::vector<LogRecord> recs = log_->ReadAll();
    if (recs.empty()) return;

    // A transaction's effects count only if it committed (autocommit txn 0 always counts).
    std::unordered_set<int> committed{0}, aborted;
    for (const auto& r : recs) {
        if (r.type == LogType::Commit) committed.insert(r.txn_id);
        else if (r.type == LogType::Abort) aborted.insert(r.txn_id);
    }
    auto winner = [&](int txn) { return committed.count(txn) && !aborted.count(txn); };

    // Rebuild from a clean slate: the WAL is the source of truth.
    disk_->Truncate();
    bpool_ = std::make_unique<BufferPool>(disk_.get(), pool_frames_);
    catalog_ = std::make_unique<Catalog>(bpool_.get());

    recovering_ = true;
    for (const auto& r : recs) {
        switch (r.type) {
            case LogType::CreateTable:
                catalog_->CreateTable(r.table, Schema(r.columns));
                break;
            case LogType::CreateIndex:
                catalog_->CreateIndex(r.table, r.column, r.unique);
                break;
            case LogType::Insert:
                if (winner(r.txn_id)) ApplyInsert(r.table, r.row);
                break;
            case LogType::Delete:
                if (winner(r.txn_id)) ApplyDeleteByValues(r.table, r.row);
                break;
            default:
                break;  // Begin/Commit/Abort markers
        }
    }
    recovering_ = false;
}

Result Database::Execute(const std::string& sql) {
    std::unique_ptr<Statement> stmt;
    try {
        Parser p(sql);
        stmt = p.Parse();
    } catch (const std::exception& e) {
        return Result::Error(std::string("parse error: ") + e.what());
    }

    // Session-mode transaction control.
    if (stmt->kind == Statement::Kind::Txn) {
        auto* ts = static_cast<TxnStmt*>(stmt.get());
        if (ts->op == TxnStmt::Op::Begin) {
            if (session_txn_) return Result::Error("already in a transaction");
            session_txn_ = BeginTxn();
            Result r; r.message = "BEGIN (txn " + std::to_string(session_txn_->id()) + ")"; return r;
        }
        if (!session_txn_) return Result::Error("no active transaction");
        int id = session_txn_->id();
        if (ts->op == TxnStmt::Op::Commit) CommitTxn(session_txn_);
        else                               AbortTxn(session_txn_);
        session_txn_ = nullptr;
        Result r; r.message = (ts->op == TxnStmt::Op::Commit ? "COMMIT (txn " : "ABORT (txn ") +
                              std::to_string(id) + ")";
        return r;
    }

    return Dispatch(stmt.get(), session_txn_);
}

Result Database::Execute(const std::string& sql, Transaction* txn) {
    std::unique_ptr<Statement> stmt;
    try {
        Parser p(sql);
        stmt = p.Parse();
    } catch (const std::exception& e) {
        return Result::Error(std::string("parse error: ") + e.what());
    }
    if (stmt->kind == Statement::Kind::Txn)
        return Result::Error("use BeginTxn/CommitTxn/AbortTxn for explicit transactions");
    return Dispatch(stmt.get(), txn);
}

Result Database::Dispatch(Statement* stmt, Transaction* txn) {
    try {
        switch (stmt->kind) {
            case Statement::Kind::CreateTable: {
                auto* c = static_cast<CreateTableStmt*>(stmt);
                Schema schema(c->columns);
                TableInfo* t = catalog_->CreateTable(c->table, schema);
                if (!t) return Result::Error("table already exists: " + c->table);
                if (!recovering_) {
                    LogRecord rec; rec.type = LogType::CreateTable; rec.table = c->table;
                    rec.columns = c->columns; log_->Append(rec);
                }
                Result r;
                r.message = "created table " + c->table;
                int pk = schema.PrimaryKeyIdx();
                if (pk >= 0) {
                    const std::string& pkcol = schema.GetColumn(pk).name;
                    catalog_->CreateIndex(c->table, pkcol, /*unique=*/true);
                    if (!recovering_) {
                        LogRecord rec; rec.type = LogType::CreateIndex; rec.table = c->table;
                        rec.column = pkcol; rec.unique = true; log_->Append(rec);
                    }
                    r.message += " (primary-key index on " + pkcol + ")";
                }
                return r;
            }
            case Statement::Kind::CreateIndex: {
                auto* c = static_cast<CreateIndexStmt*>(stmt);
                IndexInfo* ix = catalog_->CreateIndex(c->table, c->column, c->unique);
                if (!ix) return Result::Error("could not create index on " + c->table + "(" + c->column + ")");
                if (!recovering_) {
                    LogRecord rec; rec.type = LogType::CreateIndex; rec.table = c->table;
                    rec.column = c->column; rec.unique = c->unique; log_->Append(rec);
                }
                Result r; r.message = "created index on " + c->table + "(" + c->column + ")"; return r;
            }
            default:
                return RunPlanned(stmt, txn);
        }
    } catch (const std::exception& e) {
        return Result::Error(e.what());
    }
}

Result Database::RunPlanned(Statement* stmt, Transaction* txn) {
    ExecutionContext ctx{txn, &lock_mgr_, txn_mgr_.get(), nullptr, log_.get()};
    Planner planner(catalog_.get(), &ctx);
    PhysicalPlan plan = planner.Plan(stmt);

    Result r;
    r.explain = plan.explain;
    try {
        plan.exec->Init();
        if (stmt->kind == Statement::Kind::Select) {
            r.is_query = true;
            r.schema = plan.exec->OutSchema();
            Tuple row;
            while (plan.exec->Next(&row)) r.rows.push_back(row);
            return r;
        }
        r.affected = plan.exec->RowsAffected();
        r.message = (stmt->kind == Statement::Kind::Insert ? "inserted " : "deleted ") +
                    std::to_string(r.affected) + " row(s)";
        return r;
    } catch (const TxnAborted& a) {
        if (txn) AbortTxn(txn);
        return Result::Error("transaction aborted: " + a.reason);
    }
}

}  // namespace minidb
