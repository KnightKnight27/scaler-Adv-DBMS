#include "DeadlockDetector.h"
#include <iostream>
#include <algorithm>

void DeadlockDetector::update_wait_for(TxnId waiter, const std::vector<TxnId>& holders) {
    for (TxnId h : holders)
        if (h != waiter)
            wait_for[waiter].insert(h);
}

void DeadlockDetector::remove_txn(TxnId txn_id) {
    wait_for.erase(txn_id);
    for (auto& [node, edges] : wait_for)
        edges.erase(txn_id);
}

bool DeadlockDetector::dfs(TxnId node,
                            std::unordered_set<TxnId>& visited,
                            std::unordered_set<TxnId>& rec_stack,
                            std::vector<TxnId>& cycle_nodes) const {
    visited.insert(node);
    rec_stack.insert(node);

    auto it = wait_for.find(node);
    if (it != wait_for.end()) {
        for (TxnId neighbour : it->second) {
            if (visited.find(neighbour) == visited.end()) {
                if (dfs(neighbour, visited, rec_stack, cycle_nodes))
                    return true;
            } else if (rec_stack.find(neighbour) != rec_stack.end()) {
                // Cycle found — collect all nodes in recursion stack
                for (TxnId n : rec_stack)
                    cycle_nodes.push_back(n);
                return true;
            }
        }
    }

    rec_stack.erase(node);
    return false;
}

TxnId DeadlockDetector::detect_cycle() const {
    std::unordered_set<TxnId> visited;
    std::unordered_set<TxnId> rec_stack;
    std::vector<TxnId> cycle_nodes;

    for (const auto& [node, _] : wait_for) {
        if (visited.find(node) == visited.end()) {
            if (dfs(node, visited, rec_stack, cycle_nodes)) {
                // Return victim: highest TxnId (youngest) in cycle
                return *std::max_element(cycle_nodes.begin(), cycle_nodes.end());
            }
        }
    }
    return INVALID_TXN;
}

void DeadlockDetector::print_graph() const {
    std::cout << "  Wait-for graph:\n";
    for (const auto& [waiter, targets] : wait_for) {
        std::cout << "    T" << waiter << " -> ";
        for (TxnId t : targets) std::cout << "T" << t << " ";
        std::cout << "\n";
    }
}
