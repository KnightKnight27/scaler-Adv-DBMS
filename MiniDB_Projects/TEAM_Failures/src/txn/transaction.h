// ============================================================================
// transaction.h  --  Represents one running transaction.
//
// A transaction is a unit of work that is "all or nothing": either every change
// it makes becomes permanent (COMMIT) or none of them do (ABORT/ROLLBACK).
//
// This object holds the bookkeeping needed to enforce that:
//   * state_       : where we are in the 2PL lifecycle (see TxnState).
//   * locks_       : which table locks we hold, so we can release them at the
//                    end (Strict 2PL releases all locks only at commit/abort).
//   * write_set_   : an in-memory undo log.  For a runtime ROLLBACK we walk this
//                    backwards and reverse each change.  (Crash recovery uses
//                    the on-disk WAL instead; this is its in-memory cousin.)
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

enum class LockMode { kShared, kExclusive };

// 2PL lifecycle: a txn first only ACQUIRES locks (growing), then only RELEASES
// them (shrinking).  We use the *strict* variant: all releases happen together
// at COMMIT/ABORT, which also guarantees recoverability.
enum class TxnState { kGrowing, kShrinking, kCommitted, kAborted };

// One change made by the txn, kept so we can undo it on rollback.
struct WriteRecord {
  enum Kind { kInsert, kDelete } kind;
  string table;
  RID         rid;
  string tuple_bytes;   // the inserted bytes (kInsert) or deleted bytes (kDelete)
};

class Transaction {
 public:
  explicit Transaction(txn_id_t id) : txn_id_(id), state_(TxnState::kGrowing) {}

  txn_id_t id() const { return txn_id_; }
  TxnState state() const { return state_; }
  void set_state(TxnState s) { state_ = s; }

  unordered_map<string, LockMode> &locks() { return locks_; }
  vector<WriteRecord> &write_set() { return write_set_; }

  void addWrite(WriteRecord r) { write_set_.push_back(move(r)); }

 private:
  txn_id_t                                  txn_id_;
  TxnState                                  state_;
  unordered_map<string, LockMode> locks_;      // table -> mode held
  vector<WriteRecord>                  write_set_;   // for ROLLBACK
};

}  // namespace minidb
