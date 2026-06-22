#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace minidb {

enum class LockMode { Shared, Exclusive };
enum class LockResult { Granted, Aborted };

// Two-phase locking with wound-wait deadlock prevention. Locks are held to the
// end of the transaction (strict 2PL), which gives serializable isolation.
//
// Wound-wait: a transaction's id doubles as its timestamp, so a smaller id is
// "older". When transaction T wants a lock held incompatibly by H:
//   * T older than H  -> T wounds (aborts) H and takes the lock,
//   * T younger than H -> T aborts itself.
// Either way no cycle of waiters can form, so deadlock is impossible.
class LockManager {
public:
    LockResult acquire(int txn, const std::string& key, LockMode mode) {
        std::vector<int> blockers;
        for (const Holder& h : table_[key]) {
            if (h.txn == txn) continue;
            if (mode == LockMode::Exclusive || h.mode == LockMode::Exclusive)
                blockers.push_back(h.txn);
        }
        if (blockers.empty()) {
            grant(txn, key, mode);
            return LockResult::Granted;
        }
        for (int b : blockers) {
            if (txn < b) {
                wound(b);  // requester is older: abort the holder
            } else {
                wounded_.insert(txn);  // requester is younger: abort self
                return LockResult::Aborted;
            }
        }
        grant(txn, key, mode);
        return LockResult::Granted;
    }

    void release_all(int txn) {
        auto it = holds_.find(txn);
        if (it == holds_.end()) return;
        for (const std::string& key : it->second) {
            auto& hs = table_[key];
            hs.erase(std::remove_if(hs.begin(), hs.end(),
                                    [&](const Holder& h) { return h.txn == txn; }),
                     hs.end());
        }
        holds_.erase(it);
    }

    bool is_wounded(int txn) const { return wounded_.count(txn) > 0; }
    void forget(int txn) { wounded_.erase(txn); }

private:
    struct Holder {
        int txn;
        LockMode mode;
    };

    std::map<std::string, std::vector<Holder>> table_;
    std::map<int, std::set<std::string>> holds_;
    std::set<int> wounded_;

    void grant(int txn, const std::string& key, LockMode mode) {
        for (Holder& h : table_[key])
            if (h.txn == txn) {
                if (mode == LockMode::Exclusive) h.mode = mode;  // upgrade
                holds_[txn].insert(key);
                return;
            }
        table_[key].push_back({txn, mode});
        holds_[txn].insert(key);
    }

    void wound(int victim) {
        release_all(victim);
        wounded_.insert(victim);
    }
};

}  // namespace minidb
