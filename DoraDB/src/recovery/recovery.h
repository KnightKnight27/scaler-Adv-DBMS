#pragma once

#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "catalog/catalog.h"
#include "storage/buffer_pool.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// ============================================================
// RecoveryManager — ARIES-inspired redo/undo recovery
//
// On startup after crash:
//   1. Analysis: scan log, find committed & active txns
//   2. Redo: replay all committed txn operations
//   3. Undo: reverse all uncommitted txn operations
// ============================================================

class RecoveryManager {
public:
    struct TableAccess {
        HeapFile* heap;
        BPlusTree* index;  // nullable
        Schema schema;
    };

    // Run recovery using WAL records and table accessors.
    // Returns number of operations redone and undone.
    struct RecoveryResult {
        int redo_count = 0;
        int undo_count = 0;
        int committed_txns = 0;
        int aborted_txns = 0;
    };

    static RecoveryResult Recover(
        WAL& wal,
        std::unordered_map<std::string, TableAccess>& tables);
};
