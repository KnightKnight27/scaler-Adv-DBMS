#pragma once
// Single-threaded logical transactions over the WAL. begin/commit/rollback plus
// per-write hooks that append WAL records and remember undo information. commit
// flushes the WAL (NO-FORCE); rollback reverse-applies the in-memory undo list;
// recover() replays the log at startup (ARIES-lite analysis/redo/undo).
//
// Concurrency control: a table-level exclusive latch is acquired on first write
// to a table and all latches are released at commit/rollback (strict 2PL). With
// a single connection there is no contention, but the mechanism is real.
#include "minidb/lock_manager.hpp"
#include "minidb/storage/storage_engine.hpp"
#include "minidb/storage/write_ahead_log.hpp"
#include "minidb/types.hpp"
#include <cstdint>
#include <set>
#include <vector>

namespace minidb {

class TransactionManager {
 public:
  TransactionManager(LockManager& lock_manager, WriteAheadLog& wal);

  TxnId begin();
  void commit();
  void rollback(StorageEngine& engine);
  bool active() const { return active_; }
  TxnId current() const { return txn_; }

  // Record a write that has just been applied to the engine.
  void on_insert(TableId table, const RID& rid, const std::vector<uint8_t>& after);
  void on_delete(TableId table, const RID& rid, const std::vector<uint8_t>& before);

  // Startup recovery: redo committed work, undo in-flight work, truncate the log.
  void recover(StorageEngine& engine);

 private:
  struct UndoEntry {
    WalType type;                 // Insert (undo = delete) or Delete (undo = reinsert)
    TableId table;
    RID rid;
    std::vector<uint8_t> image;   // before-image for Delete undo
  };

  void lock_table(TableId table);
  void release_locks();

  LockManager& lock_manager_;
  WriteAheadLog& wal_;
  bool active_ = false;
  TxnId txn_ = 0;
  TxnId next_txn_ = 1;
  std::vector<UndoEntry> undo_;
  std::set<TableId> locked_;
};

}  // namespace minidb
