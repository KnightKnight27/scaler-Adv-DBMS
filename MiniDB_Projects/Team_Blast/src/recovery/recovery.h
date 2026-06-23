#pragma once

#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

// ─── RecoveryResult ───────────────────────────────────────────────────────────

struct RecoveryResult {
    int records_redone   = 0;   // number of INSERT/DELETE records replayed
    int txns_committed   = 0;   // number of committed transactions found
    int txns_aborted     = 0;   // number of aborted transactions skipped
    LSN last_lsn         = 0;   // highest LSN seen in the WAL
};

// ─── Recovery ─────────────────────────────────────────────────────────────────
//
// On database startup, Recovery::runRedo() scans the WAL file from the beginning.
//
// Algorithm (REDO-only log):
//   Pass 1 — Analysis: identify which TxIDs committed and which did not.
//   Pass 2 — Redo:     replay INSERT/DELETE for committed transactions only.
//                      Skip anything from non-committed transactions.
//
// This is a simplified redo-only recovery (no undo pass) because:
//   - Strict 2PL guarantees that aborted transactions never committed any changes
//     that readers saw (no dirty reads).
//   - The in-memory heap file is rebuilt from scratch on each restart anyway
//     (we don't persist the heap pages in this demo — only the WAL survives).
//
// For viva: we can explain this as "redo-only recovery" similar to ARIES redo phase.

class Recovery {
public:
    // Callback type: given a table name, return the HeapFile and BPlusTree for it.
    // Used so Recovery can replay inserts/deletes without depending on the Executor.
    using TableProvider = std::function<
        std::pair<HeapFile*, BPlusTree*>(const std::string& table_name)
    >;

    // Run redo recovery from the WAL file at wal_path.
    // table_provider is called to get the heap/index for each table mentioned in the log.
    // Returns a RecoveryResult describing what was replayed.
    static RecoveryResult runRedo(const std::string& wal_path, TableProvider table_provider);

    // Print a human-readable report of the recovery result.
    static void printResult(const RecoveryResult& result);
};
