#pragma once
#include "types.h"
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>

class DeadlockDetector {
public:
    void update_wait_for(TxnId waiter, const std::vector<TxnId>& holders);
    void remove_txn(TxnId txn_id);
    TxnId detect_cycle() const;
    void print_graph() const;

private:
    std::unordered_map<TxnId, std::set<TxnId>> wait_for;

    bool dfs(TxnId node,
             std::unordered_set<TxnId>& visited,
             std::unordered_set<TxnId>& rec_stack,
             std::vector<TxnId>& cycle_nodes) const;
};
