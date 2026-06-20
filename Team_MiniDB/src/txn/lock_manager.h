#pragma once
#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "transaction.h"

namespace minidb {

// Thrown into a waiting thread when the deadlock detector picks its
// transaction as the victim. Callers must catch this, roll the
// transaction back (TransactionManager::Abort), and surface the failure.
class TransactionAbortedException : public std::runtime_error {
 public:
  explicit TransactionAbortedException(txn_id_t id)
      : std::runtime_error("transaction " + std::to_string(id) + " aborted (deadlock victim)") {}
};

// Table-granularity Strict Two-Phase Locking: shared/exclusive locks per
// resource string (a table name, or "table:pid:slot" for row-level
// locking on point operations), held until commit/abort. A background
// thread periodically builds a wait-for graph from blocked requests and
// aborts the youngest transaction in any cycle it finds.
class LockManager {
 public:
  LockManager();
  ~LockManager();

  void LockShared(Transaction *txn, const std::string &resource);
  void LockExclusive(Transaction *txn, const std::string &resource);
  void ReleaseAll(Transaction *txn);

 private:
  struct LockRequest {
    txn_id_t txn_id;
    bool exclusive;
    bool granted = false;
  };
  struct LockQueue {
    std::mutex mtx;
    std::condition_variable cv;
    std::list<LockRequest> requests;
  };

  std::shared_ptr<LockQueue> GetQueue(const std::string &resource);
  bool IsCompatible(LockQueue &q, const std::list<LockRequest>::iterator &me);
  void DeadlockDetectionLoop();
  bool IsAborted(txn_id_t id);

  std::mutex table_mtx_;
  std::unordered_map<std::string, std::shared_ptr<LockQueue>> table_;

  std::mutex aborted_mtx_;
  std::unordered_set<txn_id_t> aborted_txns_;

  std::atomic<bool> stop_{false};
  std::thread detector_thread_;
};

}  // namespace minidb
