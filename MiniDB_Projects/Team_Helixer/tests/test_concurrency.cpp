// Concurrency test for the lock manager: (1) strict 2PL S/X compatibility and
// blocking, (2) deadlock detection picks exactly one victim to abort.
#include <cassert>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include "txn/lock_manager.h"

using namespace minidb;
using namespace std::chrono_literals;

int main() {
    // ---- Deadlock: T1 locks A then B; T2 locks B then A. One must abort. ----
    {
        LockManager lm;
        Transaction t1(1), t2(2);
        std::atomic<int> aborts{0}, both{0};

        auto worker = [&](Transaction *self, const char *first, const char *second) {
            try {
                lm.acquire(self, first, LockMode::EXCLUSIVE);
                std::this_thread::sleep_for(80ms); // ensure both grab first lock
                lm.acquire(self, second, LockMode::EXCLUSIVE);
                both++;
                lm.release_all(self);
            } catch (const TransactionAbortException &) {
                aborts++;
                lm.release_all(self);
            }
        };
        std::thread th1(worker, &t1, "A", "B");
        std::thread th2(worker, &t2, "B", "A");
        th1.join(); th2.join();

        // Exactly one transaction is the deadlock victim; the other completes.
        assert(aborts == 1);
        assert(both == 1);
    }

    // ---- Blocking: an X holder blocks an S requester until it releases. ----
    {
        LockManager lm;
        Transaction writer(10), reader(11);
        lm.acquire(&writer, "X", LockMode::EXCLUSIVE);

        std::atomic<bool> reader_got{false};
        std::thread rd([&] {
            lm.acquire(&reader, "X", LockMode::SHARED); // must block first
            reader_got = true;
            lm.release_all(&reader);
        });
        std::this_thread::sleep_for(80ms);
        assert(!reader_got); // still blocked while writer holds X
        lm.release_all(&writer);
        rd.join();
        assert(reader_got);  // proceeded once X was released
    }

    std::cout << "[OK] concurrency: 2PL S/X blocking + deadlock detection "
                 "(one victim aborted) verified" << std::endl;
    return 0;
}
