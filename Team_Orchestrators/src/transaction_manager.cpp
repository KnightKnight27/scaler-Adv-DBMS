#include "minidb/transaction_manager.hpp"

#include <stdexcept>

namespace minidb {

TransactionManager::TransactionManager(LockManager& lock_manager, WriteAheadLog& wal)
    : lock_manager_(lock_manager), wal_(wal) {}

TxnId TransactionManager::begin() {
  if (active_) throw std::runtime_error("transaction already in progress");
  active_ = true;
  txn_ = next_txn_++;
  undo_.clear();
  locked_.clear();
  WalRecord r;
  r.txn = txn_;
  r.type = WalType::Begin;
  wal_.append(r);
  return txn_;
}

void TransactionManager::lock_table(TableId table) {
  if (locked_.count(table)) return;  // already held this txn (strict 2PL grow)
  lock_manager_.acquire(table, LockManager::Mode::Exclusive);
  locked_.insert(table);
}

void TransactionManager::release_locks() {
  for (TableId t : locked_) lock_manager_.release(t);
  locked_.clear();
}

void TransactionManager::on_insert(TableId table, const RID& rid,
                                   const std::vector<uint8_t>& after) {
  if (!active_) throw std::runtime_error("no active transaction");
  lock_table(table);
  WalRecord r;
  r.txn = txn_;
  r.type = WalType::Insert;
  r.table = table;
  r.rid = rid;
  r.payload = after;
  wal_.append(r);
  wal_.flush();  // write-ahead: log durable before the page can be stolen to disk
  undo_.push_back({WalType::Insert, table, rid, {}});
}

void TransactionManager::on_delete(TableId table, const RID& rid,
                                   const std::vector<uint8_t>& before) {
  if (!active_) throw std::runtime_error("no active transaction");
  lock_table(table);
  WalRecord r;
  r.txn = txn_;
  r.type = WalType::Delete;
  r.table = table;
  r.rid = rid;
  r.payload = before;
  wal_.append(r);
  wal_.flush();
  undo_.push_back({WalType::Delete, table, rid, before});
}

void TransactionManager::commit() {
  if (!active_) throw std::runtime_error("no active transaction");
  WalRecord r;
  r.txn = txn_;
  r.type = WalType::Commit;
  wal_.append(r);
  wal_.flush();  // commit point: log durable
  undo_.clear();
  release_locks();
  active_ = false;
}

void TransactionManager::rollback(StorageEngine& engine) {
  if (!active_) throw std::runtime_error("no active transaction");
  // Reverse-apply the undo list: an insert's undo deletes its RID; a delete's
  // undo reinserts its before-image at the same RID.
  for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
    if (it->type == WalType::Insert)
      engine.replay_delete(it->table, it->rid);
    else
      engine.replay_insert(it->table, it->rid, it->image);
  }
  WalRecord r;
  r.txn = txn_;
  r.type = WalType::Abort;
  wal_.append(r);
  wal_.flush();
  undo_.clear();
  release_locks();
  active_ = false;
}

void TransactionManager::recover(StorageEngine& engine) {
  std::vector<WalRecord> recs = wal_.read_all();

  // Analysis: classify transactions.
  std::set<TxnId> committed;
  std::set<TxnId> finished;  // committed or explicitly aborted
  for (const auto& r : recs) {
    if (r.type == WalType::Commit) { committed.insert(r.txn); finished.insert(r.txn); }
    else if (r.type == WalType::Abort) { finished.insert(r.txn); }
  }

  // Redo committed work in LSN (append) order.
  for (const auto& r : recs) {
    if (!committed.count(r.txn)) continue;
    if (r.type == WalType::Insert) engine.replay_insert(r.table, r.rid, r.payload);
    else if (r.type == WalType::Delete) engine.replay_delete(r.table, r.rid);
  }

  // Undo in-flight (neither committed nor aborted) work in reverse order.
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    if (finished.count(it->txn)) continue;
    if (it->type == WalType::Insert) engine.replay_delete(it->table, it->rid);
    else if (it->type == WalType::Delete) engine.replay_insert(it->table, it->rid, it->payload);
  }

  wal_.truncate();  // checkpoint: recovered state is about to be persisted
  next_txn_ = 1;
}

}  // namespace minidb
