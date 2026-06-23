#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "minidb/transaction/transaction.h"

namespace minidb {

class LockManager {
 public:
  std::unique_ptr<Transaction> Begin();
  void LockShared(Transaction &txn, const std::string &table);
  void LockExclusive(Transaction &txn, const std::string &table);
  void Commit(Transaction &txn);
  void Abort(Transaction &txn);
  std::optional<TxnId> DetectDeadlock() const;

 private:
  struct Request {
    TxnId txn_id;
    LockMode mode;
    bool granted{false};
  };
  struct Queue {
    std::vector<Request> requests;
    std::condition_variable condition;
  };

  void Acquire(Transaction &txn, const std::string &table, LockMode mode);
  bool CanGrant(const Queue &queue, std::size_t position) const;
  void ReleaseAll(Transaction &txn);
  std::unordered_map<TxnId, std::unordered_set<TxnId>> WaitsFor() const;
  std::optional<TxnId> DetectDeadlockUnlocked() const;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, Queue> queues_;
  TxnId next_txn_id_{1};
};

}  // namespace minidb
