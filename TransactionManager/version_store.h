#pragma once
// MVCC version chains. Every write prepends a new Version to a per-key chain;
// nothing is ever overwritten in place. Reads walk the chain newest-first and
// pick the first version visible to their snapshot.
#include <string>
#include <unordered_map>
#include <limits>

using TxnId = int;
using Ts    = long long;

// "Infinity" timestamp: a pending version's begin_ts, and a live version's end_ts.
constexpr Ts INF = std::numeric_limits<Ts>::max();

struct Version {
    long long value     = 0;     // payload (meaningless for a tombstone)
    bool      tombstone = false; // true => this version is a DELETE
    TxnId     creator   = -1;    // txn that created this version
    Ts        begin_ts  = INF;   // creator's commit-ts; INF while pending
    Ts        end_ts    = INF;   // ts at which a newer committed version replaced it
    Version*  prev      = nullptr; // older version (chain link)
};

class VersionStore {
public:
    ~VersionStore() {
        for (auto& [key, head] : heads_) {
            Version* v = head;
            while (v) { Version* older = v->prev; delete v; v = older; }
        }
    }

    Version* head(const std::string& key) const {
        auto it = heads_.find(key);
        return it == heads_.end() ? nullptr : it->second;
    }

    // Prepend a pending version (begin_ts = INF until the creator commits).
    Version* prepend(const std::string& key, long long value, bool tombstone, TxnId creator) {
        Version* v = new Version{value, tombstone, creator, INF, INF, head(key)};
        heads_[key] = v;
        return v;
    }

    // Abort path: discard a version that must currently be the chain head.
    void pop_head(const std::string& key, Version* v) {
        heads_[key] = v->prev;
        delete v;
    }

    // Snapshot-isolation visibility: first version (newest-first) visible to `txn`.
    const Version* visible(const std::string& key, TxnId txn, Ts snapshot) const {
        for (const Version* v = head(key); v; v = v->prev) {
            bool sees_own       = (v->creator == txn);
            bool sees_committed = (v->begin_ts != INF) &&
                                  (v->begin_ts <= snapshot) &&
                                  (v->end_ts   >  snapshot);
            if (sees_own || sees_committed) return v;
        }
        return nullptr;
    }

private:
    std::unordered_map<std::string, Version*> heads_;
};
