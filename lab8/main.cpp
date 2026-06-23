#include "mvcc_db.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Scenario 1: MVCC Read Isolation (Snapshot Isolation)
// Demonstrates that reads are lock-free and always read a consistent snapshot
// representing the state of the database when the transaction started.
void testSnapshotIsolation() {
    std::cout << "\n==========================================================" << std::endl;
    std::cout << "         TEST 1: MVCC SNAPSHOT READ ISOLATION             " << std::endl;
    std::cout << "==========================================================" << std::endl;

    MVCCDatabase db;

    // Seed database with Key 1 = "Original" using a bootstrap transaction
    int boot_tx = db.beginTransaction();
    db.writeRecord(boot_tx, 1, "Original");
    db.commitTransaction(boot_tx);

    db.printDatabaseState();

    // Start Transaction 1 (Reader)
    int tx1 = db.beginTransaction();
    std::string val1;
    db.readRecord(tx1, 1, val1);
    std::cout << "[Tx 1] Read Key 1: '" << val1 << "' (Expected: 'Original')" << std::endl;
    assert(val1 == "Original");

    // Start Transaction 2 (Writer) in a separate thread
    std::thread writer_thread([&db]() {
        sleep_ms(100); // Let Tx 1 read first
        int tx2 = db.beginTransaction();
        db.writeRecord(tx2, 1, "Updated by Tx 2");
        db.commitTransaction(tx2);
    });
    writer_thread.join();

    // Tx 2 has committed. Let's see if Tx 1 still sees "Original"
    std::string val2;
    db.readRecord(tx1, 1, val2);
    std::cout << "[Tx 1] Read Key 1 (after Tx 2 committed): '" << val2 
              << "' (Expected: 'Original' - snapshot isolation!)" << std::endl;
    assert(val2 == "Original");

    db.commitTransaction(tx1);

    // Now start a new Transaction 3. It should see the update.
    int tx3 = db.beginTransaction();
    std::string val3;
    db.readRecord(tx3, 1, val3);
    std::cout << "[Tx 3] Read Key 1: '" << val3 << "' (Expected: 'Updated by Tx 2')" << std::endl;
    assert(val3 == "Updated by Tx 2");
    db.commitTransaction(tx3);

    db.printDatabaseState();
}

// Scenario 2: Strict 2PL Write Serialization
// Demonstrates that writes to the same record block each other.
void testWriteLocking() {
    std::cout << "\n==========================================================" << std::endl;
    std::cout << "         TEST 2: STRICT 2PL WRITE SERIALIZATION           " << std::endl;
    std::cout << "==========================================================" << std::endl;

    MVCCDatabase db;

    // Seed database with Key 2 = "Base"
    int boot_tx = db.beginTransaction();
    db.writeRecord(boot_tx, 2, "Base");
    db.commitTransaction(boot_tx);

    // Thread A starts Tx A and updates Key 2. Holds lock for 500ms before commit.
    std::thread thread_A([&db]() {
        int tx_A = db.beginTransaction();
        db.writeRecord(tx_A, 2, "Written by Tx A");
        sleep_ms(500);
        db.commitTransaction(tx_A);
    });

    // Thread B starts Tx B and tries to update Key 2. Should block until Tx A commits.
    std::thread thread_B([&db]() {
        sleep_ms(100); // Ensure Tx A gets lock first
        int tx_B = db.beginTransaction();
        std::cout << "[Tx B] Attempting to write Key 2..." << std::endl;
        db.writeRecord(tx_B, 2, "Written by Tx B"); // Blocks here
        db.commitTransaction(tx_B);
    });

    thread_A.join();
    thread_B.join();

    // Verify final state
    int tx_check = db.beginTransaction();
    std::string val;
    db.readRecord(tx_check, 2, val);
    std::cout << "[Check] Final Key 2 value: '" << val << "' (Expected: 'Written by Tx B')" << std::endl;
    assert(val == "Written by Tx B");
    db.commitTransaction(tx_check);

    db.printDatabaseState();
}

// Scenario 3: Deadlock Detection and Victim Abort
// Demonstrates deadlock detection and automatic transaction rollback.
void testDeadlockDetection() {
    std::cout << "\n==========================================================" << std::endl;
    std::cout << "         TEST 3: DEADLOCK DETECTION & ROLLBACK            " << std::endl;
    std::cout << "==========================================================" << std::endl;

    MVCCDatabase db;

    // Seed Key 10 = "Initial 10", Key 20 = "Initial 20"
    int boot_tx = db.beginTransaction();
    db.writeRecord(boot_tx, 10, "Initial 10");
    db.writeRecord(boot_tx, 20, "Initial 20");
    db.commitTransaction(boot_tx);

    int tx_A = db.beginTransaction(); // Tx 1
    int tx_B = db.beginTransaction(); // Tx 2

    // Tx A locks Key 10
    db.writeRecord(tx_A, 10, "Tx A val 10");
    // Tx B locks Key 20
    db.writeRecord(tx_B, 20, "Tx B val 20");

    // Print WFG showing locks held but no one waiting yet
    db.printDatabaseState();

    // Launch Tx A trying to write Key 20 in a separate thread (blocks)
    std::thread thread_txA([&db, tx_A]() {
        std::cout << "[Tx A] Attempting to lock Key 20 (held by Tx B)..." << std::endl;
        db.writeRecord(tx_A, 20, "Tx A val 20"); // Should block
        std::cout << "[Tx A] Woke up. Committing Tx A..." << std::endl;
        db.commitTransaction(tx_A);
    });

    sleep_ms(200); // Give Tx A thread time to block

    // Tx B tries to write Key 10. This creates a cycle: Tx A -> Tx B -> Tx A.
    // The deadlock detector should select Tx B (the requester) as the victim and abort it.
    std::cout << "[Tx B] Attempting to lock Key 10 (held by Tx A)..." << std::endl;
    bool success_B = db.writeRecord(tx_B, 10, "Tx B val 10");

    if (!success_B) {
        std::cout << "[Tx B] writeRecord failed. Tx B was aborted by Lock Manager." << std::endl;
    }

    thread_txA.join();

    // Verify database state: Tx A should have completed, Tx B's writes rolled back.
    std::cout << "\nVerifying final values..." << std::endl;
    int tx_check = db.beginTransaction();
    std::string val10, val20;
    db.readRecord(tx_check, 10, val10);
    db.readRecord(tx_check, 20, val20);
    std::cout << "Key 10: '" << val10 << "' (Expected: 'Tx A val 10' because Tx A committed)" << std::endl;
    std::cout << "Key 20: '" << val20 << "' (Expected: 'Tx A val 20' because Tx B rolled back and Tx A overwrote)" << std::endl;
    
    assert(val10 == "Tx A val 10");
    assert(val20 == "Tx A val 20");
    
    db.commitTransaction(tx_check);

    db.printDatabaseState();
}

int main() {
    std::cout << "Starting Transaction Manager Simulator..." << std::endl;
    testSnapshotIsolation();
    testWriteLocking();
    testDeadlockDetection();
    std::cout << "\nAll database concurrency tests completed successfully!" << std::endl;
    return 0;
}
