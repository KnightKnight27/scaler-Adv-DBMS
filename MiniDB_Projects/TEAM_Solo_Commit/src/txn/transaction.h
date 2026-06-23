// MiniDB - Transaction and TransactionManager.
//
// A Transaction carries an id, a start timestamp (its MVCC snapshot point), a commit timestamp,
// a 2PL phase (GROWING while taking locks, SHRINKING after the first release), and a list of
// versions it created so an abort can roll them back. The manager hands out monotonically
// increasing ids/timestamps and drives commit/abort (release locks, stamp or discard versions).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../common/rid.h"
#include "lock_manager.h"

namespace minidb {

enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };

// A version this transaction created, remembered so abort can undo it.
struct UndoSlot {
    std::string table;
    RID rid;
};

class Transaction {
public:
    Transaction(int id, int64_t start_ts) : id_(id), start_ts_(start_ts) {}

    int id() const { return id_; }
    int64_t start_ts() const { return start_ts_; }
    int64_t commit_ts() const { return commit_ts_; }
    TxnState state() const { return state_; }

    void SetState(TxnState s) { state_ = s; }
    void SetCommitTs(int64_t t) { commit_ts_ = t; }

    void RecordWrite(const std::string& table, RID rid) { undo_.push_back({table, rid}); }
    const std::vector<UndoSlot>& undo() const { return undo_; }

private:
    int id_;
    int64_t start_ts_;
    int64_t commit_ts_ = 0;
    TxnState state_ = TxnState::GROWING;
    std::vector<UndoSlot> undo_;
};

class VersionStore;  // MVCC store (M5); abort rolls back through it

class TransactionManager {
public:
    explicit TransactionManager(LockManager* lm) : lm_(lm) {}

    Transaction* Begin();
    void Commit(Transaction* t);
    void Abort(Transaction* t);

    int64_t Now() { return clock_.fetch_add(1); }
    Transaction* Get(int id);

    // Optional MVCC hook: when set, commit stamps and abort discards versions.
    void SetVersionStore(VersionStore* vs) { vstore_ = vs; }

private:
    LockManager* lm_;
    VersionStore* vstore_ = nullptr;
    std::atomic<int> next_id_{1};
    std::atomic<int64_t> clock_{1};
    std::mutex mtx_;
    std::unordered_map<int, std::unique_ptr<Transaction>> txns_;  // kept alive for the session
};

}  // namespace minidb
