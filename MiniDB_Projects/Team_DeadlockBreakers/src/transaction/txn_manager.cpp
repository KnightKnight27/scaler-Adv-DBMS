#include "transaction/txn_manager.h"
#include "recovery/wal.h"
#include <algorithm>
#include <iostream>

namespace minidb {

TransactionManager::TransactionManager(LockManager* lock_mgr, WALManager* wal_mgr)
    : lock_mgr_(lock_mgr), wal_mgr_(wal_mgr) {
    if (wal_mgr_) {
        txn_id_t next = 1;
        for (const auto& record : wal_mgr_->ReadLog()) {
            next = std::max(next, static_cast<txn_id_t>(record.txn_id + 1));
        }
        next_txn_id_ = next;
    }
}

Transaction* TransactionManager::Begin() {
    std::lock_guard<std::mutex> lock(mutex_);

    txn_id_t id = next_txn_id_++;
    auto txn = std::make_unique<Transaction>(id);
    Transaction* ptr = txn.get();
    active_txns_[id] = std::move(txn);

    // Log BEGIN
    if (wal_mgr_) {
        wal_mgr_->AppendLog(id, LogType::BEGIN);
    }

    std::cout << "  [TXN] BEGIN Transaction " << id << std::endl;
    return ptr;
}

bool TransactionManager::Commit(Transaction* txn) {
    if (!txn || txn->GetState() != TxnState::ACTIVE) return false;

    // Log COMMIT (must be written BEFORE releasing locks — WAL protocol)
    if (wal_mgr_) {
        wal_mgr_->AppendLog(txn->GetTxnId(), LogType::COMMIT);
        wal_mgr_->Flush();
    }

    // Deferred writes implement NO-STEAL. Apply and force pages only after
    // the commit record is durable; recovery can safely REDO after a crash.
    bool applied = txn->Apply();

    // Release all locks (Strict 2PL: only at commit)
    lock_mgr_->UnlockAll(txn);
    txn->SetState(TxnState::COMMITTED);

    std::cout << "  [TXN] COMMIT Transaction " << txn->GetTxnId() << std::endl;

    // Remove from active set
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_txns_.erase(txn->GetTxnId());
    }
    return applied;
}

bool TransactionManager::Abort(Transaction* txn) {
    if (!txn || txn->GetState() != TxnState::ACTIVE) return false;

    // Log ABORT
    if (wal_mgr_) {
        wal_mgr_->AppendLog(txn->GetTxnId(), LogType::ABORT);
        wal_mgr_->Flush();
    }

    txn->Discard();

    // Release all locks
    lock_mgr_->UnlockAll(txn);
    txn->SetState(TxnState::ABORTED);

    std::cout << "  [TXN] ABORT Transaction " << txn->GetTxnId() << std::endl;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_txns_.erase(txn->GetTxnId());
    }
    return true;
}

Transaction* TransactionManager::GetTransaction(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_txns_.find(txn_id);
    return (it != active_txns_.end()) ? it->second.get() : nullptr;
}

} // namespace minidb
