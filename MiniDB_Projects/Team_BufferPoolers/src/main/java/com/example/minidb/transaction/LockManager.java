package com.example.minidb.transaction;

import java.util.HashMap;
import java.util.Map;

public class LockManager {

    private final Map<String, Lock> lockTable =
            new HashMap<>();

    public synchronized boolean acquireSharedLock(
            String resource,
            Transaction tx) {

        Lock lock =
                lockTable.computeIfAbsent(
                        resource,
                        k -> new Lock()
                );

        if (lock.getType() == null) {

            lock.setType(
                    LockType.SHARED
            );
        }

        if (lock.getType() == LockType.EXCLUSIVE &&
                !lock.getOwners()
                        .contains(
                                tx.getTransactionId()
                        )) {

            return false;
        }

        lock.getOwners()
                .add(
                        tx.getTransactionId()
                );

        return true;
    }

    public synchronized boolean acquireExclusiveLock(
            String resource,
            Transaction tx) {

        Lock lock =
                lockTable.computeIfAbsent(
                        resource,
                        k -> new Lock()
                );

        if (lock.getOwners().isEmpty()) {

            lock.setType(
                    LockType.EXCLUSIVE
            );

            lock.getOwners()
                    .add(
                            tx.getTransactionId()
                    );

            return true;
        }

        if (lock.getOwners().size() == 1 &&
                lock.getOwners()
                        .contains(
                                tx.getTransactionId()
                        )) {

            lock.setType(
                    LockType.EXCLUSIVE
            );

            return true;
        }

        return false;
    }

    public synchronized void releaseLocks(
            Transaction tx) {

        for (Lock lock : lockTable.values()) {

            lock.getOwners()
                    .remove(
                            tx.getTransactionId()
                    );

            if (lock.getOwners().isEmpty()) {

                lock.setType(null);
            }
        }
    }
}