#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <memory>

using TxnId = uint64_t;
using RowId = uint64_t;
const TxnId INVALID_TXN_ID = 0;

enum class TxnState { ACTIVE, COMMITTED, ABORTED };

struct Version {
    TxnId xmin;
    TxnId xmax;
    std::string data;
};

class Transaction {
public:
    TxnId id;
    TxnState state;
    std::unordered_set<TxnId> active_snapshot;
    std::vector<RowId> locked_rows;

    Transaction(TxnId id, const std::unordered_set<TxnId>& snapshot)
        : id(id), state(TxnState::ACTIVE), active_snapshot(snapshot) {}
};

class LockManager {
private:
    std::mutex mtx;
    std::unordered_map<RowId, TxnId> exclusive_locks;
    std::unordered_map<RowId, std::condition_variable> cvs;
    std::unordered_map<TxnId, TxnId> waits_for;

    bool DetectCycle(TxnId start_txn);

public:
    bool AcquireLock(TxnId txn_id, RowId row_id);
    void ReleaseLocks(TxnId txn_id, const std::vector<RowId>& locked_rows);
};

class Database {
private:
    std::unordered_map<RowId, std::vector<Version>> rows;
    std::mutex mtx;
    LockManager& lock_manager;
    
    bool IsVisible(const Transaction& txn, const Version& v, const std::unordered_set<TxnId>& committed_txns);

public:
    Database(LockManager& lm) : lock_manager(lm) {}
    void InsertInitialData(RowId row_id, const std::string& data, TxnId init_txn_id = 1);
    std::optional<std::string> Read(Transaction& txn, RowId row_id, const std::unordered_set<TxnId>& committed_txns);
    bool Update(Transaction& txn, RowId row_id, const std::string& data);
    void RollbackWrites(TxnId txn_id, const std::vector<RowId>& locked_rows);
};

class TransactionManager {
private:
    std::atomic<TxnId> next_txn_id{1};
    std::mutex mtx;
    std::unordered_set<TxnId> active_txns;
    std::unordered_set<TxnId> committed_txns;
    
    Database& db;
    LockManager& lock_manager;

public:
    TransactionManager(Database& db, LockManager& lm) : next_txn_id(2), db(db), lock_manager(lm) {
        committed_txns.insert(1);
    }

    std::unique_ptr<Transaction> Begin();
    void Commit(Transaction& txn);
    void Abort(Transaction& txn);
    
    std::unordered_set<TxnId> GetCommittedTxns() {
        std::lock_guard<std::mutex> lock(mtx);
        return committed_txns;
    }
};
