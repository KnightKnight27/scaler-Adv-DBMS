#pragma once
#ifndef DEADLOCK_DETECTOR_H
#define DEADLOCK_DETECTOR_H

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using TransactionID = int;

class DeadlockDetector {
private:
    std::unordered_map<TransactionID, std::set<TransactionID>> wait_for_graph;
    std::unordered_set<TransactionID> visited;
    std::unordered_set<TransactionID> rec_stack;

    bool hasCycleDFS(TransactionID node);

public:
    DeadlockDetector() = default;

    void addWaitForEdge(TransactionID waiter, TransactionID blocker);
    void removeTransaction(TransactionID tx_id);
    bool detectCycle();
    void printGraph() const;
};

#endif // DEADLOCK_DETECTOR_H
