package com.example.minidb.transaction;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class LockManagerTest {

    @Test
    void testExclusiveLockConflict() {

        LockManager manager =
                new LockManager();

        Transaction tx1 =
                new Transaction(1);

        Transaction tx2 =
                new Transaction(2);

        assertTrue(
                manager.acquireExclusiveLock(
                        "users",
                        tx1
                )
        );

        assertFalse(
                manager.acquireExclusiveLock(
                        "users",
                        tx2
                )
        );
    }
}