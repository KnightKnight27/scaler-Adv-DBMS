#pragma once
#include "storage/storage_engine.h"
#include "txn/wal.h"

namespace minidb {

// Rebuilds a consistent table after a crash from the write-ahead log.
//   analysis: a transaction is "committed" iff its COMMIT record is present.
//   redo:     replay every committed data record forward.
//   undo:     reverse every uncommitted data record, newest first.
// Because each data operation is logical and idempotent (insert = upsert,
// delete = erase, and their inverses), recovery is correct no matter how much
// of the table actually reached disk before the crash.
class RecoveryManager {
 public:
  RecoveryManager(LogManager& log, StorageEngine& table, TableId table_id = 0)
      : log_(log), table_(table), table_id_(table_id) {}

  // Returns the number of transactions found committed (for reporting).
  int recover();

 private:
  LogManager&    log_;
  StorageEngine& table_;
  TableId        table_id_;
};

}  // namespace minidb
