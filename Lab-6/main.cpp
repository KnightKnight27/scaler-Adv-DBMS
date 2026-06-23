#include "transaction_manager.h"
#include <iostream>

int main() {
    TransactionManager tm;

    std::cout << "=== Scenario 1: Basic MVCC Read-Write ===\n";
    TransactionID tx1 = tm.beginTransaction();
    tm.write(tx1, "account_A", "1000");
    tm.write(tx1, "account_B", "500");
    tm.commit(tx1);

    tm.printVersionChains();

    std::cout << "\n=== Scenario 2: Concurrent Reads (Shared Locks) ===\n";
    TransactionID tx2 = tm.beginTransaction();
    TransactionID tx3 = tm.beginTransaction();

    std::string val_a, val_b;
    tm.read(tx2, "account_A", val_a);
    tm.read(tx3, "account_B", val_b);

    std::cout << "TX2 read account_A: " << val_a << "\n";
    std::cout << "TX3 read account_B: " << val_b << "\n";

    tm.commit(tx2);
    tm.commit(tx3);

    std::cout << "\n=== Scenario 3: Write-Write Conflict (Exclusive Locks) ===\n";
    TransactionID tx4 = tm.beginTransaction();
    TransactionID tx5 = tm.beginTransaction();

    tm.write(tx4, "account_A", "2000");
    tm.write(tx5, "account_A", "3000");

    tm.commit(tx4);
    tm.commit(tx5);

    tm.printVersionChains();

    std::cout << "\n=== Scenario 4: Abort and Rollback ===\n";
    TransactionID tx6 = tm.beginTransaction();
    tm.write(tx6, "account_C", "5000");
    tm.abort(tx6);

    tm.printVersionChains();

    std::cout << "\n=== Final Transaction State ===\n";
    tm.printTransactionState();

    return 0;
}
