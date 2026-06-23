#pragma once
#include "txn/lock_manager.h"
#include <unordered_map>
#include <vector>

namespace minidb {

class DeadlockDetector {
public:
    DeadlockDetector(LockManager* lock_manager);
    int detect_deadlock();

private:
    LockManager* lock_manager_;
    bool dfs(int node, std::unordered_map<int, std::vector<int>>& graph,
             std::unordered_map<int, int>& status, int& cycle_node);
};

} // namespace minidb
