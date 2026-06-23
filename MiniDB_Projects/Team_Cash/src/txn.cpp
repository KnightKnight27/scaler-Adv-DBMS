#include "txn.h"

#include <algorithm>

namespace minidb {

bool LockManager::reaches(int from, int target, std::set<int>& seen) const {
    auto it = waitsFor_.find(from);
    if (it == waitsFor_.end()) return false;
    for (int next : it->second) {
        if (next == target) return true;
        if (seen.insert(next).second && reaches(next, target, seen)) return true;
    }
    return false;
}

LockManager::Outcome LockManager::acquire(int txn, int64_t key, LockMode mode) {
    std::vector<Holder>& holders = table_[key];

    std::set<int> conflictWith;
    for (const Holder& h : holders)
        if (h.txn != txn && incompatible(h.mode, mode)) conflictWith.insert(h.txn);

    if (conflictWith.empty()) {
        bool already = false;
        for (Holder& h : holders) {
            if (h.txn == txn) {
                if (mode == LockMode::Exclusive) h.mode = LockMode::Exclusive;  // upgrade
                already = true;
            }
        }
        if (!already) holders.push_back({txn, mode});
        waitsFor_.erase(txn);
        return Granted;
    }

    // The request must wait on the conflicting holders. Record the edges, then
    // see if that closes a cycle in the waits-for graph.
    waitsFor_[txn] = conflictWith;
    std::set<int> seen;
    if (reaches(txn, txn, seen)) return Deadlock;
    return Waiting;
}

void LockManager::release(int txn) {
    for (auto& kv : table_) {
        auto& holders = kv.second;
        holders.erase(std::remove_if(holders.begin(), holders.end(),
                                     [&](const Holder& h) { return h.txn == txn; }),
                      holders.end());
    }
    waitsFor_.erase(txn);
    for (auto& kv : waitsFor_) kv.second.erase(txn);
}

std::string LockManager::waitsForGraph() const {
    std::string out;
    for (const auto& kv : waitsFor_) {
        for (int w : kv.second)
            out += "  T" + std::to_string(kv.first) + " -> T" + std::to_string(w) + "\n";
    }
    return out.empty() ? "  (no waits)\n" : out;
}

}  // namespace minidb
