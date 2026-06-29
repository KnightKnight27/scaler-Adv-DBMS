#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Interfaces.h"

namespace txn {

// Multi-Version store. Each key maps to the head (newest) node of a version
// chain; every node links to the older version it superseded. A version is
// visible to a snapshot timestamp `ts` once it is committed with
// beginTs <= ts < endTs.
class VersionStore : public IVersionStore {
public:
    std::optional<Value> readSnapshot(const Key& key, const Transaction& txn) const override;
    std::optional<Value> readCurrent(const Key& key, const Transaction& txn) const override;
    void put(const Key& key, Value value, TxnId txn) override;
    void commit(TxnId txn, Timestamp commitTs) override;
    void abort(TxnId txn) override;
    void dump(std::ostream& out) const override;

private:
    struct Version {
        Value value;
        TxnId creator;
        Timestamp beginTs;  // kUncommitted until the creator commits
        Timestamp endTs;    // kInfinity until a newer version commits over it
        bool committed;
        std::unique_ptr<Version> older;
    };

    std::unordered_map<Key, std::unique_ptr<Version>> heads_;
    std::unordered_map<TxnId, std::vector<Key>> writeSet_;  // keys each txn has pending versions for
};

}  // namespace txn
