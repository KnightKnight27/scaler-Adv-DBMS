#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <string>

using TxID   = uint64_t;
using RowKey = std::string;

// A transaction id of 0 is reserved as a sentinel ("no transaction"),
// e.g. an xmax of 0 means the row version has not been deleted.
constexpr TxID kInvalidTxID = 0;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;          // highest xid considered "in the past" for visibility
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;  // true once the tx has released a lock (2PL shrinking phase)
};

#endif
