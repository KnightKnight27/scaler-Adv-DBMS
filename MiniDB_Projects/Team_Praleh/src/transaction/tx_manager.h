#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// tx_manager.h  –  Transaction lifecycle manager (BEGIN / COMMIT / ABORT)
// ─────────────────────────────────────────────────────────────────────────────
#include "transaction/lock_manager.h"
#include "recovery/wal.h"
#include <unordered_map>
#include <mutex>

namespace minidb {

enum class TxnState { ACTIVE, COMMITTED, ABORTED };

struct TxnInfo {
    txn_id_t id;
    TxnState state = TxnState::ACTIVE;
};

/**
 * TxManager – central coordinator for transactions.
 *
 * A transaction is ACTIVE from BEGIN until COMMIT or ABORT.
 * On COMMIT: flush WAL then release all locks.
 * On ABORT : release all locks (undo logic skipped for demo simplicity).
 */
class TxManager {
public:
    explicit TxManager(LockManager* lm, WAL* wal);

    txn_id_t Begin();
    bool     Commit(txn_id_t tid);
    bool     Abort (txn_id_t tid);

    TxnState GetState(txn_id_t tid) const;
    bool     IsActive(txn_id_t tid) const;

private:
    LockManager*                              lm_;
    WAL*                                      wal_;
    std::unordered_map<txn_id_t, TxnInfo>     txns_;
    mutable std::mutex                        latch_;
    txn_id_t                                  next_id_{1};
};

} // namespace minidb
