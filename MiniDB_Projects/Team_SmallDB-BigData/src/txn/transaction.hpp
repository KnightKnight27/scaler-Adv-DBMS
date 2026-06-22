#pragma once

#include <cstdint>

// 0 = genesis (owns rows loaded from disk, visible to all)
using TxID = std::uint64_t;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id = 0;
    TxID     snapshot = 0;          // sees commits with xid < snapshot
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;  // 2PL: set once we start releasing locks
};
