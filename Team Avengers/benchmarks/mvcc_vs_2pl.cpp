// ============================================================================
//  mvcc_vs_2pl.cpp — Track B benchmark: read throughput under write contention.
//
//  Workload: HOT_KEYS rows, each currently being written by an uncommitted
//  transaction (a writer holding the row). Then READERS reader-transactions
//  each read a hot row. We measure, for each scheme:
//
//     * reads BLOCKED  (had to wait for a writer)
//     * reads SERVED   (completed)
//     * wall-clock time for the whole read workload
//
//  Expectation (and the point of the extension):
//     2PL  — every read of a write-locked row blocks  -> ~100% blocked
//     MVCC — readers ride their snapshot, never block  -> 0% blocked
//
//  Build:  clang++ -std=c++17 -O2 -I../src mvcc_vs_2pl.cpp -o bench && ./bench
// ============================================================================
#include "../src/txn/mvcc.hpp"
#include "../src/txn/two_pl.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace minidb;
using clk = std::chrono::high_resolution_clock;

constexpr int HOT_KEYS = 200;
constexpr int READERS  = 200000;

static std::string key_of(int i) { return "acct/" + std::to_string(i % HOT_KEYS); }

int main() {
    // ---- MVCC -------------------------------------------------------------
    long mvcc_blocked = 0, mvcc_served = 0;
    double mvcc_ms;
    {
        MVCCManager m;
        auto seed = m.begin();
        for (int i = 0; i < HOT_KEYS; ++i) m.write(seed, key_of(i), "100");
        m.commit(seed);
        // a writer holds an uncommitted new version of every hot key
        auto writer = m.begin();
        for (int i = 0; i < HOT_KEYS; ++i) m.write(writer, key_of(i), "200");

        auto t0 = clk::now();
        for (int i = 0; i < READERS; ++i) {
            auto r = m.begin();
            std::string v;
            TxnResult res = m.read(r, key_of(i), v);
            if (res == TxnResult::LockWait) ++mvcc_blocked; else ++mvcc_served;
            m.commit(r);
        }
        mvcc_ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    }

    // ---- 2PL --------------------------------------------------------------
    long pl_blocked = 0, pl_served = 0;
    double pl_ms;
    {
        TwoPLManager p;
        auto seed = p.begin();
        for (int i = 0; i < HOT_KEYS; ++i) p.write(seed, key_of(i), "100");
        p.commit(seed);
        auto writer = p.begin();
        for (int i = 0; i < HOT_KEYS; ++i) p.write(writer, key_of(i), "200");  // X-locks held

        auto t0 = clk::now();
        for (int i = 0; i < READERS; ++i) {
            auto r = p.begin();
            std::string v;
            TxnResult res = p.read(r, key_of(i), v);
            if (res == TxnResult::LockWait) ++pl_blocked; else ++pl_served;
            p.abort(r);   // blocked reader gives up (would retry in a real system)
        }
        pl_ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    }

    // ---- report -----------------------------------------------------------
    std::printf("\n  Track B benchmark — %d reads against %d write-locked rows\n",
                READERS, HOT_KEYS);
    std::printf("  ------------------------------------------------------------\n");
    std::printf("  %-6s | %10s | %10s | %8s | %s\n", "scheme", "served", "blocked", "blk %", "time");
    std::printf("  %-6s | %10ld | %10ld | %7.1f%% | %.1f ms\n", "MVCC",
                mvcc_served, mvcc_blocked, 100.0 * mvcc_blocked / READERS, mvcc_ms);
    std::printf("  %-6s | %10ld | %10ld | %7.1f%% | %.1f ms\n", "2PL",
                pl_served, pl_blocked, 100.0 * pl_blocked / READERS, pl_ms);
    std::printf("  ------------------------------------------------------------\n");
    std::printf("  Reads served without blocking:  MVCC %.0f%%  vs  2PL %.0f%%\n\n",
                100.0 * mvcc_served / READERS, 100.0 * pl_served / READERS);
    return 0;
}
