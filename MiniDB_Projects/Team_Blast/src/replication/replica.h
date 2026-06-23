#pragma once

#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// ─── Replica ──────────────────────────────────────────────────────────────────
//
// Track D: Replica node in the primary-replica replication model.
//
// The Replica:
//   1. Polls the replication.log file written by the Primary.
//   2. Reads new WAL records that it hasn't seen yet (tracks its own LSN).
//   3. Applies committed INSERT/DELETE records to its own in-memory heap.
//   4. Reports the gap between its LSN and the Primary's LSN.
//
// In a real distributed DB, this would happen over a TCP socket.
// Here, the file acts as the "replication channel" for local demo purposes.

class Replica {
public:
    using TableProvider = std::function<
        std::pair<HeapFile*, BPlusTree*>(const std::string& table_name)
    >;

    Replica() = default;

    // Open the replication log and set the table provider callback.
    bool open(const std::string& replication_log_path, TableProvider provider);

    // Poll the replication log for new records and apply them.
    // Returns the number of records applied in this poll cycle.
    int applyNewRecords();

    // Current replica LSN (highest record applied so far).
    LSN replicaLSN() const { return replica_lsn_; }

    // Close the replication log.
    void close();

    // Print replica status.
    void printStatus(LSN primary_lsn) const;

private:
    int            fd_           = -1;
    LSN            replica_lsn_  = 0;
    off_t          read_offset_  = 0;
    TableProvider  table_provider_;

    // Set of committed TxIDs seen in the replication log so far.
    std::unordered_set<TxID> committed_txids_;

    // Buffered records waiting for their transaction to commit.
    // We buffer INSERT/DELETE records until we see the COMMIT for their TxID.
    std::unordered_map<TxID, std::vector<WALRecord>> pending_records_;
};
