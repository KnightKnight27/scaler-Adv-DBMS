#include "txn/LockManager.h"

#include <algorithm>

namespace txn {

bool LockManager::compatibleWithGranted(const LockEntry& entry, TxnId txn, LockMode mode) {
    for (const Request& r : entry.requests) {
        if (!r.granted || r.txn == txn) continue;  // ignore waiters and our own holds
        // Exclusive conflicts with everything; two Shared locks coexist.
        if (mode == LockMode::Exclusive || r.mode == LockMode::Exclusive) {
            return false;
        }
    }
    return true;
}

bool LockManager::acquire(TxnId txn, const Key& key, LockMode mode) {
    LockEntry& entry = table_[key];

    // Locate this transaction's existing granted / waiting requests, if any.
    int grantedIdx = -1;
    int waitingIdx = -1;
    for (int i = 0; i < static_cast<int>(entry.requests.size()); ++i) {
        if (entry.requests[i].txn != txn) continue;
        if (entry.requests[i].granted) grantedIdx = i;
        else waitingIdx = i;
    }

    if (grantedIdx != -1) {
        Request& held = entry.requests[grantedIdx];
        // Already hold an adequate lock (X covers everything; S satisfies S).
        if (held.mode == LockMode::Exclusive || mode == LockMode::Shared) {
            return true;
        }
        // Upgrade request S -> X: only when no other transaction holds a lock.
        if (compatibleWithGranted(entry, txn, LockMode::Exclusive)) {
            held.mode = LockMode::Exclusive;
            if (waitingIdx != -1) entry.requests.erase(entry.requests.begin() + waitingIdx);
            return true;
        }
        if (waitingIdx == -1) {
            entry.requests.push_back({txn, LockMode::Exclusive, false});
        }
        return false;
    }

    if (waitingIdx != -1) {
        // Re-evaluate a previously blocked request (e.g. after a blocker left).
        if (compatibleWithGranted(entry, txn, mode)) {
            entry.requests[waitingIdx].mode = mode;
            entry.requests[waitingIdx].granted = true;
            return true;
        }
        return false;
    }

    // Brand-new request for this key.
    const bool granted = compatibleWithGranted(entry, txn, mode);
    entry.requests.push_back({txn, mode, granted});
    return granted;
}

void LockManager::release(TxnId txn) {
    for (auto& kv : table_) {
        std::vector<Request>& reqs = kv.second.requests;
        reqs.erase(std::remove_if(reqs.begin(), reqs.end(),
                                  [txn](const Request& r) { return r.txn == txn; }),
                   reqs.end());
    }
}

std::vector<WaitsForEdge> LockManager::waitsForEdges() const {
    std::vector<WaitsForEdge> edges;
    for (const auto& kv : table_) {
        const std::vector<Request>& reqs = kv.second.requests;
        for (const Request& waiter : reqs) {
            if (waiter.granted) continue;
            for (const Request& holder : reqs) {
                if (!holder.granted || holder.txn == waiter.txn) continue;
                if (waiter.mode == LockMode::Exclusive || holder.mode == LockMode::Exclusive) {
                    edges.emplace_back(waiter.txn, holder.txn);
                }
            }
        }
    }
    return edges;
}

void LockManager::dump(std::ostream& out) const {
    std::vector<Key> keys;
    keys.reserve(table_.size());
    for (const auto& kv : table_) {
        if (!kv.second.requests.empty()) keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());

    for (const Key& key : keys) {
        out << "  " << key << ": ";
        bool first = true;
        for (const Request& r : table_.at(key).requests) {
            if (!first) out << ", ";
            first = false;
            out << 'T' << r.txn << '(' << toString(r.mode) << (r.granted ? ":held" : ":wait") << ')';
        }
        out << '\n';
    }
}

}  // namespace txn
