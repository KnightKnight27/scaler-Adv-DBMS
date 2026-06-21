#pragma once

#include <vector>

#include "catalog/catalog.h"
#include "recovery/wal_manager.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// RecoveryManager -- ARIES-lite crash recovery over the logical WAL.
//
//   1. ANALYSIS -- scan the log; a transaction is "committed" iff it has a
//      Commit record.  Everything else (crashed mid-flight or explicitly
//      aborted-but-not-yet-checkpointed) is a loser.
//   2. REDO     -- replay every committed Insert/Delete forward (idempotent
//      upsert / delete-by-key), bringing the database up to the committed state
//      even for changes whose data pages never reached disk.
//   3. UNDO     -- replay every loser's Insert/Delete in reverse, removing any
//      uncommitted change that *did* reach disk.
//
// No checkpointing: recovery replays the entire log since the last clean
// shutdown.  This is a stated trade-off (correct, simpler, fine at project log
// sizes).  Operations are logical and keyed by primary key, which is what makes
// redo/undo idempotent without page-LSN bookkeeping.
// ---------------------------------------------------------------------------
class RecoveryManager {
 public:
  explicit RecoveryManager(Catalog* catalog) : catalog_(catalog) {}

  struct Stats {
    size_t redone = 0;
    size_t undone = 0;
    size_t committed_txns = 0;
    size_t loser_txns = 0;
  };

  Stats run(const std::vector<LogRecord>& records);

 private:
  Catalog* catalog_;
};

}  // namespace walterdb
