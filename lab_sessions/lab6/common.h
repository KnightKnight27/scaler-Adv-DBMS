#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <string>

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;   
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;   
};

#endif