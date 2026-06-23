// M4 tests: 2PL lock manager — shared-lock compatibility, exclusive-lock
// blocking, and waits-for deadlock detection.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "txn/txn_manager.h"

using namespace minidb;
using namespace std::chrono_literals;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// Simple reusable barrier for N threads (C++17 has no std::barrier).
class Barrier {
public:
    explicit Barrier(int n) : need_(n) {}
    void wait() {
        std::unique_lock<std::mutex> lk(m_);
        if (++count_ == need_) { count_ = 0; ++gen_; cv_.notify_all(); }
        else { int g = gen_; cv_.wait(lk, [&] { return g != gen_; }); }
    }
private:
    std::mutex m_; std::condition_variable cv_;
    int need_, count_ = 0, gen_ = 0;
};

static void test_shared_compatible() {
    LockManager lm;
    lm.acquire(1, "k", LockMode::SHARED);
    lm.acquire(2, "k", LockMode::SHARED);  // S/S compatible -> returns without blocking
    lm.release_all(1);
    lm.release_all(2);
    std::cout << "[ok] shared locks coexist\n";
}

static void test_exclusive_blocks() {
    LockManager lm;
    lm.acquire(1, "k", LockMode::EXCLUSIVE);  // T1 holds X

    std::atomic<bool> t2_got{false};
    std::thread t2([&] {
        lm.acquire(2, "k", LockMode::SHARED);  // must block until T1 releases
        t2_got = true;
        lm.release_all(2);
    });

    std::this_thread::sleep_for(100ms);
    CHECK(!t2_got.load());     // still blocked while T1 holds X
    lm.release_all(1);
    t2.join();
    CHECK(t2_got.load());      // proceeded after release
    std::cout << "[ok] exclusive lock blocks, then grants after release\n";
}

static void test_deadlock_detected() {
    LockManager lm;
    TransactionManager tm(&lm);
    Transaction t1 = tm.begin(), t2 = tm.begin();
    Barrier barrier(2);
    std::atomic<int> deadlocks{0}, completed{0};

    auto worker = [&](Transaction& self, const char* first, const char* second) {
        try {
            lm.acquire(self.id, first, LockMode::EXCLUSIVE);
            barrier.wait();                                  // both hold their first lock
            lm.acquire(self.id, second, LockMode::EXCLUSIVE); // one of these closes the cycle
            ++completed;
            tm.commit(self);
        } catch (const DeadlockException&) {
            ++deadlocks;
            tm.abort(self);   // releasing unblocks the survivor
        }
    };

    std::thread a(worker, std::ref(t1), "A", "B");
    std::thread b(worker, std::ref(t2), "B", "A");
    a.join();
    b.join();

    CHECK(deadlocks.load() == 1);   // exactly one victim
    CHECK(completed.load() == 1);   // the other transaction committed
    std::cout << "[ok] deadlock detected: 1 victim aborted, 1 committed\n";
}

int main() {
    test_shared_compatible();
    test_exclusive_blocks();
    for (int i = 0; i < 20; ++i) test_deadlock_detected();  // repeat to shake out races
    if (g_failures == 0) { std::cout << "ALL TXN TESTS PASSED\n"; return 0; }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
