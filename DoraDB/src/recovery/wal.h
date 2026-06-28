#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <fstream>
#include <mutex>

// ============================================================
// WAL — Write-Ahead Log for crash recovery
//
// Append-only binary log. Every modification is logged BEFORE
// being applied to data pages. On crash, replay log to recover.
// ============================================================

enum class WALRecordType : uint8_t {
    BEGIN, COMMIT, ABORT,
    INSERT, DELETE_REC, UPDATE_REC,
    CHECKPOINT
};

struct WALRecord {
    int lsn = 0;
    int txn_id = 0;
    WALRecordType type;
    std::string table_name;
    RID rid;
    std::vector<char> before_image;  // old data (for undo)
    std::vector<char> after_image;   // new data (for redo)
};

class WAL {
public:
    explicit WAL(const std::string& log_file);
    ~WAL();

    // Append a record. Returns the LSN assigned.
    int AppendBegin(int txn_id);
    int AppendCommit(int txn_id);
    int AppendAbort(int txn_id);
    int AppendInsert(int txn_id, const std::string& table, const RID& rid,
                     const char* data, int size);
    int AppendDelete(int txn_id, const std::string& table, const RID& rid,
                     const char* before_data, int before_size);
    int AppendUpdate(int txn_id, const std::string& table, const RID& rid,
                     const char* before_data, int before_size,
                     const char* after_data, int after_size);
    int AppendCheckpoint();

    // Force-flush the log to disk
    void Flush();

    // Read all records from the log file (for recovery)
    std::vector<WALRecord> ReadAll();

    // Clear the log (after checkpoint)
    void Truncate();

    int GetCurrentLSN() const { return next_lsn_; }

private:
    std::string log_file_;
    std::ofstream out_;
    std::mutex mu_;
    int next_lsn_ = 1;

    int AppendRecord(const WALRecord& rec);
    void WriteRecord(std::ostream& os, const WALRecord& rec);
    WALRecord ReadRecord(std::istream& is);
};
