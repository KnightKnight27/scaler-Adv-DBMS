#pragma once

#include "recovery/wal.h"
#include <string>

// ─── Primary ──────────────────────────────────────────────────────────────────
//
// Track D: Primary node in the primary-replica replication model.
//
// The Primary node:
//   1. Writes all changes to the WAL (done by the normal write path).
//   2. After each committed WAL record, appends the same record to a
//      separate "replication log" file: replication.log
//   3. The Replica process (replica.h) polls this file and applies new records.
//
// This simulates WAL streaming / log shipping — the same concept used in
// PostgreSQL streaming replication. We use a file instead of a network socket
// for simplicity (same architecture, demonstrable locally).
//
// Replication flow:
//   Primary:  WAL write → data change → ship to replication.log
//   Replica:  poll replication.log → apply committed records to its own heap

class Primary {
public:
    Primary() = default;

    // Open the replication log file for appending.
    bool open(const std::string& replication_log_path);

    // Ship a WAL record to the replication log.
    // Called after every logCommit() in the main WAL.
    // Reads records from the main WAL starting from lsn_from and ships them.
    void shipLog(const std::string& wal_path, LSN lsn_from);

    // Close the replication log.
    void close();

    // The highest LSN shipped to the replica so far.
    LSN shippedLSN() const { return shipped_lsn_; }

private:
    int  fd_          = -1;
    LSN  shipped_lsn_ = 0;
};
