#pragma once

#include <cstddef>

#include "engine/storage_engine.h"
#include "recovery/log_manager.h"

namespace minidb {

// Crash recovery: replay the committed portion of the WAL onto the engine.
//
// 1. Analysis: scan the log and collect the set of committed transactions
//    (those with a COMMIT record).
// 2. Redo: re-apply every PUT/ERASE belonging to a committed transaction, in log
//    order. Operations from transactions without a COMMIT (the "losers") are
//    skipped, which rolls them back. Replays are idempotent (put on an existing
//    key / erase of a missing key are no-ops), so recovering twice is safe.
//
// Returns the number of operations replayed.
class RecoveryManager {
public:
    static std::size_t recover(StorageEngine* engine, const LogManager& log);
};

} // namespace minidb
