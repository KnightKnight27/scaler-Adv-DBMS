#pragma once

#include <unordered_map>
#include <vector>

using namespace std;

#include "common/Types.hpp"
#include "transaction/LockManager.hpp"

namespace minidb {

enum class TxnState { INACTIVE, ACTIVE, COMMITTED, ABORTED };

class TransactionManager {
public:
    int beginTransaction();
    bool commit(int txn_id);
    bool rollback(int txn_id);
    bool isActive(int txn_id) const;
    TxnState getState(int txn_id) const;
    LockManager& getLockManager() { return lock_manager_; }

    struct UndoRecord { string table; Row row; RowLocation location; bool is_insert = false; };
    void recordInsert(int txn_id, const string& table, const Row& row, const RowLocation& loc);
    const vector<UndoRecord>& getUndoLog(int txn_id) const;

private:
    int next_txn_id_ = 1;
    LockManager lock_manager_;
    unordered_map<int, TxnState> states_;
    unordered_map<int, vector<UndoRecord>> undo_logs_;
};

}  // namespace minidb
