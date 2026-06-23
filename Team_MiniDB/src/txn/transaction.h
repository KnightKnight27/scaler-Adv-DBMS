#pragma once
#include <cstdint>
#include <unordered_set>

namespace minidb {

using txn_id_t = int64_t;

enum class TxnState { GROWING, SHRINKING, COMMITTED, ABORTED };

// One client transaction. `GROWING` means it may still acquire locks
// (Strict 2PL); it moves to `SHRINKING` only while releasing them at
// commit/abort, never in between - so no lock is ever released before
// the transaction's last acquisition, which is what makes this schedule
// serializable.
class Transaction {
 public:
  explicit Transaction(txn_id_t id) : id_(id) {}

  txn_id_t Id() const { return id_; }
  TxnState State() const { return state_; }
  void SetState(TxnState s) { state_ = s; }

  bool aborted_by_deadlock = false;

  std::unordered_set<std::string> SharedLocks() const { return shared_locks_; }
  std::unordered_set<std::string> ExclusiveLocks() const { return exclusive_locks_; }
  void RecordSharedLock(const std::string &res) { shared_locks_.insert(res); }
  void RecordExclusiveLock(const std::string &res) { exclusive_locks_.insert(res); }

 private:
  txn_id_t id_;
  TxnState state_ = TxnState::GROWING;
  std::unordered_set<std::string> shared_locks_;
  std::unordered_set<std::string> exclusive_locks_;
};

}  // namespace minidb
