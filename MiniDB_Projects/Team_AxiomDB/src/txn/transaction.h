#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/config.h"
#include "common/rid.h"

namespace axiomdb {

enum class LockMode { Shared, Exclusive };

enum class TxnState {
  Growing,    // 2PL phase 1: may acquire locks
  Shrinking,  // 2PL phase 2: committing/aborting, releasing locks
  Committed,
  Aborted,
};

// A logical undo record kept in memory for the duration of a transaction, used
// to roll back on ABORT (and mirrored in the WAL so recovery can undo a
// transaction that never committed).  Undo is logical and keyed by primary key:
//   * to undo an INSERT -> delete the row by its primary key
//   * to undo a DELETE  -> re-insert the saved row image
struct UndoAction {
  enum Op { Insert, Delete } op;
  uint32_t table_id;
  std::string row_image;  // encoded tuple (the inserted row, or the deleted row)
};

// Encode a row identity into the lock manager's opaque resource id space.  We
// lock at row (RID) granularity; the high bit distinguishes whole-table locks.
inline uint64_t lock_id_for_rid(RID r) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(r.page_id)) << 16) | r.slot;
}
inline uint64_t lock_id_for_table(uint32_t table_id) {
  return (uint64_t{1} << 63) | table_id;
}

// ---------------------------------------------------------------------------
// Transaction -- the per-transaction control block.  Holds the 2PL state, the
// set of locks currently held (released all-at-once on commit/abort, i.e.
// STRICT 2PL), and the in-memory undo log for rollback.
// ---------------------------------------------------------------------------
class Transaction {
 public:
  explicit Transaction(txn_id_t id) : id_(id) {}

  txn_id_t id() const { return id_; }
  TxnState state() const { return state_; }
  void set_state(TxnState s) { state_ = s; }
  bool aborted() const { return state_ == TxnState::Aborted; }

  std::unordered_set<uint64_t>& locks() { return locks_; }
  std::vector<UndoAction>& undo_log() { return undo_; }

  void record_insert(uint32_t table_id, std::string row) {
    undo_.push_back({UndoAction::Insert, table_id, std::move(row)});
  }
  void record_delete(uint32_t table_id, std::string row) {
    undo_.push_back({UndoAction::Delete, table_id, std::move(row)});
  }

 private:
  txn_id_t id_;
  TxnState state_ = TxnState::Growing;
  std::unordered_set<uint64_t> locks_;
  std::vector<UndoAction> undo_;
};

}  // namespace axiomdb
