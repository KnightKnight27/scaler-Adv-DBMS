#include "storage.h"
#include "buffer_pool.h"
#include "table.h"
#include "execution.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static const std::string DB_FILE = "/tmp/bench_engine.db";
static constexpr int     N_TUPLES    = 100'000;
static constexpr size_t  BATCH_SIZE  = 100;
// Pool large enough to hold all ~590 heap pages in memory — benchmarks
// pure execution overhead, not disk I/O.
static constexpr size_t  POOL_SIZE   = 1024;

static void insert_tuples(TableHeap &heap) {
    for (int i = 0; i < N_TUPLES; ++i) {
        Tuple t{static_cast<int64_t>(i),
                static_cast<int64_t>(i * 2),
                static_cast<int64_t>(i * 3)};
        heap.InsertTuple(t);
    }
}

int main() {
    std::filesystem::remove(DB_FILE);

    DiskManager       dm(DB_FILE);
    BufferPoolManager bpm(POOL_SIZE, &dm);
    TableHeap         heap(&bpm);

    std::printf("Inserting %d tuples...\n", N_TUPLES);
    insert_tuples(heap);
    assert(bpm.AllUnpinned());
    std::printf("Insert complete.\n\n");

    // ── Benchmark 1: Volcano row-by-row (SeqScanExecutor) ────────────────────
    long long seq_count = 0;
    auto t1_start = std::chrono::high_resolution_clock::now();
    {
        SeqScanExecutor scan(&heap);
        scan.Init();
        Tuple t;
        while (scan.Next(&t)) ++seq_count;
    }
    auto t1_end = std::chrono::high_resolution_clock::now();
    assert(bpm.AllUnpinned());
    assert(seq_count == N_TUPLES);

    // ── Benchmark 2: Vectorized batch scan (BatchSeqScanExecutor) ────────────
    long long batch_count = 0;
    auto t2_start = std::chrono::high_resolution_clock::now();
    {
        BatchSeqScanExecutor bscan(&heap);
        bscan.Init();
        std::vector<Tuple> batch;
        batch.reserve(BATCH_SIZE);
        size_t got;
        while ((got = bscan.NextBatch(&batch, BATCH_SIZE)) > 0)
            batch_count += static_cast<long long>(got);
    }
    auto t2_end = std::chrono::high_resolution_clock::now();
    assert(bpm.AllUnpinned());
    assert(batch_count == N_TUPLES);

    // ── Report ────────────────────────────────────────────────────────────────
    auto to_ms = [](auto start, auto end) -> double {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };

    double seq_ms   = to_ms(t1_start, t1_end);
    double batch_ms = to_ms(t2_start, t2_end);

    double seq_tps   = (seq_ms   > 0) ? N_TUPLES * 1000.0 / seq_ms   : 0;
    double batch_tps = (batch_ms > 0) ? N_TUPLES * 1000.0 / batch_ms : 0;

    std::printf("=== Execution Benchmark: %d tuples, batch_size=%zu ===\n\n",
                N_TUPLES, BATCH_SIZE);
    std::printf("%-22s %10s   %16s\n", "Executor", "Time (ms)", "Throughput (t/s)");
    std::printf("%-22s %10.2f   %16.0f\n", "SeqScanExecutor",       seq_ms,   seq_tps);
    std::printf("%-22s %10.2f   %16.0f\n", "BatchSeqScanExecutor",  batch_ms, batch_tps);
    std::printf("\nSpeedup: %.2fx\n", seq_ms / batch_ms);

    std::filesystem::remove(DB_FILE);
    return 0;
}
