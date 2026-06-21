#include "database.hpp"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <unordered_set>

Database::Database(const std::string& dir)
    : catalog_(dir), wal_(dir + "/minidb.wal") {}

void Database::begin() {
    if (current_ != 0) throw std::runtime_error("transaction already in progress");
    current_ = next_txid_++;
    implicit_ = false;
    undo_.clear();
    wal_.append({LogType::BEGIN, current_, "", 0, ""});
}

void Database::commit() {
    if (current_ == 0 || implicit_) throw std::runtime_error("no active transaction to COMMIT");
    commit_internal();
}

void Database::rollback() {
    if (current_ == 0 || implicit_) throw std::runtime_error("no active transaction to ROLLBACK");
    rollback_internal();
}

TxID Database::begin_stmt() {
    if (current_ != 0) return current_;            // inside explicit txn
    current_ = next_txid_++;                        // start implicit
    implicit_ = true;
    undo_.clear();
    wal_.append({LogType::BEGIN, current_, "", 0, ""});
    return current_;
}

void Database::end_stmt(bool ok) {
    if (!implicit_) return;                         // explicit: wait for COMMIT/ROLLBACK
    if (ok) commit_internal();
    else    rollback_internal();
}

void Database::track_insert(const std::string& table, int pk, RowID rid) {
    undo_.push_back({LogType::INSERT, table, pk, rid, ""});
}

void Database::track_delete(const std::string& table, int pk, const std::string& before_image) {
    undo_.push_back({LogType::DELETE, table, pk, RowID{}, before_image});
}

void Database::commit_internal() {
    wal_.append({LogType::COMMIT, current_, "", 0, ""});
    wal_.flush();  // committed records now durable
    undo_.clear();
    current_ = 0;
    implicit_ = false;
}

void Database::rollback_internal() {
    // reverse applied writes, newest first
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
        Table* t = catalog_.get_table(it->table);
        if (!t) continue;
        if (it->type == LogType::INSERT) {           // undo insert
            t->heap->erase(it->rid);
            t->index->remove(it->pk);
            if (t->row_count) --t->row_count;
        } else {                                     // undo delete: put row back
            RowID rid = t->heap->insert(it->image);
            t->index->insert(it->pk, rid);
            ++t->row_count;
        }
    }
    wal_.append({LogType::ABORT, current_, "", 0, ""});
    wal_.flush();
    undo_.clear();
    current_ = 0;
    implicit_ = false;
}

void Database::recover(std::ostream& out) {
    std::vector<LogRecord> log = wal_.read_all();

    // committed iff COMMIT record on disk
    std::unordered_set<TxID> committed;
    for (const LogRecord& r : log)
        if (r.type == LogType::COMMIT) committed.insert(r.txid);

    long redo = 0, undo = 0;

    // redo committed writes in log order
    for (const LogRecord& r : log) {
        Table* t = catalog_.get_table(r.table);
        if (!t || !committed.count(r.txid)) continue;
        if (r.type == LogType::INSERT) {
            if (!t->index->search(r.pk)) {
                RowID rid = t->heap->insert(r.image);
                t->index->insert(r.pk, rid);
                ++t->row_count;
                ++redo;
            }
        } else if (r.type == LogType::DELETE) {
            if (auto rid = t->index->search(r.pk)) {
                t->heap->erase(*rid);
                t->index->remove(r.pk);
                if (t->row_count) --t->row_count;
                ++redo;
            }
        }
    }

    // undo loser writes that reached the heap
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        Table* t = catalog_.get_table(it->table);
        if (!t || committed.count(it->txid)) continue;
        if (it->type == LogType::INSERT) {
            if (auto rid = t->index->search(it->pk)) {
                t->heap->erase(*rid);
                t->index->remove(it->pk);
                if (t->row_count) --t->row_count;
                ++undo;
            }
        } else if (it->type == LogType::DELETE) {
            if (!t->index->search(it->pk)) {
                RowID rid = t->heap->insert(it->image);
                t->index->insert(it->pk, rid);
                ++t->row_count;
                ++undo;
            }
        }
    }

    out << "recovery complete: redid " << redo << " op(s), undid " << undo
        << " loser op(s) from " << log.size() << " log record(s)\n";
}
