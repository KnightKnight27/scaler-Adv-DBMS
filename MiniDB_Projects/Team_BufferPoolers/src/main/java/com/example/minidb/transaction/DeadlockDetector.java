package com.example.minidb.transaction;

public class DeadlockDetector {

    private final WaitForGraph graph =
            new WaitForGraph();

    public void waitFor(
            long waitingTx,
            long owningTx) {

        graph.addEdge(
                waitingTx,
                owningTx
        );
    }

    public boolean detectDeadlock() {

        return graph.hasCycle();
    }

    public void transactionFinished(
            long txId) {

        graph.removeTransaction(txId);
    }
}