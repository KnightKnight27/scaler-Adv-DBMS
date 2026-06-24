#ifndef TRANSACTIONMANAGER_H
#define TRANSACTIONMANAGER_H

#include <iostream>
#include <unordered_set>
#include <string>

#include "../mvcc/MVCCStore.h"
#include "../locking/LockManager.h"
#include "../deadlock/DeadlockDetector.h"

class TransactionManager {
private:

    int nextTransactionId = 1;

    std::unordered_set<int>
        activeTransactions;

    MVCCStore store;

    LockManager lockManager;

    DeadlockDetector deadlockDetector;

public:

    int beginTransaction() {

        int txnId =
            nextTransactionId++;

        activeTransactions
            .insert(txnId);

        std::cout
            << "Transaction T"
            << txnId
            << " started\n";

        return txnId;
    }

    void read(
        int transactionId,
        int key
    ) {

        if (
            !lockManager.acquireSharedLock(
                transactionId,
                key
            )
        ) {

            std::cout
                << "T"
                << transactionId
                << " blocked on read\n";

            return;
        }

        std::string value =
            store.read(key);

        std::cout
            << "T"
            << transactionId
            << " read Key "
            << key
            << " = "
            << value
            << std::endl;
    }

    void write(
        int transactionId,
        int key,
        const std::string& value
    ) {

        if (
            !lockManager.acquireExclusiveLock(
                transactionId,
                key
            )
        ) {

            std::cout
                << "T"
                << transactionId
                << " blocked on write\n";

            return;
        }

        store.write(
            key,
            transactionId,
            value
        );

        std::cout
            << "T"
            << transactionId
            << " wrote Key "
            << key
            << " = "
            << value
            << std::endl;
    }

    void commit(
        int transactionId
    ) {

        lockManager.releaseLocks(
            transactionId
        );

        activeTransactions.erase(
            transactionId
        );

        std::cout
            << "Transaction T"
            << transactionId
            << " committed\n";
    }

    void abort(
        int transactionId
    ) {

        lockManager.releaseLocks(
            transactionId
        );

        activeTransactions.erase(
            transactionId
        );

        std::cout
            << "Transaction T"
            << transactionId
            << " aborted\n";
    }

    void addWait(
        int waitingTxn,
        int blockingTxn
    ) {

        deadlockDetector.addEdge(
            waitingTxn,
            blockingTxn
        );
    }

    bool detectDeadlock() {

        return
            deadlockDetector
                .detectDeadlock();
    }

    void printVersions(
        int key
    ) {

        store.printVersions(key);
    }

    void printLocks() {

        lockManager.printLocks();
    }

    void printWaitGraph() {

        deadlockDetector.printGraph();
    }
};

#endif