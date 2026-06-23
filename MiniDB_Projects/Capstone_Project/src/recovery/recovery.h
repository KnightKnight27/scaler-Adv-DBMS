#pragma once

#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <utility>

/**
 * @struct RecoveryResult
 * @brief Accumulates statistics on transactions and records replayed during startup.
 */
struct RecoveryResult {
    int records_redone = 0;   ///< Number of INSERT/DELETE log records successfully replayed
    int txns_committed = 0;   ///< Number of committed transactions replayed
    int txns_aborted = 0;     ///< Number of aborted transactions skipped
    LSN last_lsn = 0;         ///< Highest Log Sequence Number detected
};

/**
 * @class Recovery
 * @brief Rebuilds the memory database state by executing a Redo-only pass over the Write-Ahead Log.
 */
class Recovery {
public:
    /**
     * @brief Callback function locating table storage components for table name keys.
     */
    using TableProvider = std::function<std::pair<HeapFile*, BPlusTree*>(const std::string& table_name)>;

    /**
     * @brief Performs Pass 1 (Analysis) and Pass 2 (Redo) recovery from the WAL file.
     * @return Statistics of the executed recovery pass.
     */
    static RecoveryResult runRedo(const std::string& wal_path, TableProvider table_provider);

    /**
     * @brief Outputs recovery stats results to stdout.
     */
    static void printResult(const RecoveryResult& result);
};
