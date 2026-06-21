#pragma once

#include <cstdint>

// A transaction id. 0 is reserved for "genesis" — the implicit committed
// transaction that owns rows loaded from disk, so they're visible to everyone.
using TxID = std::uint64_t;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id = 0;
    TxID     snapshot = 0;          // MVCC: sees commits with xid < snapshot (== id here)
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;  // Strict 2PL: set once the txn starts releasing locks
};
