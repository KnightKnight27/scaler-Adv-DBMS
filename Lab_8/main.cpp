#include "transaction_manager.h"
#include <iostream>
#include <thread>
#include <chrono>

void outputValue(const std::optional<std::string> &valOpt, TxnID txId, const DbKey &dbKey)
{
    std::cout << "  [TX " << txId << "] READ " << dbKey << " = "
              << (valOpt ? *valOpt : "<not visible>") << "\n";
}

int main()
{
    DbTransactionEngine engine;
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxnID t1 = engine.startTransaction();
        engine.insertKey(t1, "balance", "1000");
        engine.commitTransaction(t1);
        TxnID t2 = engine.startTransaction(); // snapshot after t1 committed
        TxnID t3 = engine.startTransaction();
        // t3 updates balance — t2 should still see old value
        engine.updateKey(t3, "balance", "2000");
        engine.commitTransaction(t3);

        auto v = engine.readKey(t2, "balance");
        outputValue(v, t2, "balance"); // expects 1000 (t3 committed after t2 started)
        engine.commitTransaction(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxnID t4 = engine.startTransaction();
        TxnID t5 = engine.startTransaction();
        outputValue(engine.readKey(t4, "balance"), t4, "balance"); // shared lock
        outputValue(engine.readKey(t5, "balance"), t5, "balance"); // shared lock — both granted
        engine.commitTransaction(t4);
        engine.commitTransaction(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxnID t6 = engine.startTransaction();
        engine.updateKey(t6, "balance", "3000"); // holds EXCLUSIVE lock on "balance"

        // t7 runs on a separate thread, will block until t6 commits
        std::thread reader([&]()
        {
            TxnID t7 = engine.startTransaction();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            auto v = engine.readKey(t7, "balance");
            outputValue(v, t7, "balance");  // sees 3000 after t6 commits
            engine.commitTransaction(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.commitTransaction(t6); // releases lock, unblocks t7
        reader.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxnID ta = engine.startTransaction();
        TxnID tb = engine.startTransaction();

        // ta holds lock on "A", tb holds lock on "B"
        engine.insertKey(ta, "A", "val_a");
        engine.insertKey(tb, "B", "val_b");
        engine.commitTransaction(ta);
        engine.commitTransaction(tb);

        TxnID t8 = engine.startTransaction();
        TxnID t9 = engine.startTransaction();

        engine.requestLock("A", t8, LockType::WRITE_LOCK);
        engine.requestLock("B", t9, LockType::WRITE_LOCK);

        // t8 wants B (held by t9), t9 wants A (held by t8) → deadlock
        std::thread th1([&]()
        {
            try {
                engine.requestLock("B", t8, LockType::WRITE_LOCK);
                engine.commitTransaction(t8);
            } catch (CycleDetectedException& e) {
                std::cout << "  " << e.what() << "\n";
                engine.rollbackTransaction(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try
        {
            engine.requestLock("A", t9, LockType::WRITE_LOCK);
            engine.commitTransaction(t9);
        }
        catch (CycleDetectedException &e)
        {
            std::cout << "  " << e.what() << "\n";
            engine.rollbackTransaction(t9);
        }

        th1.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}