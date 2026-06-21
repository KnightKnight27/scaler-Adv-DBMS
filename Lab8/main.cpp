#include "transaction_manager.h"

#include <chrono>
#include <iostream>
#include <thread>

void printRead(TxID xid,const RowKey& key,const std::optional<std::string>& value)
{
    std::cout << "[TX " << xid << "] READ "<< key << " = "<< (value ? *value : "<not visible>")<< '\n';
}

int main()
{
    TransactionManager manager;

    std::cout << "\n=== 1. MVCC Snapshot Isolation ===\n";

    TxID seed = manager.begin();
    manager.insert(seed, "account_101", "10000");
    manager.commit(seed);

    TxID oldReader = manager.begin();
    TxID writer = manager.begin();

    manager.update(writer, "account_101", "12000");
    manager.commit(writer);

    printRead(
        oldReader,
        "account_101",
        manager.read(oldReader, "account_101")
    );

    manager.commit(oldReader);

    std::cout << "\n=== 2. Concurrent Shared Locks ===\n";

    TxID reader1 = manager.begin();
    TxID reader2 = manager.begin();

    printRead(
        reader1,
        "account_101",
        manager.read(reader1, "account_101")
    );

    printRead(
        reader2,
        "account_101",
        manager.read(reader2, "account_101")
    );

    manager.commit(reader1);
    manager.commit(reader2);

    std::cout << "\n=== 3. Exclusive Lock Blocks Reader ===\n";

    TxID blockingWriter = manager.begin();
    manager.update(blockingWriter, "account_101", "15000");

    std::thread waitingReader([&]()
    {
        TxID reader = manager.begin();

        std::cout
            << "[TX " << reader
            << "] waiting for account_101\n";

        printRead(
            reader,
            "account_101",
            manager.read(reader, "account_101")
        );

        manager.commit(reader);
    });

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    manager.commit(blockingWriter);

    waitingReader.join();

    std::cout << "\n=== 4. Deadlock Detection ===\n";

    TxID setup = manager.begin();

    manager.insert(
        setup,
        "account_201",
        "25000"
    );

    manager.insert(
        setup,
        "account_202",
        "30000"
    );

    manager.commit(setup);

    TxID left = manager.begin();
    TxID right = manager.begin();

    manager.update(
        left,
        "account_201",
        "24000"
    );

    manager.update(
        right,
        "account_202",
        "29000"
    );

    std::thread firstWaiter([&]()
    {
        try
        {
            manager.update(
                left,
                "account_202",
                "28000"
            );

            manager.commit(left);
        }
        catch(const DeadlockException& ex)
        {
            std::cout << ex.what() << '\n';
            manager.abort(left);
        }
    });

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    try
    {
        manager.update(
            right,
            "account_201",
            "23000"
        );

        manager.commit(right);
    }
    catch(const DeadlockException& ex)
    {
        std::cout << ex.what() << '\n';
        manager.abort(right);
    }

    firstWaiter.join();

    std::cout << "\nAll scenarios completed successfully.\n";

    return 0;
}