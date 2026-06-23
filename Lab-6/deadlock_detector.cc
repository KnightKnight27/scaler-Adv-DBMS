#include "deadlock_detector.h"
#include <iostream>

bool DeadlockDetector::hasCycleDFS(TransactionID node) {
    visited.insert(node);
    rec_stack.insert(node);

    auto it = wait_for_graph.find(node);
    if (it != wait_for_graph.end()) {
        for (TransactionID neighbor : it->second) {
            if (rec_stack.count(neighbor)) {
                return true;
            }
            if (!visited.count(neighbor) && hasCycleDFS(neighbor)) {
                return true;
            }
        }
    }

    rec_stack.erase(node);
    return false;
}

void DeadlockDetector::addWaitForEdge(TransactionID waiter, TransactionID blocker) {
    wait_for_graph[waiter].insert(blocker);
}

void DeadlockDetector::removeTransaction(TransactionID tx_id) {
    wait_for_graph.erase(tx_id);

    for (auto &pair : wait_for_graph) {
        pair.second.erase(tx_id);
    }
}

bool DeadlockDetector::detectCycle() {
    visited.clear();
    rec_stack.clear();

    for (const auto &pair : wait_for_graph) {
        if (!visited.count(pair.first)) {
            if (hasCycleDFS(pair.first)) {
                return true;
            }
        }
    }

    return false;
}

void DeadlockDetector::printGraph() const {
    std::cout << "\n=== Wait-For Graph ===\n";
    for (const auto &pair : wait_for_graph) {
        std::cout << "TX" << pair.first << " waits for: ";
        for (TransactionID blocker : pair.second) {
            std::cout << "TX" << blocker << " ";
        }
        std::cout << "\n";
    }
}
