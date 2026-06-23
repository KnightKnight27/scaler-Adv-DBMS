// Extension Track C benchmark: LSM tree vs page-based B+Tree.
// Measures write throughput, point-read latency, and space amplification.
//
//   ./bench_lsm_vs_btree [N]     (default N = 200000)
#include <chrono>
#include <cstdio>
#include <random>
#include <numeric>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "index/btree.h"
#include "lsm/lsm_tree.h"

using namespace minidb;
using clk = std::chrono::high_resolution_clock;
static double secs(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}
static long file_size(const std::string &p) {
    struct stat st; return ::stat(p.c_str(), &st) == 0 ? (long)st.st_size : 0;
}

int main(int argc, char **argv) {
    int N = (argc > 1) ? atoi(argv[1]) : 200000;

    // A shuffled key set so writes are random (the realistic, B+Tree-unfriendly
    // case that LSM is designed to win).
    std::vector<int> keys(N);
    std::iota(keys.begin(), keys.end(), 0);
    std::mt19937 rng(7);
    std::shuffle(keys.begin(), keys.end(), rng);

    // Random read probe set.
    std::vector<int> probes(50000);
    std::uniform_int_distribution<int> pick(0, N - 1);
    for (auto &x : probes) x = pick(rng);

    printf("=== LSM vs B+Tree  (N=%d random writes) ===\n\n", N);

    // ---------- B+Tree ----------
    double bt_write, bt_read;
    long bt_bytes;
    {
        std::remove("bench_bt.db");
        DiskManager disk("bench_bt.db");
        BufferPoolManager bpm(1024, &disk);
        BPlusTree tree(&bpm);

        auto t0 = clk::now();
        for (int k : keys) tree.insert(k, RID{0, k});
        auto t1 = clk::now();
        bpm.flush_all(); disk.sync();
        bt_write = secs(t0, t1);

        auto r0 = clk::now();
        long hits = 0; RID r;
        for (int k : probes) if (tree.search(k, &r)) ++hits;
        auto r1 = clk::now();
        if (hits != (long)probes.size()) { printf("B+Tree read mismatch!\n"); return 1; }
        bt_read = secs(r0, r1);
        bt_bytes = file_size("bench_bt.db");
        printf("B+Tree : write %8.3fs  (%8.0f ins/s)   read %6.3fs (%8.0f lookups/s)  size %6.2f MB\n",
               bt_write, N / bt_write, bt_read, probes.size() / bt_read, bt_bytes / 1e6);
    }

    // ---------- LSM ----------
    double lsm_write, lsm_read;
    long lsm_bytes_before, lsm_bytes_after;
    {
        system("rm -rf bench_lsm_dir");
        LSMTree lsm("bench_lsm_dir", /*memtable_limit=*/50000);

        auto t0 = clk::now();
        for (int k : keys) lsm.put(k, std::to_string(k));
        lsm.flush();
        auto t1 = clk::now();
        lsm_write = secs(t0, t1);
        lsm_bytes_before = lsm.total_disk_bytes();
        size_t ssts = lsm.num_sstables();

        auto r0 = clk::now();
        long hits = 0; std::string v;
        for (int k : probes) if (lsm.get(k, &v)) ++hits;
        auto r1 = clk::now();
        if (hits != (long)probes.size()) { printf("LSM read mismatch!\n"); return 1; }
        lsm_read = secs(r0, r1);

        lsm.compact();
        lsm_bytes_after = lsm.total_disk_bytes();

        printf("LSM    : write %8.3fs  (%8.0f put/s)   read %6.3fs (%8.0f lookups/s)  size %6.2f MB  (%zu SSTables)\n",
               lsm_write, N / lsm_write, lsm_read, probes.size() / lsm_read, lsm_bytes_before / 1e6, ssts);
        printf("         after compaction: %.2f MB (%zu SSTable)\n",
               lsm_bytes_after / 1e6, lsm.num_sstables());
    }

    printf("\nSummary:\n");
    printf("  Write throughput : LSM is %.2fx %s than B+Tree\n",
           bt_write / lsm_write, (lsm_write < bt_write ? "FASTER" : "slower"));
    printf("  Read latency     : B+Tree is %.2fx %s than LSM\n",
           lsm_read / bt_read, (bt_read < lsm_read ? "FASTER" : "slower"));
    printf("  Space (pre-compact): LSM %.2f MB vs B+Tree %.2f MB  (amplification %.2fx)\n",
           lsm_bytes_before / 1e6, bt_bytes / 1e6, (double)lsm_bytes_before / std::max(1L, bt_bytes));

    std::remove("bench_bt.db");
    system("rm -rf bench_lsm_dir");
    return 0;
}
