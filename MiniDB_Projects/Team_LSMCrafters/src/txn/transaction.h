#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include "common/types.h"
#include "txn/wal.h"

namespace minidb {

enum class TxnState { Active, Committed, Aborted };

// The lock granule: one row, identified by its table and primary key.
struct RowKey {
  TableId table;
  Key     key;
  bool operator==(const RowKey& o) const { return table == o.table && key == o.key; }
};

struct RowKeyHash {
  std::size_t operator()(const RowKey& k) const {
    return std::hash<uint64_t>()((static_cast<uint64_t>(k.table) << 48) ^
                                 static_cast<uint64_t>(k.key));
  }
};

// In-memory state of one transaction.
struct Txn {
  TxnId    id;
  TxnState state        = TxnState::Active;
  bool     in_shrinking = false;  // strict-2PL: true once it starts releasing locks
  std::unordered_set<RowKey, RowKeyHash> held_locks;
  std::vector<LogRecord> undo_log;  // before-images for rollback (newest last)
};

// Thrown when granting a lock would create a deadlock; the requester aborts.
struct TxnAbortException : std::runtime_error {
  TxnId victim;
  explicit TxnAbortException(TxnId v)
      : std::runtime_error("transaction aborted to break a deadlock"), victim(v) {}
};

}  // namespace minidb
