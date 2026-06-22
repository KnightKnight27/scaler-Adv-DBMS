#include "txn/DeadlockDetector.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace txn {

namespace {

enum class Color { White, Gray, Black };

// Depth-first search that records the active path. On reaching a Gray node we
// have closed a cycle; the slice of the path from that node to the end is the
// cycle, captured into `cycle`.
bool visit(TxnId node,
           const std::unordered_map<TxnId, std::vector<TxnId>>& adj,
           std::unordered_map<TxnId, Color>& color,
           std::vector<TxnId>& path,
           std::vector<TxnId>& cycle) {
    color[node] = Color::Gray;
    path.push_back(node);

    auto it = adj.find(node);
    if (it != adj.end()) {
        for (TxnId next : it->second) {
            if (color[next] == Color::Gray) {
                auto start = std::find(path.begin(), path.end(), next);
                cycle.assign(start, path.end());
                return true;
            }
            if (color[next] == Color::White && visit(next, adj, color, path, cycle)) {
                return true;
            }
        }
    }

    path.pop_back();
    color[node] = Color::Black;
    return false;
}

}  // namespace

std::optional<TxnId> DeadlockDetector::findVictim(const std::vector<WaitsForEdge>& edges) const {
    if (edges.empty()) return std::nullopt;

    std::unordered_map<TxnId, std::vector<TxnId>> adj;
    std::unordered_set<TxnId> nodes;
    for (const auto& e : edges) {
        adj[e.first].push_back(e.second);
        nodes.insert(e.first);
        nodes.insert(e.second);
    }

    std::unordered_map<TxnId, Color> color;
    for (TxnId n : nodes) color[n] = Color::White;

    std::vector<TxnId> path;
    std::vector<TxnId> cycle;
    for (TxnId n : nodes) {
        if (color[n] == Color::White && visit(n, adj, color, path, cycle)) {
            // Abort the youngest transaction in the cycle so older work survives.
            return *std::max_element(cycle.begin(), cycle.end());
        }
    }
    return std::nullopt;
}

}  // namespace txn
