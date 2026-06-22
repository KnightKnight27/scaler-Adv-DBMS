#include "transaction_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

void print_val(const std::optional<std::string> &v, TxID xid, const RowKey &key)
{
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main()
{
    TransactionManager tm;
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);
        TxID t2 = tm.begin(); // snapshot after t1 committed
        TxID t3 = tm.begin();
        // t3 updates balance — t2 should still see old value
        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        auto v = tm.read(t2, "balance");
        print_val(v, t2, "balance"); // expects 1000 (t3 committed after t2 started)
        tm.commit(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        print_val(tm.read(t4, "balance"), t4, "balance"); // shared lock
        print_val(tm.read(t5, "balance"), t5, "balance"); // shared lock — both granted
        tm.commit(t4);
        tm.commit(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000"); // holds EXCLUSIVE lock on "balance"

        // t7 runs on a separate thread, will block until t6 commits
        std::thread reader([&]()
                           {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            auto v = tm.read(t7, "balance");
            print_val(v, t7, "balance");  // sees 3000 after t6 commits
            tm.commit(t7); });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6); // releases lock, unblocks t7
        reader.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID ta = tm.begin();
        TxID tb = tm.begin();

        // ta holds lock on "A", tb holds lock on "B"
        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();

        tm.acquire_lock("A", t8, LockMode::EXCLUSIVE);
        tm.acquire_lock("B", t9, LockMode::EXCLUSIVE);

        // t8 wants B (held by t9), t9 wants A (held by t8) → deadlock
        std::thread th1([&]()
                        {
            try {
                tm.acquire_lock("B", t8, LockMode::EXCLUSIVE);
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            } });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try
        {
            tm.acquire_lock("A", t9, LockMode::EXCLUSIVE);
            tm.commit(t9);
        }
        catch (DeadlockException &e)
        {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }

        th1.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
