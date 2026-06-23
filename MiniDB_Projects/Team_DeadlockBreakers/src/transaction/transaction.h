#pragma once

#include "common/types.h"
#include <set>
#include <string>
#include <vector>

namespace minidb {

struct TableInfo;

enum class PendingWriteType { INSERT, DELETE };

struct PendingWrite {
    PendingWriteType type;
    TableInfo* table;
    Row row;
    RecordId record_id;
    int64_t key;
};

/*
 * Transaction: represents a single database transaction.
 * Tracks its ID, state, and the set of locks it holds.
 */
class Transaction {
public:
    explicit Transaction(txn_id_t txn_id)
        : txn_id_(txn_id), state_(TxnState::ACTIVE) {}

    txn_id_t  GetTxnId()  const { return txn_id_; }
    TxnState  GetState()  const { return state_; }
    void      SetState(TxnState s) { state_ = s; }

    // Lock tracking (for Strict 2PL — released only on commit/abort)
    void AddLock(const std::string& resource, LockMode mode) {
        held_locks_.insert(resource);
    }

    void ClearLocks() { held_locks_.clear(); }

    const std::set<std::string>& GetHeldLocks() const { return held_locks_; }

    void StageInsert(TableInfo* table, const Row& row, int64_t key);
    void StageDelete(TableInfo* table, const RecordId& record_id, int64_t key);
    bool Apply();
    void Discard() { pending_writes_.clear(); }

    bool ReservePrimaryKey(const std::string& table, int64_t key) {
        return pending_keys_.insert(table + ":" + std::to_string(key)).second;
    }

private:
    txn_id_t               txn_id_;
    TxnState               state_;
    std::set<std::string>  held_locks_;  // resource names (table names)
    std::vector<PendingWrite> pending_writes_;
    std::set<std::string> pending_keys_;
};

} // namespace minidb
