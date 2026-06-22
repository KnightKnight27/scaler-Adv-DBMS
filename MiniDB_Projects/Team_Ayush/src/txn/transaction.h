#pragma once
#include <unordered_map>
#include <vector>
#include "common/config.h"

namespace minidb {

enum class TxnState { ACTIVE, BLOCKED, COMMITTED, ABORTED };

// A transaction's bookkeeping. `start_ts` is the logical timestamp taken at
// BEGIN; under MVCC it doubles as the snapshot timestamp for visibility.
struct Transaction {
  TxnId    id = INVALID_TXN_ID;
  TxnState state = TxnState::ACTIVE;
  long     start_ts = 0;
  // Resources this txn has written (used to undo on abort under 2PL).
  std::vector<int64_t> write_set;
};

// Hands out transaction ids and a monotonically increasing logical clock, and
// tracks live transactions.
class TxnManager {
 public:
  Transaction* Begin() {
    Transaction t;
    t.id = next_id_++;
    t.start_ts = clock_++;
    t.state = TxnState::ACTIVE;
    auto res = txns_.emplace(t.id, t);
    return &res.first->second;
  }

  Transaction* Get(TxnId id) {
    auto it = txns_.find(id);
    return it == txns_.end() ? nullptr : &it->second;
  }

  long Tick() { return clock_++; }

 private:
  TxnId next_id_ = 1;
  long  clock_ = 1;
  std::unordered_map<TxnId, Transaction> txns_;
};

}  // namespace minidb
