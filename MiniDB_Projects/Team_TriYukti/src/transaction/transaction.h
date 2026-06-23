#pragma once
#include <unordered_set>
#include <string>
#include <cstdint>

namespace minidb {

enum class TransactionState { ACTIVE, COMMITTED, ABORTED };

typedef int32_t txn_id_t;

class Transaction {
public:
    Transaction(txn_id_t txn_id) : txn_id_(txn_id), state_(TransactionState::ACTIVE), snapshot_timestamp_(0) {}
    ~Transaction() = default;

    txn_id_t GetTransactionId() const { return txn_id_; }
    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    std::unordered_set<std::string> &GetSharedLockSet() { return shared_lock_set_; }
    std::unordered_set<std::string> &GetExclusiveLockSet() { return exclusive_lock_set_; }
    int32_t GetSnapshotTimestamp() const { return snapshot_timestamp_; }
    void SetSnapshotTimestamp(int32_t ts) { snapshot_timestamp_ = ts; }

private:
    txn_id_t txn_id_;
    TransactionState state_;

    std::unordered_set<std::string> shared_lock_set_;
    std::unordered_set<std::string> exclusive_lock_set_;
    
    int32_t snapshot_timestamp_;
};

} // namespace minidb
