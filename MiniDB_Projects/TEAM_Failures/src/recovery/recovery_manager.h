// ============================================================================
// recovery_manager.h  --  Brings the database to a consistent state on startup
// by replaying the WAL.
//
// Algorithm (a simplified ARIES, no checkpoints):
//   1. Read every WAL record.
//   2. Decide WINNERS (transactions that have a COMMIT record) and LOSERS
//      (everything else).
//   3. REDO every INSERT/DELETE in log order.  This re-applies all changes,
//      including losers', so the heap matches what was on disk at crash time.
//      (Our heap operations are idempotent, so re-applying is safe.)
//   4. UNDO every loser's INSERT/DELETE in reverse log order, removing changes
//      made by transactions that never committed.
//   5. Rebuild the in-memory indexes from the now-consistent heaps.
//
// After this runs, committed data is preserved and uncommitted data is gone.
// ============================================================================
#pragma once

#include "common/common.h"
#include "catalog/catalog.h"
#include "recovery/log_manager.h"

namespace minidb {

struct RecoveryStats {
  int redone{0};
  int undone{0};
  int committed_txns{0};
  int aborted_txns{0};
};

class RecoveryManager {
 public:
  RecoveryManager(LogManager *lm, Catalog *catalog, BufferPool *bpm)
      : log_(lm), catalog_(catalog), bpm_(bpm) {}

  // Run redo + undo, then rebuild indexes.  Returns stats for reporting.
  RecoveryStats recover();

 private:
  LogManager *log_;
  Catalog    *catalog_;
  BufferPool *bpm_;
};

}  // namespace minidb
