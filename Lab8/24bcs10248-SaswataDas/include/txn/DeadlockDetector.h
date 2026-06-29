#pragma once

#include "Interfaces.h"

namespace txn {

// Finds deadlocks by searching the waits-for graph for a cycle (depth-first
// search with a recursion stack). When a cycle exists, the youngest member
// (highest transaction id) is returned as the victim, which guarantees the
// older transaction makes progress.
class DeadlockDetector : public IDeadlockDetector {
public:
    std::optional<TxnId> findVictim(const std::vector<WaitsForEdge>& edges) const override;
};

}  // namespace txn
