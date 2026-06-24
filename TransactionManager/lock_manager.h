#pragma once
// Strict 2PL lock manager: full S/X compatibility matrix with S->X upgrade,
// a wait-for graph, and DFS cycle detection. All locks are released together
// at end-of-transaction (release_all).
#include <string>
#include <vector>
#include <unordered_map>
#include <set>

enum class LockMode   { S, X };
enum class LockResult { Granted, Blocked, Deadlock };

class LockManager {
public:
    // Try to acquire `mode` on `key` for `txn`. On Deadlock, `txn` is the victim
    // (it closed the cycle); the caller is expected to abort it.
    LockResult acquire(int txn, const std::string& key, LockMode mode);

    // Release every lock held by `txn` and erase it from the wait-for graph.
    void release_all(int txn);

private:
    struct Entry { int txn; LockMode mode; };

    std::unordered_map<std::string, std::vector<Entry>> locks_;     // key -> holders
    std::unordered_map<int, std::set<int>>              wait_for_;  // txn -> txns it waits on

    bool reaches(int node, int target, std::set<int>& seen) const;
    bool has_cycle(int start);
};
