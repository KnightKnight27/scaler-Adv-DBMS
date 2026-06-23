#pragma once

#include "common/types.h"
#include "common/config.h"
#include <string>
#include <fstream>
#include <mutex>
#include <atomic>

// ─── WAL Record Types ─────────────────────────────────────────────────────────

enum class WALRecordType : uint8_t {
    BEGIN   = 1,
    INSERT  = 2,
    DELETE  = 3,
    COMMIT  = 4,
    ABORT   = 5
};

// ─── WAL Record ───────────────────────────────────────────────────────────────
//
// Fixed-size binary record written sequentially to the WAL file.
// Simple format chosen for easy debugging and viva explanation.
//
// Binary layout on disk:
//   [8B LSN][8B TxID][1B type][32B table_name][4B key][256B value]
// Total = 309 bytes per record.

struct WALRecord {
    LSN            lsn        = 0;
    TxID           txid       = 0;
    WALRecordType  type       = WALRecordType::BEGIN;
    char           table_name[MAX_TABLE_NAME_LEN] = {};
    int32_t        key        = 0;
    char           value[WAL_MAX_VALUE_SIZE]      = {};
};

constexpr size_t WAL_RECORD_SIZE = sizeof(WALRecord);

// ─── WAL ──────────────────────────────────────────────────────────────────────
//
// Write-Ahead Log — appends log records to a file BEFORE the corresponding
// data page is modified. This is the fundamental WAL guarantee.
//
// On crash recovery (see recovery.h), the WAL is scanned from the beginning:
//   - All records for COMMITTED transactions are replayed (redo).
//   - Records for non-committed transactions are ignored.
//
// The WAL file is also used as the replication stream for Track D:
//   the Primary ships new WAL records to the Replica.

class WAL {
public:
    WAL() = default;
    ~WAL();

    // Open or create the WAL file at the given path.
    bool open(const std::string& path);

    // Close the WAL file.
    void close();

    // Append a BEGIN record.
    LSN logBegin(TxID txid);

    // Append an INSERT record.
    LSN logInsert(TxID txid, const std::string& table, int32_t key, const std::string& value);

    // Append a DELETE record.
    LSN logDelete(TxID txid, const std::string& table, int32_t key);

    // Append a COMMIT record.
    LSN logCommit(TxID txid);

    // Append an ABORT record.
    LSN logAbort(TxID txid);

    // Force the WAL file to disk (fsync). Called before acknowledging a commit.
    void flush();

    // Current highest LSN written.
    LSN currentLSN() const { return current_lsn_.load(); }

    const std::string& path() const { return path_; }

private:
    LSN appendRecord(const WALRecord& rec);

    std::string          path_;
    int                  fd_          = -1;   // POSIX fd for the WAL file
    std::atomic<LSN>     current_lsn_ {0};
    std::mutex           write_mutex_;         // serialize concurrent log appends
};
