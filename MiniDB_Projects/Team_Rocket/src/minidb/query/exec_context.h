#pragma once

#include <string>

#include "../catalog/catalog.h"
#include "../recovery/wal.h"
#include "../storage/buffer_pool.h"
#include "../txn/lock_manager.h"
#include "../txn/transaction.h"
#include "../types.h"

namespace minidb {

// Thrown when a lock request forces the current transaction to roll back.
struct TxnAbort {
    std::string message;
};

// The shared services an operator needs while a statement runs.
struct ExecContext {
    Catalog* cat = nullptr;
    BufferPool* bp = nullptr;
    LockManager* lm = nullptr;
    LogManager* log = nullptr;
    Transaction* txn = nullptr;

    static std::string lock_key(const std::string& table, const RID& r) {
        return table + ":" + std::to_string(r.page_id) + ":" + std::to_string(r.slot_id);
    }

    void lock_or_abort(const std::string& table, const RID& r, LockMode mode) {
        if (lm->acquire(txn->id, lock_key(table, r), mode) == LockResult::Aborted)
            throw TxnAbort{"transaction " + std::to_string(txn->id) +
                           " aborted by wound-wait to avoid deadlock"};
    }
};

}  // namespace minidb
