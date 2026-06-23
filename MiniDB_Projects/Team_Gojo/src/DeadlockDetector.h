#ifndef MINIDB_DEADLOCK_DETECTOR_H
#define MINIDB_DEADLOCK_DETECTOR_H

#include <unordered_map>
#include <unordered_set>

/**
 * DeadlockDetector implements a Wait-For Graph (WFG) for deadlock detection.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * DEADLOCK IN 2PL:
 * Two-Phase Locking can cause deadlocks where transactions form a cycle
 * of waiting dependencies:
 *
 *   Txn A holds X(r1), waits for X(r2)
 *   Txn B holds X(r2), waits for X(r1)
 *   → Deadlock! Neither can proceed.
 *
 * WAIT-FOR GRAPH:
 * We model waiting relationships as a directed graph:
 *   - Node = transaction
 *   - Edge (A → B) = "A is waiting for B to release a lock"
 *
 * A deadlock exists if and only if the WFG has a CYCLE.
 *
 * DETECTION ALGORITHM:
 * We use DFS-based cycle detection. When a new wait edge is added,
 * we check if the graph now contains a cycle.
 *
 * VICTIM SELECTION:
 * When a cycle is detected, we must abort one transaction to break it.
 * We choose the YOUNGEST transaction (highest txnId) as the victim.
 * This is a common heuristic because:
 *   1. Younger transactions have done less work (cheaper to redo)
 *   2. Older transactions are closer to completion
 *   3. Prevents starvation of long-running transactions
 *
 * ALTERNATIVE: Timeout-based detection is simpler but less precise.
 * If a transaction waits longer than T seconds, assume deadlock and abort.
 * We implement the graph approach for correctness.
 * ═══════════════════════════════════════════════════════════════════════
 */
class DeadlockDetector {
public:
    DeadlockDetector() = default;

    /** Add a wait-for edge: waiter is blocked on holder. */
    void addEdge(int waiter, int holder) {
        waitsFor_[waiter].insert(holder);
    }

    /** Remove a wait-for edge (when a lock is granted or txn aborts). */
    void removeEdge(int waiter, int holder) {
        auto it = waitsFor_.find(waiter);
        if (it != waitsFor_.end()) {
            it->second.erase(holder);
            if (it->second.empty()) {
                waitsFor_.erase(it);
            }
        }
    }

    /** Remove all edges involving a transaction (on commit/abort). */
    void removeTxn(int txnId) {
        waitsFor_.erase(txnId);
        for (auto& [waiter, holders] : waitsFor_) {
            holders.erase(txnId);
        }
    }

    /**
     * Check if adding a wait-for edge would create a cycle (deadlock).
     *
     * Uses depth-first search starting from the holder to see if
     * we can reach the waiter through existing edges.
     */
    bool wouldCauseCycle(int waiter, int holder) const {
        // If holder transitively waits for waiter → cycle
        std::unordered_set<int> visited;
        return dfs(holder, waiter, visited);
    }

    /**
     * Detect if any cycle exists in the current WFG.
     *
     * Returns the ID of the victim transaction to abort, or -1 if
     * no deadlock exists.
     */
    int detectAndChooseVictim() const {
        // Try DFS from every node to find cycles
        for (const auto& [txnId, _] : waitsFor_) {
            std::unordered_set<int> visited;
            std::unordered_set<int> recursionStack;
            int victim = -1;

            if (dfsFindCycle(txnId, visited, recursionStack, victim)) {
                return victim;  // youngest transaction in the cycle
            }
        }
        return -1;  // no deadlock
    }

private:
    /**
     * DFS: Can we reach 'target' starting from 'current'?
     * Used for the wouldCauseCycle check.
     */
    bool dfs(int current, int target,
             std::unordered_set<int>& visited) const {
        if (current == target) return true;
        if (visited.count(current)) return false;

        visited.insert(current);

        auto it = waitsFor_.find(current);
        if (it != waitsFor_.end()) {
            for (int next : it->second) {
                if (dfs(next, target, visited)) {
                    return true;
                }
            }
        }

        return false;
    }

    /**
     * DFS with recursion stack for general cycle detection.
     * Tracks the youngest (highest txnId) node in the cycle as victim.
     */
    bool dfsFindCycle(int current,
                      std::unordered_set<int>& visited,
                      std::unordered_set<int>& recursionStack,
                      int& victim) const {
        visited.insert(current);
        recursionStack.insert(current);

        auto it = waitsFor_.find(current);
        if (it != waitsFor_.end()) {
            for (int next : it->second) {
                if (recursionStack.count(next)) {
                    // Found a cycle! Choose youngest in cycle as victim
                    victim = std::max(current, next);
                    return true;
                }
                if (!visited.count(next)) {
                    if (dfsFindCycle(next, visited, recursionStack, victim)) {
                        victim = std::max(victim, current);
                        return true;
                    }
                }
            }
        }

        recursionStack.erase(current);
        return false;
    }

    // txnId → set of txnIds it is waiting for
    std::unordered_map<int, std::unordered_set<int>> waitsFor_;
};

#endif // MINIDB_DEADLOCK_DETECTOR_H
