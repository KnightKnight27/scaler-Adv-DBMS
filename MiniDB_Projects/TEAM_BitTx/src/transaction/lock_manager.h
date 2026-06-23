#pragma once

#include "transaction/txn.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace minidb {

using namespace std;

enum class LockMode { SHARED, EXCLUSIVE };

class LockManager {
public:
  LockManager() = default;

  bool LockShared(TxnId txn, int32_t rid);
  bool LockExclusive(TxnId txn, int32_t rid);
  bool Unlock(TxnId txn, int32_t rid);
  bool UnlockAll(TxnId txn);
  bool HasCycle(TxnId txn) const;

private:
  struct LockEntry {
    unordered_set<TxnId> shared;
    TxnId exclusive = INVALID_TXN_ID;
    unordered_set<TxnId> waiters;
  };

  struct WaitForEntry {
    TxnId holder = INVALID_TXN_ID;
    LockMode mode;
  };

  bool DetectCycleFrom(TxnId start, unordered_set<TxnId>& visited,
                       unordered_set<TxnId>& stack) const;
  vector<WaitForEntry> BuildWaitFor(TxnId txn, int32_t rid, LockMode mode) const;

  mutex mu_;
  unordered_map<int32_t, LockEntry> locks_;
  unordered_map<TxnId, unordered_set<int32_t>> held_;
  unordered_map<TxnId, unordered_map<int32_t, WaitForEntry>> waiting_;
};

} // namespace minidb