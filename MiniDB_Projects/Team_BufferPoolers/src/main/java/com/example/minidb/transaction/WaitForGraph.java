package com.example.minidb.transaction;

import java.util.*;

public class WaitForGraph {

    private final Map<Long, Set<Long>> graph =
            new HashMap<>();

    public void addEdge(
            long from,
            long to) {

        graph.computeIfAbsent(
                from,
                k -> new HashSet<>()
        ).add(to);
    }

    public void removeTransaction(
            long txId) {

        graph.remove(txId);

        for (Set<Long> neighbors : graph.values()) {
            neighbors.remove(txId);
        }
    }

    public boolean hasCycle() {

        Set<Long> visited =
                new HashSet<>();

        Set<Long> recursionStack =
                new HashSet<>();

        for (Long node : graph.keySet()) {

            if (dfs(
                    node,
                    visited,
                    recursionStack
            )) {
                return true;
            }
        }

        return false;
    }

    private boolean dfs(
            Long node,
            Set<Long> visited,
            Set<Long> recursionStack) {

        if (recursionStack.contains(node)) {
            return true;
        }

        if (visited.contains(node)) {
            return false;
        }

        visited.add(node);
        recursionStack.add(node);

        for (Long neighbor :
                graph.getOrDefault(
                        node,
                        Collections.emptySet()
                )) {

            if (dfs(
                    neighbor,
                    visited,
                    recursionStack
            )) {

                return true;
            }
        }

        recursionStack.remove(node);

        return false;
    }
}