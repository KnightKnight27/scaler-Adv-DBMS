#include <iostream>

#include "TransactionManager.h"

int main() {

    TransactionManager tm;

    int t1 = tm.beginTransaction();
    int t2 = tm.beginTransaction();
    int t3 = tm.beginTransaction();

    std::cout << "\n=== MVCC Writes ===\n";

    tm.write(t1, 1, "Alice");
    tm.write(t2, 1, "Bob");
    tm.write(t3, 1, "Charlie");

    std::cout << "\n=== Version Chain ===\n";

    tm.printVersions(1);

    std::cout << "\n=== Reads ===\n";

    tm.read(t1, 1);
    tm.read(t2, 1);

    std::cout << "\n=== Lock Table ===\n";

    tm.printLocks();

    std::cout << "\n=== Deadlock Simulation ===\n";

    tm.addWait(1, 2);
    tm.addWait(2, 3);
    tm.addWait(3, 1);

    tm.printWaitGraph();

    if (tm.detectDeadlock()) {

        std::cout
            << "\nDeadlock Detected!\n";
    }
    else {

        std::cout
            << "\nNo Deadlock Found\n";
    }

    std::cout << "\n=== Commit Transactions ===\n";

    tm.commit(t1);
    tm.commit(t2);
    tm.commit(t3);

    std::cout << "\n=== Final Lock Table ===\n";

    tm.printLocks();

    return 0;
}