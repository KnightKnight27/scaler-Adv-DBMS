// MVCC vs 2PL under read/write contention; same workload both modes
// build: g++ -std=c++17 -pthread -O2 -I../src bench_mvcc_vs_2pl.cpp ../src/txn/transaction_manager.cpp -o bench -static

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "txn/transaction_manager.hpp"

using Clock = std::chrono::steady_clock;

struct Result { long reads; long writes; };

static Result run(ConcurrencyMode mode, int readers, int writers, int keys,
                  int duration_ms, int write_hold_us) {
    TransactionManager tm(mode);
    for (int k = 0; k < keys; ++k) tm.load_committed("k" + std::to_string(k), "v0");

    std::atomic<bool> stop{false};
    std::atomic<long> reads{0}, writes{0};
    std::vector<std::thread> threads;

    // writers: each updates one key, holding its X-lock briefly
    for (int w = 0; w < writers; ++w) {
        threads.emplace_back([&, w] {
            std::string key = "k" + std::to_string(w % keys);
            long n = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                try {
                    TxID t = tm.begin();
                    tm.write(t, key, "v" + std::to_string(++n));
                    std::this_thread::sleep_for(std::chrono::microseconds(write_hold_us));
                    tm.commit(t);
                    writes.fetch_add(1, std::memory_order_relaxed);
                } catch (const DeadlockException&) {}
            }
        });
    }

    // readers: hammer the hot keys
    for (int r = 0; r < readers; ++r) {
        threads.emplace_back([&, r] {
            long i = r;
            while (!stop.load(std::memory_order_relaxed)) {
                try {
                    TxID t = tm.begin();
                    tm.read(t, "k" + std::to_string((i++) % keys));
                    tm.commit(t);
                    reads.fetch_add(1, std::memory_order_relaxed);
                } catch (const DeadlockException&) {}
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    stop.store(true);
    for (std::thread& th : threads) th.join();
    return {reads.load(), writes.load()};
}

int main(int argc, char** argv) {
    int readers = (argc > 1) ? std::stoi(argv[1]) : 8;
    int writers = (argc > 2) ? std::stoi(argv[2]) : 2;
    int keys    = (argc > 3) ? std::stoi(argv[3]) : 2;
    int ms      = (argc > 4) ? std::stoi(argv[4]) : 500;
    int hold_us = (argc > 5) ? std::stoi(argv[5]) : 200;

    std::cout << "MVCC vs 2PL  |  " << readers << " readers, " << writers
              << " writers, " << keys << " hot keys, " << ms << "ms window, "
              << hold_us << "us write-hold\n";
    std::cout << "-----------------------------------------------------------\n";

    Result mvcc = run(ConcurrencyMode::MVCC,  readers, writers, keys, ms, hold_us);
    Result tpl  = run(ConcurrencyMode::TWO_PL, readers, writers, keys, ms, hold_us);

    auto tput = [&](long reads) { return reads * 1000.0 / ms; };
    std::cout << "mode    reads      writes    read_throughput(ops/s)\n";
    std::cout << "MVCC    " << mvcc.reads << "\t  " << mvcc.writes << "\t    " << (long)tput(mvcc.reads) << "\n";
    std::cout << "2PL     " << tpl.reads  << "\t  " << tpl.writes  << "\t    " << (long)tput(tpl.reads) << "\n";
    if (tpl.reads > 0)
        std::cout << "=> MVCC completed " << (double)mvcc.reads / tpl.reads
                  << "x more reads under write contention\n";
    return 0;
}
