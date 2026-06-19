#pragma once
#include <cstdint>
#include <string>

using TxID = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID id;
    TxID snapshot_xid; // Transaction start snapshot
    TxStatus status = TxStatus::ACTIVE;
    bool in_shrinking = false; // Flag to enforce Two-Phase Locking
};

struct RowVersion {
    std::string value;
    TxID xmin; // Transaction that created this version
    TxID xmax; // Transaction that invalidated/deleted this version (0 if active)
};
