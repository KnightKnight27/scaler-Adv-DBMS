#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// primary.h  –  Primary node interface (Track D: Replication)
// ─────────────────────────────────────────────────────────────────────────────
#include "common/types.h"
#include "index/bplus_tree.h"
#include "query/executor.h"
#include "query/optimizer.h"
#include "query/parser.h"
#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "transaction/lock_manager.h"
#include "transaction/tx_manager.h"

namespace minidb {

/**
 * Primary – owns the primary storage and coordinates WAL writes.
 *
 * Every INSERT/DELETE goes through:
 *   1. Lock (exclusive)
 *   2. WAL append (INSERT/DELETE record)
 *   3. Apply to heap + index
 *   4. WAL append (COMMIT) + flush
 *   5. Lock release
 *
 * The WAL file is shared with the Replica node (log-shipping).
 */
class Primary {
public:
  Primary(HeapFile *heap, BPlusTree *tree, WAL *wal, LockManager *lm,
          TxManager *txm);

  ExecResult Execute(const Statement &stmt, txn_id_t active_tid = -1);
  ExecResult Execute(const std::string &sql);

private:
  HeapFile *heap_;
  BPlusTree *tree_;
  WAL *wal_;
  LockManager *lm_;
  TxManager *txm_;
  Optimizer opt_;
  Executor exec_;
};

} // namespace minidb
