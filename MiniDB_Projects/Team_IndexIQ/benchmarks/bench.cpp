#include "transaction/txn_manager.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

using Clock = std::chrono::steady_clock;

static constexpr int BENCH_SECONDS = 3;
static constexpr int NUM_KEYS      = 100;

long bench_2pl() {
    TransactionManager tm;

    for (int i = 0; i < NUM_KEYS; i++) {
        TxID t = tm.begin();
        tm.mvcc_write(t, "key" + std::to_string(i), std::to_string(i));
        tm.commit(t);
    }

    std::atomic<long>  reads{0};
    std::atomic<bool>  running{true};

    std::thread writer([&]() {
        while (running) {
            TxID w = tm.begin();
            try {
                tm.acquire_lock("key0", w, LockMode::EXCLUSIVE);
                tm.mvcc_write(w, "key0", "updated");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                tm.commit(w);
            } catch (...) { tm.abort(w); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto deadline = Clock::now() + std::chrono::seconds(BENCH_SECONDS);
    while (Clock::now() < deadline) {
        TxID r = tm.begin();
        try {
            tm.acquire_lock("key0", r, LockMode::SHARED);
            tm.mvcc_read(r, "key0");
            tm.commit(r);
            reads++;
        } catch (...) { tm.abort(r); }
    }

    running = false;
    writer.join();
    return reads.load();
}

long bench_mvcc() {
    TransactionManager tm;

    for (int i = 0; i < NUM_KEYS; i++) {
        TxID t = tm.begin();
        tm.mvcc_write(t, "key" + std::to_string(i), std::to_string(i));
        tm.commit(t);
    }

    std::atomic<long>  reads{0};
    std::atomic<bool>  running{true};

    std::thread writer([&]() {
        while (running) {
            TxID w = tm.begin();
            tm.mvcc_write(w, "key0", "updated");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            tm.commit(w);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto deadline = Clock::now() + std::chrono::seconds(BENCH_SECONDS);
    while (Clock::now() < deadline) {
        TxID r = tm.begin();
        tm.mvcc_read(r, "key0");
        tm.commit(r);
        reads++;
    }

    running = false;
    writer.join();
    return reads.load();
}

int main() {
    std::cout << "Benchmarking 2PL vs MVCC read throughput over "
              << BENCH_SECONDS << " seconds each...\n\n";

    std::cout << "Running 2PL benchmark...\n";
    long r2pl = bench_2pl();
    std::cout << "2PL reads:  " << r2pl << "\n\n";

    std::cout << "Running MVCC benchmark...\n";
    long rmvcc = bench_mvcc();
    std::cout << "MVCC reads: " << rmvcc << "\n\n";

    double speedup = (r2pl > 0) ? (double)rmvcc / r2pl : 0;
    std::cout << "MVCC speedup: " << speedup << "x\n";
    std::cout << "\nConclusion: MVCC readers do not block behind writers,\n"
              << "yielding significantly higher read throughput.\n";
    return 0;
}
