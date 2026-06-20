#pragma once
#include "../catalog/catalog.h"
#include "../storage/disk_manager.h"
#include "log_record.h"

namespace minidb {

// ARIES-lite crash recovery, simplified by treating the B+Tree index as a
// derived structure rather than something the WAL has to redo/undo:
//
//   1. Analysis: scan the WAL once to find which transactions never reached
//      COMMIT/ABORT ("losers") and collect every txn's INSERT/DELETE ops
//      in log order.
//   2. Redo: reapply every INSERT/DELETE (winners and losers alike) onto
//      the heap pages. Each op is applied idempotently (skipped if the
//      target slot already reflects it), since pages may or may not have
//      been flushed before the crash.
//   3. Undo: walk each loser transaction's ops in reverse, undoing INSERTs
//      (delete the row) and DELETEs (restore the row) so only durably
//      committed effects remain.
//   4. Rebuild every table's primary-key B+Tree from scratch by scanning
//      its now-correct heap file - this is what makes step 2/3 not need
//      to touch index pages at all.
//
// Documented limitation: index pages allocated before the crash are
// abandoned rather than reclaimed; a production system would WAL-log
// index mutations too, or run garbage collection over the page file.
class RecoveryManager {
 public:
  RecoveryManager(DiskManager *disk_manager, BufferPool *bpm, Catalog *catalog)
      : disk_manager_(disk_manager), bpm_(bpm), catalog_(catalog) {}

  // Returns the number of operations redone (for the demo/CLI to report).
  int Recover();

  // Live (non-crash) rollback for an explicit ABORT/ROLLBACK: undoes every
  // INSERT/DELETE this transaction logged, in reverse order, then rebuilds
  // the index/row-count for every table it touched - the same "index is
  // derived, not WAL-logged" simplification used by Recover().
  void UndoTransaction(txn_id_t id);

 private:
  DiskManager *disk_manager_;
  BufferPool *bpm_;
  Catalog *catalog_;
};

}  // namespace minidb
