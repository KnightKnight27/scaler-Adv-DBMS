#include "TransactionManager.h"
#include <iostream>

static void separator(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << title << "\n";
    std::cout << "============================================================\n";
}

int main() {
    // ---- Scenario 1: Basic Read / Write / Commit ----
    separator("SCENARIO 1: Basic Read / Write / Commit");
    {
        TransactionManager tm;

        auto t1 = tm.begin();
        std::cout << "T1 write A: " << tm.write(t1, "A", "100") << "\n";
        std::cout << "T1 write B: " << tm.write(t1, "B", "200") << "\n";
        tm.commit(t1);

        auto t2 = tm.begin();
        std::cout << "T2 reads A: " << tm.read(t2, "A") << "\n";
        std::cout << "T2 reads B: " << tm.read(t2, "B") << "\n";
        tm.commit(t2);
    }

    // ---- Scenario 2: MVCC snapshot isolation ----
    separator("SCENARIO 2: MVCC Snapshot Isolation");
    {
        TransactionManager tm;

        auto t3 = tm.begin();
        tm.write(t3, "X", "original");
        tm.commit(t3);

        auto t4 = tm.begin();    // T4 starts BEFORE T5 commits
        auto t5 = tm.begin();
        tm.write(t5, "X", "modified");

        std::cout << "T4 reads X (before T5 commits): " << tm.read(t4, "X") << "\n";  // original
        tm.commit(t5);
        std::cout << "T4 reads X (after T5 commits):  " << tm.read(t4, "X") << "\n";  // still original
        tm.commit(t4);
    }

    // ---- Scenario 3: Deadlock detection ----
    separator("SCENARIO 3: Deadlock Detection");
    {
        TransactionManager tm;

        auto t6 = tm.begin();
        auto t7 = tm.begin();

        std::cout << "T6 writes P: " << tm.write(t6, "P", "v1") << "\n";   // OK
        std::cout << "T7 writes Q: " << tm.write(t7, "Q", "v2") << "\n";   // OK
        std::cout << "T6 writes Q: " << tm.write(t6, "Q", "v3") << "\n";   // BLOCKED (T7 holds Q)
        std::cout << "T7 writes P: " << tm.write(t7, "P", "v4") << "\n";   // DEADLOCK -> T7 aborted
        std::cout << "T6 writes Q: " << tm.write(t6, "Q", "v3") << "\n";   // OK now
        tm.commit(t6);
    }

    // ---- Scenario 4: Abort and undo ----
    separator("SCENARIO 4: Abort and Undo");
    {
        TransactionManager tm;

        auto t8 = tm.begin();
        tm.write(t8, "Y", "temp_value");
        tm.abort(t8);

        auto t9 = tm.begin();
        std::cout << "T9 reads Y after T8 abort: " << tm.read(t9, "Y") << "\n";  // NOT_FOUND
        tm.commit(t9);
    }

    return 0;
}
