#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// recovery.h  –  WAL-based crash recovery (Redo pass)
// ─────────────────────────────────────────────────────────────────────────────
#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include <string>

namespace minidb {

/**
 * Recovery – replays committed WAL entries onto the heap + index.
 *
 * Algorithm:
 *   Pass 1 – scan WAL; collect committed TXN IDs and their log records.
 *   Pass 2 – sort by LSN; redo INSERT / DELETE for committed TXNs only.
 *
 * Idempotent: duplicate-key check prevents double-insert.
 */
class Recovery {
public:
    Recovery(const std::string& wal_path, HeapFile* heap, BPlusTree* tree);
    void Run(); // call once at startup before accepting queries

private:
    std::string wal_path_;
    HeapFile*   heap_;
    BPlusTree*  tree_;
};

} // namespace minidb
