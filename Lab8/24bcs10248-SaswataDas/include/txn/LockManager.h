#pragma once

#include <unordered_map>
#include <vector>

#include "Interfaces.h"

namespace txn {

// Strict 2PL lock table. Each key owns an ordered list of requests, some
// granted and some waiting. Shared locks are mutually compatible; an
// Exclusive lock conflicts with everything. A transaction holding a Shared
// lock may upgrade to Exclusive when it is the only holder.
class LockManager : public ILockManager {
public:
    bool acquire(TxnId txn, const Key& key, LockMode mode) override;
    void release(TxnId txn) override;
    std::vector<WaitsForEdge> waitsForEdges() const override;
    void dump(std::ostream& out) const override;

private:
    struct Request {
        TxnId txn;
        LockMode mode;
        bool granted;
    };
    struct LockEntry {
        std::vector<Request> requests;
    };

    // True if `mode` is compatible with all requests currently granted to
    // transactions other than `txn`.
    static bool compatibleWithGranted(const LockEntry& entry, TxnId txn, LockMode mode);

    std::unordered_map<Key, LockEntry> table_;
};

}  // namespace txn
