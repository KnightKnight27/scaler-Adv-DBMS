#pragma once
// Transaction API tying MVCC (version_store) and Strict 2PL (lock_manager)
// together. Reads are lock-free snapshot reads; writes/removes take X locks
// held until commit/abort (MV2PL).
#include "version_store.h"
#include "lock_manager.h"
#include <optional>
#include <vector>
#include <utility>

class TxnManager {
public:
    int begin();   // returns the txn id (also its snapshot timestamp)

    std::optional<long long> read(int txn, const std::string& key);   // nullopt = absent
    LockResult write (int txn, const std::string& key, long long value);
    LockResult remove(int txn, const std::string& key);               // writes a tombstone

    void commit(int txn);
    void abort (int txn);

private:
    struct Txn {
        int  id;
        Ts   begin_ts;          // snapshot, taken at begin()
        bool active = true;
        std::vector<std::pair<std::string, Version*>> writes; // versions created, in order
    };

    Txn& get(int id);
    LockResult do_write(int txn, const std::string& key, long long value, bool tombstone);

    VersionStore                 store_;
    LockManager                  locks_;
    std::unordered_map<int, Txn> txns_;
    Ts                           clock_ = 0;  // issues both begin-ts and commit-ts
};
