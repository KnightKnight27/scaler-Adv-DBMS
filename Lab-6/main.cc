// Lab 6 - demo driver for the MVCC + 2PL transaction manager.
//
// Four scenarios:
//   1. MVCC snapshot isolation - a reader keeps seeing the old value
//      even after another transaction commits a new one.
//   2. Concurrent shared locks - two readers hold a shared lock at once.
//   3. Exclusive lock blocks a reader - the reader waits until commit.
//   4. Deadlock detection - two transactions grab locks in opposite
//      order; one is aborted to break the cycle.

#include <chrono>
#include <iostream>
#include <thread>

#include "transaction_manager.h"

namespace {

void show(lab6::TransactionManager& tm, lab6::TxId xid, const std::string& key) {
    auto v = tm.read(xid, key);
    std::cout << "  [tx " << xid << "] read " << key << " = "
              << (v ? *v : std::string("<not visible>")) << "\n";
}

void scenario1(lab6::TransactionManager& tm) {
    std::cout << "=== Scenario 1: MVCC snapshot isolation ===\n";

    lab6::TxId t1 = tm.begin();
    tm.insert(t1, "balance", "1000");
    tm.commit(t1);

    lab6::TxId t2 = tm.begin();   // snapshot taken now: balance = 1000
    lab6::TxId t3 = tm.begin();
    tm.update(t3, "balance", "2000");
    tm.commit(t3);                // commits AFTER t2 started

    show(tm, t2, "balance");      // still 1000 - t3 is invisible to t2
    tm.commit(t2);

    lab6::TxId t4 = tm.begin();
    show(tm, t4, "balance");      // 2000 - new snapshot sees t3's commit
    tm.commit(t4);
    std::cout << "\n";
}

void scenario2(lab6::TransactionManager& tm) {
    std::cout << "=== Scenario 2: concurrent shared locks ===\n";
    lab6::TxId a = tm.begin();
    lab6::TxId b = tm.begin();
    show(tm, a, "balance");       // shared lock granted
    show(tm, b, "balance");       // shared lock also granted (no conflict)
    tm.commit(a);
    tm.commit(b);
    std::cout << "\n";
}

void scenario3(lab6::TransactionManager& tm) {
    std::cout << "=== Scenario 3: exclusive lock blocks a reader ===\n";
    lab6::TxId w = tm.begin();
    tm.update(w, "balance", "3000");   // holds EXCLUSIVE lock on balance

    std::thread reader([&tm]() {
        lab6::TxId r = tm.begin();
        std::cout << "  [tx " << r << "] waiting for shared lock...\n";
        show(tm, r, "balance");        // blocks until w commits, then sees 3000
        tm.commit(r);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tm.commit(w);                      // releases the lock, unblocks the reader
    reader.join();
    std::cout << "\n";
}

void scenario4(lab6::TransactionManager& tm) {
    std::cout << "=== Scenario 4: deadlock detection ===\n";

    // Seed two rows.
    lab6::TxId seed = tm.begin();
    tm.insert(seed, "A", "a0");
    tm.insert(seed, "B", "b0");
    tm.commit(seed);

    lab6::TxId t1 = tm.begin();
    lab6::TxId t2 = tm.begin();

    tm.update(t1, "A", "a1");   // t1 holds X-lock on A
    tm.update(t2, "B", "b1");   // t2 holds X-lock on B

    // t1 now wants B (held by t2); t2 wants A (held by t1) -> cycle.
    std::thread th([&tm, t1]() {
        try {
            tm.update(t1, "B", "b2");   // will block, then deadlock
            tm.commit(t1);
        } catch (const lab6::DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t1);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    try {
        tm.update(t2, "A", "a2");       // closes the cycle
        tm.commit(t2);
    } catch (const lab6::DeadlockException& e) {
        std::cout << "  " << e.what() << "\n";
        tm.abort(t2);
    }

    th.join();
    std::cout << "\n";
}

}  // namespace

int main() {
    lab6::TransactionManager tm;
    scenario1(tm);
    scenario2(tm);
    scenario3(tm);
    scenario4(tm);
    std::cout << "All Lab 6 scenarios ran.\n";
    return 0;
}
