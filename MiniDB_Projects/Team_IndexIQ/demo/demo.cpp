#include "transaction/txn_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <string>

static TransactionManager tm;

void scenario_2pl() {
    std::cout << "\n=== Scenario 1: 2PL — reader waits for writer ===\n";

    TxID writer = tm.begin();
    std::cout << "[TX " << writer << "] Acquiring EXCLUSIVE lock on 'accounts'\n";
    tm.acquire_lock("accounts", writer, LockMode::EXCLUSIVE);
    tm.mvcc_write(writer, "balance", "500");

    std::thread reader([&]() {
        TxID r = tm.begin();
        std::cout << "[TX " << r << "] Waiting for SHARED lock on 'accounts'...\n";
        tm.acquire_lock("accounts", r, LockMode::SHARED);
        auto val = tm.mvcc_read(r, "balance");
        std::cout << "[TX " << r << "] Read balance = " << (val ? *val : "<none>") << "\n";
        tm.commit(r);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "[TX " << writer << "] Committing — reader will now unblock\n";
    tm.commit(writer);
    reader.join();
}

void scenario_mvcc() {
    std::cout << "\n=== Scenario 2: MVCC — reader sees snapshot, no wait ===\n";

    TxID seed = tm.begin();
    tm.mvcc_write(seed, "counter", "100");
    tm.commit(seed);

    TxID writer = tm.begin();
    tm.mvcc_write(writer, "counter", "999");

    TxID reader = tm.begin();
    auto val = tm.mvcc_read(reader, "counter");
    std::cout << "[TX " << reader << "] MVCC read counter = "
              << (val ? *val : "<none>")
              << "  (expected 100 — writer uncommitted)\n";
    tm.commit(reader);

    tm.commit(writer);

    TxID r2 = tm.begin();
    auto val2 = tm.mvcc_read(r2, "counter");
    std::cout << "[TX " << r2 << "] MVCC read counter = "
              << (val2 ? *val2 : "<none>")
              << "  (expected 999 — writer committed)\n";
    tm.commit(r2);
}

void scenario_deadlock() {
    std::cout << "\n=== Scenario 3: Deadlock detection ===\n";

    TxID t1 = tm.begin();
    TxID t2 = tm.begin();

    tm.acquire_lock("table_A", t1, LockMode::EXCLUSIVE);
    tm.acquire_lock("table_B", t2, LockMode::EXCLUSIVE);

    std::thread th1([&]() {
        try {
            std::cout << "[TX " << t1 << "] Wants lock on table_B — may deadlock\n";
            tm.acquire_lock("table_B", t1, LockMode::EXCLUSIVE);
            tm.commit(t1);
        } catch (DeadlockException& e) {
            std::cout << "  Caught: " << e.what() << "\n";
            tm.abort(t1);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    try {
        std::cout << "[TX " << t2 << "] Wants lock on table_A — may deadlock\n";
        tm.acquire_lock("table_A", t2, LockMode::EXCLUSIVE);
        tm.commit(t2);
    } catch (DeadlockException& e) {
        std::cout << "  Caught: " << e.what() << "\n";
        tm.abort(t2);
    }

    th1.join();
    std::cout << "Deadlock resolved.\n";
}

int main() {
    scenario_2pl();
    scenario_mvcc();
    scenario_deadlock();
    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
