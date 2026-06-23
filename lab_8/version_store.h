// version_store.h
// -----------------------------------------------------------------------------
// MVCC (Multi-Version Concurrency Control) version chains.
//
// Each key in the store owns a singly-linked chain of versions, NEWEST first.
// A write never overwrites in place; it prepends a brand new version that points
// back at the prior (older) version. A delete prepends a tombstone version.
//
// A version is identified by who created it and when it became visible:
//   begin_ts : commit timestamp of the creating txn (INF while uncommitted),
//              i.e. the point in time from which the version is "live".
//   end_ts   : timestamp at which this version was superseded by a newer
//              committed version (INF while it is still the live version).
//   creator  : id of the txn that created the version (used so a txn can read
//              its own not-yet-committed writes).
//   tombstone: true if this version represents a DELETE.
//
// Visibility rule (snapshot isolation) is implemented in TxnManager which owns
// the snapshot; VersionStore only stores and walks the chains.
// -----------------------------------------------------------------------------
#ifndef LAB8_VERSION_STORE_H
#define LAB8_VERSION_STORE_H

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

namespace lab8 {

using Ts    = std::uint64_t;   // timestamp / txn-id space (monotonic counter)
using TxnId = std::uint64_t;

// "infinity" sentinel for begin_ts (uncommitted) and end_ts (still live).
inline constexpr Ts INF = std::numeric_limits<Ts>::max();

using Value = long long;

// One node of a version chain.
struct Version {
    Value     value{};                 // payload (ignored when tombstone)
    bool      tombstone   = false;      // true => this version is a DELETE
    TxnId     creator     = 0;          // txn that created this version
    Ts        begin_ts    = INF;        // commit-ts of creator (INF if pending)
    Ts        end_ts      = INF;        // when superseded (INF if still live)
    std::shared_ptr<Version> prev;      // older version (chain is newest-first)
};

using VersionPtr = std::shared_ptr<Version>;

// Stores, per key, the head (newest) version of its chain.
class VersionStore {
public:
    // Head (newest) version for a key, or nullptr if the key was never touched.
    VersionPtr head(const std::string& key) const {
        auto it = heads_.find(key);
        return it == heads_.end() ? nullptr : it->second;
    }

    // Prepend a new version to key's chain and return it. Caller fills in
    // begin_ts/end_ts semantics via the returned node + commit logic.
    VersionPtr prepend(const std::string& key, Value v, bool tombstone,
                       TxnId creator) {
        auto node       = std::make_shared<Version>();
        node->value     = v;
        node->tombstone = tombstone;
        node->creator   = creator;
        node->begin_ts  = INF;          // uncommitted until commit assigns ts
        node->end_ts    = INF;
        node->prev      = head(key);    // link to prior (older) version
        heads_[key]     = node;
        return node;
    }

    // Remove the head version of a key (used to roll back an aborted write).
    // Restores the previous version as the head.
    void pop_head(const std::string& key) {
        auto it = heads_.find(key);
        if (it == heads_.end()) return;
        if (it->second && it->second->prev)
            it->second = it->second->prev;
        else
            heads_.erase(it);
    }

    const std::unordered_map<std::string, VersionPtr>& heads() const {
        return heads_;
    }

private:
    std::unordered_map<std::string, VersionPtr> heads_;
};

}  // namespace lab8

#endif  // LAB8_VERSION_STORE_H
