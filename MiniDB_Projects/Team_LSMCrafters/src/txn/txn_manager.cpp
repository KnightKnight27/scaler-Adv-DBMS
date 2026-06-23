#include "txn/txn_manager.h"

namespace minidb {

TransactionManager::TransactionManager(LockManager& locks, LogManager& log,
                                       StorageEngine& table, TableId table_id)
    : locks_(locks), log_(log), table_(table), table_id_(table_id) {}

LogRecord TransactionManager::make_record(TxnId txn, LogType type, Key key) {
  LogRecord r;
  r.txn   = txn;
  r.type  = type;
  r.table = table_id_;
  r.key   = key;
  return r;
}

TxnId TransactionManager::begin() {
  std::lock_guard<std::mutex> lk(txns_mu_);
  TxnId id = next_id_++;
  txns_.emplace(id, Txn{id});
  log_.append(make_record(id, LogType::Begin, 0));
  return id;
}

Txn& TransactionManager::get(TxnId txn) {
  std::lock_guard<std::mutex> lk(txns_mu_);
  return txns_.at(txn);  // unordered_map references stay valid across rehash
}

std::optional<Bytes> TransactionManager::read(TxnId txn, Key key) {
  locks_.lock_shared(txn, RowKey{table_id_, key});
  std::lock_guard<std::mutex> latch(storage_latch_);
  return table_.get(key);
}

void TransactionManager::write(TxnId txn, Key key, const Bytes& value) {
  locks_.lock_exclusive(txn, RowKey{table_id_, key});

  LogRecord rec = make_record(txn, LogType::Insert, key);
  rec.has_after = true;
  rec.after     = value;
  {
    std::lock_guard<std::mutex> latch(storage_latch_);
    if (auto before = table_.get(key)) { rec.has_before = true; rec.before = *before; }
  }
  LSN lsn = log_.append(rec);
  log_.flush_upto(lsn);  // write-ahead: record is durable before the data change

  {
    std::lock_guard<std::mutex> latch(storage_latch_);
    table_.insert(key, value);
  }
  get(txn).undo_log.push_back(std::move(rec));
}

void TransactionManager::remove(TxnId txn, Key key) {
  locks_.lock_exclusive(txn, RowKey{table_id_, key});

  LogRecord rec = make_record(txn, LogType::Delete, key);
  {
    std::lock_guard<std::mutex> latch(storage_latch_);
    auto before = table_.get(key);
    if (!before) return;  // nothing to delete
    rec.has_before = true;
    rec.before     = *before;
  }
  LSN lsn = log_.append(rec);
  log_.flush_upto(lsn);

  {
    std::lock_guard<std::mutex> latch(storage_latch_);
    table_.erase(key);
  }
  get(txn).undo_log.push_back(std::move(rec));
}

void TransactionManager::commit(TxnId txn) {
  LSN lsn = log_.append(make_record(txn, LogType::Commit, 0));
  log_.flush_upto(lsn);  // force the commit record to disk before returning
  get(txn).state = TxnState::Committed;
  locks_.release_all(txn);
}

void TransactionManager::abort(TxnId txn) {
  Txn& t = get(txn);
  // Undo this transaction's writes in reverse order.
  for (auto it = t.undo_log.rbegin(); it != t.undo_log.rend(); ++it) {
    std::lock_guard<std::mutex> latch(storage_latch_);
    if (it->type == LogType::Insert) {
      if (it->has_before) table_.insert(it->key, it->before);
      else                table_.erase(it->key);
    } else if (it->type == LogType::Delete) {
      if (it->has_before) table_.insert(it->key, it->before);
    }
  }
  LSN lsn = log_.append(make_record(txn, LogType::Abort, 0));
  log_.flush_upto(lsn);
  t.state = TxnState::Aborted;
  locks_.release_all(txn);
}

}  // namespace minidb
