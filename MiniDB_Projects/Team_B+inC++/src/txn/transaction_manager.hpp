#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "transaction.hpp"

// differ only in read(): MVCC readers don't lock, 2PL readers take a shared lock
enum class ConcurrencyMode { MVCC, TWO_PL };

using RowKey = std::string;

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid);
};

// MVCC + Strict-2PL engine over an in-memory versioned key->value store
class TransactionManager {
public:
    explicit TransactionManager(ConcurrencyMode mode = ConcurrencyMode::MVCC);

    TxID begin();
    std::optional<std::string> read(TxID xid, const RowKey& key);
    void write(TxID xid, const RowKey& key, const std::string& value);  // insert or update
    void erase(TxID xid, const RowKey& key);
    void commit(TxID xid);
    void abort(TxID xid);

    // seed a genesis row when loading a table
    void load_committed(const RowKey& key, const std::string& value);
    // all (key,value) pairs visible to xid's snapshot
    std::vector<std::pair<RowKey, std::string>> snapshot_scan(TxID xid);

    ConcurrencyMode mode() const { return mode_; }

private:
    enum class LockMode { SHARED, EXCLUSIVE };

    // xmax == 0 means not deleted
    struct RowVersion { std::string value; TxID xmin; TxID xmax; };
    struct LockRequest { TxID xid; LockMode mode; bool granted; };

    ConcurrencyMode mode_;

    // transaction table
    std::mutex                              tx_mu_;
    TxID                                    next_xid_ = 1;
    std::unordered_map<TxID, Transaction>   txns_;

    // versioned store
    std::mutex                                            heap_mu_;
    std::unordered_map<RowKey, std::list<RowVersion>>     heap_;  // newest version first

    // lock manager: one mutex + cv guard the lock table and waits-for graph
    std::mutex                                                lm_mu_;
    std::condition_variable                                   lm_cv_;
    std::unordered_map<RowKey, std::list<LockRequest>>        lock_table_;
    std::unordered_map<TxID, std::unordered_set<TxID>>        waits_for_;

    bool is_committed(TxID xid);
    bool is_visible(const RowVersion& v, TxID snapshot, TxID reader);
    void acquire_lock(TxID xid, const RowKey& key, LockMode mode);
    void release_locks(TxID xid);
    bool has_cycle(TxID start);  // lm_mu_ held
};
