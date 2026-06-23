#include "tx_manager.h"
#include "lock_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

void printVal(const std::optional<std::string>& val, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (val ? *val : "<not visible / deleted>") << "\n";
}

int main() {
    TransactionManager tm;

    // -------------------------------------------------------------
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin(); // Snapshot taken here
        TxID t3 = tm.begin();

        tm.update(t3, "balance", "2000");
        tm.commit(t3); // T3 commits, but T2 was active first

        // T2 reads balance: expects "1000" because of snapshot visibility
        auto val = tm.read(t2, "balance");
        printVal(val, t2, "balance");
        tm.commit(t2);
    }

    // -------------------------------------------------------------
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();

        printVal(tm.read(t4, "balance"), t4, "balance");
        printVal(tm.read(t5, "balance"), t5, "balance"); // Both shared locks granted simultaneously

        tm.commit(t4);
        tm.commit(t5);
    }

    // -------------------------------------------------------------
    std::cout << "\n=== Scenario 3: Exclusive Lock blocks Shared ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000"); // Holds EXCLUSIVE lock

        std::thread readerThread([&]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] Attempting to read 'balance' (will block)...\n";
            auto val = tm.read(t7, "balance");
            printVal(val, t7, "balance");
            tm.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "  [TX " << t6 << "] Committing, releasing lock...\n";
        tm.commit(t6);

        readerThread.join();
    }

    // -------------------------------------------------------------
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        // Setup resource records
        TxID setup = tm.begin();
        tm.insert(setup, "res_A", "init_a");
        tm.insert(setup, "res_B", "init_b");
        tm.commit(setup);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();

        // T8 locks A, T9 locks B
        tm.update(t8, "res_A", "val_t8");
        tm.update(t9, "res_B", "val_t9");

        std::thread worker([&]() {
            try {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::cout << "  [TX " << t8 << "] Requesting EXCLUSIVE lock on res_B...\n";
                tm.update(t8, "res_B", "val_t8_new");
                tm.commit(t8);
            } catch (const DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });

        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "  [TX " << t9 << "] Requesting EXCLUSIVE lock on res_A (triggers Deadlock Cycle)...\n";
            tm.update(t9, "res_A", "val_t9_new");
            tm.commit(t9);
        } catch (const DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }

        worker.join();
    }

    std::cout << "\nAll scenarios completed successfully.\n";
    return 0;
}
