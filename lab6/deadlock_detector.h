#pragma once
/**
 * Lab 6 — Deadlock Detector
 *
 * Builds a wait-for graph and detects cycles using DFS.
 * A cycle in the wait-for graph means a deadlock exists.
 */

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <iostream>
#include <algorithm>

using TxnId = uint64_t;

class DeadlockDetector {
private:
    // Wait-for graph: edges[A] = {B, C} means A is waiting for B and C
    std::unordered_map<TxnId, std::unordered_set<TxnId>> edges_;

    // DFS-based cycle detection
    bool dfs(TxnId node,
             std::unordered_set<TxnId>& visited,
             std::unordered_set<TxnId>& in_stack,
             std::vector<TxnId>& cycle) const {

        visited.insert(node);
        in_stack.insert(node);
        cycle.push_back(node);

        auto it = edges_.find(node);
        if (it != edges_.end()) {
            for (TxnId neighbor : it->second) {
                if (in_stack.count(neighbor)) {
                    // Found a cycle! Trace it back
                    cycle.push_back(neighbor);
                    return true;
                }
                if (!visited.count(neighbor)) {
                    if (dfs(neighbor, visited, in_stack, cycle)) {
                        return true;
                    }
                }
            }
        }

        in_stack.erase(node);
        cycle.pop_back();
        return false;
    }

public:
    // Add a wait-for edge: waiter is waiting for holder
    void add_edge(TxnId waiter, TxnId holder) {
        if (waiter != holder) {
            edges_[waiter].insert(holder);
        }
    }

    // Remove all edges involving a transaction (called on commit/abort)
    void remove_transaction(TxnId txn_id) {
        edges_.erase(txn_id);  // remove outgoing edges

        // Remove incoming edges
        for (auto& [from, neighbors] : edges_) {
            neighbors.erase(txn_id);
        }
    }

    // Clear a specific edge
    void remove_edge(TxnId waiter, TxnId holder) {
        auto it = edges_.find(waiter);
        if (it != edges_.end()) {
            it->second.erase(holder);
            if (it->second.empty()) {
                edges_.erase(it);
            }
        }
    }

    /**
     * Detect deadlock: returns true if a cycle exists.
     * If found, `cycle` contains the transactions forming the cycle.
     */
    bool detect(std::vector<TxnId>& cycle) const {
        std::unordered_set<TxnId> visited;
        std::unordered_set<TxnId> in_stack;

        for (const auto& [node, _] : edges_) {
            if (!visited.count(node)) {
                cycle.clear();
                if (dfs(node, visited, in_stack, cycle)) {
                    return true;
                }
            }
        }

        return false;
    }

    /**
     * Choose a victim for deadlock resolution.
     * Simple policy: choose the youngest transaction (highest txn_id)
     * This minimizes work lost since the youngest has done the least.
     */
    TxnId choose_victim(const std::vector<TxnId>& cycle) const {
        if (cycle.empty()) return 0;
        return *std::max_element(cycle.begin(), cycle.end());
    }

    // Print the wait-for graph
    void print_graph() const {
        std::cout << "\n  Wait-For Graph:" << std::endl;
        if (edges_.empty()) {
            std::cout << "    (empty)" << std::endl;
            return;
        }
        for (const auto& [from, neighbors] : edges_) {
            for (TxnId to : neighbors) {
                std::cout << "    T" << from << " → T" << to
                          << " (T" << from << " waits for T" << to << ")" << std::endl;
            }
        }
    }

    bool has_edges() const { return !edges_.empty(); }
};
