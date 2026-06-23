#include "engine/database.h"
#include "exec/executor.h"
#include "sql/parser.h"
#include <unordered_set>
#include <unordered_map>

namespace minidb {

Database::Database(const std::string &base_path) : base_(base_path) {
    // WAL first (it is the source of truth), then a *fresh* data file which we
    // rebuild by replaying the log.
    log_     = std::make_unique<LogManager>(base_ + ".wal");
    disk_    = std::make_unique<DiskManager>(base_ + ".db", /*truncate=*/true);
    bpm_     = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_.get());
    catalog_ = std::make_unique<Catalog>(bpm_.get());
    recover();
}

Database::~Database() {
    checkpoint();
}

// ---- Recovery: REDO committed work, implicitly UNDO the rest ----
void Database::recover() {
    std::vector<LogRecord> recs = log_->read_all();
    if (recs.empty()) return;

    // Which transactions reached COMMIT? Only their effects are replayed.
    std::unordered_set<txn_id_t> committed;
    txn_id_t max_txn = 0;
    for (const auto &r : recs) {
        if (r.type == LogType::COMMIT) committed.insert(r.txn);
        max_txn = std::max(max_txn, r.txn);
    }
    next_txn_id_ = max_txn + 1;

    for (const auto &r : recs) {
        switch (r.type) {
            case LogType::CREATE_TABLE:
                // DDL is auto-committed; always reinstate the table.
                if (!catalog_->get_table(r.table))
                    create_table(r.table, r.schema, nullptr, /*logging=*/false);
                break;
            case LogType::INSERT: {
                if (!committed.count(r.txn)) break;       // loser -> skip (undo)
                TableInfo *ti = catalog_->get_table(r.table);
                if (!ti) break;
                Tuple row = DeserializeTuple(r.tuple_bytes.data(), ti->schema);
                insert_row(ti, row, nullptr, /*logging=*/false);
                break;
            }
            case LogType::DELETE: {
                if (!committed.count(r.txn)) break;
                TableInfo *ti = catalog_->get_table(r.table);
                if (!ti || !ti->has_index) break;          // need PK to locate row
                Tuple row = DeserializeTuple(r.tuple_bytes.data(), ti->schema);
                RID rid;
                int pk = ti->schema.pk_index;
                if (ti->index->search(row[pk].as_int(), &rid))
                    delete_row(ti, rid, row, nullptr, /*logging=*/false);
                break;
            }
            default: break; // BEGIN/COMMIT/ABORT/CHECKPOINT need no data action
        }
    }
}

// ---- Transaction lifecycle ----
Transaction *Database::begin() {
    txn_id_t id = next_txn_id_++;
    auto *txn = new Transaction(id);
    LogRecord r; r.type = LogType::BEGIN; r.txn = id;
    log_->append(r);
    return txn;
}

void Database::commit(Transaction *txn) {
    LogRecord r; r.type = LogType::COMMIT; r.txn = txn->id();
    log_->append(r);
    log_->flush();                 // durability point: force WAL before releasing
    txn->set_state(TxnState::COMMITTED);
    lock_mgr_.release_all(txn);    // strict 2PL: release all locks at end
    delete txn;
}

void Database::abort(Transaction *txn) {
    // Roll back by applying the inverse of each change, newest first.
    auto &undo = txn->undo_log();
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
        TableInfo *ti = catalog_->get_table(it->table);
        if (!ti) continue;
        if (it->kind == UndoKind::UNDO_INSERT) {
            // Undo an insert: remove the row.
            if (ti->has_index) {
                Tuple row;
                if (ti->heap->get_tuple(it->rid, &row))
                    ti->index->remove(row[ti->schema.pk_index].as_int());
            }
            if (ti->heap->delete_tuple(it->rid)) ti->row_count--;
        } else { // UNDO_DELETE: reinsert the removed row
            RID nr = ti->heap->insert_tuple(it->old_tuple);
            if (ti->has_index) ti->index->insert(it->old_tuple[ti->schema.pk_index].as_int(), nr);
            ti->row_count++;
        }
    }
    LogRecord r; r.type = LogType::ABORT; r.txn = txn->id();
    log_->append(r);
    log_->flush();
    txn->set_state(TxnState::ABORTED);
    lock_mgr_.release_all(txn);
    delete txn;
}

// ---- DDL / DML ----
TableInfo *Database::create_table(const std::string &name, const Schema &schema,
                                  Transaction *txn, bool logging) {
    TableInfo *ti = catalog_->create_table(name, schema);
    if (logging) {
        LogRecord r; r.type = LogType::CREATE_TABLE; r.txn = txn ? txn->id() : 0;
        r.table = name; r.schema = schema;
        log_->append(r);
        log_->flush(); // DDL is auto-committed
    }
    return ti;
}

RID Database::insert_row(TableInfo *t, const Tuple &row, Transaction *txn, bool logging) {
    RID rid = t->heap->insert_tuple(row);
    if (t->has_index) t->index->insert(row[t->schema.pk_index].as_int(), rid);
    t->row_count++;
    if (logging) {
        LogRecord r; r.type = LogType::INSERT; r.txn = txn ? txn->id() : 0;
        r.table = t->name; r.tuple_bytes = SerializeTuple(row, t->schema);
        log_->append(r);
    }
    if (txn) txn->record_undo({UndoKind::UNDO_INSERT, t->name, rid, {}});
    return rid;
}

void Database::delete_row(TableInfo *t, const RID &rid, const Tuple &old_row,
                          Transaction *txn, bool logging) {
    if (t->has_index) t->index->remove(old_row[t->schema.pk_index].as_int());
    if (t->heap->delete_tuple(rid)) t->row_count--;
    if (logging) {
        LogRecord r; r.type = LogType::DELETE; r.txn = txn ? txn->id() : 0;
        r.table = t->name; r.tuple_bytes = SerializeTuple(old_row, t->schema);
        log_->append(r);
    }
    if (txn) txn->record_undo({UndoKind::UNDO_DELETE, t->name, rid, old_row});
}

void Database::checkpoint() {
    bpm_->flush_all();
    disk_->sync();
    log_->flush();
}

// ---- SQL entry point ----
QueryResult Database::execute(const std::string &sql, Transaction *txn) {
    std::unique_ptr<Statement> stmt;
    try {
        stmt = ParseSQL(sql);
    } catch (const std::exception &e) {
        return QueryResult::error(std::string("parse error: ") + e.what());
    }

    // Transaction-control statements are managed by the caller/REPL.
    if (stmt->type == StmtType::BEGIN || stmt->type == StmtType::COMMIT ||
        stmt->type == StmtType::ABORT) {
        return QueryResult::status("transaction control handled by shell");
    }

    bool autocommit = (txn == nullptr);
    Transaction *t = autocommit ? begin() : txn;

    QueryResult res;
    try {
        switch (stmt->type) {
            case StmtType::CREATE_TABLE: {
                auto *s = static_cast<CreateTableStmt *>(stmt.get());
                create_table(s->name, s->schema, t, true);
                res = QueryResult::status("CREATE TABLE " + s->name);
                break;
            }
            case StmtType::INSERT: {
                Executor ex(*this, t);
                res = ex.run_insert(*static_cast<InsertStmt *>(stmt.get()));
                break;
            }
            case StmtType::DELETE: {
                Executor ex(*this, t);
                res = ex.run_delete(*static_cast<DeleteStmt *>(stmt.get()));
                break;
            }
            case StmtType::SELECT: {
                Executor ex(*this, t);
                res = ex.run_select(*static_cast<SelectStmt *>(stmt.get()));
                break;
            }
            default: res = QueryResult::error("unsupported statement");
        }
    } catch (const TransactionAbortException &e) {
        abort(t);
        return QueryResult::error(e.what());
    } catch (const std::exception &e) {
        if (autocommit) abort(t);
        return QueryResult::error(e.what());
    }

    if (autocommit) {
        if (res.ok) commit(t);
        else        abort(t);
    }
    return res;
}

} // namespace minidb
