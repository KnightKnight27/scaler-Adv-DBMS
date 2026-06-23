#pragma once
#include "types.h"
#include <unordered_map>
#include <vector>
#include <iostream>

struct LockEntry {
    LockMode mode;
    std::vector<TxnId> holders;
};

class LockManager {
public:
    bool try_acquire(TxnId txn_id, const RecordKey& key, LockMode mode);
    void release_all(TxnId txn_id);
    std::vector<TxnId> get_holders(const RecordKey& key) const;
    void print_lock_table() const;

private:
    std::unordered_map<RecordKey, LockEntry> lock_table;

    bool is_compatible(LockMode existing_mode, LockMode requested) const;
    bool already_holds_compatible(TxnId txn_id, const RecordKey& key, LockMode mode) const;
};
