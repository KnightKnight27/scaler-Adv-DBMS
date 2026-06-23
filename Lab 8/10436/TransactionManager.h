#pragma once
#include "types.h"
#include "MVCCStore.h"
#include "LockManager.h"
#include "DeadlockDetector.h"
#include <unordered_map>
#include <string>
#include <iostream>

struct TxnContext {
    TxnId     id;
    Timestamp start_ts;
    TxnState  state;
};

class TransactionManager {
public:
    TxnId begin();
    std::string read(TxnId txn_id, const RecordKey& key);
    std::string write(TxnId txn_id, const RecordKey& key, const std::string& value);
    void commit(TxnId txn_id);
    void abort(TxnId txn_id);
    void print_status() const;

private:
    MVCCStore        mvcc;
    LockManager      lock_mgr;
    DeadlockDetector deadlock_det;

    std::unordered_map<TxnId, TxnContext> txns;
    TxnId     next_id = 1;
    Timestamp clock   = 1;

    Timestamp now() { return clock++; }
    bool is_active(TxnId id) const;
};
