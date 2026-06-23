#pragma once
#include <unordered_set>
#include "common/types.h"

namespace minidb {

enum class TransactionState { GROWING, SHRINKING, COMMITTED, ABORTED };

class Transaction {
public:
    explicit Transaction(int txn_id) : txn_id_(txn_id), state_(TransactionState::GROWING) {}

    int get_txn_id() const { return txn_id_; }
    TransactionState get_state() const { return state_; }
    void set_state(TransactionState state) { state_ = state; }

    void add_shared_lock(RecordId rid) { shared_locks_.insert(rid); }
    void add_exclusive_lock(RecordId rid) { exclusive_locks_.insert(rid); }
    
    void remove_shared_lock(RecordId rid) { shared_locks_.erase(rid); }
    void remove_exclusive_lock(RecordId rid) { exclusive_locks_.erase(rid); }

    const std::unordered_set<RecordId>& get_shared_locks() const { return shared_locks_; }
    const std::unordered_set<RecordId>& get_exclusive_locks() const { return exclusive_locks_; }

private:
    int txn_id_;
    TransactionState state_;
    std::unordered_set<RecordId> shared_locks_;
    std::unordered_set<RecordId> exclusive_locks_;
};

} // namespace minidb
