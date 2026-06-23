// MiniDB - per-statement execution context. Carries the current transaction and the lock
// manager (and, in the recovery build, the WAL + MVCC store) into the operators so reads take
// shared locks and writes take exclusive locks under Strict 2PL. A null txn means autocommit
// with no locking (the simple path used by demos and the optimizer benchmark).
#pragma once

#include <string>

#include "../txn/lock_manager.h"
#include "../txn/transaction.h"

namespace minidb {

class VersionStore;  // MVCC store (M5)
class LogManager;    // WAL (M5)

struct ExecutionContext {
    Transaction* txn = nullptr;
    LockManager* lock_mgr = nullptr;
    TransactionManager* txn_mgr = nullptr;
    VersionStore* vstore = nullptr;  // set in the MVCC build
    LogManager* log = nullptr;       // set in the recovery build
};

// Thrown when a lock request would deadlock. Database catches it and aborts the transaction.
struct TxnAborted {
    std::string reason;
};

}  // namespace minidb
