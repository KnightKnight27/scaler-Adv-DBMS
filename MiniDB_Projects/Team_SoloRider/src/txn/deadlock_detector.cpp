#include "txn/deadlock_detector.h"
#include <algorithm>

namespace minidb {

DeadlockDetector::DeadlockDetector(LockManager* lock_manager) : lock_manager_(lock_manager) {}

bool DeadlockDetector::dfs(int node, std::unordered_map<int, std::vector<int>>& graph,
                           std::unordered_map<int, int>& status, int& cycle_node) {
    status[node] = 1; // visiting
    for (int neighbor : graph[node]) {
        if (status[neighbor] == 0) {
            if (dfs(neighbor, graph, status, cycle_node)) {
                cycle_node = std::max(cycle_node, node);
                return true;
            }
        } else if (status[neighbor] == 1) {
            cycle_node = std::max(node, neighbor);
            return true;
        }
    }
    status[node] = 2; // visited
    return false;
}

int DeadlockDetector::detect_deadlock() {
    auto edges = lock_manager_->get_wait_for_graph();
    std::unordered_map<int, std::vector<int>> graph;
    std::unordered_map<int, int> status;
    
    for (auto e : edges) {
        graph[e.first].push_back(e.second);
        status[e.first] = 0;
        status[e.second] = 0;
    }
    
    int cycle_node = -1;
    
    for (auto& [node, s] : status) {
        if (s == 0) {
            if (dfs(node, graph, status, cycle_node)) {
                return cycle_node;
            }
        }
    }
    
    return -1;
}

} // namespace minidb
