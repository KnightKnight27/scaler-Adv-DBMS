#pragma once
#include <cstdint>
#include <stdexcept>
#include "../common/types.hpp"

namespace minidb {

// Thrown when a transaction's lock request would close a cycle in the
// waits-for graph; the requesting transaction is chosen as the deadlock victim.
struct DeadlockAbort : std::runtime_error {
    explicit DeadlockAbort(TxnId id)
        : std::runtime_error("transaction " + std::to_string(id) + " aborted (deadlock)"),
          txid(id) {}
    TxnId txid;
};

// Thrown by commit() in MVCC mode under first-committer-wins: a key this
// transaction wrote was committed by someone else after our snapshot, so
// committing would silently lose that update. The transaction is aborted and
// the caller should retry. (This is what makes MVCC prevent lost updates.)
struct TxnConflict : std::runtime_error {
    explicit TxnConflict(TxnId id)
        : std::runtime_error("transaction " + std::to_string(id) + " aborted (write conflict)"),
          txid(id) {}
    TxnId txid;
};

// A live transaction handle. `snapshot` is the commit timestamp the transaction
// observed when it began; under MVCC, reads see only versions committed at or
// before this value.
struct Txn {
    TxnId id = INVALID_TXN;
    int64_t snapshot = 0;
};

} // namespace minidb
