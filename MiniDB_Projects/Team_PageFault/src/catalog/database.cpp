#include "database.hpp"
#include <stdexcept>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace minidb {

Database::Database(const std::string& db_path)
    : db_path_(db_path),
      disk_(std::make_unique<DiskManager>(db_path + ".dat")),
      bp_(std::make_unique<BufferPool>(*disk_)),
      catalog_(*bp_, db_path + ".cat"),
      wal_(db_path + ".wal") {
    catalog_.load();
    recover();
}

Database::~Database() {
    // Best-effort clean shutdown. (The REPL's CRASH command bypasses this.)
    try { flush(); } catch (...) {}
}

Table* Database::createTable(const std::string& name, const Schema& schema) {
    return catalog_.create(name, schema);
}

void Database::physInsert(Table* t, int64_t pk, const std::string& image) {
    RowId rid = t->heap->insert(image.data(), static_cast<uint16_t>(image.size()));
    t->index->insert(pk, rid);
}

void Database::physDelete(Table* t, int64_t pk) {
    RowId rid;
    if (!t->index->search(pk, rid)) return;
    t->heap->erase(rid);
    t->index->erase(pk);
}

bool Database::ensureTxn() {
    if (current_txn_ != INVALID_TXN) return false; // already inside an explicit txn
    begin();
    autocommit_started_ = true;
    return true;
}

void Database::maybeAutoCommit(bool implicit) {
    if (implicit) commit();
}

void Database::begin() {
    if (current_txn_ != INVALID_TXN) throw std::runtime_error("transaction already active");
    current_txn_ = next_txn_++;
    autocommit_started_ = false;
    undo_.clear();
    wal_.logBegin(current_txn_);
}

void Database::commit() {
    if (current_txn_ == INVALID_TXN) throw std::runtime_error("no active transaction");
    wal_.logCommit(current_txn_); // flushed -> transaction is now durable
    current_txn_ = INVALID_TXN;
    autocommit_started_ = false;
    undo_.clear();
}

void Database::rollback() {
    if (current_txn_ == INVALID_TXN) throw std::runtime_error("no active transaction");
    // Replay inverse operations in reverse order.
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
        Table* t = catalog_.get(it->table);
        if (!t) continue;
        if (it->was_insert) physDelete(t, it->pk);            // undo an insert
        else physInsert(t, it->pk, it->image);                // undo a delete
    }
    wal_.logAbort(current_txn_);
    current_txn_ = INVALID_TXN;
    autocommit_started_ = false;
    undo_.clear();
}

void Database::insertRow(Table* t, const std::vector<Value>& row) {
    int64_t pk = t->schema.primaryKey(row);
    RowId existing;
    if (t->index->search(pk, existing))
        throw std::runtime_error("duplicate primary key: " + std::to_string(pk));

    bool implicit = ensureTxn();
    std::string image = t->schema.serialize(row);
    wal_.logInsert(current_txn_, t->name, pk, image); // write-ahead
    physInsert(t, pk, image);
    undo_.push_back(Undo{true, t->name, pk, image});
    maybeAutoCommit(implicit);
}

bool Database::deleteByKey(Table* t, int64_t pk) {
    RowId rid;
    if (!t->index->search(pk, rid)) return false;
    std::string before;
    t->heap->get(rid, before); // before-image for UNDO

    bool implicit = ensureTxn();
    wal_.logDelete(current_txn_, t->name, pk, before); // write-ahead
    t->heap->erase(rid);
    t->index->erase(pk);
    undo_.push_back(Undo{false, t->name, pk, before});
    maybeAutoCommit(implicit);
    return true;
}

void Database::flush() {
    if (bp_) bp_->flushAll();
    catalog_.save();
}

void Database::recover() {
    std::vector<LogRecord> log = wal_.readAll();
    if (log.empty()) return;

    // Pass 1: which transactions committed?
    std::unordered_set<TxnId> committed;
    TxnId max_txn = 0;
    for (const LogRecord& r : log) {
        max_txn = std::max(max_txn, r.txid);
        if (r.type == LogType::COMMIT) committed.insert(r.txid);
    }
    next_txn_ = max_txn + 1;

    // Pass 2: REDO every committed op, in log order (idempotent by pk).
    for (const LogRecord& r : log) {
        if (!committed.count(r.txid)) continue;
        Table* t = catalog_.get(r.table);
        if (!t) continue;
        RowId rid;
        if (r.type == LogType::INSERT) {
            if (!t->index->search(r.pk, rid)) physInsert(t, r.pk, r.image); // insert-if-absent
        } else if (r.type == LogType::DELETE) {
            if (t->index->search(r.pk, rid)) physDelete(t, r.pk);           // delete-if-present
        }
    }

    // Pass 3: UNDO loser ops (uncommitted), in reverse log order.
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        const LogRecord& r = *it;
        if (committed.count(r.txid)) continue;
        Table* t = catalog_.get(r.table);
        if (!t) continue;
        RowId rid;
        if (r.type == LogType::INSERT) {
            if (t->index->search(r.pk, rid)) physDelete(t, r.pk);           // remove loser insert
        } else if (r.type == LogType::DELETE) {
            if (!t->index->search(r.pk, rid)) physInsert(t, r.pk, r.image); // restore loser delete
        }
    }

    // Checkpoint: persist the recovered state and start a fresh log.
    flush();
    wal_.reset();
}

} // namespace minidb
