#ifndef TX_MANAGER_H
#define TX_MANAGER_H

#include "common.h"
#include "lock_manager.h"
#include "mvcc_heap.h"
#include "tx_registry.h"
#include <optional>
#include <string>

// Facade tying together the transaction registry, the MVCC heap and the lock
// manager. Each data operation acquires the appropriate lock (2PL) and then
// applies the MVCC mutation.
class TransactionManager {
public:
    TransactionManager() : heap_(registry_), locks_(registry_) {}

    TxID begin();

    std::optional<std::string> read(TxID xid, const RowKey& key);
    void insert(TxID xid, const RowKey& key, const std::string& value);
    void update(TxID xid, const RowKey& key, const std::string& value);
    void remove(TxID xid, const RowKey& key);

    void commit(TxID xid);
    void abort(TxID xid);

    // Exposed so callers can drive the lock manager directly (e.g. to
    // demonstrate deadlock handling).
    LockManager& locks() { return locks_; }

private:
    // Declaration order matters: registry_ must outlive the components that
    // hold a reference to it.
    TransactionRegistry registry_;
    MvccHeap            heap_;
    LockManager         locks_;
};

#endif
