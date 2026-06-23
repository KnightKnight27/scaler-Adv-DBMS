// Extension-track benchmark: LSM-tree storage vs. B+ Tree storage.
//
// Track C asks us to compare the LSM-tree design against the B+ Tree based
// store on write throughput, read latency, and storage amplification. We run
// two experiments:
//
//   Experiment 1 -- in-memory data-structure micro-benchmark. Raw insert/lookup
//     speed of the B+ Tree index vs. the LSM's MemTable+SSTables. Highlights the
//     LSM's read amplification (a read may probe several runs).
//
//   Experiment 2 -- *durable* write throughput, which is what the LSM is built
//     to win. The "B+ Tree store" is MiniDB's real heap+B+Tree engine writing
//     through the WAL (fsync per commit = random writes + sync). The LSM batches
//     writes in memory and flushes sequentially. This is the classic result:
//     log-structured, batched, sequential writes >> in-place random writes.
//
//   ./bin/bench_lsm [num_keys]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "database.hpp"
#include "index/bplus_tree.hpp"
#include "lsm/lsm_tree.hpp"

using namespace minidb;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

static double ms_since(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
static size_t dir_size(const std::string& dir) {
    size_t total = 0;
    if (!fs::exists(dir)) return 0;
    for (auto& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file()) total += e.file_size();
    return total;
}

int main(int argc, char** argv) {
    int N = argc > 1 ? std::atoi(argv[1]) : 200000;
    int M = std::min(N, 20000);   // durable experiment is fsync-bound; keep modest

    std::vector<int> keys(N);
    for (int i = 0; i < N; ++i) keys[i] = i;
    std::shuffle(keys.begin(), keys.end(), std::mt19937(12345));

    std::printf("=== MiniDB Extension Benchmark: LSM-tree vs B+ Tree ===\n\n");

    // ===== Experiment 1: in-memory micro-benchmark ========================
    std::printf("[Experiment 1] in-memory structures, %d keys (insert then point-read)\n", N);
    double bt_w, bt_r, lsm_w, lsm_r;
    size_t lsm_bytes, lsm_sst;
    {
        BPlusTree bt;
        auto t = Clock::now();
        for (int i = 0; i < N; ++i) bt.insert(Value::Int(keys[i]), RID{keys[i], 0});
        bt_w = ms_since(t);
        t = Clock::now();
        long h = 0; for (int i = 0; i < N; ++i) h += bt.search(Value::Int(keys[i])) ? 1 : 0;
        bt_r = ms_since(t);
        std::printf("  B+ Tree : height=%d hits=%ld\n", bt.height(), h);
    }
    {
        std::string dir = "bench_lsm_data";
        fs::remove_all(dir);
        LSMOptions o; o.memtable_bytes = 256 << 10; o.compaction_trigger = 4;
        LSMTree lsm(dir, o);
        auto t = Clock::now();
        for (int i = 0; i < N; ++i)
            lsm.put(std::to_string(keys[i]), "v" + std::to_string(keys[i]));
        lsm.flush();
        lsm_w = ms_since(t);
        t = Clock::now();
        long h = 0; for (int i = 0; i < N; ++i) h += lsm.get(std::to_string(keys[i])) ? 1 : 0;
        lsm_r = ms_since(t);
        auto st = lsm.stats();
        lsm_sst = st.num_sstables; lsm_bytes = dir_size(dir);
        std::printf("  LSM-tree: flushes=%zu compactions=%zu sstables=%zu hits=%ld\n",
                    st.flushes, st.compactions, st.num_sstables, h);
        fs::remove_all(dir);
    }
    auto kops = [](int n, double ms) { return n / ms; };  // K ops/sec (n/ms)
    std::printf("  %-9s | write Kops/s | read Kops/s | on-disk\n", "store");
    std::printf("  %-9s | %12.1f | %11.1f | %s\n", "B+ Tree", kops(N, bt_w), kops(N, bt_r), "in-memory");
    std::printf("  %-9s | %12.1f | %11.1f | %zu KB (%zu sst)\n\n", "LSM-tree",
                kops(N, lsm_w), kops(N, lsm_r), lsm_bytes / 1024, lsm_sst);

    // ===== Experiment 2: durable write throughput =========================
    std::printf("[Experiment 2] DURABLE writes, %d rows (fsync-backed)\n", M);
    double db_w, lsmd_w;
    {
        fs::remove_all("bench_db");
        Database db("bench_db");
        db.execute("CREATE TABLE t (id INT PRIMARY KEY, v INT);");
        auto t = Clock::now();
        for (int i = 0; i < M; ++i)   // each auto-commit forces the WAL (fsync)
            db.execute("INSERT INTO t VALUES (" + std::to_string(keys[i]) + "," +
                       std::to_string(i) + ");");
        db_w = ms_since(t);
        fs::remove_all("bench_db");
        std::printf("  B+ Tree store (heap+index+WAL, fsync/commit): done\n");
    }
    {
        fs::remove_all("bench_lsm2");
        LSMTree lsm("bench_lsm2");
        auto t = Clock::now();
        for (int i = 0; i < M; ++i)
            lsm.put(std::to_string(keys[i]), std::to_string(i));
        lsm.flush();                 // single sequential, batched durability point
        lsmd_w = ms_since(t);
        fs::remove_all("bench_lsm2");
        std::printf("  LSM-tree (buffered MemTable, batched sequential flush): done\n");
    }
    std::printf("  %-26s | write Kops/s\n", "store");
    std::printf("  %-26s | %12.1f\n", "B+ Tree store (sync/commit)", kops(M, db_w));
    std::printf("  %-26s | %12.1f\n", "LSM-tree (batched)", kops(M, lsmd_w));
    std::printf("  -> LSM write speedup: %.1fx\n\n", db_w / lsmd_w);

    std::printf("Analysis:\n"
        "  * Exp.1: the in-memory B+ Tree wins raw ops (no I/O), but is volatile and\n"
        "    does random in-place updates; LSM reads probe up to `sstables` runs\n"
        "    (read amplification), bounded by compaction.\n"
        "  * Exp.2: with durability on, the LSM's batched sequential writes beat the\n"
        "    B+ Tree store's per-commit random write + fsync -- the LSM's core win.\n"
        "  * Space: the LSM keeps overlapping runs until compaction (space amp);\n"
        "    compaction reclaims tombstoned/overwritten keys.\n");
    return 0;
}
