#pragma once
#include <unordered_set>
#include "storage/record_id.h"

namespace minidb {

enum class TransactionState { ACTIVE, COMMITTED, ABORTED };

struct RecordIdHash {
    std::size_t operator()(const RecordId& r) const {
        return std::hash<int32_t>()(r.page_id_) ^ (std::hash<int>()(r.slot_num_) << 1);
    }
};

class Transaction {
public:
    explicit Transaction(int txn_id) : txn_id_(txn_id), state_(TransactionState::ACTIVE) {}

    int GetTransactionId() const { return txn_id_; }
    TransactionState GetState() const { return state_; }
    void SetState(TransactionState state) { state_ = state; }

    std::unordered_set<RecordId, RecordIdHash>& GetSharedLockSet() { return shared_lock_set_; }
    std::unordered_set<RecordId, RecordIdHash>& GetExclusiveLockSet() { return exclusive_lock_set_; }

private:
    int txn_id_;
    TransactionState state_;
    
    // Sets of RecordIds locked by this transaction
    std::unordered_set<RecordId, RecordIdHash> shared_lock_set_;
    std::unordered_set<RecordId, RecordIdHash> exclusive_lock_set_;
};

} // namespace minidb
