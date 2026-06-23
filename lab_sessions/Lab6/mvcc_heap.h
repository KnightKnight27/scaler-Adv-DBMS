#ifndef MVCC_HEAP_H
#define MVCC_HEAP_H

#include "common.h"
#include "tx_registry.h"
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

struct RowVersion {
    std::string value;
    TxID        xmin;   // transaction that created this version
    TxID        xmax;   // transaction that deleted/superseded it (0 = live)
};

// A multi-version heap: each key maps to a chain of row versions, newest first.
// Visibility is resolved against the reading transaction's snapshot.
class MvccHeap {
public:
    explicit MvccHeap(TransactionRegistry& registry) : registry_(registry) {}

    std::optional<std::string> read(const RowKey& key, TxID xid);
    void insert(const RowKey& key, const std::string& value, TxID xid);
    void update(const RowKey& key, const std::string& new_value, TxID xid);
    void remove(const RowKey& key, TxID xid);

    // Undoes the effects of an aborted transaction on every version chain.
    void rollback(TxID xid);

private:
    bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) const;

    TransactionRegistry&                                    registry_;
    std::mutex                                              mu_;
    std::unordered_map<RowKey, std::list<RowVersion>>       heap_;
};

#endif
