#ifndef DEADLOCKDETECTOR_H
#define DEADLOCKDETECTOR_H

#include <iostream>
#include <unordered_map>
#include <unordered_set>

class DeadlockDetector {
private:

    std::unordered_map<
        int,
        std::unordered_set<int>
    > waitForGraph;

    bool dfs(
        int node,
        std::unordered_set<int>& visited,
        std::unordered_set<int>& recursionStack
    ) {

        visited.insert(node);
        recursionStack.insert(node);

        for (int neighbor :
             waitForGraph[node]) {

            if (!visited.count(neighbor)) {

                if (dfs(
                        neighbor,
                        visited,
                        recursionStack
                    ))
                    return true;
            }

            else if (
                recursionStack.count(
                    neighbor
                )
            ) {

                return true;
            }
        }

        recursionStack.erase(node);

        return false;
    }

public:

    void addEdge(
        int fromTxn,
        int toTxn
    ) {

        waitForGraph[fromTxn]
            .insert(toTxn);
    }

    void removeEdge(
        int fromTxn,
        int toTxn
    ) {

        waitForGraph[fromTxn]
            .erase(toTxn);
    }

    bool detectDeadlock() {

        std::unordered_set<int>
            visited;

        std::unordered_set<int>
            recursionStack;

        for (const auto& entry :
             waitForGraph) {

            int txn =
                entry.first;

            if (!visited.count(txn)) {

                if (
                    dfs(
                        txn,
                        visited,
                        recursionStack
                    )
                ) {

                    return true;
                }
            }
        }

        return false;
    }

    void printGraph() {

        std::cout
            << "\nWait-For Graph:\n";

        for (const auto& entry :
             waitForGraph) {

            std::cout
                << "T"
                << entry.first
                << " -> ";

            for (int txn :
                 entry.second) {

                std::cout
                    << "T"
                    << txn
                    << " ";
            }

            std::cout
                << std::endl;
        }
    }
};

#endif