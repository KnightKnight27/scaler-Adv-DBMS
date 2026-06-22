#pragma once
#include <unordered_set>
#include <string>
#include <vector>
#include <stdexcept>
#include "common/types.h"
#include "common/value.h"

namespace minidb {

// Thrown when a transaction must be rolled back (e.g. a deadlock victim). The
// executor catches this, aborts the transaction, and reports the failure.
struct TransactionAbortException : std::runtime_error {
    explicit TransactionAbortException(const std::string &why)
        : std::runtime_error("transaction aborted: " + why) {}
};

enum class TxnState { GROWING, COMMITTED, ABORTED };
enum class LockMode { SHARED, EXCLUSIVE };

// An undo entry records how to physically reverse one data change during ABORT
// or recovery UNDO. We log the inverse operation:
//   - an INSERT is undone by deleting the inserted RID
//   - a DELETE is undone by re-inserting the old tuple
enum class UndoKind { UNDO_INSERT, UNDO_DELETE };
struct UndoRecord {
    UndoKind    kind;
    std::string table;
    RID         rid;       // location affected
    Tuple       old_tuple; // for UNDO_DELETE: the row to restore
};

// A transaction tracks its id, state, the lock resources it holds (for strict
// 2PL release at end), and an in-memory undo log used to roll back.
class Transaction {
public:
    explicit Transaction(txn_id_t id) : id_(id), state_(TxnState::GROWING) {}

    txn_id_t id() const { return id_; }
    TxnState state() const { return state_; }
    void set_state(TxnState s) { state_ = s; }

    std::unordered_set<std::string> &locks() { return held_locks_; }
    std::vector<UndoRecord>         &undo_log() { return undo_log_; }

    void record_undo(UndoRecord r) { undo_log_.push_back(std::move(r)); }

private:
    txn_id_t                        id_;
    TxnState                        state_;
    std::unordered_set<std::string> held_locks_; // resource ids held
    std::vector<UndoRecord>         undo_log_;    // newest at the back
};

} // namespace minidb
