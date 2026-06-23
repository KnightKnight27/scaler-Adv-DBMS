#include "transaction/transaction_manager.h"

#include <stdexcept>

namespace minidb {

TransactionManager::TransactionManager(LockManager* lock_manager, WriteAheadLog* wal)
    : lock_manager_(lock_manager), wal_(wal) {}

void TransactionManager::Begin() {
    if (active_txn_) {
        throw std::runtime_error("Transaction already active");
    }
    active_txn_ = next_txn_id_++;
    explicit_txn_ = true;
    undo_log_.clear();
    LogRecord rec;
    rec.type = LogRecordType::BEGIN;
    rec.txn_id = *active_txn_;
    wal_->Append(rec);
}

void TransactionManager::Commit() {
    if (!active_txn_) {
        throw std::runtime_error("No active transaction");
    }
    LogRecord rec;
    rec.type = LogRecordType::COMMIT;
    rec.txn_id = *active_txn_;
    wal_->Append(rec);
    wal_->Flush();
    lock_manager_->UnlockAll(*active_txn_);
    active_txn_.reset();
    explicit_txn_ = false;
    undo_log_.clear();
}

void TransactionManager::Rollback() {
    if (!active_txn_) {
        throw std::runtime_error("No active transaction");
    }
    LogRecord rec;
    rec.type = LogRecordType::ABORT;
    rec.txn_id = *active_txn_;
    wal_->Append(rec);
    lock_manager_->UnlockAll(*active_txn_);
    active_txn_.reset();
    explicit_txn_ = false;
    // undo_log_ cleared by Database after applying undo
}

void TransactionManager::LockTable(const std::string& table, LockMode mode) {
    if (!active_txn_) {
        active_txn_ = next_txn_id_++;
        explicit_txn_ = false;
        undo_log_.clear();
        LogRecord rec;
        rec.type = LogRecordType::BEGIN;
        rec.txn_id = *active_txn_;
        wal_->Append(rec);
    }
    if (!lock_manager_->Lock(table, mode, *active_txn_)) {
        throw std::runtime_error("Deadlock detected while locking table: " + table);
    }
}

void TransactionManager::LogInsert(const std::string& table, const Row& row, const Rid& rid) {
    undo_log_.push_back(UndoEntry{UndoEntry::Op::INSERT, table, row, rid});
    LogRecord rec;
    rec.type = LogRecordType::INSERT;
    rec.txn_id = active_txn_.value_or(0);
    rec.table = table;
    rec.row = row;
    rec.rid = rid;
    wal_->Append(rec);
}

void TransactionManager::LogDelete(const std::string& table, const Row& row, const Rid& rid) {
    undo_log_.push_back(UndoEntry{UndoEntry::Op::DELETE_TUP, table, row, rid});
    LogRecord rec;
    rec.type = LogRecordType::DELETE_TUP;
    rec.txn_id = active_txn_.value_or(0);
    rec.table = table;
    rec.row = row;
    rec.rid = rid;
    wal_->Append(rec);
}

void TransactionManager::CommitIfAuto() {
    if (active_txn_ && !explicit_txn_) {
        Commit();
    }
}

}  // namespace minidb
