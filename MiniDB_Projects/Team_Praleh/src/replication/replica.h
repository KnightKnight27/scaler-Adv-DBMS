#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// replica.h  –  Replica node (Track D: Replication via WAL log-shipping)
// ─────────────────────────────────────────────────────────────────────────────
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "recovery/wal.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

namespace minidb {

/**
 * Replica – background thread that tails the primary's WAL and applies
 * committed changes to a separate HeapFile + BPlusTree (replica storage).
 *
 * Design (log-shipping / eventual consistency):
 *   - Primary writes WAL and commits.
 *   - Replica polls the WAL file every POLL_MS milliseconds.
 *   - Only committed TXNs are applied (same logic as Recovery).
 *   - applied_pos_ tracks byte offset so we don't re-read old records.
 *
 * Failure simulation:
 *   StopReplication()  → simulates network partition / replica failure.
 *   StartReplication() → replica reconnects and catches up from applied_pos_.
 */
class Replica {
public:
    static constexpr int POLL_MS = 300;

    Replica(const std::string& wal_path, HeapFile* heap, BPlusTree* tree);
    ~Replica();

    void StartReplication();
    void StopReplication();
    bool IsRunning() const { return running_.load(); }

    std::vector<Record> ScanAll() const { return heap_->ScanAll(); }

private:
    void WorkerLoop();

    std::string       wal_path_;
    HeapFile*         heap_;
    BPlusTree*        tree_;
    std::thread       worker_;
    std::atomic<bool> running_{false};
    std::streamoff    applied_pos_{0};
    std::mutex        pos_latch_;
};

} // namespace minidb
