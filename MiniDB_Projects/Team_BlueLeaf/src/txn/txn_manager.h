#pragma once

#include <atomic>

#include "common/types.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"

namespace minidb {

// Hands out monotonically increasing transaction ids and ends transactions.
// Under strict 2PL, commit and abort both simply release every lock the
// transaction holds (the recovery side of abort, rolling back writes, is added
// with the WAL in M5).
class TransactionManager {
public:
    explicit TransactionManager(LockManager* lm) : lm_(lm) {}

    Transaction begin() { return Transaction{next_id_.fetch_add(1), TxState::ACTIVE}; }
    void commit(Transaction& t) { lm_->release_all(t.id); t.state = TxState::COMMITTED; }
    void abort(Transaction& t)  { lm_->release_all(t.id); t.state = TxState::ABORTED; }

private:
    LockManager*          lm_;
    std::atomic<TxId>     next_id_{1};
};

} // namespace minidb
