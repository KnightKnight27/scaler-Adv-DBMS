package com.example.minidb.transaction;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class DeadlockDetectorTest {

    @Test
    void testDeadlockDetected() {

        DeadlockDetector detector =
                new DeadlockDetector();

        detector.waitFor(
                1,
                2
        );

        detector.waitFor(
                2,
                1
        );

        assertTrue(
                detector.detectDeadlock()
        );
    }
}
