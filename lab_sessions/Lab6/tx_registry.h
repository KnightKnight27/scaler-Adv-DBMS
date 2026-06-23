#ifndef TX_REGISTRY_H
#define TX_REGISTRY_H

#include "common.h"
#include <mutex>
#include <unordered_map>

// Owns the set of live transactions and their status. Both the MVCC heap and
// the lock manager consult it, so it is the lowest-level shared component and
// must not depend on either of them.
class TransactionRegistry {
public:
    // Starts a new transaction and returns its id. The snapshot xid is the id
    // itself, so the transaction sees everything committed before it began.
    TxID begin();

    TxID snapshot_xid(TxID xid) const;

    bool is_committed(TxID xid) const;
    bool is_aborted(TxID xid) const;
    bool in_shrinking(TxID xid) const;

    void set_status(TxID xid, TxStatus status);
    void enter_shrinking(TxID xid);

private:
    mutable std::mutex                    mu_;
    std::unordered_map<TxID, Transaction> txns_;
    TxID                                  next_xid_ = 1;
};

#endif
