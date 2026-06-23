#include "db_engine.hpp"

#include <iostream>

int main() {
    DatabaseSystem db;

    TransactionHandle txn1 = db.begin_transaction();
    TransactionHandle txn2 = db.begin_transaction();

    db.perform_read(txn1, "A");
    db.perform_write(txn1, "A", 110);

    db.perform_read(txn2, "B");
    db.perform_write(txn2, "B", 190);

    std::cout << "\n--- Creating Deadlock Scenario ---\n";
    db.perform_write(txn1, "B", 210);  // txn1 needs B (held by txn2)
    db.perform_write(txn2, "A", 90);   // txn2 needs A (held by txn1) -> Cycle -> txn2 aborts

    db.display_status();

    std::cout << "\n--- Resolving and Continuing ---\n";
    db.perform_write(txn1, "B", 210);  // Try again since txn2 gave up locks
    db.perform_commit(txn1);
    db.perform_commit(txn2); // Will be skipped as txn2 failed

    TransactionHandle txn3 = db.begin_transaction();
    db.perform_read(txn3, "A");
    db.perform_read(txn3, "B");
    db.perform_commit(txn3);

    db.display_status();
    return 0;
}
