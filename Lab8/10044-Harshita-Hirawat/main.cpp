#include "transaction_manager.h"

#include <chrono>
#include <iostream>
#include <thread>

void printRead(TxID xid, const RowKey& key, const std::optional<std::string>& value) {
    std::cout << "[TX " << xid << "] READ " << key << " = " << (value ? *value : "<not visible>") << '\n';
}

int main() {
    TransactionManager manager;

    std::cout << "\n=== 1. MVCC snapshot ===\n";
    TxID first = manager.begin();
    manager.insert(first, "balance", "1000");
    manager.commit(first);

    TxID oldReader = manager.begin();
    TxID writer = manager.begin();
    manager.update(writer, "balance", "2000");
    manager.commit(writer);
    printRead(oldReader, "balance", manager.read(oldReader, "balance"));
    manager.commit(oldReader);

    std::cout << "\n=== 2. Concurrent shared locks ===\n";
    TxID readerOne = manager.begin();
    TxID readerTwo = manager.begin();
    printRead(readerOne, "balance", manager.read(readerOne, "balance"));
    printRead(readerTwo, "balance", manager.read(readerTwo, "balance"));
    manager.commit(readerOne);
    manager.commit(readerTwo);

    std::cout << "\n=== 3. Exclusive lock blocks a reader ===\n";
    TxID blockingWriter = manager.begin();
    manager.update(blockingWriter, "balance", "3000");

    std::thread waitingReader([&]() {
        TxID reader = manager.begin();
        std::cout << "[TX " << reader << "] waiting for balance\n";
        printRead(reader, "balance", manager.read(reader, "balance"));
        manager.commit(reader);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.commit(blockingWriter);
    waitingReader.join();

    std::cout << "\n=== 4. Deadlock detection ===\n";
    TxID seed = manager.begin();
    manager.insert(seed, "A", "value-a");
    manager.insert(seed, "B", "value-b");
    manager.commit(seed);

    TxID left = manager.begin();
    TxID right = manager.begin();
    manager.update(left, "A", "left-holds-a");
    manager.update(right, "B", "right-holds-b");

    std::thread firstWaiter([&]() {
        try {
            manager.update(left, "B", "left-wants-b");
            manager.commit(left);
        }
        catch(const DeadlockException& error) {
            std::cout << error.what() << '\n';
            manager.abort(left);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    try {
        manager.update(right, "A", "right-wants-a");
        manager.commit(right);
    }
    catch(const DeadlockException& error) {
        std::cout << error.what() << '\n';
        manager.abort(right);
    }

    firstWaiter.join();
    std::cout << "\nAll scenarios completed.\n";
    return 0;
}
