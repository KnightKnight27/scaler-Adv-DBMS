#include "transaction/tx_manager.h"
#include <stdexcept>

namespace minidb {

TxManager::TxManager(LockManager* lm, WAL* wal) : lm_(lm), wal_(wal) {}

txn_id_t TxManager::Begin() {
    std::lock_guard<std::mutex> lk(latch_);
    txn_id_t tid = next_id_++;
    txns_[tid] = TxnInfo{tid, TxnState::ACTIVE};
    LogRecord rec; rec.txn_id=tid; rec.type=LogType::BEGIN;
    wal_->Append(rec);
    return tid;
}

bool TxManager::Commit(txn_id_t tid) {
    {
        std::lock_guard<std::mutex> lk(latch_);
        auto it = txns_.find(tid);
        if (it==txns_.end() || it->second.state!=TxnState::ACTIVE) return false;
        LogRecord rec; rec.txn_id=tid; rec.type=LogType::COMMIT;
        wal_->Append(rec);
        wal_->Flush(); // WAL must be durable before we say COMMITTED
        it->second.state = TxnState::COMMITTED;
    }
    lm_->UnlockAll(tid);
    return true;
}

bool TxManager::Abort(txn_id_t tid) {
    {
        std::lock_guard<std::mutex> lk(latch_);
        auto it = txns_.find(tid);
        if (it==txns_.end() || it->second.state!=TxnState::ACTIVE) return false;
        LogRecord rec; rec.txn_id=tid; rec.type=LogType::ABORT;
        wal_->Append(rec);
        wal_->Flush();
        it->second.state = TxnState::ABORTED;
    }
    lm_->UnlockAll(tid);
    return true;
}

TxnState TxManager::GetState(txn_id_t tid) const {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = txns_.find(tid);
    if (it==txns_.end()) throw std::runtime_error("Unknown txn");
    return it->second.state;
}

bool TxManager::IsActive(txn_id_t tid) const {
    std::lock_guard<std::mutex> lk(latch_);
    auto it = txns_.find(tid);
    return it!=txns_.end() && it->second.state==TxnState::ACTIVE;
}

} // namespace minidb
