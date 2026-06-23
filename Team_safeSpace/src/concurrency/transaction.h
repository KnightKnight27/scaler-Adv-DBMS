#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/config.h"
#include "common/rid.h"

namespace minidb {

// Two-phase locking states. MiniDB uses STRICT 2PL: a transaction only ever
// acquires locks (growing) and releases them all at once at commit/abort, which
// guarantees serializable, recoverable schedules.
enum class TxnState { kGrowing, kCommitted, kAborted };

// One data change a transaction made, kept so Abort can undo it. A heap delete
// only tombstones a slot (its bytes are never reclaimed), so undoing a delete is
// just restoring the slot's original byte offset.
enum class WriteKind { kInsert, kDelete };
struct WriteRecord {
  WriteKind kind;
  std::string table;
  RID rid;
  uint16_t slot_offset{0};  // original slot offset, for undoing a delete
};

// A running transaction: its id (also its age — a smaller id is older), state,
// the set of locks it holds, and an undo log of its writes.
class Transaction {
 public:
  explicit Transaction(txn_id_t id) : txn_id_(id) {}

  txn_id_t GetId() const { return txn_id_; }
  TxnState GetState() const { return state_; }
  void SetState(TxnState s) { state_ = s; }

  std::vector<WriteRecord> &Writes() { return writes_; }
  std::unordered_set<RID> &LockSet() { return lock_set_; }

 private:
  txn_id_t txn_id_;
  TxnState state_{TxnState::kGrowing};
  std::vector<WriteRecord> writes_;
  std::unordered_set<RID> lock_set_;
};

}  // namespace minidb
