#pragma once
#include <mutex>
#include <optional>
#include <unordered_map>
#include "storage/storage_engine.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "txn/wal.h"

namespace minidb {

// Ties locking and logging to a single table's storage engine. Every data
// access follows the same invariant:
//   acquire lock  ->  append + flush WAL record  ->  mutate the storage engine
// Locks are released only at commit/abort (strict 2PL); the WAL is forced on
// every write and at commit, so the log is always ahead of the data on disk.
//
// For clarity the demos use one table (table_id 0); a multi-table system would
// key everything by table id, which the records and locks already carry.
class TransactionManager {
 public:
  TransactionManager(LockManager& locks, LogManager& log, StorageEngine& table,
                     TableId table_id = 0);

  TxnId begin();
  void  commit(TxnId txn);
  void  abort(TxnId txn);

  std::optional<Bytes> read(TxnId txn, Key key);             // shared lock
  void                 write(TxnId txn, Key key, const Bytes& value);  // exclusive lock, upsert
  void                 remove(TxnId txn, Key key);            // exclusive lock, delete

  Txn& get(TxnId txn);

 private:
  LogRecord make_record(TxnId txn, LogType type, Key key);

  LockManager&   locks_;
  LogManager&    log_;
  StorageEngine& table_;
  TableId        table_id_;

  std::mutex                     txns_mu_;       // guards the txns_ map
  std::unordered_map<TxnId, Txn> txns_;
  TxnId                          next_id_ = 1;
  std::mutex                     storage_latch_;  // serializes access to the engine
};

}  // namespace minidb
